#pragma once

#include "ActorScan.h"

namespace RP
{
	// Owns the only repeating work in the plugin: a timer thread that asks the
	// main thread for short slices. No per-NPC script, no polling loop, and
	// nothing at all runs until the warm-up after a load has passed.
	class Scheduler
	{
	public:
		[[nodiscard]] static Scheduler& GetSingleton() noexcept;

		void Start();            // once, after game data is ready
		void OnLoad();           // new game or save loaded: restart the warm-up
		void OnUnload() noexcept;  // leaving a game session: stop ticking
		void Stop() noexcept;

	private:
		void ThreadMain(std::stop_token a_stop);
		void PostSlice();
		void FinishPass();

		std::jthread     _thread;
		std::atomic_bool _started{ false };
		std::atomic_bool _inSession{ false };
		bool             _panicked{ false };   // PanicClear runs once per load
		std::atomic_bool _passInFlight{ false };
		std::atomic_bool _skipReported{ false };

		std::mutex                            _clockLock;
		std::chrono::steady_clock::time_point _nextTickAt{};

		ActorScan     _scan;   // main thread only
		std::uint64_t _ticks{ 0 };

		// Timing counters, main thread only.
		double _passMs{ 0.0 };
		double _worstSliceMs{ 0.0 };
		double _worstPassSliceEver{ 0.0 };
		double _totalSliceMs{ 0.0 };
		std::uint64_t _sliceCount{ 0 };
	};
}
