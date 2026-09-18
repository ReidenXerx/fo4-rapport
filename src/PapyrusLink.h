#pragma once

#include "NamedLock.h"
#include "Orders.h"

namespace RP
{
	// The join between the native scheduler and the one Papyrus script.
	//
	// Papyrus calls in; the plugin never calls out. Dispatching into the VM from
	// here crashed the game twice inside DispatchMethodCallImpl: F4SE tasks run on
	// a BSJobs job thread, and the VM packs call arguments through the per-thread
	// scrap heap. The scheduler therefore leaves a request on the counter and the
	// bridge collects it, which keeps every VM operation on the VM's own thread.
	//
	// The cost is one native call every PollSeconds that almost always returns 0.
	// That is not the polling the design rules out: no actor is touched, nothing
	// is enumerated, and the scheduler still does all the work natively.
	class PapyrusLink
	{
	public:
		[[nodiscard]] static PapyrusLink& GetSingleton() noexcept;

		static bool RegisterNatives(RE::BSScript::IVirtualMachine* a_vm);

		void OnDataReady();

		[[nodiscard]] bool Ready() const noexcept { return _bridgeReady.load(); }
		[[nodiscard]] bool Busy() const noexcept { return _sceneInFlight.load(); }

		// Leaves a request for the bridge to collect. False when one is already
		// outstanding or the bridge is not listening.
		// a_scenario is the addon's choice of story, or empty for a single
		// animation. Rapport does not pick one: the framework executes a scenario,
		// it does not decide that there should be one.
		bool RequestScene(
			RE::Actor* a_first, RE::Actor* a_second, float a_duration, std::string_view a_scenario);

		// ---- called from Papyrus ----
		std::int32_t TakeRequest();

		// The plugin is the only part of this mod that reliably starts fresh every
		// session, so it owns the answer to "have we introduced ourselves yet?".
		// The script cannot answer it: its variables persist in the save.
		[[nodiscard]] bool NeedsHandshake() const noexcept { return !_bridgeReady.load(); }

		// A game LOAD is a new lifetime for the Papyrus half even though the plugin
		// keeps running, so the bridge must introduce itself again. Its arrays come
		// back from the save -- sometimes as None, when a struct has changed shape
		// since that save was written -- and only Connect() re-creates them.
		//
		// Before this, the plugin stayed "ready" across a load, Connect never
		// re-ran, and _inFlight was None: "Cannot add elements to a None array",
		// silently, with the request simply never starting.
		void RequireHandshake();
		[[nodiscard]] std::int32_t TakenFirstID() const noexcept { return _takenFirst; }
		[[nodiscard]] std::int32_t TakenSecondID() const noexcept { return _takenSecond; }
		[[nodiscard]] float        TakenDuration() const noexcept { return _takenDuration; }

		// Belt and braces: if the bridge stops answering, nothing else would ever
		// clear the in-flight flag.
		// Takes no length: it uses the duration of the scene ACTUALLY in flight.
		//
		// It used to be handed Config::sceneSeconds, which is the default length
		// for a plain scene and has nothing to do with a scenario's. With the
		// default of 30 that made the limit 210s, and athome asks for 285 -- so
		// every scenario scene was released mid-play, without fail, and then its
		// real ending arrived to find someone else in flight.
		void CheckWatchdog();

		// The request whose scene has run for as long as we asked, or 0. AAF does
		// not enforce the duration it is given, so somebody has to, and the clock
		// belongs here: the bridge has exactly ONE timer that is known to work --
		// its poll -- and every attempt to add a second one killed the function
		// that started it. 17 polls, then the poll that called StartTimer with a
		// second id, then nothing.
		[[nodiscard]] std::int32_t SceneToStop();

		// Papyrus has asked AAF to stop it; do not ask again every three seconds.
		void NoteStopAsked();

		// Every AAF event the bridge receives is counted here. The point is not the
		// count: it is that "no AAF event has EVER arrived" becomes a fact the log
		// states out loud, instead of an absence nobody notices. A whole night was
		// spent diagnosing three different bugs out of one silence, because a scene
		// that worked and a scene that never started produced identical logs.
		void NoteEvent(std::string_view a_name);

		// Counted on every poll. A frozen count means the bridge stopped asking --
		// which looks exactly like "the framework decided to do nothing", and this
		// run could not tell those apart.
		void NotePump() { _pumps.fetch_add(1); }

		// Called once per scheduler tick. The bridge polls every PollSeconds and a
		// tick is twenty seconds, so between two ticks the count MUST have moved.
		// When it has not, the bridge has gone silent and nothing timed can happen
		// -- no expression, no overlay, no scene ending, no request collected.
		//
		// This is an alarm rather than a number on the health line because the two
		// states it separates are invisible: a poll with nothing to do writes
		// exactly the same log as a poll that never happened, and three runs went
		// into telling those apart by argument.
		void CheckBridgeAlive();

		// An actor the bridge found already flagged busy by AAF. Two things follow
		// from that flag: they cannot be animated, and nothing we do clears it --
		// only a scene ending does. So the honest response is to stop offering them
		// for a while rather than to keep asking and keep being refused.
		void NoteActorBusy(std::uint32_t a_formID);
		[[nodiscard]] bool IsActorBusy(std::uint32_t a_formID);

