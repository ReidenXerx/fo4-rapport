#include "Mailbox.h"

#include "Config.h"
#include "PapyrusLink.h"

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
				std::vector<std::string> lines;
				{
					std::ifstream in{ _inbox };
					std::string   line;
					while (std::getline(in, line)) {
						if (!line.empty()) {
							lines.push_back(line);
						}
					}
				}
				// Truncated BEFORE anything runs, not after. A command that takes
				// the game down would otherwise be re-read and re-run on the next
				// launch, which is how one mistake becomes a boot loop.
				std::filesystem::remove(_inbox, ec);

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
			"ERR unknown verb \"{}\" - try: ping, health, stranded, heal, who, say, console", verb);
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
