#include "Config.h"

#include "McmSettings.h"

namespace
{
	[[nodiscard]] std::string_view Trim(std::string_view a_text) noexcept
	{
		constexpr auto blank = " \t\r\n"sv;
		const auto     first = a_text.find_first_not_of(blank);
		if (first == std::string_view::npos) {
			return {};
		}
		return a_text.substr(first, a_text.find_last_not_of(blank) - first + 1);
	}

	template <class T>
	[[nodiscard]] T Clamp(T a_value, T a_low, T a_high) noexcept
	{
		return a_value < a_low ? a_low : (a_value > a_high ? a_high : a_value);
	}
}

namespace RP
{
	Config& Config::GetSingleton() noexcept
	{
		static Config singleton;
		return singleton;
	}

	std::filesystem::path Config::ScoringPath()
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "scoring.json";
	}

	std::filesystem::path Config::RacesPath()
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "races.json";
	}

	std::filesystem::path Config::IniPath()
	{
		// The game's working directory is its install root, and our DLL lives in
		// Data/F4SE/Plugins next to the ini.
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport.ini";
	}

	void Config::Load()
	{
		const auto path = IniPath();
		std::ifstream file{ path };
		if (!file) {
			logger::info("no {} — using defaults", path.string());
			return;
		}

		std::uint32_t read = 0;
		std::string   line;
		while (std::getline(file, line)) {
			const auto trimmed = Trim(line);
			if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#' || trimmed.front() == '[') {
				continue;
			}

			const auto split = trimmed.find('=');
			if (split == std::string_view::npos) {
				continue;
			}

			const auto key = Trim(trimmed.substr(0, split));
			auto       raw = Trim(trimmed.substr(split + 1));
			if (const auto comment = raw.find_first_of(";#"); comment != std::string_view::npos) {
				raw = Trim(raw.substr(0, comment));
			}
			if (key.empty() || raw.empty()) {
				continue;
			}

			const std::string value{ raw };
			try {
				if (key == "TickSeconds") {
					tickSeconds = Clamp<std::uint32_t>(std::stoul(value), 10, 30);
				} else if (key == "WarmupSeconds") {
					warmupSeconds = Clamp<std::uint32_t>(std::stoul(value), 0, 600);
				} else if (key == "FrameBudgetMs") {
					frameBudgetMs = Clamp(std::stof(value), 0.05f, 5.0f);
				} else if (key == "ScanRadius") {
					scanRadius = Clamp(std::stof(value), 512.0f, 16384.0f);
				} else if (key == "MaxConcurrentScenes") {
					maxConcurrentScenes = Clamp<std::uint32_t>(std::stoul(value), 1, 4);
				} else if (key == "SceneSeconds") {
					sceneSeconds = Clamp(std::stof(value), 5.0f, 600.0f);
				} else if (key == "MaxSceneSeconds") {
					maxSceneSeconds = Clamp(std::stof(value), 60.0f, 7200.0f);
				} else if (key == "PruneHours") {
					pruneHours = Clamp(std::stof(value), 0.0f, 100000.0f);
				} else if (key == "StandInScenario") {
					standInScenario = value;
				} else if (key == "PanicClear") {
					panicClear = value != "0";
				} else if (key == "CooldownHours") {
					cooldownHours = Clamp(std::stof(value), 0.0f, 720.0f);
				} else if (key == "BusyBackoffSeconds") {
					busyBackoffSeconds = Clamp(std::stof(value), 0.0f, 3600.0f);
				} else if (key == "StaleFlagGraceSeconds") {
					staleFlagGraceSeconds = Clamp(std::stof(value), 0.0f, 3600.0f);
				} else if (key == "BlockAnimationFaces") {
					blockAnimationFaces = value != "0";
				} else if (key == "ForeignFaces") {
					foreignFaces = value != "0";
				} else if (key == "AAFReviveGraceSeconds") {
					aafReviveGraceSeconds = Clamp(std::stof(value), 0.0f, 600.0f);
				} else if (key == "AAFReviveRetrySeconds") {
					aafReviveRetrySeconds = Clamp(std::stof(value), 1.0f, 600.0f);
				} else if (key == "AAFReviveAttempts") {
					aafReviveAttempts = Clamp<std::uint32_t>(std::stoul(value), 0, 10);
				} else if (key == "PollSeconds") {
					pollSeconds = Clamp(std::stof(value), 1.0f, 30.0f);
				} else if (key == "DryRun") {
					dryRun = !(value == "0" || value == "false" || value == "False");
				} else if (key == "DriveFaces") {
					driveFaces = !(value == "0" || value == "false" || value == "False");
				} else if (key == "DiagnoseStageTags") {
					diagnoseStageTags = (value == "1" || value == "true" || value == "True");
				} else if (key == "Verbose") {
					verbose = (value == "1" || value == "true" || value == "True");
				} else if (key == "DevMailbox") {
					devMailbox = (value == "1" || value == "true" || value == "True");
				} else if (key == "DevConsole") {
					devConsole = (value == "1" || value == "true" || value == "True");
				} else {
					continue;
				}
				++read;
			} catch (const std::exception& e) {
				logger::warn("ini: {} = {} rejected ({})", key, value, e.what());
			}
		}

		logger::info(
			"config: {} key(s) from {} — tick {}s, warmup {}s, budget {:.2f} ms, radius {:.0f}, "
			"maxScenes {}, scene {:.0f}s, dry run {}, verbose {}",
			read, path.string(), tickSeconds, warmupSeconds, frameBudgetMs, scanRadius,
			maxConcurrentScenes, sceneSeconds, dryRun, verbose);
	}
}

