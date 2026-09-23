#pragma once

#include "NamedLock.h"

namespace RP
{
	// AAF's body morphs, taken off when AAF did not.
	//
	// AAF puts its scene morphs -- Erection, CErection and the like -- on actors
	// under ONE LooksMenu keyword, AAF_MorphKeyword (KYWD 000F9E in AAF.esm), and
	// normally removes them when the scene ends. 2026-09-23 the Diamond City guard
	// 000F61B6, one of the two actors in Rapport's first real scene, was found
	// still wearing Erection = 1.0 and CErection = 1.0 in two consecutive saves,
	// measured from the co-saves (docs/safeguards.md). How they survived is not
	// established. What it costs is: LooksMenu runs BodyGen only for an actor with
	// NO stored morphs, so a leftover morph also keeps that actor out of every
	// body-distribution mod for the rest of the save.
	//
	// Cleared BY KEYWORD, so only AAF's layer goes; BodyGen's bodies and the
	// player's LooksMenu sliders sit under other keys. And only on actors RAPPORT
	// had in a scene -- the same rule as every other safeguard here: another mod's
	// scene is that mod's business.
	class Morphs
	{
	public:
		[[nodiscard]] static Morphs& GetSingleton() noexcept;

		// A scene of ours ended. Both are cleared LATER, not now: AAF's own teardown
		// may still be running its morphs down, and clearing under it would be
		// clearing something AAF is about to write again.
		void OnSceneEnded(std::uint32_t a_first, std::uint32_t a_second);

		// A save was loaded: everyone in the ledger, once. They are the only actors a
		// scene of ours can have left a morph on -- the guard above is in a ledger --
		// and just after a load AAF has not announced itself, so no AAF scene can be
		// running on any of them.
		void OnGameLoaded(const std::vector<std::uint32_t>& a_actors);

		// From the bridge's pump, every poll: queue whatever has come due.
		void Pump();

		// A load: whatever was pending belongs to the world being left.
		void Forget();

	private:
		struct Due
		{
			std::uint32_t                         actor{ 0 };
			std::chrono::steady_clock::time_point at{};
		};

		// After the 20 s afterglow, with a margin: the face clears first, then this.
		static constexpr auto kDelay = std::chrono::seconds{ 25 };

		mutable std::timed_mutex _lock;
		std::vector<Due>         _due;
	};
}
