#include "PapyrusLink.h"

#include "Config.h"
#include "DebugHub.h"
#include "Aftermath.h"
#include "Expressions.h"
#include "Ledger.h"
#include "Takeover.h"

namespace
{
	constexpr auto kCoreScript = "Rapport:Core"sv;

	// ---- called from Papyrus -------------------------------------------------

	void Papyrus_Trace(std::monostate, RE::BSFixedString a_text)
	{
		logger::info("{}", a_text.c_str());
	}

	std::int32_t Papyrus_TakeRequest(std::monostate)
	{
		try {
			return RP::PapyrusLink::GetSingleton().TakeRequest();
		} catch (const std::exception& e) {
			logger::critical("TakeRequest threw: {}", e.what());
			return 0;
		}
	}

	std::int32_t Papyrus_TakenFirstID(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().TakenFirstID();
	}

	std::int32_t Papyrus_TakenSecondID(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().TakenSecondID();
	}

	float Papyrus_TakenDuration(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().TakenDuration();
	}

	bool Papyrus_NeedsHandshake(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().NeedsHandshake();
	}

	float Papyrus_PollSeconds(std::monostate)
	{
		return RP::Config::GetSingleton().pollSeconds;
	}

	void Papyrus_NoteEvent(std::monostate, RE::BSFixedString a_name)
	{
		RP::PapyrusLink::GetSingleton().NoteEvent(a_name.c_str());
	}

	void Papyrus_NoteActorBusy(std::monostate, std::int32_t a_formID)
	{
		RP::PapyrusLink::GetSingleton().NoteActorBusy(static_cast<std::uint32_t>(a_formID));
	}

	// ---- aftermath -----------------------------------------------------------
	// AAF's tags say what an animation WAS, and only the bridge can hear them. The
	// plugin decides what that leaves behind, so the tags have to come over.

	// Only Papyrus holds a real Actor to ask, so the sex comes from there rather
	// than from a form lookup on whichever thread happens to be running.
	void Papyrus_NoteActorSex(std::monostate, std::int32_t a_formID, std::int32_t a_sex)
	{
		RP::Aftermath::GetSingleton().NoteSex(static_cast<std::uint32_t>(a_formID), a_sex);
	}

	// The order AAF actually placed them in. It is the only thing that separates a
	// same-sex pair, and slot 0 is the receiving role in 559 of the 562 two-actor
	// animations that name both genders.
	void Papyrus_NoteSceneSlots(std::monostate, std::int32_t a_slot0, std::int32_t a_slot1)
	{
		RP::Aftermath::GetSingleton().NoteSlots(
			static_cast<std::uint32_t>(a_slot0), static_cast<std::uint32_t>(a_slot1));
	}

	void Papyrus_NoteSceneTags(std::monostate, RE::BSFixedString a_tags)
	{
		// BOTH. One decides what a scene leaves behind, the other decides what the
		// face does while it happens, and they read the same tags for it.
		RP::Aftermath::GetSingleton().NoteTags(a_tags.c_str());
		RP::Expressions::GetSingleton().NoteTags(a_tags.c_str());
	}

	// The second doorbell. Same shape as the first and for the same reason: the
	// plugin cannot call AAF, so it leaves an instruction and the bridge collects
	// it on the poll it is already making.
	// Called at the top of every poll. Everything that has to happen on a clock
	// finer than the scheduler's twenty seconds lives behind this: the expression
	// progression through a scene, and the clearing afterwards.
	// Guarded, and it is not defensive decoration. A C++ exception crossing back
	// into the VM kills the Papyrus stack that made the call, and leaves a log
	// indistinguishable from a native that simply never returned -- which is the
	// pair of states three runs have now been spent telling apart.
	void Papyrus_Pump(std::monostate)
	{
		RP::PapyrusLink::GetSingleton().NotePump();

		// Loud, and only while a scene is running, so it is a handful of lines per
		// scene rather than a flood. It exists because the last run produced three
		// negatives -- no deadlock, no exception, no step -- which rules out our
		// locks and our exceptions but leaves "was Pump even entered?" unanswered.
		// The counter above proves the NATIVE was called; this proves the body was.
		const auto watching = RP::PapyrusLink::GetSingleton().Busy();
		if (watching) {
			logger::info("pump: entering with a scene in flight");
		}

		try {
			RP::Expressions::GetSingleton().Pump();
			if (watching) {
				logger::info("pump: returned normally");
			}
		} catch (const std::exception& e) {
			logger::critical("Pump threw: {} - the bridge's poll would have died here silently", e.what());
		} catch (...) {
			logger::critical("Pump threw something that is not a std::exception - the poll would have died here silently");
		}
	}

