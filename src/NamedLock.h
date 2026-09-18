#pragma once

namespace RP
{
	// A mutex that reports rather than hangs.
	//
	// A deadlock on a plain std::mutex is perfectly silent: the thread stops, and
	// from the outside that is indistinguishable from a function that is slow, or
	// one that returned and did nothing. Five runs were spent telling those apart
	// by argument. A lock that gives up after a second and NAMES ITSELF turns the
	// whole class of problem into one line of log.
	//
	// The wait is deliberately long. This is not a performance guard -- every real
	// hold here is measured in microseconds -- it is a tripwire, and a second is
	// far past anything legitimate.
	class NamedLock
	{
	public:
		explicit NamedLock(std::timed_mutex& a_mutex, const char* a_name) :
			_mutex(a_mutex), _name(a_name)
		{
			_held = _mutex.try_lock_for(std::chrono::seconds{ 1 });
			if (!_held) {
				logger::critical(
					"DEADLOCK: could not take the {} lock in one second. Somebody else is holding it "
					"and is not letting go, and the caller would have stopped here silently.",
					_name);
			}
		}

		~NamedLock()
		{
			if (_held) {
				_mutex.unlock();
			}
		}

		NamedLock(const NamedLock&) = delete;
		NamedLock& operator=(const NamedLock&) = delete;

		// False when the lock was never taken. A caller that reads shared state
		// must check it; one that only writes may proceed and lose the write,
		// which is better than stopping the framework.
		[[nodiscard]] explicit operator bool() const noexcept { return _held; }

	private:
		std::timed_mutex& _mutex;
		const char*       _name;
		bool              _held{ false };
	};
}
