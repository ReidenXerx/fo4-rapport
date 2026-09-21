#include "McmSettings.h"

namespace RP::McmSettings
{
	namespace
	{
		// Scheduler thread only.
		std::filesystem::file_time_type g_stamp{};

		std::filesystem::path PlayerIni()
		{
			return std::filesystem::path{ "Data" } / "MCM" / "Settings" / "Rapport.ini";
		}

		[[nodiscard]] std::string Trim(std::string_view a_text)
		{
			constexpr std::string_view blank = " \t\r\n";
			const auto                 first = a_text.find_first_not_of(blank);
			if (first == std::string_view::npos) {
				return {};
			}
			return std::string{ a_text.substr(first, a_text.find_last_not_of(blank) - first + 1) };
		}
	}

	void Overlay(std::string_view a_section, nlohmann::json& a_target)
	{
		std::ifstream file{ PlayerIni() };
		if (!file || !a_target.is_object()) {
			return;
		}
		std::string line;
		std::string section;
		int         applied = 0;
		while (std::getline(file, line)) {
			const auto text = Trim(line);
			if (text.empty() || text.front() == ';' || text.front() == '#') {
				continue;
			}
			if (text.front() == '[' && text.back() == ']') {
				section = Trim(std::string_view{ text }.substr(1, text.size() - 2));
				continue;
			}
			const auto equals = text.find('=');
			if (section != a_section || equals == std::string::npos) {
				continue;
			}
			const auto key = Trim(std::string_view{ text }.substr(0, equals));
			const auto raw = Trim(std::string_view{ text }.substr(equals + 1));
			double     value = 0.0;
			try {
				value = std::stod(raw);
			} catch (const std::exception&) {
				logger::warn("mcm: [{}] {}={} is not a number - ignored", section, key, raw);
				continue;
			}
			const auto it = a_target.find(key);
			if (it != a_target.end() && it->is_boolean()) {
				a_target[key] = value != 0.0;
			} else {
				a_target[key] = value;
			}
			++applied;
		}
		if (applied > 0) {
			logger::info("mcm: {} setting(s) from the player's MCM applied over [{}]", applied, a_section);
		}
	}

	bool ChangedSinceLastCheck()
	{
		std::error_code ec;
		const auto      stamp = std::filesystem::last_write_time(PlayerIni(), ec);
		const auto      now = ec ? std::filesystem::file_time_type{} : stamp;
		// No priming on the first look. It used to take the first pass's view as the
		// baseline, so an ini written between data ready (when the loaders read) and
		// that first pass was never applied - seen 2026-09-22. Starting from "no file"
		// costs one redundant reload when the player has settings; missing one costs
		// the player's settings.
		if (now == g_stamp) {
			return false;
		}
		g_stamp = now;
		return true;
	}
}
