#include "Mailbox.h"

#include "Config.h"
#include "ActorScan.h"
#include "Orders.h"

#include <cmath>
#include "PapyrusLink.h"
#include "Scenarios.h"

namespace
{
	// Trim, then split off the first word. A parser worth attacking is a parser
	// worth getting wrong; this one is deliberately two lines.
	[[nodiscard]] std::pair<std::string, std::string> SplitVerb(const std::string& a_line)
	{
		const auto begin = a_line.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos) {
			return {};
		}
		const auto end = a_line.find_last_not_of(" \t\r\n");
		const auto trimmed = a_line.substr(begin, end - begin + 1);

		const auto space = trimmed.find(' ');
		if (space == std::string::npos) {
			return { trimmed, {} };
		}
		auto       rest = trimmed.substr(space + 1);
		const auto restBegin = rest.find_first_not_of(" \t");
		return { trimmed.substr(0, space),
			restBegin == std::string::npos ? std::string{} : rest.substr(restBegin) };
	}

	[[nodiscard]] std::uint32_t ParseFormID(std::string_view a_text, bool& a_ok)
	{
		a_ok = false;
		if (a_text.empty()) {
			return 0;
		}
		// HEX by default: that is what every other tool prints, and what
		// FormIdText exists to stop people converting by hand. "d:" in front says
		// decimal, for anyone reading a line of the log written before that.
		int base = 16;
		if (a_text.starts_with("d:")) {
			base = 10;
			a_text.remove_prefix(2);
		} else if (a_text.starts_with("0x") || a_text.starts_with("0X")) {
			a_text.remove_prefix(2);
		}

		std::uint32_t out = 0;
		const auto [ptr, ec] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), out, base);
		a_ok = (ec == std::errc{}) && ptr == a_text.data() + a_text.size();
		return out;
	}
}

namespace
{
	// TWO KINDS OF GOING, because they are genuinely different operations.
	//
	// Crossing a cell costs a load and nothing can avoid that. Moving WITHIN the
	// cell you are already in should cost nothing -- and it was costing a load
	// anyway, because MoveTo was being used for both. Two teleports inside
	// Diamond City in quick succession put the owner on a loading screen for
	// minutes, for a move of a few hundred units inside one cell.
	//
	// Same cell: SetPosition, right here on the main thread. Instant, no poll, no
	// load. Different cell: the doorbell and Papyrus MoveTo, which is the only
	// thing that handles a cell change, and one load is the honest price.
	[[nodiscard]] bool SameCellAsPlayer(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
	{
		if (!a_actor || !a_player) {
			return false;
		}
		auto* a = a_actor->GetParentCell();
		auto* b = a_player->GetParentCell();
		return a != nullptr && a == b;
	}
}

namespace RP
{
	void Mailbox::Start()
	{
		const auto& config = Config::GetSingleton();
		if (!config.devMailbox) {
			return;
		}
		if (_running.exchange(true)) {
			return;
		}

		auto dir = F4SE::log::log_directory();
		if (!dir) {
			logger::warn("mailbox: no log directory - the command channel is not running");
			_running.store(false);
			return;
		}
		_inbox = *dir / "Rapport.cmd";
		_outbox = *dir / "Rapport.cmd.out";

		// Said LOUDLY, at error level, on every launch it is on. A command channel
		// somebody forgot about is worse than one they never had, and the only
		// real defence a dev flag has is being impossible to leave on by accident.
		logger::error(
			"mailbox: THE DEV COMMAND CHANNEL IS OPEN. Anything that can write {} can drive this "
			"game. The console verb is {}. Set DevMailbox=0 in Rapport.ini to close it.",
			_inbox.string(), config.devConsole ? "ALSO ENABLED" : "disabled");

		_watcher = std::thread{ [this] { Watch(); } };
	}

	void Mailbox::Stop()
	{
		if (!_running.exchange(false)) {
			return;
		}
		if (_watcher.joinable()) {
			_watcher.join();
		}
	}