	std::int32_t Papyrus_SceneToStop(std::monostate)
	{
		try {
			return RP::PapyrusLink::GetSingleton().SceneToStop();
		} catch (const std::exception& e) {
			logger::critical("SceneToStop threw: {}", e.what());
			return 0;
		}
	}

	void Papyrus_NoteStopAsked(std::monostate)
	{
		RP::PapyrusLink::GetSingleton().NoteStopAsked();
	}

	std::int32_t Papyrus_TakeOverlayOrder(std::monostate)
	{
		try {
			return RP::PapyrusLink::GetSingleton().TakeOverlayOrder();
		} catch (const std::exception& e) {
			logger::critical("TakeOverlayOrder threw: {}", e.what());
			return 0;
		}
	}

	std::int32_t Papyrus_OrderActorID(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().OrderActorID();
	}

	RE::BSFixedString Papyrus_OrderSetID(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().OrderSetID().c_str();
	}

	// ---- the optional Commonwealth Moisturizer plugin ------------------------
	// Its own doorbell, drained by its own script. Rapport's bridge must never
	// name a Moisturizer type: it would carry an unresolvable reference on every
	// install that does not have the mod.

	std::int32_t Papyrus_TakeMoisturizerOrder(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().TakeMoisturizerOrder();
	}

	std::int32_t Papyrus_MoisturizerActorID(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().MoisturizerActorID();
	}

	RE::BSFixedString Papyrus_MoisturizerRegions(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().MoisturizerRegions().c_str();
	}

	// Three booleans rather than a string to pick apart: Papyrus has no string
	// search without F4SE's StringUtil, and the plugin already knows the answer.
	bool Papyrus_MoisturizerFront(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().MoisturizerHas('F');
	}

	bool Papyrus_MoisturizerOral(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().MoisturizerHas('O');
	}

	bool Papyrus_MoisturizerRear(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().MoisturizerHas('R');
	}

	void Papyrus_DeferOrder(std::monostate, std::int32_t a_formID)
	{
		RP::Aftermath::GetSingleton().Defer(static_cast<std::uint32_t>(a_formID));
	}

	bool Papyrus_MoisturizerWanted(std::monostate)
	{
		return RP::Aftermath::GetSingleton().Which() == RP::Aftermath::Backend::kMoisturizer;
	}

	// ---- takeover ------------------------------------------------------------

	std::int32_t Papyrus_TakeoverCount(std::monostate)
	{
		return static_cast<std::int32_t>(RP::Takeover::GetSingleton().Items().size());
	}

	[[nodiscard]] const RP::Takeover::Item* TakeoverItem(std::int32_t a_index)
	{
		const auto& items = RP::Takeover::GetSingleton().Items();
		if (a_index < 0 || static_cast<std::size_t>(a_index) >= items.size()) {
			return nullptr;
		}
		return &items[static_cast<std::size_t>(a_index)];
	}

	std::int32_t Papyrus_TakeoverFormID(std::monostate, std::int32_t a_index)
	{
		const auto item = TakeoverItem(a_index);
		return item ? static_cast<std::int32_t>(item->formID) : 0;
	}

	RE::BSFixedString Papyrus_TakeoverName(std::monostate, std::int32_t a_index)
	{
		const auto item = TakeoverItem(a_index);
		return item ? item->name.c_str() : "";
	}

