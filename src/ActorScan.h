#pragma once

namespace RP
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

		// form id -> how many actors were rejected for wearing it. A count alone
		// cannot tell "this cell is full of dogs" from "the race field is wrong",
		// and that difference decides whether there is a bug.
		[[nodiscard]] const std::unordered_map<std::uint32_t, std::uint32_t>& RejectedRaces() const noexcept
		{
			return _rejectedRaces;
		}

		// Handles, not pointers: a pass spans several frames and an actor collected
		// in the first slice can be unloaded before the last one.
		[[nodiscard]] const std::vector<RE::ActorHandle>& Candidates() const noexcept { return _candidates; }

		// Everyone loaded and alive, whatever their race — a brahmin cannot be a
		// candidate but a settler standing next to one can still see them.
		[[nodiscard]] const std::vector<RE::NiPoint3>& ObserverPositions() const noexcept
		{
			return _observerPositions;
		}
		[[nodiscard]] std::size_t         Size() const noexcept { return _handles.size(); }
		[[nodiscard]] std::uint32_t       Slices() const noexcept { return _slices; }

	private:
		std::unordered_map<std::uint32_t, std::uint32_t> _rejectedRaces;
		std::vector<RE::ActorHandle>                     _candidates;
		std::vector<RE::NiPoint3>                        _observerPositions;
		std::vector<RE::ActorHandle> _handles;
		std::size_t                  _cursor{ 0 };
		ScanCounters                 _counters;
		std::uint32_t                _slices{ 0 };
		RE::NiPoint3                 _origin;
		float                        _radiusSq{ 0.0f };
	};
}
