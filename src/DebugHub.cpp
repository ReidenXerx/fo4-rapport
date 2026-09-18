#include "DebugHub.h"

namespace
{
	// The engine reads these once, at startup, from the user's own ini. We can
	// write them; we cannot make the current session honour them.
	constexpr auto kLoggingKey = "bEnableLogging"sv;
	constexpr auto kTraceKey = "bEnableTrace"sv;

	[[nodiscard]] std::filesystem::path UserIniPath()
	{
		// Documents\My Games\Fallout4\Fallout4Custom.ini, found the same way F4SE
		// finds its own log directory.
		auto dir = F4SE::log::log_directory();   // ...\My Games\<SaveFolder>\F4SE
		if (!dir) {
			return {};
		}
		return dir->parent_path() / "Fallout4Custom.ini";
	}

	// Rewrites one key inside the [Papyrus] section, leaving every other line and
	// the file's ordering exactly as the user had it.
	[[nodiscard]] bool SetIniValue(std::string& a_text, std::string_view a_key, bool a_on)
	{
		const std::string wanted{ a_on ? "1" : "0" };
		std::size_t       cursor = 0;
		bool              inPapyrus = false;
		bool              changed = false;

		std::string out;
		out.reserve(a_text.size() + 32);

		while (cursor <= a_text.size()) {
			const auto eol = a_text.find('\n', cursor);
			const auto line = a_text.substr(cursor, eol == std::string::npos ? std::string::npos : eol - cursor);

			auto trimmed = line;
			while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
				trimmed.erase(trimmed.begin());
			}

			if (!trimmed.empty() && trimmed.front() == '[') {
				inPapyrus = trimmed.rfind("[Papyrus]", 0) == 0;
			}

			if (inPapyrus && trimmed.rfind(a_key, 0) == 0) {
				const auto equals = trimmed.find('=');
				if (equals != std::string::npos) {
					const auto current = trimmed.substr(equals + 1);
					if (current.find(wanted) == std::string::npos) {
						out.append(std::string{ a_key }).append("=").append(wanted);
						changed = true;
					} else {
						out.append(line);
					}
				} else {
					out.append(line);
				}
			} else {
				out.append(line);
			}

			if (eol == std::string::npos) {
				break;
			}
			out.append("\n");
			cursor = eol + 1;
		}

		if (changed) {
			a_text = out;
		}
		return changed;
	}
}

namespace RP
{
	DebugHub& DebugHub::GetSingleton() noexcept
	{
		static DebugHub singleton;
		return singleton;
	}

	std::filesystem::path DebugHub::ConfigPath()
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "debug.json";
	}

	void DebugHub::Load()
	{
		_entries.clear();
		_profile = "off";

		const auto path = ConfigPath();
		std::ifstream file{ path };
		if (!file) {
			logger::info("debug hub: no {} — nothing is changed anywhere", path.string());
			return;
		}

		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			logger::error("debug hub: {} is not valid json ({}) — nothing is changed", path.string(), e.what());
			return;
		}

		_profile = document.value("active", std::string{ "off" });

		const auto profiles = document.find("profiles");
		if (profiles == document.end() || !profiles->contains(_profile)) {
			logger::error("debug hub: no profile named \"{}\" — nothing is changed", _profile);
			return;
		}
		const auto& profile = (*profiles)[_profile];

		if (_profile == "off") {
			logger::info("debug hub: applying profile \"off\"");
		} else {
			// Loud on purpose. Leaving diagnostics on is right on a development
			// machine and wrong in a release -- Papyrus tracing slows the script
			// engine, which is the one cost this mod exists not to add. The only
			// thing that stops a dev default shipping is somebody noticing, so
			// this makes it impossible not to.
			logger::warn(
				"debug hub: profile \"{}\" is a DEVELOPMENT profile. Papyrus tracing is on and it "
				"slows the script engine. Set \"active\" to \"off\" in debug.json before shipping.",
				_profile);
		}

		// ---- the engine's own logging, which needs a restart --------------------
		if (const auto papyrus = profile.find("papyrus"); papyrus != profile.end()) {
			ApplyEngineLogging(papyrus->value("enabled", false));
		}

		// ---- everything the bridge has to apply for us --------------------------
		if (const auto aaf = profile.find("aaf"); aaf != profile.end() && aaf->is_object()) {
			for (const auto& [key, value] : aaf->items()) {
				_entries.push_back(Entry{ Target::kAAF, {}, key, "string", value.is_string() ? value.get<std::string>() : value.dump() });
			}
		}

		if (const auto mcm = profile.find("mcm"); mcm != profile.end() && mcm->is_array()) {
			for (const auto& item : *mcm) {
				Entry entry;
				entry.target = Target::kMCM;
				entry.mod = item.value("mod", std::string{});
				entry.key = item.value("setting", std::string{});
				entry.type = item.value("type", std::string{ "bool" });
				const auto value = item.find("value");
				if (value == item.end()) {
					continue;
				}
				entry.value = value->is_string() ? value->get<std::string>() : value->dump();
				if (entry.mod.empty() || entry.key.empty()) {
					logger::warn("debug hub: an mcm entry is missing \"mod\" or \"setting\" — skipped");
					continue;
				}
				_entries.push_back(std::move(entry));
			}
		}

		// Every change is named. A diagnostic switch nobody can audit is how a
		// setting gets left on.
		for (const auto& entry : _entries) {
			if (entry.target == Target::kAAF) {
				logger::info("debug hub:   aaf  {} = {}", entry.key, entry.value);
			} else {
				logger::info("debug hub:   mcm  {}::{} = {} ({})", entry.mod, entry.key, entry.value, entry.type);
			}
		}
		if (_entries.empty()) {
			logger::info("debug hub:   nothing for the bridge to apply");
		}
	}

	void DebugHub::ApplyEngineLogging(bool a_enabled)
	{
		const auto path = UserIniPath();
		if (path.empty() || !std::filesystem::exists(path)) {
			logger::warn("debug hub: no Fallout4Custom.ini found — engine logging left alone");
			return;
		}

		std::string text;
		{
			std::ifstream in{ path, std::ios::binary };
			if (!in) {
				logger::error("debug hub: could not read {}", path.string());
				return;
			}
			text.assign(std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{});
		}

		auto changed = SetIniValue(text, kLoggingKey, a_enabled);
		changed = SetIniValue(text, kTraceKey, a_enabled) || changed;

		if (!changed) {
			logger::info("debug hub: engine logging already {}", a_enabled ? "on" : "off");
			return;
		}

		// Back up once, and never overwrite an existing backup: the first one is the
		// user's own settings, and that is the copy worth keeping.
		const auto backup = path.string() + ".before-rapport-debug";
		std::error_code ec;
		if (!std::filesystem::exists(backup)) {
			std::filesystem::copy_file(path, backup, ec);
			if (ec) {
				logger::error("debug hub: could not back up the ini ({}) — leaving it untouched", ec.message());
				return;
			}
			logger::info("debug hub: backed up Fallout4Custom.ini");
		}

		std::ofstream out{ path, std::ios::binary | std::ios::trunc };
		if (!out) {
			logger::error("debug hub: could not write {}", path.string());
			return;
		}
		out << text;

		logger::warn(
			"debug hub: engine papyrus logging turned {} in Fallout4Custom.ini. "
			"The game reads it at startup, so THIS session is unchanged — restart for it to matter.",
			a_enabled ? "ON" : "OFF");
	}
}
