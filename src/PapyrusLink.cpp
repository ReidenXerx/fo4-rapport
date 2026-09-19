#include "PapyrusLink.h"

#include "Candidates.h"
#include "Config.h"
#include "DebugHub.h"
#include "Aftermath.h"
#include "Expressions.h"
#include "AAFHealth.h"
#include "Ledger.h"
#include "Scenarios.h"
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

	void Papyrus_NoteSceneLive(std::monostate, std::int32_t a_sceneID)
	{
		RP::PapyrusLink::GetSingleton().NoteSceneLive(a_sceneID);
	}

	void Papyrus_NoteSceneEnded(std::monostate, std::int32_t a_sceneID)
	{
		RP::PapyrusLink::GetSingleton().NoteSceneEnded(a_sceneID);
	}

	void Papyrus_NoteBridgeConnected(std::monostate)
	{
		RP::PapyrusLink::GetSingleton().NoteBridgeConnected();
	}

	bool Papyrus_BlockFaces(std::monostate)
	{
		return RP::Config::GetSingleton().blockAnimationFaces;
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

	// AAF refused what was asked for. It reports this down OnSceneInit with four
	// arguments instead of eleven, which is why a real scene init is told apart by
	// its length -- treating a refusal as a start restarted the scenario, which
	// asked for the same impossible thing again.
	void Papyrus_SceneRefused(std::monostate, RE::BSFixedString a_why)
	{
		RP::Scenarios::GetSingleton().OnRefused(a_why.c_str());
	}

	void Papyrus_NoteScenePosition(std::monostate, RE::BSFixedString a_position)
	{
		RP::Expressions::GetSingleton().NotePosition(a_position.empty() ? "" : a_position.c_str());
	}

	void Papyrus_NoteSceneTags(std::monostate, RE::BSFixedString a_tags)
	{
		// BOTH. One decides what a scene leaves behind, the other decides what the
		// face does while it happens, and they read the same tags for it.
		RP::Aftermath::GetSingleton().NoteTags(a_tags.c_str());
		RP::Expressions::GetSingleton().NoteTags(a_tags.c_str());

		// THREE. The tags say what is happening; the fact that this arrived at all
		// says the tree moved, and that is what advances the story now.
		RP::Scenarios::GetSingleton().NoteAnimationAdvanced();
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
			RP::AAFHealth::GetSingleton().Pump();
			RP::Scenarios::GetSingleton().Pump();
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

	// What AAF says about itself, reported on every poll. The plugin cannot ask
	// -- GetAAFStatus is Papyrus -- and the number has to arrive on the same poll
	// that acts on it, or the watchdog would be reasoning about a stale one.
	void Papyrus_NoteAAFStatus(std::monostate, std::int32_t a_status, bool a_hudReady)
	{
		RP::AAFHealth::GetSingleton().NoteStatus(a_status, a_hudReady);
	}

	// The player's answer to "AAF's quest is not running - start it?".
	void Papyrus_NoteAAFRevivalChoice(std::monostate, bool a_yes)
	{
		RP::AAFHealth::GetSingleton().NoteChoice(a_yes);
	}

	// The position this scene should START on, chosen from the tree catalogue for
	// the scenario the request named. Empty means "start unconstrained".
	//
	// Asked at the one moment that can act on it: StartScene honours a position,
	// and ChangePosition honours nothing.
	RE::BSFixedString Papyrus_ScenePosition(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().ChooseScenePosition().c_str();
	}

	void Papyrus_NoteStopAsked(std::monostate)
	{
		RP::PapyrusLink::GetSingleton().NoteStopAsked();
	}

	// How many orders are still queued. The bridge logs it when its per-poll
	// budget runs out, so a backlog is something the log SAYS rather than
	// something a reader infers from orders arriving late.
	std::int32_t Papyrus_PendingOrders(std::monostate)
	{
		return static_cast<std::int32_t>(RP::PapyrusLink::GetSingleton().PendingOrders());
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

	// Only the animation query uses this now: the tags to exclude.
	RE::BSFixedString Papyrus_OrderExtra(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().OrderExtra().c_str();
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

	// No arguments on purpose. The order in question is the one the plugin just
	// handed this poll, and the plugin still has it latched -- so asking the
	// bridge to pass it back would be asking it to re-describe something we
	// already know, with a chance of describing it differently.
	void Papyrus_RequeueOrder(std::monostate)
	{
		RP::PapyrusLink::GetSingleton().RequeueStranded();
	}

	// "FF004C9B (-16757605)" -- both, because both are the id somebody is holding.
	//
	// Papyrus has no hex formatter and its Int is SIGNED, so every dynamically
	// created form -- the FF-prefixed ones, which is most of what autonomy pairs:
	// settlers, drifters, the Goodneighbor watch -- prints as a negative number
	// that matches nothing in AAF's log, in xEdit, or in the console. Hex is what
	// every other tool says. The decimal is kept beside it because it is what
	// every earlier line of this log says, and a diagnosis that spans two sessions
	// should not need a converter.
	RE::BSFixedString Papyrus_FormIdText(std::monostate, std::int32_t a_formID)
	{
		return RE::BSFixedString{ std::format(
			"{:08X} ({})", static_cast<std::uint32_t>(a_formID), a_formID) };
	}

	// ---- what the medic asks, and what it is allowed to do -----------------
	//
	// The medic is a SEPARATE script on a SEPARATE quest precisely so that it is
	// still running when the bridge is not, so everything it needs has to be
	// answerable without touching the bridge. These two are that.

	std::int32_t Papyrus_BridgeSilentTicks(std::monostate)
	{
		return static_cast<std::int32_t>(RP::PapyrusLink::GetSingleton().SilentTicks());
	}

	bool Papyrus_AbandonInFlight(std::monostate, RE::BSFixedString a_why)
	{
		return RP::PapyrusLink::GetSingleton().AbandonInFlight(
			a_why.empty() ? "the medic gave up on it" : a_why.c_str());
	}

	std::int32_t Papyrus_MoisturizerLayers(std::monostate)
	{
		return RP::Aftermath::GetSingleton().Layers();
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

	// ---- the addon door: WHICH TWO -------------------------------------------
	//
	// Rapport scores, the addon selects. These publish what a pass measured about
	// every viable pair so an addon can apply its own policy -- privacy, frequency,
	// whether these two have done this before -- none of which the framework has an
	// opinion about.
	//
	// Form ids, not Actors. The pointers behind a scored pair are valid on the tick
	// that produced them; an addon reads this later on its own poll and an actor can
	// unload in between. A stale id resolves to None and the addon skips it.
	//
	// Index out of range returns 0 / false rather than erroring. That is how a
	// Papyrus loop is expected to find the end here.

	std::int32_t Papyrus_CandidateCount(std::monostate)
	{
		return static_cast<std::int32_t>(RP::Candidates::GetSingleton().Count());
	}

	std::int32_t Papyrus_CandidateFirst(std::monostate, std::int32_t a_index)
	{
		const auto offer = RP::Candidates::GetSingleton().At(static_cast<std::size_t>(a_index));
		return offer ? static_cast<std::int32_t>(offer->first) : 0;
	}

	std::int32_t Papyrus_CandidateSecond(std::monostate, std::int32_t a_index)
	{
		const auto offer = RP::Candidates::GetSingleton().At(static_cast<std::size_t>(a_index));
		return offer ? static_cast<std::int32_t>(offer->second) : 0;
	}

	float Papyrus_CandidateScore(std::monostate, std::int32_t a_index)
	{
		const auto offer = RP::Candidates::GetSingleton().At(static_cast<std::size_t>(a_index));
		return offer ? offer->score : 0.0f;
	}

	float Papyrus_CandidateDistance(std::monostate, std::int32_t a_index)
	{
		const auto offer = RP::Candidates::GetSingleton().At(static_cast<std::size_t>(a_index));
		return offer ? offer->signals.distance : 0.0f;
	}

	std::int32_t Papyrus_CandidateObservers(std::monostate, std::int32_t a_index)
	{
		const auto offer = RP::Candidates::GetSingleton().At(static_cast<std::size_t>(a_index));
		return offer ? static_cast<std::int32_t>(offer->signals.observers) : 0;
	}

	bool Papyrus_CandidatePlayerNear(std::monostate, std::int32_t a_index)
	{
		const auto offer = RP::Candidates::GetSingleton().At(static_cast<std::size_t>(a_index));
		return offer && offer->signals.playerNear;
	}

	bool Papyrus_CandidateInterior(std::monostate, std::int32_t a_index)
	{
		const auto offer = RP::Candidates::GetSingleton().At(static_cast<std::size_t>(a_index));
		return offer && offer->signals.interior;
	}

	bool Papyrus_CandidateNight(std::monostate, std::int32_t a_index)
	{
		const auto offer = RP::Candidates::GetSingleton().At(static_cast<std::size_t>(a_index));
		return offer && offer->signals.night;
	}

	bool Papyrus_CandidateSharedFaction(std::monostate, std::int32_t a_index)
	{
		const auto offer = RP::Candidates::GetSingleton().At(static_cast<std::size_t>(a_index));
		return offer && offer->signals.sharedFaction;
	}

	// An actor's display name, because Papyrus cannot get one.
	//
	// Fallout 4's base Papyrus has NO name accessor: no GetName on Form, no
	// GetDisplayName on ObjectReference, nothing on Actor. Which is why AAF's own
	// scripts never print a name and why an addon's log is otherwise a wall of form
	// ids -- and a table of 0002F0B and 000F61B6 is unreadable, which is the only
	// way a diagnostic log can fail at its job.
	//
	// The plugin side has had this all along; GetDisplayFullName is what Rapport's
	// own pair logging uses. Takes an Actor rather than a form id because the caller
	// has already resolved one through Game.GetForm, and CommonLibF4 offers no
	// lookup-by-id to do it on this side.
	RE::BSFixedString Papyrus_ActorName(std::monostate, RE::Actor* a_who)
	{
		if (!a_who) {
			return "";
		}
		const auto* const name = a_who->GetDisplayFullName();
		return name ? name : "";
	}

	// ---- the addon door: WHAT HAPPENED BEFORE --------------------------------
	//
	// The Ledger's own comment states this split: it records FACTS and nothing
	// else, and "too soon", "bored of this partner" and "wants company" are policy
	// that belongs to an addon. These publish the facts so an addon can decide.
	//
	// Game hours throughout, from the Calendar's GameDaysPassed.

	float Papyrus_HoursSinceScene(std::monostate, std::int32_t a_formID)
	{
		// Infinity for someone who has never had one, so "longest since" sorts
		// correctly. Papyrus has no infinity literal to compare against, so it
		// arrives as a very large number instead: any sane cooldown test passes.
		const auto hours =
			RP::Ledger::GetSingleton().HoursSinceScene(static_cast<std::uint32_t>(a_formID));
		return std::isfinite(hours) ? hours : 1.0e9f;
	}

	std::int32_t Papyrus_LastPartner(std::monostate, std::int32_t a_formID)
	{
		return static_cast<std::int32_t>(
			RP::Ledger::GetSingleton().Get(static_cast<std::uint32_t>(a_formID)).lastPartner);
	}

	std::int32_t Papyrus_SceneCount(std::monostate, std::int32_t a_formID)
	{
		return static_cast<std::int32_t>(
			RP::Ledger::GetSingleton().Get(static_cast<std::uint32_t>(a_formID)).scenes);
	}

	// WHEN they were last refused, not just how often. A cumulative count with no
	// recency is the wrong signal on its own: an actor AAF turned down twice a week
	// ago should not be avoided forever. The two together let an addon back off for
	// longer each time an actor keeps failing, which is the shape this needs --
	// there is at least one NPC on this install permanently flagged busy inside AAF
	// by something that is not us, and asking about them every poll is wasted.
	float Papyrus_HoursSinceRefusal(std::monostate, std::int32_t a_formID)
	{
		const auto hours =
			RP::Ledger::GetSingleton().HoursSinceRefusal(static_cast<std::uint32_t>(a_formID));
		return std::isfinite(hours) ? hours : 1.0e9f;
	}

	std::int32_t Papyrus_RefusalCount(std::monostate, std::int32_t a_formID)
	{
		return static_cast<std::int32_t>(
			RP::Ledger::GetSingleton().Get(static_cast<std::uint32_t>(a_formID)).refusals);
	}

	// The same two facts about a PAIR, which the per-actor records cannot answer
	// once either of them has been with somebody else. Order does not matter.
	float Papyrus_HoursSincePair(std::monostate, std::int32_t a_first, std::int32_t a_second)
	{
		const auto hours = RP::Ledger::GetSingleton().HoursSincePair(
			static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second));
		return std::isfinite(hours) ? hours : 1.0e9f;
	}

	std::int32_t Papyrus_PairSceneCount(std::monostate, std::int32_t a_first, std::int32_t a_second)
	{
		return static_cast<std::int32_t>(RP::Ledger::GetSingleton().PairScenes(
			static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second)));
	}

	// An addon's own number per actor, kept in the save on its behalf so it does
	// not have to build a second co-save for one float. Rapport never reads it.
	float Papyrus_GetNeed(std::monostate, std::int32_t a_formID)
	{
		return RP::Ledger::GetSingleton().Get(static_cast<std::uint32_t>(a_formID)).need;
	}

	void Papyrus_SetNeed(std::monostate, std::int32_t a_formID, float a_need)
	{
		RP::Ledger::GetSingleton().SetNeed(static_cast<std::uint32_t>(a_formID), a_need);
	}

	void Papyrus_TakeOverDecisions(std::monostate, RE::BSFixedString a_who)
	{
		RP::Candidates::GetSingleton().StandDown(a_who.empty() ? "" : a_who.c_str());
	}

	// ---- the addon door ------------------------------------------------------
	//
	// Everything above this line is the BRIDGE talking to the plugin. These two
	// are the opposite direction: another mod's Papyrus talking to Rapport.
	//
	// By NAME, not by a tag list or a position id. A scenario is the unit an addon
	// reasons about -- "these two are at home and comfortable" -- and it is the
	// only unit whose meaning survives the catalogue changing under it. An addon
	// that named tags would be choosing from a catalogue it cannot see, and every
	// install has a different one.

	// Is a scene of ours already running or starting?
	//
	// So an addon can stop before doing work it cannot use. RequestScene already
	// declines while busy, but by then the addon has read every candidate, scored
	// them, and printed a decision -- observed four polls running, each one a full
	// twenty-row evaluation thrown away. A scene lasts minutes and a poll is
	// seconds, so that is most of what an addon does while one plays.
	bool Papyrus_Busy(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().Busy();
	}

	bool Papyrus_RequestScene(
		std::monostate, RE::Actor* a_first, RE::Actor* a_second, RE::BSFixedString a_scenario)
	{
		const std::string_view scenario{ a_scenario.empty() ? "" : a_scenario.c_str() };

		// The scenario owns its own length. An addon that had to pass seconds
		// would be guessing at content it has never read, and the number it
		// guessed would silently become the deadlock breaker's baseline.
		const auto& settings = RP::Config::GetSingleton();
		const auto  seconds =
			scenario.empty()
				? settings.sceneSeconds
				: (std::max)(settings.sceneSeconds,
				             RP::Scenarios::GetSingleton().SecondsFor(scenario));

		const auto ok =
			RP::PapyrusLink::GetSingleton().RequestScene(a_first, a_second, seconds, scenario);
		if (!ok) {
			// An addon gets a plain false and no reason, because the reasons are
			// all transient -- busy, bridge not up, one of them is None. Saying
			// WHY in the log and `false` on the wire keeps the addon's side a
			// retry loop instead of an error-handling tree.
			logger::info(
				"papyrus: an addon asked for \"{}\" and was turned down - see the line above for "
				"why",
				scenario);
		}
		return ok;
	}

	// Ask BEFORE walking two actors across a room. Returns Scenarios::Quality:
	// -1 unknown scenario, 0 nothing fits, 1 unconstrained, 2 no guaranteed
	// ending, 3 good. Worse-to-better, so an addon comparing two scenarios takes
	// the larger without a table.
	std::int32_t Papyrus_CanRun(
		std::monostate, RE::BSFixedString a_scenario, RE::Actor* a_first, RE::Actor* a_second)
	{
		if (!a_first || !a_second) {
			return static_cast<std::int32_t>(RP::Scenarios::Quality::kNothingFits);
		}
		const std::string_view scenario{ a_scenario.empty() ? "" : a_scenario.c_str() };
		const auto quality = RP::Scenarios::GetSingleton().Preflight(
			scenario, a_first->GetFormID(), a_second->GetFormID());
		return static_cast<std::int32_t>(quality);
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
		a_vm->BindNativeMethod(kCoreScript, "NoteSceneLive"sv, Papyrus_NoteSceneLive, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteSceneEnded"sv, Papyrus_NoteSceneEnded, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteBridgeConnected"sv, Papyrus_NoteBridgeConnected, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "BlockFaces"sv, Papyrus_BlockFaces, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneRefused"sv, Papyrus_SceneRefused, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteSceneTags"sv, Papyrus_NoteSceneTags, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteScenePosition"sv, Papyrus_NoteScenePosition, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteActorSex"sv, Papyrus_NoteActorSex, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteSceneSlots"sv, Papyrus_NoteSceneSlots, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "Pump"sv, Papyrus_Pump, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneToStop"sv, Papyrus_SceneToStop, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteStopAsked"sv, Papyrus_NoteStopAsked, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteAAFStatus"sv, Papyrus_NoteAAFStatus, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ScenePosition"sv, Papyrus_ScenePosition, std::nullopt, false);
		a_vm->BindNativeMethod(
			kCoreScript, "NoteAAFRevivalChoice"sv, Papyrus_NoteAAFRevivalChoice, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeOverlayOrder"sv, Papyrus_TakeOverlayOrder, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "PendingOrders"sv, Papyrus_PendingOrders, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "OrderActorID"sv, Papyrus_OrderActorID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "OrderSetID"sv, Papyrus_OrderSetID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "OrderExtra"sv, Papyrus_OrderExtra, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeMoisturizerOrder"sv, Papyrus_TakeMoisturizerOrder, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerActorID"sv, Papyrus_MoisturizerActorID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerRegions"sv, Papyrus_MoisturizerRegions, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DeferOrder"sv, Papyrus_DeferOrder, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "RequeueOrder"sv, Papyrus_RequeueOrder, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "FormIdText"sv, Papyrus_FormIdText, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "BridgeSilentTicks"sv, Papyrus_BridgeSilentTicks, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "AbandonInFlight"sv, Papyrus_AbandonInFlight, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerFront"sv, Papyrus_MoisturizerFront, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerOral"sv, Papyrus_MoisturizerOral, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerRear"sv, Papyrus_MoisturizerRear, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerLayers"sv, Papyrus_MoisturizerLayers, std::nullopt, false);
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
		a_vm->BindNativeMethod(kCoreScript, "CandidateCount"sv, Papyrus_CandidateCount, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CandidateFirst"sv, Papyrus_CandidateFirst, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CandidateSecond"sv, Papyrus_CandidateSecond, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CandidateScore"sv, Papyrus_CandidateScore, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CandidateDistance"sv, Papyrus_CandidateDistance, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CandidateObservers"sv, Papyrus_CandidateObservers, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CandidatePlayerNear"sv, Papyrus_CandidatePlayerNear, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CandidateInterior"sv, Papyrus_CandidateInterior, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CandidateNight"sv, Papyrus_CandidateNight, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CandidateSharedFaction"sv, Papyrus_CandidateSharedFaction, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ActorName"sv, Papyrus_ActorName, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "HoursSinceScene"sv, Papyrus_HoursSinceScene, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "LastPartner"sv, Papyrus_LastPartner, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneCount"sv, Papyrus_SceneCount, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "HoursSinceRefusal"sv, Papyrus_HoursSinceRefusal, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "RefusalCount"sv, Papyrus_RefusalCount, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "HoursSincePair"sv, Papyrus_HoursSincePair, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "PairSceneCount"sv, Papyrus_PairSceneCount, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "GetNeed"sv, Papyrus_GetNeed, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SetNeed"sv, Papyrus_SetNeed, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeOverDecisions"sv, Papyrus_TakeOverDecisions, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "Busy"sv, Papyrus_Busy, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "RequestScene"sv, Papyrus_RequestScene, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CanRun"sv, Papyrus_CanRun, std::nullopt, false);

		logger::info("papyrus: bound 48 native functions on {}", kCoreScript);
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

	bool PapyrusLink::RequestScene(
		RE::Actor* a_first, RE::Actor* a_second, float a_duration, std::string_view a_scenario)
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

		// The pending request and the in-flight block are published TOGETHER, under
		// one lock.
		//
		// They used to be two steps: _pending inside the lock, the latch after it.
		// TakeRequest runs on the Papyrus thread and only needs _pending, so the
		// bridge could collect the request and call ScenePosition() while this
		// thread was still mid-latch -- reading _inFlightScenario as a string_view
		// while assign() was reallocating it. That is not a stale value, it is a
		// freed buffer, and this whole block is shared between the VM threads and
		// the scheduler's main-thread slice with nothing else holding it together.
		{
			NamedLock lock{ _counter, "request counter" };
			_pending = Pending{
				request,
				static_cast<std::int32_t>(a_first->GetFormID()),
				static_cast<std::int32_t>(a_second->GetFormID()),
				a_duration
			};

			_inFlightFirst = static_cast<std::int32_t>(a_first->GetFormID());
			_inFlightSecond = static_cast<std::int32_t>(a_second->GetFormID());
			_inFlightDuration = a_duration;
			_inFlightScenario.assign(a_scenario);
			_inFlightRequest = request;
			_sceneRunning = false;
			_stopAsked = false;
			_requestedAt = std::chrono::steady_clock::now();
		}
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

	void PapyrusLink::CheckWatchdog()
	{
		if (!_sceneInFlight.load()) {
			return;
		}

		// The same budget the stop uses, so the two cannot disagree about when a
		// scene has gone wrong. This used to be handed Config::sceneSeconds -- the
		// default for a plain scene, nothing to do with the one running -- which
		// with the default of 30 made the limit 210s. athome asks for 285, so every
		// scenario scene was released mid-play without fail, and its real ending
		// then arrived to find somebody else in flight.
		const auto a_sceneSeconds = Config::GetSingleton().maxSceneSeconds;

		// Generous: the scene's own length, the bridge's own timeout, and room for
		// AAF to walk two people across a market before anyone calls it stuck.
		const auto limit = std::chrono::seconds{ static_cast<std::int64_t>(a_sceneSeconds) + 180 };
		if (std::chrono::steady_clock::now() - _requestedAt < limit) {
			return;
		}

		AbandonInFlight(std::format(
			"nothing has been heard about the running scene for {}s", limit.count()));
	}

	bool PapyrusLink::AbandonInFlight(std::string_view a_why)
	{
		if (!_sceneInFlight.load()) {
			return false;
		}

		logger::error("giving up on the scene in flight: {} - releasing", a_why);
		{
			NamedLock lock{ _counter, "request counter" };
			_pending = Pending{};
		}
		// Deliberately no ledger entry. Giving up means we do not know what
		// happened, and a guess written into a save outlives the session that made
		// it. The face and the busy flags are a different matter: those are state
		// we put on somebody, and not knowing what happened is exactly when they
		// have to come off.
		Expressions::GetSingleton().OnSceneEnded();

		// And the scenario. Without this it keeps its stage clock running against a
		// scene that is gone, advancing through the rest of its stages and asking a
		// dead AAF to change position for each one -- forever, because nothing else
		// ever ends a scenario. The expression layer was already being cleaned up
		// here; this one was missed when scenarios were added.
		Scenarios::GetSingleton().End();

		Release(static_cast<std::uint32_t>(_inFlightFirst));
		Release(static_cast<std::uint32_t>(_inFlightSecond));
		ClearInFlight();
		_sceneInFlight.store(false);
		_heals.fetch_add(1);
		return true;
	}

	void PapyrusLink::RequireHandshake()
	{
		if (_bridgeReady.exchange(false)) {
			logger::info("a save was loaded - the bridge will introduce itself again");
		}

		// AAF re-initialises on a load too, and that is exactly where it fails. Its
		// grace period and its restart count both belong to THIS load; the previous
		// one says nothing about it.
		AAFHealth::GetSingleton().Reset();
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
		NamedLock lock{ _counter, "request counter" };
		if (!_sceneRunning || _stopAsked || _inFlightRequest == 0) {
			return 0;
		}
		// Deliberately NOT _inFlightDuration. A scene lasts as long as its author
		// made it: a tree walks its own authored stages and leaves through its own
		// Finish branch, and stopping it on our clock is exactly how the climax was
		// being lost -- the ending is the last thing to happen, so ours was the one
		// part guaranteed to be cut.
		//
		// ONE emergency stop, for every scene, and deliberately not a model of how
		// scenes end.
		//
		// This briefly had two caps, on the theory that a tree scene ends itself and
		// an unconstrained one never does. The first half is true -- observed four
		// times, AAF leaves through the tree's own Finish branch. The second half was
		// wrong within ten minutes: an unconstrained scene ended ITSELF at 290.8s
		// with nothing here firing, six seconds off the duration AAF was handed.
		// Whether AAF honours that duration or the staged animation simply ran out of
		// stages is not answerable from one scene.
		//
		// So do not pretend to know. One number, high enough that a real scene can
		// never reach it, whose only job is that two actors are never left flagged
		// busy for the rest of the save.
		//
		// 600s is chosen against the content: the longest tree in this install
		// declares 265s, and at the worst ratio measured between declared and actual
		// (1.89x) that is 501s. Roughly 100s of headroom, and nothing in the
		// catalogue comes close. A pack with a much longer tree would want this
		// raised -- which is why the log says the number rather than just tripping.
		const auto elapsed =
			std::chrono::duration<float>{ std::chrono::steady_clock::now() - _sceneStartedAt }.count();
		if (elapsed < Config::GetSingleton().maxSceneSeconds) {
			return 0;
		}

		logger::warn(
			"request {}: still running after {:.0f}s - emergency stop. Nothing should reach this: "
			"the longest tree installed would run about 501s at worst, so either an animation is "
			"looping with no way out or something is stuck. Raise MaxSceneSeconds if a pack really "
			"has a scene this long",
			_inFlightRequest, elapsed);
		return _inFlightRequest;
	}

	void PapyrusLink::NoteStopAsked()
	{
		NamedLock lock{ _counter, "request counter" };
		_stopAsked = true;
	}

	std::string PapyrusLink::ChooseScenePosition()
	{
		// Snapshot under the lock, choose outside it. Copying the scenario name is
		// the point: this runs on the Papyrus thread while the scheduler's thread
		// can be assigning that same std::string, and handing a string_view into it
		// across a reallocation is a freed buffer, not a stale name.
		std::string   scenario;
		std::uint32_t first = 0;
		std::uint32_t second = 0;
		{
			NamedLock lock{ _counter, "request counter" };
			scenario = _inFlightScenario;
			first = static_cast<std::uint32_t>(_inFlightFirst);
			second = static_cast<std::uint32_t>(_inFlightSecond);
		}

		if (scenario.empty()) {
			return {};
		}
		return Scenarios::GetSingleton().ChooseSceneStart(scenario, first, second);
	}

	void PapyrusLink::OnSceneStarted(std::int32_t a_request)
	{
		_started.fetch_add(1);

		// The same guard its two siblings got, and for the same reason. AAF's
		// events can arrive long after this framework has released a request --
		// measured at 94 seconds for a scene end -- and OnSceneInit travels the
		// same path, so a stale start would restart the LIVE scene's clock,
		// extending its deadlock window by a whole budget, and run Begin() a second
		// time for a pair that is already mid-story. With nothing in flight it
		// would hand Expressions form id 0 and let it drive.
		if (a_request != _inFlightRequest) {
			logger::warn(
				"request {}: a scene started for it, but {} - leaving the running scene alone",
				a_request,
				_inFlightRequest == 0
					? "this framework had already released it"
					: std::format("request {} is the one in flight", _inFlightRequest));
			return;
		}

		logger::info("request {}: scene started", a_request);

		// Below the guard, not above it. A stale start clearing this would let the
		// next scene ask for furniture the room has already refused once.
		Scenarios::GetSingleton().NoteSceneStarted();

		// The clock starts HERE, not when the request was made: AAF walks the pair
		// to each other first, and that walk is not the scene. Measured at 12.5
		// seconds across an open market -- 40% of a thirty-second scene.
		_sceneStartedAt = std::chrono::steady_clock::now();
		_sceneRunning = true;
		_stopAsked = false;

		// A scenario, if the caller named one, drives the stages AND the faces --
		// which is why the expression layer is told to stand down for this scene
		// rather than running its own percentage schedule on top.
		const auto staged =
			!_inFlightScenario.empty() &&
			Scenarios::GetSingleton().Begin(
				_inFlightScenario,
				static_cast<std::uint32_t>(_inFlightFirst),
				static_cast<std::uint32_t>(_inFlightSecond));

		if (!staged) {
			Expressions::GetSingleton().OnSceneStarted(
				static_cast<std::uint32_t>(_inFlightFirst),
				static_cast<std::uint32_t>(_inFlightSecond),
				_inFlightDuration);
		} else {
			// The scenario owns the face, but the expression layer still owns
			// TAKING IT OFF -- the wearing list, the co-save and the clearing are
			// all its, and a stage-driven face must still come off at the end.
			Expressions::GetSingleton().OnSceneStarted(
				static_cast<std::uint32_t>(_inFlightFirst),
				static_cast<std::uint32_t>(_inFlightSecond),
				_inFlightDuration);
			Expressions::GetSingleton().StandDown();
		}
	}

	void PapyrusLink::NoteEvent(std::string_view a_name)
	{
		_events.fetch_add(1);
		_lastEventAt = std::chrono::steady_clock::now();
		(void)a_name;
	}

	void PapyrusLink::QueueOrder(Order a_order)
	{
		// One gate for every face, wherever it came from -- the scenario's stages,
		// the expression schedule, or the clearing at the end. Gating at the source
		// would mean gating in three places and forgetting one.
		if (!Config::GetSingleton().driveFaces &&
			(a_order.kind == Order::Kind::kApplyExpression ||
				a_order.kind == Order::Kind::kClearExpression)) {
			static std::once_flag said;
			std::call_once(said, [] {
				logger::info(
					"faces: DriveFaces is off - Rapport applies no expressions this session, so "
					"anything still flickering is not ours");
			});
			return;
		}

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

	void PapyrusLink::QueueOrders(const std::vector<Order>& a_orders)
	{
		for (const auto& order : a_orders) {
			QueueOrder(order);
		}
	}

	std::size_t PapyrusLink::PendingOrders() const
	{
		NamedLock lock{ _orderLock, "order queue" };
		return _orders.size();
	}

	void PapyrusLink::RequeueStranded()
	{
		NamedLock lock{ _orderLock, "order queue" };

		// Only what is still true whenever the actor comes back. A removal is: the
		// sweat is on them and should not be. An application is not: the face was
		// chosen for a stage of a scene that has since ended, and putting it on
		// them when they walk back into the cell would be a bug wearing the
		// costume of a fix.
		switch (_orderKind) {
		case Order::Kind::kRemoveOverlay:
		case Order::Kind::kRelease:
		case Order::Kind::kClearExpression:
			break;
		default:
			return;
		}

		const auto formID = static_cast<std::uint32_t>(_orderActor);
		if (formID == 0) {
			return;
		}

		// The same clear asked for twice is the same clear. Without this an actor
		// who stays away accumulates one entry per scene they were never cleared
		// from, and the list is the thing that has to stay small.
		for (const auto& held : _stranded) {
			if (held.formID == formID && held.kind == _orderKind && held.setID == _orderSet) {
				return;
			}
		}

		// A bound, because an NPC who never comes back never clears. 64 is far
		// more than a session produces and still small enough to be free; when it
		// is full the OLDEST goes, since the newest is the one most likely to
		// still describe what is on the actor.
		constexpr std::size_t kMaxStranded = 64;
		if (_stranded.size() >= kMaxStranded) {
			logger::warn(
				"stranded: holding {} cleanup order(s) for actors who have not come back - "
				"dropping the oldest ({:08X} {}). A reload clears everything from _wearing.",
				_stranded.size(), _stranded.front().formID, _stranded.front().setID);
			_stranded.erase(_stranded.begin());
		}

		_stranded.push_back(Order{ _orderKind, formID, _orderSet, _orderExtra });
	}

	void PapyrusLink::ReissueStranded(const std::vector<std::uint32_t>& a_loaded)
	{
		std::vector<Order> ready;
		{
			NamedLock lock{ _orderLock, "order queue" };
			if (_stranded.empty()) {
				return;
			}

			for (auto it = _stranded.begin(); it != _stranded.end();) {
				if (std::find(a_loaded.begin(), a_loaded.end(), it->formID) != a_loaded.end()) {
					ready.push_back(*it);
					it = _stranded.erase(it);
				} else {
					++it;
				}
			}
		}

		// Queued OUTSIDE the lock. QueueOrder takes the same one, and taking a lock
		// you already hold is the deadlock this codebase keeps its lock rules for.
		for (const auto& order : ready) {
			logger::info("stranded: {:08X} ({}) is back - re-issuing {}",
				order.formID, static_cast<std::int32_t>(order.formID), order.setID);
			QueueOrder(order);
		}
	}

	std::size_t PapyrusLink::StrandedOrders() const
	{
		NamedLock lock{ _orderLock, "order queue" };
		return _stranded.size();
	}

	std::int32_t PapyrusLink::TakeOverlayOrder()
	{
		NamedLock lock{ _orderLock, "order queue" };
		if (_orders.empty()) {
			_orderActor = 0;
			_orderSet.clear();
			_orderExtra.clear();
			return 0;
		}

		const auto order = _orders.front();
		_orders.pop_front();

		_orderActor = static_cast<std::int32_t>(order.formID);
		_orderSet = order.setID;
		_orderExtra = order.extra;
		_orderKind = order.kind;
		return static_cast<std::int32_t>(order.kind);
	}

	void PapyrusLink::Release(std::uint32_t a_formID)
	{
		if (a_formID == 0) {
			return;
		}
		QueueOrder(Order{ Order::Kind::kRelease, a_formID, {} });
	}

	std::pair<std::uint32_t, std::uint32_t> PapyrusLink::InFlightPair() const
	{
		NamedLock lock{ _counter, "request counter" };
		return { static_cast<std::uint32_t>(_inFlightFirst),
			static_cast<std::uint32_t>(_inFlightSecond) };
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

		// IS THAT FLAG REAL, OR IS IT A CORPSE?
		//
		// AAF_ActorBusy is a keyword AAF stamps and only AAF clears. The engine
		// knows nothing about it -- it is not an AI package, not IsInScene, not
		// dialogue -- so when a scene dies before it finishes, NOTHING will ever
		// take it off. It goes into the save, and that actor is refused by every
		// AAF mod from then on, forever. The bridge found this the hard way: "one
		// NPC picked by three failed runs became unusable for the rest of the
		// save."
		//
		// We can answer it because OnSceneInit and OnSceneEnd are broadcast for
		// EVERY scene, not just ours: if nobody is running a scene with this actor
		// in it, the keyword is a corpse and we take it off. Rapport has always
		// refused to touch a flag it did not set, which is the correct instinct
		// and left the actor broken forever; the release is now conditioned on
		// evidence rather than on ownership.
		const auto grace = Config::GetSingleton().staleFlagGraceSeconds;
		const auto listening = std::chrono::duration<float>{
			std::chrono::steady_clock::now() - _bridgeConnectedAt
		}.count();

		// Conservative: ANY scene running anywhere and we decline to judge. We
		// cannot tell whose actors they are (see NoteSceneLive), so the only safe
		// reading of a live scene is "this flag might be real".
		if (AnySceneLive()) {
			logger::info(
				"{:08X} is flagged busy and a scene IS running somewhere - not judging it",
				a_formID);
			return;
		}
		if (grace <= 0.0f) {
			return;
		}
		// A scene that began before we were listening has an init we never saw, so
		// its actors would look stale. Nothing is called stale until that window
		// has passed.
		if (listening < grace) {
			logger::info(
				"{:08X} is flagged busy with no scene running, but the bridge has only been "
				"listening {:.0f}s of {:.0f}s - not calling it stale yet",
				a_formID, listening, grace);
			return;
		}

		logger::warn(
			"{:08X} carries AAF's busy flag but NO scene is running anywhere - it is stale "
			"and "
			"nothing else will ever remove it. Releasing.",
			a_formID);
		QueueOrder(Order{ Order::Kind::kRelease, a_formID, {}, {} });
	}

	void PapyrusLink::NoteBridgeConnected()
	{
		NamedLock lock{ _sceneLock, "live scenes" };
		_bridgeConnectedAt = std::chrono::steady_clock::now();
		// A reconnect means the bridge restarted; whatever we thought was running
		// is from before that and cannot be trusted.
		_liveScenes.clear();
	}

	void PapyrusLink::NoteSceneLive(std::int32_t a_sceneID)
	{
		if (a_sceneID == 0) {
			return;
		}
		NamedLock lock{ _sceneLock, "live scenes" };
		_liveScenes.insert(a_sceneID);
	}

	void PapyrusLink::NoteSceneEnded(std::int32_t a_sceneID)
	{
		if (a_sceneID == 0) {
			return;
		}
		NamedLock lock{ _sceneLock, "live scenes" };
		_liveScenes.erase(a_sceneID);
	}

	bool PapyrusLink::AnySceneLive()
	{
		NamedLock lock{ _sceneLock, "live scenes" };
		return !_liveScenes.empty();
	}

	bool PapyrusLink::PeekActorBusy(std::uint32_t a_formID)
	{
		NamedLock lock{ _busyLock, "busy bench" };
		const auto entry = _busyUntil.find(a_formID);
		return entry != _busyUntil.end() && std::chrono::steady_clock::now() < entry->second;
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
			_silentTicks = 0;
			return;
		}

		if (!_bridgeReady.load() || _stallReported) {
			return;
		}

		// Two ticks, not one. The VM stops for a save and for a loading screen, and
		// neither is a fault -- a single silent tick names an ordinary fast travel
		// as a failure, which is the most expensive kind of wrong a log can be,
		// because it sends somebody looking for a bug that is not there.
		constexpr std::uint32_t kSilentTicksBeforeAlarm = 2;
		if (++_silentTicks < kSilentTicksBeforeAlarm) {
			return;
		}

		_stallReported = true;
		logger::error(
			"THE BRIDGE HAS STOPPED POLLING. It last answered after {} poll(s) and has not asked "
			"once in {} ticks, so nothing timed is happening: no expression, no overlay, no scene "
			"ending, no request collected. A poll with nothing to do and a poll that never "
			"happened write the same log, which is why this is said out loud. If it comes back a "
			"line will say so -- watch for an \"answering again\" line before treating this as "
			"the fault.{}",
			pumps, _silentTicks,
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

		logger::info("health: {}", AAFHealth::GetSingleton().Summary());

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

		// Only for the scene we still believe is running, and the CHECK and the
		// SNAPSHOT happen together under one lock.
		//
		// AAF's OnSceneEnd can arrive long after this framework has given up on a
		// scene -- 94 seconds late, in the run that found this -- and by then the
		// in-flight pair belongs to somebody else's request. Crediting them writes
		// the wrong couple into the SAVE: a scene they never had, a cooldown they
		// did not earn, and cum on an actor who was not there.
		//
		// Checking and then reading as two unlocked steps left a window of its own:
		// the watchdog clears the PAIR before it clears the request id, so a guard
		// could pass and the read that followed find zeroes -- a scene that really
		// did end, correctly accepted, and then recorded for nobody. The window is
		// not nanoseconds either; the watchdog takes three other locks inside it.
		std::uint32_t first = 0;
		std::uint32_t second = 0;
		{
			NamedLock lock{ _counter, "request counter" };
			if (a_request != _inFlightRequest) {
				logger::warn(
					"request {}: its scene ended, but {} - so the ledger and the aftermath are left "
					"alone rather than credited to whoever is in flight now",
					a_request,
					_inFlightRequest == 0
						? "this framework had already released it"
						: std::format("request {} is the one in flight", _inFlightRequest));
				return;
			}
			first = static_cast<std::uint32_t>(_inFlightFirst);
			second = static_cast<std::uint32_t>(_inFlightSecond);
		}

		// The state is released FIRST, under the lock, so nothing arriving while the
		// collaborators run can mistake this scene for the live one.
		ClearInFlight();

		// Then the bookkeeping, outside the lock and on the snapshot -- Ledger and
		// Aftermath take their own locks and the save callback takes ours, so
		// holding _counter across them would close a cycle.
		//
		// A scene that ENDED is the only thing worth remembering. One that failed
		// says nothing about these two beyond "not now", and writing it as history
		// would put a cooldown on people who never had a scene.
		if (first != 0 && second != 0) {
			Ledger::GetSingleton().RecordScene(first, second);
			Aftermath::GetSingleton().OnSceneEnded(first, second);
		}
		Expressions::GetSingleton().OnSceneEnded();
		Scenarios::GetSingleton().End();
		_sceneInFlight.store(false);
	}

	// One place that lets a request go, so the five fields cannot drift apart
	// again. Under the lock, and touching nothing that takes another one.
	void PapyrusLink::ClearInFlight()
	{
		NamedLock lock{ _counter, "request counter" };
		_inFlightFirst = 0;
		_inFlightSecond = 0;
		_inFlightRequest = 0;
		_inFlightDuration = 0.0f;
		_inFlightScenario.clear();
		_sceneRunning = false;
		_stopAsked = false;
	}

	void PapyrusLink::OnRequestFailed(std::int32_t a_request, std::string_view a_why)
	{
		_failed.fetch_add(1);
		logger::warn("request {}: {}", a_request, a_why);

		// Same rule as a late ending, for the same reason: a request that failed
		// before it ever started is not the one whose scene is running, and tearing
		// that scene's state down here would hand the next request a pair it never
		// asked for.
		if (a_request != _inFlightRequest) {
			logger::warn(
				"request {}: failed, but it is not the request in flight ({}) - leaving that "
				"scene's state alone",
				a_request, _inFlightRequest);
			return;
		}

		// BELOW the guard. This narrows the tree catalogue to no-furniture trees,
		// and it must only do that on evidence from the scene it is about: a stale
		// request failing for "the actor is already busy" says nothing whatsoever
		// about the room, and used to ban furniture for every scene after it.
		Scenarios::GetSingleton().NoteSceneRefused();

		// The scenario teardown that the watchdog path already got, and this one
		// did not. Latent while every live failure fires before the scene starts,
		// but the moment one fails after, the scenario keeps pumping its clock
		// against a dead scene.
		Scenarios::GetSingleton().End();

		// BLAME ONLY WHOEVER WAS ACTUALLY BUSY.
		//
		// RecordRefusal is what Chemistry reads, and it escalates: one refusal
		// benches a pair for 2 game hours, two for 4, three for 6. Blaming both
		// ends of the pair meant a stuck flag on ONE npc benched every partner he
		// was ever offered with. Measured live: Johnny Friendly carried a stale
		// AAF busy flag, and Melvin Koch -- who was merely standing next to him --
		// was sat down for six game hours twice over for it.
		//
		// The busy bench was written a moment ago by NoteActorBusy and names the
		// actual actor, so ask it rather than guess from the reason string. A zero
		// is skipped by RecordRefusal, so this penalises one end or neither.
		// When neither is benched the failure is about the request, not a person,
		// and both are recorded as before.
		const auto first = static_cast<std::uint32_t>(_inFlightFirst);
		const auto second = static_cast<std::uint32_t>(_inFlightSecond);
		const bool firstBusy = PeekActorBusy(first);
		const bool secondBusy = PeekActorBusy(second);
		if (firstBusy != secondBusy) {
			logger::info(
				"request {}: {:08X} was the busy one - {:08X} is not charged with this refusal",
				a_request, firstBusy ? first : second, firstBusy ? second : first);
			Ledger::GetSingleton().RecordRefusal(firstBusy ? first : 0u,
				secondBusy ? second : 0u);
		} else {
			Ledger::GetSingleton().RecordRefusal(first, second);
		}
		Expressions::GetSingleton().OnSceneEnded();

		ClearInFlight();
		_sceneInFlight.store(false);
	}
}
