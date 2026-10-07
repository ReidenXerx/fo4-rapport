#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>

namespace RP
{
	// steady_clock minus the time the game stood still: the pause menu, photo mode
	// (SAM's ScreenArcherMenu pauses the game), frozen game time, or any stretch in
	// which the bridge's poll did not run at all -- Papyrus stops when the game does.
	//
	// The scene safety deadlines (the emergency stop, a foreign scene's overdue end)
	// ran on wall time, so ten minutes in photo mode retired a scene that AAF itself
	// had frozen, and AAF's own timers had not moved (microscope wave 1, 2026-10-05).
	//
	// Fed by the bridge poll (Tick); read anywhere (now). Before the first two ticks it
	// is plain steady time.
	struct ActiveClock
	{
		using duration = std::chrono::steady_clock::duration;
		using rep = duration::rep;
		using period = duration::period;
		using time_point = std::chrono::time_point<ActiveClock>;
		static constexpr bool is_steady = true;

		[[nodiscard]] static time_point now() noexcept
		{
			// A pause is credited only at the next tick, after the fact, which would step
			// the clock BACK. Never: it holds still until real time catches up instead.
			auto       seen = Seen().load();
			const auto mine = std::chrono::steady_clock::now().time_since_epoch().count() - Frozen().load();
			while (mine > seen && !Seen().compare_exchange_weak(seen, mine)) {}
			return time_point{ duration{ (std::max)(mine, seen) } };
		}

		// a_still: the game says it is paused right now. a_gap: the longest a LIVE poll
		// interval can be; a longer one means nothing ran, and that time is not scene time.
		static void Tick(bool a_still, duration a_gap) noexcept
		{
			const auto real = std::chrono::steady_clock::now().time_since_epoch().count();
			const auto prev = Last().exchange(real);
			if (prev == 0) {
				return;
			}
			const auto delta = real - prev;
			if (delta > 0 && (a_still || delta > a_gap.count())) {
				Frozen().fetch_add(delta);
			}
		}

	private:
		[[nodiscard]] static std::atomic<rep>& Frozen() noexcept
		{
			static std::atomic<rep> frozen{ 0 };
			return frozen;
		}

		[[nodiscard]] static std::atomic<rep>& Seen() noexcept
		{
			static std::atomic<rep> seen{ 0 };
			return seen;
		}

		[[nodiscard]] static std::atomic<rep>& Last() noexcept
		{
			static std::atomic<rep> last{ 0 };
			return last;
		}
	};
}
