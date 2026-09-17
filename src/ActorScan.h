#pragma once

namespace AF
{
	// Why each actor was dropped, so a log line can prove the filters ran
	// rather than just reporting a candidate count.
	struct ScanCounters
	{
		std::uint32_t seen{ 0 };
		std::uint32_t stale{ 0 };       // handle no longer resolves
		std::uint32_t notLoaded{ 0 };   // no 3D
		std::uint32_t child{ 0 };       // hard rule: adults only
		std::uint32_t dead{ 0 };
		std::uint32_t inCombat{ 0 };
		std::uint32_t outOfRange{ 0 };
		std::uint32_t raceNotAllowed{ 0 };   // race is not on the allow-list
		std::uint32_t inDialogue{ 0 };
		std::uint32_t questDriven{ 0 };      // a quest alias is running packages on them
		std::uint32_t candidates{ 0 };
	};

	// One pass over the loaded actors, run in slices on the main thread so no
	// single frame pays for the whole list.
	class ActorScan
	{
	public:
		// Snapshots the process lists. Main thread only.
		void Begin(float a_radius);

		// Advances the pass for at most a_budgetMs. Returns true when the pass is done.
		[[nodiscard]] bool Step(float a_budgetMs);

		[[nodiscard]] const ScanCounters& Counters() const noexcept { return _counters; }
		[[nodiscard]] std::size_t         Size() const noexcept { return _handles.size(); }
		[[nodiscard]] std::uint32_t       Slices() const noexcept { return _slices; }

	private:
		std::vector<RE::ActorHandle> _handles;
		std::size_t                  _cursor{ 0 };
		ScanCounters                 _counters;
		std::uint32_t                _slices{ 0 };
		RE::NiPoint3                 _origin;
		float                        _radiusSq{ 0.0f };
	};
}
