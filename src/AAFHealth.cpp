#include "AAFHealth.h"

#include "Config.h"
#include "PapyrusLink.h"

namespace
{
	[[nodiscard]] float SecondsSince(std::chrono::steady_clock::time_point a_then)
	{
		return std::chrono::duration<float>{ std::chrono::steady_clock::now() - a_then }.count();
	}
}

namespace RP
{
	AAFHealth& AAFHealth::GetSingleton() noexcept
	{
		static AAFHealth singleton;
		return singleton;
	}

	std::string_view AAFHealth::Describe(std::int32_t a_status)
	{
		switch (a_status) {
		case kUnknown:
			return "not reported yet";
		case kQuestStopped:
			return "AAF's main quest is not running";
		case kNotAnnounced:
			return "running, but its interface has never announced itself";
		case kReady:
			return "ready";
		default:
			return "a value AAF has never returned before";
		}
	}

	void AAFHealth::NoteStatus(std::int32_t a_status)
	{
		NamedLock lock{ _lock, "aaf health" };
		_status = a_status;
	}

	std::int32_t AAFHealth::Status() const
	{
		NamedLock lock{ _lock, "aaf health" };
		return _status;
	}

	void AAFHealth::Reset()
	{
		NamedLock lock{ _lock, "aaf health" };
		_status = kUnknown;
		_attempts = 0;
		_asked = false;
		_gaveUp = false;
		_unhealthySince.reset();
		_lastAttemptAt.reset();
	}

	void AAFHealth::NoteChoice(bool a_yes)
	{
		NamedLock lock{ _lock, "aaf health" };
		if (a_yes) {
			logger::info("aaf watchdog: starting AAF's main quest at the player's word");
			_lastAttemptAt = std::chrono::steady_clock::now();
			return;
		}

		// They said no, so they meant no. Not for this outage, not every poll for
		// the rest of the session.
		_gaveUp = true;
		logger::info(
			"aaf watchdog: the player chose to leave AAF's quest alone - no scene will start, "
			"and this will not be asked again this session");
	}

	std::string AAFHealth::Summary() const
	{
		NamedLock lock{ _lock, "aaf health" };
		if (_status == kReady) {
			return _revivals == 0 ? std::string{ "aaf ready" }
			                      : std::format("aaf ready (restarted {}x)", _revivals);
		}
		return std::format("aaf NOT READY: status {} - {}", _status, Describe(_status));
	}

	void AAFHealth::Pump()
	{
		std::optional<Order> outgoing;
		{
			NamedLock lock{ _lock, "aaf health" };
			outgoing = Decide();
		}
		if (outgoing) {
			PapyrusLink::GetSingleton().QueueOrder(*outgoing);
		}
	}

	// Called under the lock. Returns the one order to send, if any.
	std::optional<Order> AAFHealth::Decide()
	{
		const auto& config = Config::GetSingleton();
		if (config.aafReviveAttempts == 0) {
			return std::nullopt;   // turned off
		}

		// Nothing is known yet. An unknown status is not a broken one.
		if (_status == kUnknown) {
			return std::nullopt;
		}

		const auto now = std::chrono::steady_clock::now();

		if (_status >= kReady) {
			if (_unhealthySince) {
				const auto outage = SecondsSince(*_unhealthySince);
				if (_attempts > 0) {
					++_revivals;
					logger::info(
						"aaf watchdog: AAF is answering again after {:.0f}s and {} restart(s)",
						outage, _attempts);
				} else {
					logger::info("aaf watchdog: AAF came back on its own after {:.0f}s", outage);
				}
				_unhealthySince.reset();
				_lastAttemptAt.reset();
				_attempts = 0;
				_asked = false;
				_gaveUp = false;
			}
			return std::nullopt;
		}

		if (!_unhealthySince) {
			_unhealthySince = now;
			logger::warn(
				"aaf watchdog: AAF is at status {} - {}. Giving it {:.0f}s to come back on its own",
				_status, Describe(_status), config.aafReviveGraceSeconds);
			return std::nullopt;
		}

		if (_gaveUp) {
			return std::nullopt;
		}

		// Never mid-scene. Rebooting the SWF is exactly what ends one, and a scene
		// that is running is proof AAF was working when it started.
		if (PapyrusLink::GetSingleton().Busy()) {
			return std::nullopt;
		}

		const auto outage = SecondsSince(*_unhealthySince);
		if (outage < config.aafReviveGraceSeconds) {
			return std::nullopt;
		}
		if (_lastAttemptAt && SecondsSince(*_lastAttemptAt) < config.aafReviveRetrySeconds) {
			return std::nullopt;
		}

		// A stopped quest is a different failure from a quiet one, and starting
		// another mod's quest is a bigger act than re-running its own init. The
		// reason it is stopped may be that AAF is on its way out of this save --
		// which nothing here can see and the player can. So they decide, once.
		if (_status == kQuestStopped) {
			if (_asked) {
				return std::nullopt;
			}
			_asked = true;
			_lastAttemptAt = now;
			logger::warn(
				"aaf watchdog: AAF's main quest has been stopped for {:.0f}s - asking whether to "
				"start it",
				outage);
			return Order{ Order::Kind::kAskStartAAF, 0, {}, {} };
		}

		if (_attempts >= config.aafReviveAttempts) {
			_gaveUp = true;
			logger::error(
				"aaf watchdog: AAF has not answered after {} restart(s) over {:.0f}s - giving up. "
				"No scene will start until AAF is working; restarting the game is the way back",
				_attempts, outage);
			return std::nullopt;
		}

		++_attempts;
		_lastAttemptAt = now;
		logger::warn(
			"aaf watchdog: AAF has been at status {} for {:.0f}s - calling its own "
			"EveryTime_Initialization (attempt {} of {})",
			_status, outage, _attempts, config.aafReviveAttempts);
		return Order{ Order::Kind::kReviveAAF, 0, {}, {} };
	}
}
