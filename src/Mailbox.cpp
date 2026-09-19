#include "Mailbox.h"

#include "Config.h"
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
			// Queued, not done here. Papyrus owns MoveTo -- it is the only one of
			// the two languages that handles a cell change -- so this goes through
			// the same doorbell as everything else and costs one poll.
			link.QueueOrder(Order{
				verb == "goto" ? Order::Kind::kMovePlayerTo : Order::Kind::kMoveHere,
				formID, "", "" });
			return std::format("OK queued - {} {:08X}; it lands on the next poll",
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

			link.QueueOrder(Order{ Order::Kind::kMovePlayerTo, formID,
				std::format("{:.1f}", ox), std::format("{:.1f}", oy) });
			link.QueueOrder(Order{ Order::Kind::kLookAt, formID, "0.00", std::format("{:.2f}", yaw) });
			return std::format("OK queued - standing {:.0f} units off {:08X} and facing them (yaw {:.1f})",
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
			"ERR unknown verb \"{}\" - try: ping, health, stranded, heal, who, bring, goto, look, watch, request, pause, resume, say, console",
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