	RE::BSFixedString Papyrus_TakeoverReason(std::monostate, std::int32_t a_index)
	{
		const auto item = TakeoverItem(a_index);
		return item ? item->reason.c_str() : "";
	}

	bool Papyrus_TakeoverShouldStop(std::monostate, std::int32_t a_index)
	{
		return a_index >= 0 &&
		       RP::Takeover::GetSingleton().ShouldTakeOver(static_cast<std::size_t>(a_index));
	}

	// ---- the debug hub's table, read by the bridge on connect ----------------

	std::int32_t Papyrus_DebugCount(std::monostate)
	{
		return static_cast<std::int32_t>(RP::DebugHub::GetSingleton().Entries().size());
	}

	[[nodiscard]] const RP::DebugHub::Entry* DebugEntry(std::int32_t a_index)
	{
		const auto& entries = RP::DebugHub::GetSingleton().Entries();
		if (a_index < 0 || static_cast<std::size_t>(a_index) >= entries.size()) {
			return nullptr;
		}
		return &entries[static_cast<std::size_t>(a_index)];
	}

	RE::BSFixedString Papyrus_DebugTarget(std::monostate, std::int32_t a_index)
	{
		const auto entry = DebugEntry(a_index);
		return entry ? (entry->target == RP::DebugHub::Target::kAAF ? "aaf" : "mcm") : "";
	}

	RE::BSFixedString Papyrus_DebugMod(std::monostate, std::int32_t a_index)
	{
		const auto entry = DebugEntry(a_index);
		return entry ? entry->mod.c_str() : "";
	}

	RE::BSFixedString Papyrus_DebugKey(std::monostate, std::int32_t a_index)
	{
		const auto entry = DebugEntry(a_index);
		return entry ? entry->key.c_str() : "";
	}

	RE::BSFixedString Papyrus_DebugType(std::monostate, std::int32_t a_index)
	{
		const auto entry = DebugEntry(a_index);
		return entry ? entry->type.c_str() : "";
	}

	RE::BSFixedString Papyrus_DebugValue(std::monostate, std::int32_t a_index)
	{
		const auto entry = DebugEntry(a_index);
		return entry ? entry->value.c_str() : "";
	}

	void Papyrus_BridgeReady(std::monostate, bool a_aafPresent)
	{
		RP::PapyrusLink::GetSingleton().OnBridgeReady(a_aafPresent);
	}

	void Papyrus_SceneStarted(std::monostate, std::int32_t a_request)
	{
		RP::PapyrusLink::GetSingleton().OnSceneStarted(a_request);
	}

	void Papyrus_SceneEnded(std::monostate, std::int32_t a_request)
	{
		RP::PapyrusLink::GetSingleton().OnSceneEnded(a_request);
	}

	void Papyrus_RequestFailed(std::monostate, std::int32_t a_request, RE::BSFixedString a_why)
	{
		RP::PapyrusLink::GetSingleton().OnRequestFailed(a_request, a_why.c_str());
	}
}

namespace RP
{
	PapyrusLink& PapyrusLink::GetSingleton() noexcept
	{
		static PapyrusLink singleton;
		return singleton;
	}

	bool PapyrusLink::RegisterNatives(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			return false;
		}

