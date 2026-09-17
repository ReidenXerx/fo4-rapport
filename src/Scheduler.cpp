#include "Scheduler.h"

#include "Config.h"

namespace AF
{
	Scheduler& Scheduler::GetSingleton() noexcept
	{
		static Scheduler singleton;
		return singleton;
	}

	void Scheduler::Start()
	{
		if (_started.exchange(true)) {
			return;
		}
		_thread = std::jthread{ [this](std::stop_token a_stop) { ThreadMain(a_stop); } };
		logger::info("scheduler started (tick {}s)", Config::GetSingleton().tickSeconds);
	}

	void Scheduler::OnLoad()
	{
		const auto& config = Config::GetSingleton();
		{
			std::scoped_lock lock{ _clockLock };
			_nextTickAt = std::chrono::steady_clock::now() + std::chrono::seconds{ config.warmupSeconds };
		}
		_inSession.store(true);
		logger::info("session ready — warming up for {}s before the first tick", config.warmupSeconds);
	}

	void Scheduler::OnUnload() noexcept
	{
		_inSession.store(false);
	}

	void Scheduler::Stop() noexcept
	{
		_thread.request_stop();
	}

	void Scheduler::ThreadMain(std::stop_token a_stop)
	{
		const auto tick = std::chrono::seconds{ Config::GetSingleton().tickSeconds };

		while (!a_stop.stop_requested()) {
			// Wake often enough to notice a stop request without holding a long sleep.
			std::this_thread::sleep_for(std::chrono::milliseconds{ 250 });
			if (a_stop.stop_requested()) {
				return;
			}
			if (!_inSession.load()) {
				continue;
			}

			{
				std::scoped_lock lock{ _clockLock };
				if (std::chrono::steady_clock::now() < _nextTickAt) {
					continue;
				}
			}

			if (_passInFlight.exchange(true)) {
				// The previous pass has not finished its slices yet. Skipping is
				// correct: a tick we cannot afford is a tick we do not take.
				logger::warn("tick skipped — previous pass still in flight");
				continue;
			}

			// The next deadline is set when this pass FINISHES, not here. A task
			// posted to the main thread runs whenever the game gets round to it,
			// and scheduling from the post time turned a 19-second delay into two
			// passes one second apart.

			PostSlice();
		}
	}

	void Scheduler::PostSlice()
	{
		const auto task = F4SE::GetTaskInterface();
		if (!task) {
			_passInFlight.store(false);
			return;
		}

		task->AddTask([this]() {
			const auto& config = Config::GetSingleton();

			if (_scan.Slices() == 0 && _scan.Size() == 0) {
				_scan.Begin(config.scanRadius);
			}

			const auto started = std::chrono::steady_clock::now();
			const bool done = _scan.Step(config.frameBudgetMs);
			const auto elapsed =
				std::chrono::duration<double, std::milli>{ std::chrono::steady_clock::now() - started }.count();

			_passMs += elapsed;
			_worstSliceMs = (std::max)(_worstSliceMs, elapsed);
			_worstPassSliceEver = (std::max)(_worstPassSliceEver, elapsed);
			_totalSliceMs += elapsed;
			++_sliceCount;

			if (done) {
				FinishPass();
			} else {
				PostSlice();
			}
		});
	}

	void Scheduler::FinishPass()
	{
		const auto& counters = _scan.Counters();
		++_ticks;

		logger::info(
			"tick {}: {} actors -> {} candidates in {} slice(s), {:.3f} ms total, worst slice {:.3f} ms "
			"(stale {}, unloaded {}, child {}, dead {}, combat {}, out of range {}, race {}, dialogue {}, quest {})",
			_ticks, _scan.Size(), counters.candidates, _scan.Slices(), _passMs, _worstSliceMs,
			counters.stale, counters.notLoaded, counters.child, counters.dead, counters.inCombat,
			counters.outOfRange, counters.raceNotAllowed, counters.inDialogue, counters.questDriven);

		if (_ticks % 10 == 0) {
			logger::info(
				"budget so far: {} slices, {:.3f} ms average, {:.3f} ms worst",
				_sliceCount, _sliceCount ? _totalSliceMs / static_cast<double>(_sliceCount) : 0.0,
				_worstPassSliceEver);
		}

		_passMs = 0.0;
		_worstSliceMs = 0.0;
		_scan = {};

		{
			std::scoped_lock lock{ _clockLock };
			_nextTickAt = std::chrono::steady_clock::now() +
			              std::chrono::seconds{ Config::GetSingleton().tickSeconds };
		}
		_passInFlight.store(false);
	}
}