	void Mailbox::Watch()
	{
		// IO ONLY on this thread. Anything touching a form or plugin state goes
		// through the task interface and runs on the MAIN thread -- the same rule
		// this codebase keeps about the Papyrus VM, for the same reason.
		while (_running.load()) {
			std::error_code ec;
			if (std::filesystem::exists(_inbox, ec) && !ec) {
				// RENAME FIRST, then read the renamed file.
				//
				// The first version read the inbox and then deleted it, and
				// anything a writer appended BETWEEN those two steps was deleted
				// unread. That is not theoretical -- it silently ate the first
				// teleport command sent at it, and the failure looked exactly like
				// the command having never been sent, which is the shape of bug
				// this whole evening has been about.
				//
				// A rename is atomic within a volume, so a writer appending a
				// moment later creates a fresh Rapport.cmd and loses nothing. It
				// also keeps the property the delete was there for: whatever is
				// about to run is already out of the inbox, so a command that takes
				// the game down is not re-run on the next launch.
				auto taken = _inbox;
				taken += ".taken";
				std::filesystem::remove(taken, ec);
				std::filesystem::rename(_inbox, taken, ec);
				if (ec) {
					std::this_thread::sleep_for(std::chrono::milliseconds{ 250 });
					continue;
				}

				std::vector<std::string> lines;
				{
					std::ifstream in{ taken };
					std::string   line;
					while (std::getline(in, line)) {
						if (!line.empty()) {
							lines.push_back(line);
						}
					}
				}
				std::filesystem::remove(taken, ec);

				for (const auto& line : lines) {
					if (auto* task = F4SE::GetTaskInterface()) {
						task->AddTask([this, line] { Reply(line, Run(line)); });
					} else {
						Reply(line, "ERR no task interface - cannot reach the main thread");
					}
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds{ 250 });
		}
	}

	void Mailbox::Reply(std::string_view a_line, std::string_view a_answer) const
	{
		std::ofstream out{ _outbox, std::ios::app };
		out << "> " << a_line << "\n"
			<< a_answer << "\n";
		logger::info("mailbox: \"{}\" -> {}", a_line, a_answer);
	}

	std::string Mailbox::Run(const std::string& a_line)
	{
		const auto [verb, rest] = SplitVerb(a_line);
		if (verb.empty()) {
			return "ERR empty";
		}

		auto& link = PapyrusLink::GetSingleton();

		if (verb == "ping") {
			return "OK pong";
		}

		if (verb == "health") {
			link.LogHealth();
			return std::format("OK health written to the log - holding {} cleanup order(s), {} heal(s)",
				link.StrandedOrders(), link.Heals());
		}

		if (verb == "stranded") {
			return std::format("OK holding {} cleanup order(s) for actors who were not loaded",
				link.StrandedOrders());
		}

		if (verb == "heal") {
			const bool did = link.AbandonInFlight("the mailbox asked");
			return did ? "OK gave up on the scene in flight" : "OK nothing was in flight";
		}

		if (verb == "nearby") {
			// WHO IS ACTUALLY HERE, with the ids every other verb needs.
			//
			// Until now those ids were learned by grepping the log for "would
			// pair" lines, which only names the top three of a ranked list and
			// only when the scheduler happened to tick. The plugin has had the
			// whole list the entire time.
			//
			// Read-only, and that is the point: the one rule this channel has
			// earned tonight is to act on NPCs and on our own framework, never on
			// the player's body -- SetAngle on the player crashed the game.
			auto* lists = RE::ProcessLists::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!lists || !player) {
				return "ERR no process lists";
			}
			float radius = 3000.0f;
			if (!rest.empty()) {
				try {
					radius = std::stof(rest);
				} catch (const std::exception&) {
					return "ERR nearby [radius]";
				}
			}

			const auto here = player->GetPosition();
			struct Row
			{
				float         distance;
				std::uint32_t formID;
				std::string   name;
				bool          busy;
				bool          loaded;
			};
			std::vector<Row> rows;

			for (const auto* list : { &lists->highActorHandles, &lists->middleHighActorHandles }) {
				for (const auto& handle : *list) {
					auto actor = handle.get();
					if (!actor) {
						continue;
					}
					const auto pos = actor->GetPosition();
					const auto dx = pos.x - here.x;
					const auto dy = pos.y - here.y;
					const auto dz = pos.z - here.z;
					const auto d = std::sqrt(dx * dx + dy * dy + dz * dz);
					if (d > radius) {
						continue;
					}
					const char* n = actor->GetDisplayFullName();
					rows.push_back(Row{ d, actor->GetFormID(), (n && *n) ? n : "(unnamed)",
						link.IsActorBusy(actor->GetFormID()), actor->Get3D() != nullptr });
				}
			}

			std::sort(rows.begin(), rows.end(),
				[](const Row& a, const Row& b) { return a.distance < b.distance; });

			std::string out = std::format("OK {} actor(s) within {:.0f} units:", rows.size(), radius);
			std::size_t shown = 0;
			for (const auto& row : rows) {
				if (shown++ >= 25) {
					out += std::format("\n  ... and {} more", rows.size() - 25);
					break;
				}
				out += std::format("\n  {:08X}  {:>6.0f}u  {}{}{}",
					row.formID, row.distance, row.name,
					row.loaded ? "" : "  [no 3D]", row.busy ? "  [AAF busy]" : "");
			}
			return out;
		}

		if (verb == "who") {
			bool       ok = false;
			const auto formID = ParseFormID(rest, ok);
			if (!ok) {
				return "ERR who <formid>   (hex by default, or d:<decimal>)";
			}
			auto* form = RE::TESForm::GetFormByID(formID);
			auto* actor = form ? form->As<RE::Actor>() : nullptr;
			if (!actor) {
				return std::format("OK {:08X} does not resolve to an actor", formID);
			}
			// Get3D(), not Is3DLoaded(): the latter is Papyrus's name for this and
			// does not exist on RE::Actor. ActorScan's own filter has always asked
			// it this way -- establish the contract from the thing that defines it.
			return std::format("OK {:08X} loaded={} busy={}",
				formID,
				actor->Get3D() ? "yes" : "NO",
				link.IsActorBusy(formID) ? "yes" : "no");
		}

		if (verb == "bring" || verb == "goto") {
			bool       ok = false;
			const auto formID = ParseFormID(rest, ok);
			if (!ok) {
				return std::format("ERR {} <formid>   (hex by default, or d:<decimal>)", verb);
			}
			auto* form = RE::TESForm::GetFormByID(formID);
			auto* actor = form ? form->As<RE::Actor>() : nullptr;
			auto* player = RE::PlayerCharacter::GetSingleton();

			// SAME CELL: do it here and now, no load, no poll. Stand a little short
			// of them rather than inside them -- landing in somebody is the one
			// position from which they cannot be seen.
			if (verb == "goto" && actor && player && SameCellAsPlayer(actor, player)) {
				const auto here = player->GetPosition();
				const auto there = actor->GetPosition();
				auto       bx = here.x - there.x;
				auto       by = here.y - there.y;
				const auto len = std::sqrt(bx * bx + by * by);
				constexpr float kStandOff = 160.0f;
				if (len < 1.0f) {
					bx = 0.0f;
					by = -1.0f;
				} else {
					bx /= len;
					by /= len;
				}
				const RE::NiPoint3 dest{ there.x + bx * kStandOff, there.y + by * kStandOff, there.z };
				player->SetPosition(dest, true);

				constexpr float kRad3 = 57.2957795f;
				const auto      yaw3 = std::atan2(-bx, -by) * kRad3;
				link.QueueOrder(Order{ Order::Kind::kLookAt, formID, "0.00",
					std::format("{:.2f}", yaw3) });
				return std::format(
					"OK same cell - moved locally to {:.0f} units off {:08X} and facing them, no load",
					kStandOff, formID);
			}

			// DIFFERENT CELL (or no actor to compare): the doorbell, because
			// Papyrus MoveTo is the only thing here that handles a cell change.
			link.QueueOrder(Order{
				verb == "goto" ? Order::Kind::kMovePlayerTo : Order::Kind::kMoveHere,
				formID, "", "" });
			return std::format("OK queued - {} {:08X} across a cell, so expect one load",
				verb == "goto" ? "moving the player to" : "bringing", formID);
		}

		if (verb == "request") {
			// Start a scene on demand instead of waiting for Chemistry to choose a
			// pair. This is the one that turns "play until it happens" into a test.
			const auto space = rest.find(' ');
			if (space == std::string::npos) {
				return "ERR request <formid> <formid> [scenario]";
			}
			bool       okA = false, okB = false;
			const auto firstID = ParseFormID(std::string_view{ rest }.substr(0, space), okA);
			auto       tail = rest.substr(space + 1);
			const auto space2 = tail.find(' ');
			const auto secondID = ParseFormID(
				std::string_view{ tail }.substr(0, space2 == std::string::npos ? tail.size() : space2), okB);
			const std::string scenario =
				space2 == std::string::npos ? std::string{} : tail.substr(space2 + 1);
			if (!okA || !okB) {
				return "ERR request <formid> <formid> [scenario]";
			}

			auto* formA = RE::TESForm::GetFormByID(firstID);
			auto* formB = RE::TESForm::GetFormByID(secondID);
			auto* first = formA ? formA->As<RE::Actor>() : nullptr;
			auto* second = formB ? formB->As<RE::Actor>() : nullptr;
			if (!first || !second) {
				return std::format("ERR {:08X} or {:08X} is not an actor", firstID, secondID);
			}

			// The scenario owns its own length, exactly as the Papyrus entry point
			// does it. Two copies of that rule would drift.
			const auto& settings = Config::GetSingleton();
			const auto  seconds =
				scenario.empty()
					? settings.sceneSeconds
					: (std::max)(settings.sceneSeconds, Scenarios::GetSingleton().SecondsFor(scenario));

			// Capture the reason BEFORE asking, because the interesting one --
			// a scene already in flight -- is exactly what the call would change.
			// "REFUSED" with no reason is the uninformative message this whole
			// codebase keeps being rewritten to stop printing.
			const bool wasBusy = link.Busy();
			const bool paused = link.AutonomyPaused();

			const bool taken = link.RequestScene(first, second, seconds, scenario);
			if (taken) {
				return std::format("OK Rapport took the request: {:08X} + {:08X}{} over {:.0f}s",
					firstID, secondID,
					scenario.empty() ? "" : std::format(" as \"{}\"", scenario), seconds);
			}
			return std::format(
				"OK Rapport REFUSED {:08X} + {:08X} - {}",
				firstID, secondID,
				wasBusy
					? "a scene was already in flight (maxConcurrentScenes). Autonomy re-fills that slot "
					  "within seconds of one ending, so send \"pause\" first if you want the slot."
					: (paused ? "autonomy is paused, but this is a different refusal - check the log"
							  : "transient - check the log for which check said no"));
		}

		if (verb == "look") {
			bool       ok = false;
			const auto formID = ParseFormID(rest, ok);
			if (!ok) {
				return "ERR look <formid>   (points the player's camera at them)";
			}
			auto* form = RE::TESForm::GetFormByID(formID);
			auto* actor = form ? form->As<RE::Actor>() : nullptr;
			if (!actor) {
				return std::format("ERR {:08X} is not an actor", formID);
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return "ERR no player";
			}

			// Bethesda angles: yaw (Z) is measured from +Y and increases
			// CLOCKWISE, which is why this is atan2(dx, dy) and not the atan2(dy,
			// dx) every maths textbook writes. Pitch (X) is positive looking DOWN.
			// Roll stays zero -- a rolled camera is a bug, never a request.
			const auto here = player->GetPosition();
			const auto there = actor->GetPosition();
			const auto dx = there.x - here.x;
			const auto dy = there.y - here.y;
			// Aim at the head rather than the feet, or every portrait is a
			// close-up of somebody's boots.
			const auto dz = (there.z + 120.0f) - (here.z + 120.0f);

			constexpr float kRad = 57.2957795f;
			const auto      flat = std::sqrt(dx * dx + dy * dy);
			const auto      yaw = std::atan2(dx, dy) * kRad;
			const auto      pitch = -std::atan2(dz, flat) * kRad;

			link.QueueOrder(Order{ Order::Kind::kLookAt, formID,
				std::format("{:.2f}", pitch), std::format("{:.2f}", yaw) });
			return std::format("OK queued - looking at {:08X} (pitch {:.1f}, yaw {:.1f}, {:.0f} units away)",
				formID, pitch, yaw, flat);
		}

		if (verb == "watch") {
			// STAND BACK, THEN FACE THEM. goto alone lands the player inside the
			// actor, which is the one position from which they cannot be seen --
			// a portrait needs a few metres and a direction, not proximity.
			const auto space = rest.find(' ');
			bool       ok = false;
			const auto formID = ParseFormID(
				space == std::string::npos ? std::string_view{ rest } : std::string_view{ rest }.substr(0, space),
				ok);
			if (!ok) {
				return "ERR watch <formid> [distance]   (default 250 units, about 3.5m)";
			}
			float distance = 250.0f;
			if (space != std::string::npos) {
				try {
					distance = std::stof(rest.substr(space + 1));
				} catch (const std::exception&) {
					return "ERR watch <formid> [distance]";
				}
			}

			auto* form = RE::TESForm::GetFormByID(formID);
			auto* actor = form ? form->As<RE::Actor>() : nullptr;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!actor || !player) {
				return std::format("ERR {:08X} is not an actor", formID);
			}

			// DO NOT TELEPORT IF ALREADY IN A GOOD SPOT.
			//
			// The first version moved the player every time, and in a dense cell
			// every teleport is a CELL LOAD -- two in quick succession left the
			// owner staring at a loading screen for minutes. Standing 130 units
			// from somebody is already a fine place to look at them from; moving
			// 300 units to "improve" it bought nothing and cost a load.
			//
			// So: if the player is already within a sensible band, only turn the
			// camera. The move is for the case where they are genuinely too far or
			// standing inside the target.
			const auto here = player->GetPosition();
			const auto there = actor->GetPosition();
			{
				const auto ddx = here.x - there.x;
				const auto ddy = here.y - there.y;
				const auto already = std::sqrt(ddx * ddx + ddy * ddy);
				constexpr float kCloseEnough = 700.0f;
				constexpr float kTooClose = 90.0f;
				if (already >= kTooClose && already <= kCloseEnough) {
					constexpr float kRad2 = 57.2957795f;
					const auto      dz2 = there.z - here.z;
					const auto      yaw2 = std::atan2(-ddx, -ddy) * kRad2;
					const auto      pitch2 = -std::atan2(dz2, already) * kRad2;
					link.QueueOrder(Order{ Order::Kind::kLookAt, formID,
						std::format("{:.2f}", pitch2), std::format("{:.2f}", yaw2) });
					return std::format(
						"OK already {:.0f} units away - just turning to face {:08X} (yaw {:.1f}), no teleport",
						already, formID, yaw2);
				}
			}
			auto       dx = here.x - there.x;
			auto       dy = here.y - there.y;
			const auto len = std::sqrt(dx * dx + dy * dy);
			if (len < 1.0f) {
				dx = 0.0f;
				dy = -1.0f;
			} else {
				dx /= len;
				dy /= len;
			}
			const auto ox = dx * distance;
			const auto oy = dy * distance;

			// The yaw from the STAND-OFF point back to the actor, which is the
			// opposite of the offset direction.
			constexpr float kRad = 57.2957795f;
			const auto      yaw = std::atan2(-ox, -oy) * kRad;

			if (SameCellAsPlayer(actor, player)) {
				const RE::NiPoint3 dest{ there.x + ox, there.y + oy, here.z };
				player->SetPosition(dest, true);
				link.QueueOrder(Order{ Order::Kind::kLookAt, formID, "0.00", std::format("{:.2f}", yaw) });
				return std::format(
					"OK same cell - stood {:.0f} units off {:08X} locally and facing them (yaw {:.1f}), no load",
					distance, formID, yaw);
			}

			link.QueueOrder(Order{ Order::Kind::kMovePlayerTo, formID,
				std::format("{:.1f}", ox), std::format("{:.1f}", oy) });
			link.QueueOrder(Order{ Order::Kind::kLookAt, formID, "0.00", std::format("{:.2f}", yaw) });
			return std::format(
				"OK queued - standing {:.0f} units off {:08X} across a cell, so expect one load (yaw {:.1f})",
				distance, formID, yaw);
		}