		// The second doorbell, shared by overlays and expressions. Latched exactly
		// like the scene one so the two follow-up calls cannot see a different
		// order from the one handed out.
		void         QueueOrder(Order a_order);
		void         QueueOrders(const std::vector<Order>& a_orders);
		std::int32_t TakeOverlayOrder();
		[[nodiscard]] std::size_t PendingOrders() const;

		// The optional Moisturizer plugin's half of the same arrangement.
		std::int32_t TakeMoisturizerOrder();
		[[nodiscard]] std::int32_t       MoisturizerActorID() const noexcept { return _cmkzActor; }
		[[nodiscard]] const std::string& MoisturizerRegions() const noexcept { return _cmkzRegions; }
		[[nodiscard]] bool MoisturizerHas(char a_letter) const noexcept
		{
			return _cmkzRegions.find(a_letter) != std::string::npos;
		}
		[[nodiscard]] std::int32_t       OrderActorID() const noexcept { return _orderActor; }
		[[nodiscard]] const std::string& OrderSetID() const noexcept { return _orderSet; }
		[[nodiscard]] const std::string& OrderExtra() const noexcept { return _orderExtra; }

		// One line that says what has and has not happened. Logged periodically and
		// on anything notable.
		void LogHealth() const;

		// Asks the bridge to take AAF's busy and locked keywords off this actor.
		void Release(std::uint32_t a_formID);

		// Who a scene of ours had hold of when the save was written. Nothing else
		// can tell us, because AAF's own scene state does not survive a save and
		// the plugin starts from nothing.
		[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> InFlightPair() const noexcept
		{
			return { static_cast<std::uint32_t>(_inFlightFirst), static_cast<std::uint32_t>(_inFlightSecond) };
		}
		void RestoreInFlightPair(std::uint32_t a_first, std::uint32_t a_second);

		void OnBridgeReady(bool a_aafPresent);
		// The scene is being restarted elsewhere for the SAME request. Its ending
		// is a move, not a finish: no ledger entry, no aftermath, no cooldown, and
		// the pair stays in flight throughout.
		void BeginRelocation();
		[[nodiscard]] bool Relocating() const noexcept { return _relocating.load(); }

		void OnSceneStarted(std::int32_t a_request);
		void OnSceneEnded(std::int32_t a_request);
		void OnRequestFailed(std::int32_t a_request, std::string_view a_why);

	private:
		struct Pending
		{
			std::int32_t request{ 0 };
			std::int32_t first{ 0 };
			std::int32_t second{ 0 };
			float        duration{ 0.0f };
		};

		mutable std::timed_mutex _counter;
		Pending            _pending{};

		// Latched by TakeRequest so the three follow-up calls cannot see a torn
		// or replaced request.
		std::int32_t _takenFirst{ 0 };
		std::int32_t _takenSecond{ 0 };
		float        _takenDuration{ 0.0f };

		// The pending request is cleared the moment the bridge collects it, but the
		// ledger needs the two actors when the scene ENDS -- which is minutes later
		// and several handoffs away.
		std::int32_t _inFlightFirst{ 0 };
		std::int32_t _inFlightSecond{ 0 };
		float        _inFlightDuration{ 0.0f };
		std::string  _inFlightScenario;
		std::int32_t _inFlightRequest{ 0 };
		std::chrono::steady_clock::time_point _sceneStartedAt{};
		bool         _sceneRunning{ false };
		bool         _stopAsked{ false };

		// Two queues, because two scripts drain them. Rapport's own bridge must
		// never name a Commonwealth Moisturizer type -- it would then carry an
		// unresolvable reference on every install without that mod -- so those
		// orders wait in their own queue for the optional plugin's script.
		mutable std::timed_mutex _orderLock;
		std::deque<Order>  _orders;
		std::deque<Order>  _cmkzOrders;
		std::int32_t       _orderActor{ 0 };
		std::string        _orderSet;
		std::string        _orderExtra;
		std::int32_t       _cmkzActor{ 0 };
		std::string        _cmkzRegions;

		std::atomic_bool          _bridgeReady{ false };
		std::atomic_bool          _sceneInFlight{ false };
		std::atomic_bool          _relocating{ false };
		std::atomic<std::int32_t> _nextRequest{ 1 };
		std::chrono::steady_clock::time_point _requestedAt{};

		std::atomic<std::uint32_t> _queued{ 0 };
		std::atomic<std::uint32_t> _collected{ 0 };
		std::atomic<std::uint32_t> _started{ 0 };
		std::atomic<std::uint32_t> _ended{ 0 };
		std::atomic<std::uint32_t> _failed{ 0 };
		std::atomic<std::uint32_t> _events{ 0 };
		std::atomic<std::uint32_t> _busySkips{ 0 };
		std::atomic<std::uint32_t> _pumps{ 0 };
		std::uint32_t _pumpsAtLastTick{ 0 };
		bool          _stallReported{ false };

		// Written from the VM thread, read from the scheduler's main-thread slice.
		mutable std::timed_mutex _busyLock;
		mutable std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> _busyUntil;
		std::chrono::steady_clock::time_point _lastEventAt{};
	};
}