		a_vm->BindNativeMethod(kCoreScript, "Trace"sv, Papyrus_Trace, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeRequest"sv, Papyrus_TakeRequest, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakenFirstID"sv, Papyrus_TakenFirstID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakenSecondID"sv, Papyrus_TakenSecondID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakenDuration"sv, Papyrus_TakenDuration, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "PollSeconds"sv, Papyrus_PollSeconds, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NeedsHandshake"sv, Papyrus_NeedsHandshake, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteEvent"sv, Papyrus_NoteEvent, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteActorBusy"sv, Papyrus_NoteActorBusy, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteSceneTags"sv, Papyrus_NoteSceneTags, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteActorSex"sv, Papyrus_NoteActorSex, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteSceneSlots"sv, Papyrus_NoteSceneSlots, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "Pump"sv, Papyrus_Pump, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneToStop"sv, Papyrus_SceneToStop, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteStopAsked"sv, Papyrus_NoteStopAsked, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeOverlayOrder"sv, Papyrus_TakeOverlayOrder, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "OrderActorID"sv, Papyrus_OrderActorID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "OrderSetID"sv, Papyrus_OrderSetID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeMoisturizerOrder"sv, Papyrus_TakeMoisturizerOrder, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerActorID"sv, Papyrus_MoisturizerActorID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerRegions"sv, Papyrus_MoisturizerRegions, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DeferOrder"sv, Papyrus_DeferOrder, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerFront"sv, Papyrus_MoisturizerFront, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerOral"sv, Papyrus_MoisturizerOral, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerRear"sv, Papyrus_MoisturizerRear, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerWanted"sv, Papyrus_MoisturizerWanted, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeoverCount"sv, Papyrus_TakeoverCount, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeoverFormID"sv, Papyrus_TakeoverFormID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeoverName"sv, Papyrus_TakeoverName, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeoverReason"sv, Papyrus_TakeoverReason, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeoverShouldStop"sv, Papyrus_TakeoverShouldStop, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DebugCount"sv, Papyrus_DebugCount, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DebugTarget"sv, Papyrus_DebugTarget, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DebugMod"sv, Papyrus_DebugMod, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DebugKey"sv, Papyrus_DebugKey, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DebugType"sv, Papyrus_DebugType, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DebugValue"sv, Papyrus_DebugValue, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "BridgeReady"sv, Papyrus_BridgeReady, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneStarted"sv, Papyrus_SceneStarted, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneEnded"sv, Papyrus_SceneEnded, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "RequestFailed"sv, Papyrus_RequestFailed, std::nullopt, false);