namespace RP
{
	void Config::LoadRaces()
	{
		_allowedRaces.clear();

		const auto path = RacesPath();
		std::ifstream file{ path };
		if (!file) {
			logger::error("no {} — no race is allowed, so nothing will be selected", path.string());
			return;
		}

		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			logger::error("{} is not valid json ({}) — no race is allowed", path.string(), e.what());
			return;
		}

		const auto handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			logger::error("no data handler — no race is allowed");
			return;
		}

		const auto allow = document.find("allow");
		if (allow == document.end() || !allow->is_array()) {
			logger::error("{} has no \"allow\" array — no race is allowed", path.string());
			return;
		}

		for (const auto& entry : *allow) {
			const auto id = entry.value("id", std::string{ "?" });
			const auto plugin = entry.value("plugin", std::string{});
			const auto raw = entry.value("form", std::string{});
			if (plugin.empty() || raw.empty()) {
				logger::warn("race \"{}\" skipped: needs both \"plugin\" and \"form\"", id);
				continue;
			}

			std::uint32_t formID = 0;
			try {
				formID = static_cast<std::uint32_t>(std::stoul(raw, nullptr, 16));
			} catch (const std::exception&) {
				logger::warn("race \"{}\" skipped: \"{}\" is not a hex form id", id, raw);
				continue;
			}

			// A race that is not loaded is not an error: the player simply does not
			// have that content. It only means nobody of that race is a candidate.
			const auto race = handler->LookupForm<RE::TESRace>(formID, plugin);
			if (!race) {
				logger::info("race \"{}\" ({} {}) is not loaded — skipped", id, plugin, raw);
				continue;
			}

			_allowedRaces.insert(race);
			logger::info("race allowed: {} ({} {} -> {:08X})", id, plugin, raw, race->GetFormID());
		}

		if (_allowedRaces.empty()) {
			logger::error("no race resolved — nothing will ever be selected");
		}
	}
}

namespace RP
{
	void Config::LoadScoring()
	{
		const auto path = ScoringPath();
		std::ifstream file{ path };
		nlohmann::json document = nlohmann::json::object();
		if (!file) {
			// Still through the MCM overlay below: the player's settings do not
			// depend on the shipped file being there.
			logger::info("no {} — using built-in weights", path.string());
		}
		if (file) {
			try {
				file >> document;
			} catch (const std::exception& e) {
				logger::error("{} is not valid json ({}) - using built-in weights", path.string(), e.what());
				document = nlohmann::json::object();
			}
		}

		McmSettings::Overlay("Scoring", document);
		_weights.LoadFrom(document);
		_ambientQuests.clear();
		if (const auto list = document.find("ambientQuests"); list != document.end() && list->is_array()) {
			for (const auto& quest : *list) {
				if (quest.is_string()) {
					_ambientQuests.push_back(quest.get<std::string>());
				}
			}
		}
		logger::info("scoring: {} ambient quest(s) never hold anyone: {}", _ambientQuests.size(),
			_ambientQuests.empty() ? std::string{ "none" } : [&] {
				std::string all;
				for (const auto& q : _ambientQuests) {
					all += all.empty() ? q : ", " + q;
				}
				return all;
			}());
		logger::info(
			"scoring: range {:.0f}, observers within {:.0f} | proximity {:.2f}, faction {:.2f}, "
			"interior {:.2f}, night {:.2f}, per observer -{:.2f}, player near -{:.2f}",
			_weights.maxPairDistance, _weights.observerRadius, _weights.proximity,
			_weights.sharedFaction, _weights.interior, _weights.night, _weights.perObserver,
			_weights.playerNear);
	}
}
