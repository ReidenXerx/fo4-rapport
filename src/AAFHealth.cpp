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

	void AAFHealth::NoteVersion(std::int32_t a_version)
	{
		NamedLock lock{ _lock, "aaf health" };
		if (a_version != _version) {
			_version = a_version;
			logger::info("aaf watchdog: AAF reports version {}", a_version);
		}
	}

	void AAFHealth::NoteStatus(std::int32_t a_status, bool a_hudReady)
	{
		NamedLock lock{ _lock, "aaf health" };
		_status = a_status;
		_hudReady = a_hudReady;
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
		_questStartDeclined = false;
		_heldForMenu = false;
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

		// They said no, so they meant no -- about the QUEST, which is the only
		// thing they were asked. The gentle restart is a different question and
		// stays available.
		_questStartDeclined = true;
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
		// AAF 1.7.8 probes its remembered interface, checks HUDMenu, retries a failed
		// load and reports error [107] itself. Its author asked that third parties stop
		// calling its init functions once that ships (fo4-rapport issue #1, §1) - so
		// from that version the watchdog only watches.
		if (_version >= kSelfHealingVersion) {
			if (!_saidSelfHealing) {
				_saidSelfHealing = true;
				logger::info("aaf watchdog: AAF {} recovers its own interface - Rapport watches and never re-initialises it", _version);
			}
			return std::nullopt;
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
				_heldForMenu = false;
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

		// Not unless HUDMenu is actually open. Every cure below ends in a
		// UI.Load into that menu, so firing one while it is absent does not fail
		// -- it is counted as an attempt having tried nothing, and three of those
		// exhaust the budget in under a minute while the player stares at a load
		// screen. This is the same emptiness that swallowed AAF's own reboot.
		if (!_hudReady) {
			if (!_heldForMenu) {
				_heldForMenu = true;
				logger::info(
					"aaf watchdog: holding off - HUDMenu is not open, and the restart has to "
					"reach it. Waiting rather than spending an attempt on nothing");
			}
			return std::nullopt;
		}
		_heldForMenu = false;

		// A stopped quest is a different failure from a quiet one, and starting
		// another mod's quest is a bigger act than re-running its own init. The
		// reason it is stopped may be that AAF is on its way out of this save --
		// which nothing here can see and the player can. So they decide, once.
		if (_status == kQuestStopped) {
			if (_asked || _questStartDeclined) {
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
				"No scene will start until AAF is working. LOADING A SAVE is usually enough: AAF "
				"re-initialises both its quests on a load and the count starts again here",
				_attempts, outage);
			return std::nullopt;
		}

		++_attempts;
		_lastAttemptAt = now;

		// The last attempt is AAF's own harder reboot rather than a fourth polite
		// request. Six gentle restarts across two loads changed nothing, so
		// repeating it a seventh time is not a plan.
		// The hard restart is never the FIRST thing tried. With
		// AAFReviveAttempts = 1 it used to be the only one -- so a cautious player
		// limiting how much Rapport does to another mod got the most invasive act
		// available as the opening response to a fifteen-second outage, under a
		// message claiming the gentle restart had not taken when it had never been
		// attempted.
		if (config.aafReviveAttempts > 1 && _attempts >= config.aafReviveAttempts) {
			logger::warn(
				"aaf watchdog: AAF has been at status {} for {:.0f}s and the gentle restart has "
				"not taken - stopping and starting its main quest, which is what AAF does to "
				"itself on a version change (attempt {} of {}, the last)",
				_status, outage, _attempts, config.aafReviveAttempts);
			return Order{ Order::Kind::kRestartAAFQuest, 0, {}, {} };
		}

		logger::warn(
			"aaf watchdog: AAF has been at status {} for {:.0f}s - calling its own "
			"EveryTime_Initialization (attempt {} of {})",
			_status, outage, _attempts, config.aafReviveAttempts);
		return Order{ Order::Kind::kReviveAAF, 0, {}, {} };
	}
}
