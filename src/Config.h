#pragma once

#include "Pairing.h"

namespace AF
{
	// Read once at startup from Data/F4SE/Plugins/AutonomyFramework.ini.
	// Nothing here is read again at runtime, so a tick never pays for I/O.
	struct Config
	{
		std::uint32_t tickSeconds{ 20 };        // scheduler cadence, clamped to [10, 30]
		std::uint32_t warmupSeconds{ 60 };      // silence after a load before the first tick
		float         frameBudgetMs{ 0.25f };   // budget for one main-thread slice
		float         scanRadius{ 4096.0f };    // units around the player
		std::uint32_t maxConcurrentScenes{ 1 };
		bool          verbose{ false };

		[[nodiscard]] static Config& GetSingleton() noexcept;

		void Load();

		// Needs the data handler, so this runs when the game's data is ready —
		// never at plugin load, when no plugin is resolvable yet.
		void LoadRaces();
		void LoadScoring();

		[[nodiscard]] const PairWeights& Weights() const noexcept { return _weights; }

		[[nodiscard]] bool IsRaceAllowed(const RE::TESRace* a_race) const noexcept
		{
			return a_race && _allowedRaces.contains(a_race);
		}

		[[nodiscard]] std::size_t AllowedRaceCount() const noexcept { return _allowedRaces.size(); }

		[[nodiscard]] static std::filesystem::path IniPath();
		[[nodiscard]] static std::filesystem::path RacesPath();
		[[nodiscard]] static std::filesystem::path ScoringPath();

	private:
		std::unordered_set<const RE::TESRace*> _allowedRaces;
		PairWeights                            _weights{};
	};
}
