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
			// A UTF-8 byte-order mark would hide the first [Section] and file every key under ""
			// -- the same "settings not read" failure this reader exists to end (release review).
			if (a_text.starts_with("\xEF\xBB\xBF")) {
				a_text.remove_prefix(3);
			}
			constexpr std::string_view blank = " \t\r\n";
			const auto                 first = a_text.find_first_not_of(blank);
			if (first == std::string_view::npos) {
				return {};
			}
			return std::string{ a_text.substr(first, a_text.find_last_not_of(blank) - first + 1) };
		}

		// "[Section]", also with a comment after it ("[Meta] ; proof"), which used to be read as
		// no header at all and leave every key below it under the section before.
		[[nodiscard]] bool SectionOf(const std::string& a_text, std::string& a_section)
		{
			if (a_text.empty() || a_text.front() != '[') {
				return false;
			}
			const auto close = a_text.find(']');
			if (close == std::string::npos) {
				return false;
			}
			a_section = Trim(std::string_view{ a_text }.substr(1, close - 1));
			return true;
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
			if (SectionOf(text, section)) {
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
			// MCM ids carry a TYPE PREFIX -- i, f, b or s followed by the
			// capitalised name -- because MCM reads a setting's type from the
			// first letter and refuses to register anything else. Rapport's own
			// json keys are the bare names, so strip it.
			//
			// The bare name is tried FIRST and on purpose: Rapport shipped with
			// unprefixed ids, so a player who changed a setting before this fix
			// has their value stored under the old key. Trying bare first means
			// they keep it instead of silently reverting to the default.
			auto it = a_target.find(key);
			if (it == a_target.end() && key.size() > 1 &&
				std::string_view{ "ifbs" }.find(key.front()) != std::string_view::npos &&
				std::isupper(static_cast<unsigned char>(key[1]))) {
				auto bare = key.substr(1);
				bare.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(bare.front())));
				it = a_target.find(bare);
				if (it != a_target.end()) {
					if (it->is_boolean()) {
						a_target[bare] = value != 0.0;
					} else {
						a_target[bare] = value;
					}
					++applied;
					continue;
				}
			}
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

	namespace
	{
		struct Parsed
		{
			std::filesystem::file_time_type                    stamp{};
			std::unordered_map<std::string, double>             values;   // "name:Section"
			bool                                                parsed{ false };   // an empty file is cached too
		};
		std::mutex                                     g_parsedLock;
		std::unordered_map<std::string, Parsed>        g_parsed;          // by path

		[[nodiscard]] const Parsed* Read(const std::filesystem::path& a_path)
		{
			std::error_code ec;
			const auto      stamp = std::filesystem::last_write_time(a_path, ec);
			if (ec) {
				return nullptr;
			}
			auto& entry = g_parsed[a_path.string()];
			if (entry.parsed && entry.stamp == stamp) {
				return &entry;
			}
			entry = Parsed{ stamp, {}, true };
			std::ifstream file{ a_path };
			std::string   line;
			std::string   section;
			while (std::getline(file, line)) {
				const auto text = Trim(line);
				if (text.empty() || text.front() == ';' || text.front() == '#') {
					continue;
				}
				if (SectionOf(text, section)) {
					continue;
				}
				const auto equals = text.find('=');
				if (equals == std::string::npos) {
					continue;
				}
				try {
					entry.values[Trim(std::string_view{ text }.substr(0, equals)) + ":" + section] =
						std::stod(Trim(std::string_view{ text }.substr(equals + 1)));
				} catch (const std::exception&) {
					// a string setting: not a number, not for these natives
				}
			}
			return &entry;
		}
	}

	std::optional<double> ModSetting(std::string_view a_mod, std::string_view a_key)
	{
		if (a_mod.empty() || a_mod.find_first_of("/\\.:") != std::string_view::npos) {
			return std::nullopt;   // a mod NAME, never a path
		}
		const std::string key{ a_key };
		std::scoped_lock  lock{ g_parsedLock };
		for (const auto& path : { std::filesystem::path{ "Data" } / "MCM" / "Settings" / (std::string{ a_mod } + ".ini"),
				 std::filesystem::path{ "Data" } / "MCM" / "Config" / std::string{ a_mod } / "settings.ini" }) {
			if (const auto* parsed = Read(path)) {
				if (const auto it = parsed->values.find(key); it != parsed->values.end()) {
					return it->second;
				}
			}
		}
		return std::nullopt;
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
