#include "Config.h"

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

namespace AF
{
	Config& Config::GetSingleton() noexcept
	{
		static Config singleton;
		return singleton;
	}

	std::filesystem::path Config::RacesPath()
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "AutonomyFramework" / "races.json";
	}

	std::filesystem::path Config::IniPath()
	{
		// The game's working directory is its install root, and our DLL lives in
		// Data/F4SE/Plugins next to the ini.
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "AutonomyFramework.ini";
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
				} else if (key == "Verbose") {
					verbose = (value == "1" || value == "true" || value == "True");
				} else {
					continue;
				}
				++read;
			} catch (const std::exception& e) {
				logger::warn("ini: {} = {} rejected ({})", key, value, e.what());
			}
		}

		logger::info(
			"config: {} key(s) from {} — tick {}s, warmup {}s, budget {:.2f} ms, radius {:.0f}, maxScenes {}, verbose {}",
			read, path.string(), tickSeconds, warmupSeconds, frameBudgetMs, scanRadius, maxConcurrentScenes, verbose);
	}
}

namespace AF
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
