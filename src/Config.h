#pragma once

#include "Pairing.h"

namespace RP
{
	// Read once at startup from Data/F4SE/Plugins/Rapport.ini.
	// Nothing here is read again at runtime, so a tick never pays for I/O.
	struct Config
	{
		std::uint32_t tickSeconds{ 20 };        // scheduler cadence, clamped to [10, 30]
		std::uint32_t warmupSeconds{ 60 };      // silence after a load before the first tick
		float         frameBudgetMs{ 0.25f };   // budget for one main-thread slice
		float         scanRadius{ 4096.0f };    // units around the player
		std::uint32_t maxConcurrentScenes{ 1 };
		float         sceneSeconds{ 30.0f };

		// How long an actor AAF refused as busy is left out of the running. AAF's
		// busy flag outlives our request -- it is cleared by the scene ending, and
		// a scene we never started never ends -- so without this the scheduler
		// offers the same unusable person on every tick. One run burned four
		// consecutive ticks on Johnny Friendly that way.
		float         busyBackoffSeconds{ 300.0f };

		// How long after a scene an actor is left out of the running, in GAME
		// hours. This is the stand-in's policy, not the framework's: the ledger
		// records when a scene happened and takes no view on what is too soon.
		// Chemistry will own this number, and this one goes away with the
		// stand-in that reads it.
		float         cooldownHours{ 24.0f };

		// Drop an actor's record once nothing has happened to them for this many
		// GAME hours and they have no overlay standing. A long playthrough would
		// otherwise carry every settler who ever met anyone. 0 keeps everything.
		float         pruneHours{ 720.0f };

		// The way out. With this set, the next load takes every overlay Rapport
		// applied back off, clears every face it set, and empties the ledger --
		// then says so and reminds you to turn it off again. It exists because an
		// overlay this mod applied must never be something only this mod can
		// remove.
		bool          panicClear{ false };

		// How often the Papyrus bridge asks whether there is a scene to start.
		// Almost every ask returns nothing, so this is a doorbell, not a scan.
		float         pollSeconds{ 3.0f };

		// True by default on purpose: a fresh install watches and reports, and
		// starts nothing until someone turns it on deliberately.
		bool          dryRun{ true };
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
