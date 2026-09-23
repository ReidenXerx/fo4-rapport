#include "Config.h"
#include "DebugHub.h"
#include "Mailbox.h"
#include "Aftermath.h"
#include "Barks.h"
#include "Narrator.h"
#include "Names.h"
#include "Voices.h"
#include "Watchers.h"
#include "Expressions.h"
#include "Ledger.h"
#include "Morphs.h"
#include "Scenarios.h"
#include "Takeover.h"
#include "PapyrusLink.h"
#include "Scheduler.h"

namespace
{
	// Must run AFTER F4SE::Init: log_directory() is built from GetSaveFolderName(),
	// which Init is what populates. Called any earlier it resolves to
	// "Documents/My Games//F4SE" and the log lands next to the game's folder
	// instead of inside it.
	void InitLogging()
	{
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= RP_PROJECT_NAME ".log"sv;

		// Keep the PREVIOUS run before truncating this one.
		//
		// The log is opened with truncate, so launching the game destroys the only
		// record of what went wrong last time -- and what goes wrong in this mod is
		// very often a thing that ENDS the session, which means the launch that
		// follows it is the launch that erases the evidence. That happened tonight:
		// a wedge at 01:20 was diagnosed from the log, the game was relaunched, and
		// the lines needed to settle a follow-up question were gone.
		//
		// One generation is enough. Two runs back has never been the interesting
		// one, and an unbounded pile of logs in somebody's Documents folder is its
		// own small rudeness.
		std::error_code ec;
		auto previous = *path;
		previous.replace_extension(".prev.log");
		std::filesystem::remove(previous, ec);
		std::filesystem::rename(*path, previous, ec);
		// ec is deliberately ignored: there is no log to report a logging failure
		// into, and a first run has nothing to rename. Neither is worth refusing to
		// start over.

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v"s);
	}

	void MessageHandler(F4SE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}

		switch (a_message->type) {
		case F4SE::MessagingInterface::kGameDataReady:
			RP::Config::GetSingleton().Load();
			RP::Config::GetSingleton().LoadRaces();
			RP::Config::GetSingleton().LoadScoring();
			RP::DebugHub::GetSingleton().Load();
			RP::Aftermath::GetSingleton().Load();
			RP::Expressions::GetSingleton().Load();
			RP::Voices::GetSingleton().Load();
			RP::Barks::GetSingleton().Load();
			RP::Narrator::GetSingleton().Load();
			RP::Names::GetSingleton().Load();
			RP::Names::RegisterLoadSink();
			RP::Ledger::RegisterDeathSink();
			RP::Watchers::GetSingleton().Load();
			RP::Expressions::GetSingleton().LoadOverrides();
			RP::Scenarios::GetSingleton().Load();

			// After Scenarios, because that is what builds the tree catalogue this
			// walks. Says which positions nothing can read, which IS the contents of
			// act-overrides.json.
			RP::Expressions::GetSingleton().ReportUnclassified();
			RP::Takeover::GetSingleton().Load();
			RP::PapyrusLink::GetSingleton().OnDataReady();
			RP::Scheduler::GetSingleton().Start();

			// AFTER Config::Load, because the flag that decides whether this exists
			// at all is read there. Does nothing whatsoever unless DevMailbox=1.
			RP::Mailbox::GetSingleton().Start();
			break;
		case F4SE::MessagingInterface::kNewGame:
			// New Game from the main menu never sends kPreLoadGame, and the scene
			// that was running in the last world would otherwise still be "in flight".
			RP::PapyrusLink::GetSingleton().OnGameLoading();
			[[fallthrough]];
		case F4SE::MessagingInterface::kPostLoadGame:
			// Said out loud on every load, including when it is nothing. F4SE only
			// calls the load callback when the save HAS data for us, so a save
			// written before this build logs not one line -- and "the ledger loaded
			// and was empty" then looks identical to "the ledger never loaded".
			logger::info(
				"ledger: after loading, {} actor(s), {} standing overlay(s), {} face(s) to clear",
				RP::Ledger::GetSingleton().Size(),
				RP::Aftermath::GetSingleton().Size(),
				RP::Expressions::GetSingleton().Wearing().size());
			// AFTER the ledger has been read (the co-save callback runs before
			// kPostLoadGame): everybody it names, once, before AAF is up.
			RP::Morphs::GetSingleton().OnGameLoaded(RP::Ledger::GetSingleton().ActorIDs());
			// The co-save has said who was introduced; the names go back on everyone in
			// memory, in case the game does not keep a custom name on an actor itself.
			RP::Names::GetSingleton().Reapply();
			RP::Scheduler::GetSingleton().OnLoad();
			break;
		case F4SE::MessagingInterface::kPreLoadGame:
			RP::Scheduler::GetSingleton().OnUnload();
			// Before the world changes: a scene and its orders belong to the one
			// being left, and nothing else would ever let go of them.
			RP::PapyrusLink::GetSingleton().OnGameLoading();
			break;
		default:
			break;
		}
	}
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	// No logging here. Our log file cannot be opened until F4SE::Init has run,
	// and a refusal below is recorded in f4se.log by F4SE itself.
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = RP_PROJECT_NAME;
	// The FULL version (0.x would otherwise all read as 0 to anything checking).
	a_info->version = RP_VERSION_MAJOR * 10000 + RP_VERSION_MINOR * 100 + RP_VERSION_PATCH;

	if (a_f4se->IsEditor()) {
		return false;
	}

	// Every address this plugin resolves is an OG 1.10.163 id. Refusing any
	// other runtime is the honest failure: the alternative is resolving
	// addresses that mean something else.
	if (a_f4se->RuntimeVersion() != F4SE::RUNTIME_1_10_163) {
		return false;
	}

	return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	// false: keep F4SE from installing its own logger over ours. Its logger names
	// the file after GetPluginName(), which is empty for a classic Query/Load
	// plugin — that is how this plugin's first run wrote to a file called ".log".
	F4SE::Init(a_f4se, false);

	InitLogging();
	logger::info("{} v{}", RP_PROJECT_NAME, RP_VERSION_STRING);

	const auto papyrus = F4SE::GetPapyrusInterface();
	if (!papyrus || !papyrus->Register(RP::PapyrusLink::RegisterNatives)) {
		logger::critical("could not register the papyrus functions");
		return false;
	}

	// Before the messaging listener on purpose: the ledger's revert callback has
	// to be in place before any load can happen, and a load can happen at once.
	if (!RP::Ledger::Register(F4SE::GetSerializationInterface())) {
		logger::error("nothing will be remembered between saves");
	}

	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageHandler)) {
		logger::critical("could not register the messaging listener");
		return false;
	}

	logger::info("loaded");
	return true;
}
