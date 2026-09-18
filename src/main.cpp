#include "Config.h"
#include "DebugHub.h"
#include "Aftermath.h"
#include "Expressions.h"
#include "Ledger.h"
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
			RP::Takeover::GetSingleton().Load();
			RP::PapyrusLink::GetSingleton().OnDataReady();
			RP::Scheduler::GetSingleton().Start();
			break;
		case F4SE::MessagingInterface::kNewGame:
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
			RP::Scheduler::GetSingleton().OnLoad();
			break;
		case F4SE::MessagingInterface::kPreLoadGame:
			RP::Scheduler::GetSingleton().OnUnload();
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
	a_info->version = RP_VERSION_MAJOR;

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