		if (verb == "pause" || verb == "resume") {
			const bool pause = (verb == "pause");
			link.SetAutonomyPaused(pause);
			return pause
					 ? "OK autonomy PAUSED - nothing will start itself until resume"
					 : "OK autonomy resumed";
		}

		if (verb == "say") {
			// Proves the channel end to end in the one place the owner is already
			// looking: their own console.
			if (auto* console = RE::ConsoleLog::GetSingleton()) {
				console->AddString(rest.c_str());
				return "OK printed to the console";
			}
			return "ERR no console";
		}

		if (verb == "console") {
			if (!Config::GetSingleton().devConsole) {
				return "ERR the console verb needs DevConsole=1 in Rapport.ini as well";
			}
			return RunConsole(rest);
		}

		return std::format(
			"ERR unknown verb \"{}\" - try: ping, health, nearby, stranded, heal, who, bring, goto, look, watch, request, pause, resume, say, console",
			verb);
	}

	std::string Mailbox::RunConsole(const std::string& a_command)
	{
		if (a_command.empty()) {
			return "ERR console <command>";
		}

		// NOT WIRED UP, AND SAYING SO RATHER THAN GUESSING.
		//
		// Two of the three pieces are confirmed present in this CommonLibF4:
		// Script::SetText, and Script::CompileAndRun(ScriptCompiler*,
		// COMPILER_NAME::kSystemWindow, TESObjectREFR*) with ScriptCompiler an
		// empty struct that can live on the stack.
		//
		// The missing piece is CONSTRUCTING the Script. This version exposes no
		// form factory, and Script is declared novtable -- so a plain allocation
		// produces an object whose vtable pointer is garbage, and CompileAndRun on
		// it is a jump through that garbage. That is exactly the "tried to EXECUTE
		// memory at <address>" crash read out of a Buffout log earlier tonight,
		// and shipping it would be manufacturing the bug we spent the evening
		// diagnosing.
		//
		// So: an error, not a crash. What remains is one verified construction
		// route, not a redesign.
		return std::format(
			"ERR console is not wired up yet - \"{}\" was NOT run. CompileAndRun is available but "
			"constructing a Script is unsolved in this CommonLibF4; see the comment in Mailbox.cpp",
			a_command);
	}
}
