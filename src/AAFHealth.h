#pragma once

#include "NamedLock.h"
#include "Orders.h"

namespace RP
{
	// AAF stops answering, and nothing tells you.
	//
	// AAF re-initialises off Actor.OnPlayerLoadGame (AAF_QuestBase.psc:15), and
	// the last thing that initialisation does is
	// ui.Invoke("HUDMenu", SWFPath + ".reboot") -- a call into a Scaleform menu.
	// That event fires very early in a load, and on a load screen, or on the
	// second of two loads in quick succession, the invoke goes nowhere.
	// AAF_ReadyStatus is left at 0 and NOTHING sets it again: the flag becomes 1
	// only when AAF's own SWF sends "OnAAFReady" back up
	// (AAF_MainQuestScript.psc:1140), and the SWF was never asked a second time.
	//
	// From outside it looks like nothing at all. StartScene is accepted, no error
	// comes back, no event arrives, and the scene simply never happens. Three
	// saves loaded in a row produced exactly that, and the only trace was a
	// number -- status 1 -- that had been in the log since the first session with
	// nobody acting on it.
	//
	// So this watches that number over time and calls AAF's own
	// EveryTime_Initialization() again once it has been wrong for long enough.
	// That is not a hack around AAF: it is the function AAF runs on every load,
	// and runs again itself when the LooksMenu closes
	// (AAF_MainQuestScript.psc:263). Re-ringing a doorbell AAF already rings
	// twice.
	class AAFHealth
	{
	public:
		// GetAAFStatus() is AAF_ReadyStatus + 1, except that it returns 0 when
		// AAF's main quest is not running at all (AAF_API.psc:440). Those two
		// failures need different cures and we were collapsing them:
		//
		//   0  the main quest is stopped        -> Start(), and only the player
		//                                          can say whether that is wanted
		//   1  running, the SWF never announced -> EveryTime_Initialization()
		//   2  ready
		//
		// -1 is ours: Papyrus has not answered yet, so nothing is known and
		// nothing should be done.
		static constexpr std::int32_t kUnknown = -1;
		static constexpr std::int32_t kQuestStopped = 0;
		static constexpr std::int32_t kNotAnnounced = 1;
		static constexpr std::int32_t kReady = 2;

		[[nodiscard]] static AAFHealth& GetSingleton() noexcept;

		// Reported by the bridge on every poll, immediately before Pump, so the
		// number this acts on can never be a stale one.
		void NoteStatus(std::int32_t a_status);

		// Decides, on the same poll as everything else. Queues at most one order.
		void Pump();

		// The player's answer to "AAF's quest is not running - start it?".
		void NoteChoice(bool a_yes);

		// A load is where this breaks, so a load is where the count starts again:
		// the previous session's attempts say nothing about this one.
		void Reset();

		[[nodiscard]] std::int32_t Status() const;

		// For the health line. AAF being unwell has to be visible while it is
		// happening, not only in the log entry that eventually explains it.
		[[nodiscard]] std::string Summary() const;

		[[nodiscard]] static std::string_view Describe(std::int32_t a_status);

	private:
		[[nodiscard]] std::optional<Order> Decide();

		mutable std::timed_mutex _lock;

		std::int32_t  _status{ kUnknown };
		std::uint32_t _attempts{ 0 };
		std::uint32_t _revivals{ 0 };   // times it came back after we restarted it
		bool          _asked{ false };  // the quest-stopped question, asked once
		bool          _gaveUp{ false };

		std::optional<std::chrono::steady_clock::time_point> _unhealthySince;
		std::optional<std::chrono::steady_clock::time_point> _lastAttemptAt;
	};
}
