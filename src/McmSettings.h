#pragma once

namespace RP
{
	// The player's MCM choices, laid over the shipped json (scoring.json,
	// barks.json) under the same key names once MCM's TYPE PREFIX is stripped:
	// MCM only registers a setting whose key starts with i, f, b or s, so the
	// ini says fMinimumScore where the json says minimumScore.
	//
	// The json files stay the policy and the defaults. MCM keeps what the player
	// changed in Data/MCM/Settings/Rapport.ini, one [Section] per json block, and
	// Data/MCM/Config/Rapport/settings.ini (what MCM shows as the default) is
	// GENERATED from the json by scripts/build-mcm.py, so there is one source for
	// every default rather than two that can disagree.
	//
	// The plugin never calls MCM and MCM is optional: no ini, nothing overlaid.
	namespace McmSettings
	{
		// Every key under [a_section] in the player's ini, written into a_target
		// over whatever the json said. A key the json has as a bool is written as a
		// bool (MCM stores switches as 0/1), anything else as a number.
		void Overlay(std::string_view a_section, nlohmann::json& a_target);

		// True when the player's ini changed since the last call (MCM rewrites it
		// the moment a slider moves). Called by the scheduler before it ranks, so a
		// reload never races a pass reading the weights, and no Papyrus is needed:
		// MCM's change event is an F4SE ScriptObject extension this build does not
		// compile against. Costs one stat per 20s pass.
		[[nodiscard]] bool ChangedSinceLastCheck();

		// A switch read from an overlaid document: true/false OR a number (MCM's 0/1
		// for a key the json lacked). nlohmann's value<bool> throws on a number, and a
		// throw at data ready crosses into F4SE and takes the game with it.
		[[nodiscard]] inline bool ReadBool(const nlohmann::json& a_doc, const char* a_key, bool a_default) noexcept
		{
			const auto it = a_doc.find(a_key);
			if (it == a_doc.end()) {
				return a_default;
			}
			if (it->is_boolean()) {
				return it->get<bool>();
			}
			if (it->is_number()) {
				return it->get<double>() != 0.0;
			}
			return a_default;
		}
	}
}