		logger::info("papyrus: bound 41 native functions on {}", kCoreScript);
		return true;
	}

	void PapyrusLink::OnDataReady()
	{
		// Nothing to resolve any more: the bridge finds us, not the other way
		// round. Kept so the log still says whether the plugin is present.
		const auto handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return;
		}
		if (const auto quest = handler->LookupForm<RE::TESQuest>(0x00000800, "Rapport.esp"sv)) {
			logger::info("papyrus: bridge quest {:08X} is loaded", quest->GetFormID());
		} else {
			logger::error("papyrus: Rapport.esp is not loaded - enable it in the load order");
		}
	}

	bool PapyrusLink::RequestScene(RE::Actor* a_first, RE::Actor* a_second, float a_duration)
	{
		if (!a_first || !a_second) {
			return false;
		}
		if (!_bridgeReady.load()) {
			logger::warn("scene wanted before the bridge reported ready");
			return false;
		}
		if (_sceneInFlight.exchange(true)) {
			return false;
		}

		const auto request = _nextRequest.fetch_add(1);
		{
			NamedLock lock{ _counter, "request counter" };
			_pending = Pending{
				request,
				static_cast<std::int32_t>(a_first->GetFormID()),
				static_cast<std::int32_t>(a_second->GetFormID()),
				a_duration
			};
		}

		_inFlightFirst = static_cast<std::int32_t>(a_first->GetFormID());
		_inFlightSecond = static_cast<std::int32_t>(a_second->GetFormID());
		_inFlightDuration = a_duration;
		_inFlightRequest = request;
		_sceneRunning = false;
		_stopAsked = false;
		_requestedAt = std::chrono::steady_clock::now();
		_queued.fetch_add(1);

		logger::info(
			"request {}: queued {} ({:08X}) and {} ({:08X}) for {:.0f}s",
			request, a_first->GetDisplayFullName(), a_first->GetFormID(),
			a_second->GetDisplayFullName(), a_second->GetFormID(), a_duration);
		return true;
	}

	std::int32_t PapyrusLink::TakeRequest()
	{
		NamedLock lock{ _counter, "request counter" };
		if (_pending.request == 0) {
			return 0;
		}

		_collected.fetch_add(1);
		_takenFirst = _pending.first;
		_takenSecond = _pending.second;
		_takenDuration = _pending.duration;

		const auto request = _pending.request;
		_pending = Pending{};
		return request;
	}

	void PapyrusLink::CheckWatchdog(float a_sceneSeconds)
	{
		if (!_sceneInFlight.load()) {
			return;
		}

		// Generous: the scene's own length, the bridge's own timeout, and room for
		// AAF to walk two people across a market before anyone calls it stuck.
		const auto limit = std::chrono::seconds{ static_cast<std::int64_t>(a_sceneSeconds) + 180 };
		if (std::chrono::steady_clock::now() - _requestedAt < limit) {
			return;
		}

		logger::error(
			"watchdog: nothing has been heard about the running scene for {}s - releasing",
			limit.count());
		{
			NamedLock lock{ _counter, "request counter" };
			_pending = Pending{};
		}
		// Deliberately no ledger entry. The watchdog firing means we do not know
		// what happened, and a guess written into a save outlives the session that
		// made it. The face and the busy flags are a different matter: those are
		// state we put on somebody, and not knowing what happened is exactly when
		// they have to come off.
		Expressions::GetSingleton().OnSceneEnded();
		Release(static_cast<std::uint32_t>(_inFlightFirst));
		Release(static_cast<std::uint32_t>(_inFlightSecond));
		_inFlightFirst = 0;
		_inFlightSecond = 0;
		_inFlightRequest = 0;
		_sceneRunning = false;
		_stopAsked = false;
		_sceneInFlight.store(false);
	}

	void PapyrusLink::OnBridgeReady(bool a_aafPresent)
	{
		_bridgeReady.store(a_aafPresent);
		if (a_aafPresent) {
			logger::info("bridge ready and listening every {:.1f}s", Config::GetSingleton().pollSeconds);
		} else {
			logger::error("bridge reported AAF missing - nothing will be started");
		}
	}

	std::int32_t PapyrusLink::SceneToStop()
	{
		if (!_sceneRunning || _stopAsked || _inFlightRequest == 0) {
			return 0;
		}
		const auto elapsed =
			std::chrono::duration<float>{ std::chrono::steady_clock::now() - _sceneStartedAt }.count();
		return elapsed >= _inFlightDuration ? _inFlightRequest : 0;
	}

	void PapyrusLink::NoteStopAsked()
	{
		_stopAsked = true;
	}

	void PapyrusLink::OnSceneStarted(std::int32_t a_request)
	{
		_started.fetch_add(1);
		logger::info("request {}: scene started", a_request);

		// The clock starts HERE, not when the request was made: AAF walks the pair
		// to each other first, and that walk is not the scene. Measured at 12.5
		// seconds across an open market -- 40% of a thirty-second scene.
		_sceneStartedAt = std::chrono::steady_clock::now();
		_sceneRunning = true;
		_stopAsked = false;

		Expressions::GetSingleton().OnSceneStarted(
			static_cast<std::uint32_t>(_inFlightFirst),
			static_cast<std::uint32_t>(_inFlightSecond),
			_inFlightDuration);
	}

	void PapyrusLink::NoteEvent(std::string_view a_name)
	{
		_events.fetch_add(1);
		_lastEventAt = std::chrono::steady_clock::now();
		(void)a_name;
	}

	void PapyrusLink::QueueOrder(Order a_order)
	{
		const auto forMoisturizer = a_order.kind == Order::Kind::kApplyMoisturizer ||
		                            a_order.kind == Order::Kind::kClearMoisturizer;
		NamedLock lock{ _orderLock, "order queue" };
		if (forMoisturizer) {
			_cmkzOrders.push_back(std::move(a_order));
		} else {
			_orders.push_back(std::move(a_order));
		}
	}

	std::int32_t PapyrusLink::TakeMoisturizerOrder()
	{
		NamedLock lock{ _orderLock, "order queue" };
		if (_cmkzOrders.empty()) {
			_cmkzActor = 0;
			_cmkzRegions.clear();
			return 0;
		}

		const auto order = _cmkzOrders.front();
		_cmkzOrders.pop_front();

		_cmkzActor = static_cast<std::int32_t>(order.formID);
		// "CMkz:FOR" -> "FOR". The prefix is only there so a mark can be told apart
		// from an overlay set id after a round trip through the save.
		const auto colon = order.setID.find(':');
		_cmkzRegions = colon == std::string::npos ? order.setID : order.setID.substr(colon + 1);
		return static_cast<std::int32_t>(order.kind);
	}

	std::size_t PapyrusLink::PendingOrders() const
	{
		NamedLock lock{ _orderLock, "order queue" };
		return _orders.size();
	}

	std::int32_t PapyrusLink::TakeOverlayOrder()
	{
		NamedLock lock{ _orderLock, "order queue" };
		if (_orders.empty()) {
			_orderActor = 0;
			_orderSet.clear();
			return 0;
		}

		const auto order = _orders.front();
		_orders.pop_front();

		_orderActor = static_cast<std::int32_t>(order.formID);
		_orderSet = order.setID;
		return static_cast<std::int32_t>(order.kind);
	}

	void PapyrusLink::Release(std::uint32_t a_formID)
	{
		if (a_formID == 0) {
			return;
		}
		QueueOrder(Order{ Order::Kind::kRelease, a_formID, {} });
	}

	void PapyrusLink::RestoreInFlightPair(std::uint32_t a_first, std::uint32_t a_second)
	{
		if (a_first == 0 && a_second == 0) {
			return;
		}

		// A save taken in the middle of a scene. AAF stamped both of them busy and
		// only a scene ending clears that -- and the scene this save remembers no
		// longer exists anywhere. Left alone they are unusable by every AAF mod on
		// the machine, for the rest of the playthrough, and nothing says why.
		logger::warn(
			"a scene was running when this save was made: releasing {:08X} and {:08X}, "
			"which AAF would otherwise leave flagged busy forever",
			a_first, a_second);
		Release(a_first);
		Release(a_second);
	}

	void PapyrusLink::NoteActorBusy(std::uint32_t a_formID)
	{
		const auto seconds = Config::GetSingleton().busyBackoffSeconds;
		if (seconds <= 0.0f) {
			return;
		}

		const auto until = std::chrono::steady_clock::now() +
		                   std::chrono::seconds{ static_cast<std::int64_t>(seconds) };
		{
			NamedLock lock{ _busyLock, "busy bench" };
			_busyUntil[a_formID] = until;
		}
		logger::info("{:08X} is flagged busy in AAF - passing over them for {:.0f}s", a_formID, seconds);
	}

	bool PapyrusLink::IsActorBusy(std::uint32_t a_formID)
	{
		NamedLock lock{ _busyLock, "busy bench" };
		const auto entry = _busyUntil.find(a_formID);
		if (entry == _busyUntil.end()) {
			return false;
		}
		// The bench is cleared lazily: an actor is only ever asked about when the
		// scheduler is already looking at them, so there is nothing to sweep.
		if (std::chrono::steady_clock::now() >= entry->second) {
			_busyUntil.erase(entry);
			return false;
		}
		_busySkips.fetch_add(1);
		return true;
	}

	void PapyrusLink::CheckBridgeAlive()
	{
		const auto pumps = _pumps.load();
		const auto moved = pumps != _pumpsAtLastTick;
		_pumpsAtLastTick = pumps;

		if (moved) {
			if (_stallReported) {
				logger::info("the bridge is answering again after {} poll(s) total", pumps);
				_stallReported = false;
			}
			return;
		}

		if (!_bridgeReady.load() || _stallReported) {
			return;
		}

		_stallReported = true;
		logger::error(
			"THE BRIDGE HAS STOPPED POLLING. It last answered after {} poll(s) and has not asked "
			"once in the last tick, so nothing timed can happen from here: no expression, no "
			"overlay, no scene ending, no request collected. A poll with nothing to do and a poll "
			"that never happened write the same log, which is why this is said out loud.{}",
			pumps,
			_sceneInFlight.load()
				? " A scene is in flight, and the most likely cause is the AAF call that started it:"
				  " a Papyrus stack does not return from one."
				: "");
	}

	void PapyrusLink::LogHealth() const
	{
		const auto queued = _queued.load();
		const auto events = _events.load();

		if (events == 0) {
			// The sentence that would have saved a night. An absence, said out loud.
			if (queued > 0) {
				logger::error(
					"health: {} scene(s) requested and NOT ONE AAF EVENT has arrived this session. "
					"Scenes may well be running; this mod cannot see them, so cooldowns and scene "
					"ends are guesses and the watchdog is doing all the releasing.",
					queued);
			} else {
				logger::warn("health: no AAF event has arrived yet this session (nothing requested yet)");
			}
		} else {
			const auto since = std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::steady_clock::now() - _lastEventAt).count();
			logger::info("health: {} aaf event(s), last {}s ago", events, since);
		}

		logger::info(
			"health: bridge {}, {} queued, {} collected, {} started, {} ended, {} failed, {}",
			_bridgeReady.load() ? "ready" : "NOT READY",
			queued, _collected.load(), _started.load(), _ended.load(), _failed.load(),
			_sceneInFlight.load() ? "a scene is in flight" : "idle");

		logger::info(
			"health: the save remembers {} actor(s) and {} standing overlay(s)",
			Ledger::GetSingleton().Size(), Aftermath::GetSingleton().Size());

		if (const auto pumps = _pumps.load(); pumps == 0) {
			logger::error(
				"health: the bridge has NEVER polled. Nothing timed can happen - no expression, no "
				"overlay, no removal - and a framework that is not being asked looks identical to "
				"one that has decided to do nothing.");
		} else {
			logger::info("health: {} poll(s), {} order(s) waiting", pumps, PendingOrders());
		}

		if (const auto skips = _busySkips.load(); skips > 0) {
			NamedLock lock{ _busyLock, "busy bench" };
			logger::info(
				"health: {} candidate(s) passed over for AAF's busy flag, {} actor(s) still on the bench",
				skips, _busyUntil.size());
		}
	}

	void PapyrusLink::OnSceneEnded(std::int32_t a_request)
	{
		_ended.fetch_add(1);
		logger::info("request {}: scene ended", a_request);

		// A scene that ENDED is the only thing worth remembering. One that failed
		// says nothing about these two beyond "not now", and writing it as history
		// would put a cooldown on people who never had a scene.
		if (_inFlightFirst != 0 && _inFlightSecond != 0) {
			Ledger::GetSingleton().RecordScene(
				static_cast<std::uint32_t>(_inFlightFirst),
				static_cast<std::uint32_t>(_inFlightSecond));
			Aftermath::GetSingleton().OnSceneEnded(
				static_cast<std::uint32_t>(_inFlightFirst),
				static_cast<std::uint32_t>(_inFlightSecond));
		}
		Expressions::GetSingleton().OnSceneEnded();
		_inFlightFirst = 0;
		_inFlightSecond = 0;
		_inFlightRequest = 0;
		_sceneRunning = false;
		_stopAsked = false;
		_sceneInFlight.store(false);
	}

	void PapyrusLink::OnRequestFailed(std::int32_t a_request, std::string_view a_why)
	{
		_failed.fetch_add(1);
		logger::warn("request {}: {}", a_request, a_why);

		Ledger::GetSingleton().RecordRefusal(
			static_cast<std::uint32_t>(_inFlightFirst),
			static_cast<std::uint32_t>(_inFlightSecond));
		Expressions::GetSingleton().OnSceneEnded();

		_inFlightFirst = 0;
		_inFlightSecond = 0;
		_inFlightRequest = 0;
		_sceneRunning = false;
		_stopAsked = false;
		_sceneInFlight.store(false);
	}
}
