#pragma once

#include "NamedLock.h"

namespace RP
{
	// Bystanders who see a running scene, and the ones who say something about it
	// (R-12). A framework service: any addon that wants reactions gets them from
	// here rather than re-implementing the rules.
	//
	// The owner's trigger, verbatim in spirit: not when the scene starts, but when
	// someone is CLOSE ENOUGH AND CAN SEE IT while it is still going. So:
	//
	//   - the bridge sweeps each poll: every NPC within the radius, and whether
	//     they have detection line of sight to either participant (Papyrus has
	//     HasDetectionLOS; the C++ side has no such call);
	//   - an actor is ROLLED ONCE PER SCENE, on the first sweep where they are both
	//     near and seeing. Near-but-unseeing does not spend the roll - someone who
	//     walks round the corner is rolled when they see it, not when they came
	//     within range behind the wall. Lingering never re-rolls: rolling per poll
	//     would make anyone who stops to watch eventually certain to speak;
	//   - a chance, a per-actor cooldown across scenes, and a gap between lines so
	//     two bystanders never talk over each other or over the pair's own barks;
	//   - "alone" or "crowd" from how many eligible watchers see it this sweep;
	//   - persona is the WATCHER'S own (R-7), and the voice is Voices' decision.
	//
	// Never a child, never the dead, never anyone in combat, never the player, and
	// never someone Voices cannot voice - a line nobody hears is not spent.
	class Watchers
	{
	public:
		[[nodiscard]] static Watchers& GetSingleton() noexcept;

		void Load();

		void OnSceneStarted(std::int32_t a_request, std::uint32_t a_first, std::uint32_t a_second);
		void OnSceneEnded();

		// For the bridge's sweep. Radius 0 means "do not sweep this poll": no scene,
		// switched off, or still inside the opening window the pair's own barks use.
		[[nodiscard]] float         SweepRadius() const;
		[[nodiscard]] std::uint32_t SweepFirst() const;
		[[nodiscard]] std::uint32_t SweepSecond() const;

		// One per NPC the sweep found, then one EndSweep that decides.
		void Note(std::uint32_t a_actor, bool a_sees);
		void EndSweep();

	private:
		using Clock = std::chrono::steady_clock;

		mutable std::timed_mutex _lock;

		// Settings: the "observers" block of barks.json.
		bool  _enabled{ false };
		float _radius{ 900.0f };
		float _chance{ 0.33f };
		float _startAfter{ 10.0f };    // seconds after scene start: the pair speaks first
		float _gap{ 6.0f };            // seconds between two observer lines
		float _cooldown{ 300.0f };     // seconds before the same actor may comment again

		// The running scene.
		std::int32_t                                          _request{ 0 };
		std::uint32_t                                         _first{ 0 };
		std::uint32_t                                         _second{ 0 };
		Clock::time_point                                     _startedAt{};
		std::unordered_set<std::uint32_t>                     _rolled;     // this scene
		std::vector<std::pair<std::uint32_t, bool>>           _sweep;      // this poll
		Clock::time_point                                     _lastLine{};
		std::unordered_map<std::uint32_t, Clock::time_point> _spokeAt;    // across scenes
		std::mt19937                                          _rng{ std::random_device{}() };
	};
}
