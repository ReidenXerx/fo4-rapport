#include "Takeover.h"

#include "Aftermath.h"

namespace
{
	// "0x000802" or "2050" -- the file is written by people reading xEdit, which
	// shows hex, so accept both rather than making the format a trap.
	[[nodiscard]] std::uint32_t ParseFormID(const nlohmann::json& a_value)
	{
		if (a_value.is_number_unsigned()) {
			return a_value.get<std::uint32_t>();
		}
		if (!a_value.is_string()) {
			return 0;
		}
		const auto text = a_value.get<std::string>();
		try {
			return static_cast<std::uint32_t>(std::stoul(text, nullptr, 0));
		} catch (const std::exception&) {
			return 0;
		}
	}
}

namespace RP
{
	Takeover& Takeover::GetSingleton() noexcept
	{
		static Takeover singleton;
		return singleton;
	}

	std::filesystem::path Takeover::ConfigPath()
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "takeover.json";
	}

	void Takeover::Load()
	{
		_items.clear();
		_active = Aftermath::GetSingleton().Enabled();

		const auto    path = ConfigPath();
		std::ifstream file{ path };
		if (!file) {
			logger::info("takeover: no {} - no other mod is configured", path.string());
			return;
		}

		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			logger::error("takeover: {} is not valid json ({}) - nothing is changed", path.string(), e.what());
			return;
		}

		const auto handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			logger::error("takeover: no data handler - nothing could be resolved");
			return;
		}

		const auto entries = document.find("entries");
		if (entries == document.end() || !entries->is_array()) {
			return;
		}

		for (const auto& entry : *entries) {
			const auto plugin = entry.value("plugin", std::string{});
			const auto reason = entry.value("reason", std::string{});
			const auto restore = entry.value("restore", std::string{ "start" });
			const auto quests = entry.find("quests");
			if (plugin.empty() || quests == entry.end() || !quests->is_array()) {
				continue;
			}

			std::uint32_t found = 0;
			for (const auto& quest : *quests) {
				const auto raw = quest.find("formID");
				if (raw == quest.end()) {
					continue;
				}
				const auto localID = ParseFormID(*raw);
				if (localID == 0) {
					logger::warn("takeover: {} has a quest with an unreadable formID - skipped", plugin);
					continue;
				}

				const auto form = handler->LookupForm<RE::TESQuest>(localID, plugin);
				if (!form) {
					continue;
				}

				Item item;
				item.plugin = plugin;
				item.name = quest.value("name", std::string{});
				item.reason = reason;
				item.formID = form->GetFormID();
				item.restoreByStarting = restore != "none";
				_items.push_back(std::move(item));
				++found;
			}

			// Not installed is not a failure. Saying so is still worth a line:
			// "nothing happened" and "nothing needed to happen" look identical
			// otherwise, and only one of them is a missing dependency.
			if (found == 0) {
				logger::info("takeover: {} is not installed - nothing to take over there", plugin);
			} else {
				logger::info(
					"takeover: {} quest(s) from {} will be {} - {}",
					found, plugin, _active ? "STOPPED" : "started again", reason);
			}
		}
	}
}
