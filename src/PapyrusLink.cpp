#include "PapyrusLink.h"
#include "Placement.h"

#include "Narrator.h"
#include "McmSettings.h"
#include "Names.h"
#include "Orientation.h"
#include "Traits.h"

#include "Candidates.h"
#include "Config.h"
#include "DebugHub.h"
#include "DebugTriggers.h"
#include "Aftermath.h"
#include "Barks.h"
#include "Watchers.h"
#include "Expressions.h"
#include "FaceAuthority.h"
#include "ForeignScenes.h"
#include "AAFHealth.h"
#include "Crowd.h"
#include "Ledger.h"
#include "Morphs.h"
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

	// How many onlookers are free before the crowd penalty starts. Chemistry reads
	// this rather than keeping its own copy: DESIGN C-6 there is the bug that two
	// numbers meaning the same thing produced when they disagreed.
	std::int32_t Papyrus_ObserverTolerance(std::monostate)
	{
		return static_cast<std::int32_t>(RP::Config::GetSingleton().Weights().observerTolerance);
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

	// ---- scenes Rapport did not start (R-22) -----------------------------------
	// The bridge hands AAF's actor array over exactly as it arrived, packed in a Var,
	// because Papyrus cannot cast a Var to an array ("cannot cast a var to a var[]").
	// A native can open it, and on the Papyrus thread -- the one calling this -- the
	// Actors inside are real, so their sexes are read here rather than guessed later.
	//
	// AAF's API documents the slot as "Actor[] as a Var"; a Var inside the Var and an
	// array of arrays are both walked, a little deeper than documented, because a
	// shape we did not expect should yield the actors in it rather than nothing.
	void CollectMembers(const RE::BSScript::Variable& a_var, std::vector<RP::ForeignScenes::Member>& a_out, int a_depth)
	{
		if (a_depth > 3) {
			return;
		}
		if (a_var.is<RE::BSScript::Variable>()) {
			if (const auto inner = RE::BSScript::get<RE::BSScript::Variable>(a_var)) {
				CollectMembers(*inner, a_out, a_depth + 1);
			}
			return;
		}
		if (a_var.is<RE::BSScript::Array>()) {
			if (!RE::BSScript::IsValidArray<RE::BSScript::Array>(a_var)) {
				return;
			}
			const auto array = RE::BSScript::get<RE::BSScript::Array>(a_var);
			// By INDEX, not a range-for: CommonLibF4's Array::end() is nullptr for an empty
			// but allocated array while begin() is its buffer, so a range-for over one walks
			// off the end into garbage -- inside a native, which is the game's crash.
			for (std::uint32_t i = 0; i < array->size(); ++i) {
				CollectMembers((*array)[i], a_out, a_depth + 1);
			}
			return;
		}
		if (!a_var.is<RE::BSScript::Object>()) {
			return;
		}
		const auto object = RE::BSScript::get<RE::BSScript::Object>(a_var);
		const auto game = RE::GameVM::GetSingleton();
		const auto vm = game ? game->GetVM() : nullptr;
		if (!object || !vm) {
			return;
		}
		// Only an ACTOR, and only a loaded one: the handle's type is checked before the
		// object is asked for, so a reference of another kind is skipped rather than
		// handed to an unpacker that asserts on it.
		const auto& handles = vm->GetObjectHandlePolicy();
		const auto  handle = object->GetHandle();
		const auto  actorType = static_cast<std::uint32_t>(RE::Actor::FORM_ID);
		if (!handles.HandleIsType(actorType, handle) || !handles.IsHandleLoaded(handle)) {
			return;
		}
		auto* actor = static_cast<RE::Actor*>(handles.GetObjectForHandle(actorType, handle));
		if (!actor) {
			return;
		}
		RP::ForeignScenes::Member member;
		member.formID = actor->GetFormID();
		auto* npc = actor->GetNPC();   // not const: GetSex() is not declared const here
		member.sex = npc ? static_cast<std::int32_t>(npc->GetSex()) : -1;
		member.player = actor == RE::PlayerCharacter::GetSingleton();
		member.child = actor->IsChild();
		// The player is not a race question: they are in the scene because they chose it.
		member.raceAllowed = member.player || RP::Config::GetSingleton().IsRaceAllowed(actor->race);
		a_out.push_back(member);
	}

	std::vector<RP::ForeignScenes::Member> Members(const RE::BSScript::Variable* a_actors)
	{
		std::vector<RP::ForeignScenes::Member> members;
		if (a_actors) {
			CollectMembers(*a_actors, members, 0);
		}

		// Once a session, what the Var actually held: the first raw-Var native in the
		// repo, so the first run in game is its proof (or its bug report).
		static std::atomic_bool reported{ false };
		if (!reported.exchange(true)) {
			logger::info("R-22: AAF's actor Var arrived as raw type {} ({}), and {} actor(s) were read from it",
				a_actors ? static_cast<int>(a_actors->GetType().GetRawType()) : -1,
				!a_actors                                       ? "no Var at all"
				: a_actors->is<RE::BSScript::Array>()           ? "an array"
				: a_actors->is<RE::BSScript::Variable>()        ? "a Var inside the Var"
				: a_actors->is<RE::BSScript::Object>()          ? "a single object"
																: "something else",
				members.size());
		}
		// Once each: a shape that named somebody twice would turn a pair into a group
		// and switch the aftermath to the group rule.
		std::vector<RP::ForeignScenes::Member> unique;
		for (const auto& member : members) {
			if (std::ranges::none_of(unique, [&](const auto& m) { return m.formID == member.formID; })) {
				unique.push_back(member);
			}
		}
		return unique;
	}

	std::string Text(const RE::BSFixedString& a_text)
	{
		return a_text.empty() ? std::string{} : std::string{ a_text.c_str() };
	}

	// Every one of these guarded, against anything thrown, as the pump is: an exception
	// crossing back into the VM would take the event -- and whatever else was on that
	// stack -- down with it.
	void Papyrus_ForeignSceneStarted(std::monostate, std::int32_t a_location, const RE::BSScript::Variable* a_actors,
		RE::BSFixedString a_position, RE::BSFixedString a_tags, RE::BSFixedString a_meta, bool a_npcControlled,
		float a_duration)
	{
		try {
			RP::ForeignScenes::GetSingleton().Started(a_location, Members(a_actors), Text(a_position), Text(a_tags),
				Text(a_meta), a_npcControlled, a_duration);
		} catch (const std::exception& e) {
			logger::critical("ForeignSceneStarted threw: {}", e.what());
		} catch (...) {
			logger::critical("ForeignSceneStarted threw something that is not a std::exception");
		}
	}

	// Does AAF's actor Var name actors, none of them this one? The bridge's refusal
	// test for a request whose scene has not started yet: an event naming actors, none
	// of them ours, is not ours.
	//
	// Asked as EXCLUDE so that every failure answers false and the meta alone decides,
	// exactly as before this test existed: a Var that yields nobody readable -- this is
	// the first native in the repo to take a raw Var -- and the native itself when it is
	// not bound at all, a new script over an older plugin, because false is what an
	// unbound native gives back. A strict test failing would stop every scene of OUR
	// OWN from being claimed, far worse than the rare misattribution it guards against.
	bool Papyrus_ActorsExclude(std::monostate, const RE::BSScript::Variable* a_actors, std::int32_t a_formID)
	{
		try {
			const auto members = Members(a_actors);
			return !members.empty() && std::ranges::none_of(members, [&](const RP::ForeignScenes::Member& m) {
				return m.formID == static_cast<std::uint32_t>(a_formID);
			});
		} catch (const std::exception& e) {
			logger::critical("ActorsExclude threw: {} - answering false, the meta decides", e.what());
			return false;
		} catch (...) {
			logger::critical("ActorsExclude threw something that is not a std::exception - answering false");
			return false;
		}
	}

	void Papyrus_ForeignSceneAnimation(std::monostate, std::int32_t a_location, const RE::BSScript::Variable* a_actors,
		RE::BSFixedString a_position, RE::BSFixedString a_tags)
	{
		try {
			const auto members = Members(a_actors);
			RP::ForeignScenes::GetSingleton().Animation(a_location, members, Text(a_position), Text(a_tags));
			// Stage 1 of "no scene inside a table", for menu scenes too: log only.
			std::vector<std::uint32_t> ids;
			for (const auto& m : members) {
				ids.push_back(m.formID);
			}
			RP::Placement::Survey(ids, Text(a_position), Text(a_tags), false);
		} catch (const std::exception& e) {
			logger::critical("ForeignSceneAnimation threw: {}", e.what());
		} catch (...) {
			logger::critical("ForeignSceneAnimation threw something that is not a std::exception");
		}
	}

	void Papyrus_ForeignSceneEnded(std::monostate, std::int32_t a_location, const RE::BSScript::Variable* a_actors,
		RE::BSFixedString a_position, RE::BSFixedString a_tags)
	{
		try {
			RP::ForeignScenes::GetSingleton().Ended(a_location, Members(a_actors), Text(a_position), Text(a_tags));
		} catch (const std::exception& e) {
			logger::critical("ForeignSceneEnded threw: {}", e.what());
		} catch (...) {
			logger::critical("ForeignSceneEnded threw something that is not a std::exception");
		}
	}

	// The end of one of OUR scenes, claimed by the bridge: remembered by the foreign
	// side like any end, with who was in it, so a duplicate -- which would arrive
	// unclaimed, the request released -- cannot leave the cum a second time, even after
	// another scene has taken the place.
	void Papyrus_OwnSceneEnded(std::monostate, std::int32_t a_location, const RE::BSScript::Variable* a_actors)
	{
		try {
			RP::ForeignScenes::GetSingleton().OwnSceneEnded(a_location, Members(a_actors));
		} catch (const std::exception& e) {
			logger::critical("OwnSceneEnded threw: {}", e.what());
		} catch (...) {
			logger::critical("OwnSceneEnded threw something that is not a std::exception");
		}
	}

	// ---- debug triggers (R-23): the MCM's Debug page and its hotkeys --------------
	// The actor the player faces. Also Chemistry's and Overture's way to name "him":
	// one definition of "in front of you" for all three mods.
	RE::Actor* Papyrus_ActorInFront(std::monostate, float a_maxDistance, float a_maxAngle)
	{
		try {
			return RP::DebugTriggers::ActorInFront(a_maxDistance, a_maxAngle);
		} catch (...) {
			logger::critical("ActorInFront threw");
			return nullptr;
		}
	}

	RE::BSFixedString Papyrus_DebugSceneWith(std::monostate, RE::Actor* a_first, RE::Actor* a_second, bool a_force)
	{
		try {
			return RE::BSFixedString{ RP::DebugTriggers::SceneWith(a_first, a_second, a_force) };
		} catch (const std::exception& e) {
			logger::critical("DebugSceneWith threw: {}", e.what());
		} catch (...) {
			logger::critical("DebugSceneWith threw something that is not a std::exception");
		}
		return RE::BSFixedString{ "Rapport debug: failed - see Rapport.log" };
	}

	RE::BSFixedString Papyrus_DebugSceneFor(std::monostate, RE::Actor* a_target, bool a_force)
	{
		try {
			return RE::BSFixedString{ RP::DebugTriggers::SceneFor(a_target, a_force) };
		} catch (const std::exception& e) {
			logger::critical("DebugSceneFor threw: {}", e.what());
		} catch (...) {
			logger::critical("DebugSceneFor threw something that is not a std::exception");
		}
		return RE::BSFixedString{ "Rapport debug: failed - see Rapport.log" };
	}

	RE::BSFixedString Papyrus_DebugScenePair(
		std::monostate, RE::Actor* a_facing, RE::BSFixedString a_pair, bool a_force)
	{
		try {
			const std::string_view pair{ a_pair.empty() ? "" : a_pair.c_str() };
			return RE::BSFixedString{ RP::DebugTriggers::ScenePair(a_facing, pair, a_force) };
		} catch (const std::exception& e) {
			logger::critical("DebugScenePair threw: {}", e.what());
		} catch (...) {
			logger::critical("DebugScenePair threw something that is not a std::exception");
		}
		return RE::BSFixedString{ "Rapport debug: failed - see Rapport.log" };
	}

	RE::BSFixedString Papyrus_LastRefusal(std::monostate)
	{
		return RE::BSFixedString{ RP::PapyrusLink::GetSingleton().LastRefusal() };
	}

	// R-25: the exclusion for a start with no position chosen, or "" to leave AAF's
	// settings as they are.
	RE::BSFixedString Papyrus_SceneExcludeTags(std::monostate, RE::BSFixedString a_given)
	{
		try {
			const std::string_view given{ a_given.empty() ? "" : a_given.c_str() };
			return RE::BSFixedString{ RP::PapyrusLink::GetSingleton().SceneExcludeTags(given) };
		} catch (const std::exception& e) {
			logger::critical("SceneExcludeTags threw: {} - AAF's own exclusions stand", e.what());
		} catch (...) {
			logger::critical("SceneExcludeTags threw - AAF's own exclusions stand");
		}
		return RE::BSFixedString{ "" };
	}

	// ...and what such a start should ask for (quickie's acts, when the pair has a
	// man), or "" to ask for nothing.
	// R-26 stage 2: where OUR scene should play -- {x, y, z, facing degrees}, or empty to
	// leave AAF's own spot. Placement::ChooseSpot decides and logs every decision.
	std::vector<float> Papyrus_SceneSpot(std::monostate, RE::Actor* a_slot0, RE::Actor* a_slot1, RE::BSFixedString a_position)
	{
		try {
			return RP::Placement::ChooseSpot(a_slot0, a_slot1, a_position.empty() ? "" : a_position.c_str(),
				RP::PapyrusLink::GetSingleton().InFlightRequest());
		} catch (const std::exception& e) {
			logger::critical("SceneSpot threw: {} - AAF's own spot stands", e.what());
		} catch (...) {
			logger::critical("SceneSpot threw - AAF's own spot stands");
		}
		return {};
	}

	RE::BSFixedString Papyrus_SceneIncludeTags(std::monostate)
	{
		try {
			return RE::BSFixedString{ RP::PapyrusLink::GetSingleton().SceneIncludeTags() };
		} catch (const std::exception& e) {
			logger::critical("SceneIncludeTags threw: {} - the start asks for no act", e.what());
		} catch (...) {
			logger::critical("SceneIncludeTags threw - the start asks for no act");
		}
		return RE::BSFixedString{ "" };
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

	bool Papyrus_RefusedOurScene(std::monostate, RE::BSFixedString a_why)
	{
		return RP::PapyrusLink::GetSingleton().RefusedOurScene(
			a_why.empty() ? "AAF refused the scene" : a_why.c_str());
	}

	void Papyrus_NoteScenePosition(std::monostate, RE::BSFixedString a_position)
	{
		RP::Expressions::GetSingleton().NotePosition(a_position.empty() ? "" : a_position.c_str());
		// Stage 1 of "no scene inside a table": say what stands in the way. Log only.
		try {
			auto&      link = RP::PapyrusLink::GetSingleton();
			const auto first = static_cast<std::uint32_t>(link.TakenFirstID());
			const auto second = static_cast<std::uint32_t>(link.TakenSecondID());
			if (first != 0 && second != 0) {
				RP::Placement::Survey({ first, second }, a_position.empty() ? "" : a_position.c_str(), "", true);
			}
		} catch (...) {
			logger::warn("placement: the survey threw - nothing else is affected");
		}
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
		RP::Placement::Pump();
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
			RP::ForeignScenes::GetSingleton().Pump();
			RP::FaceAuthority::GetSingleton().Pump();
			RP::Barks::GetSingleton().Pump();
			RP::Morphs::GetSingleton().Pump();
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
	void Papyrus_NoteAAFVersion(std::monostate, std::int32_t a_version)
	{
		RP::AAFHealth::GetSingleton().NoteVersion(a_version);
	}

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

	// The voice a kSayTopic line borrows (V-25), or 0. Read in the same drain
	// iteration as the fields above, so the latch cannot have moved on.
	std::int32_t Papyrus_OrderVoice(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().OrderVoice();
	}

	// ---- the watcher sweep (R-12) ----------------------------------------------
	// The bridge finds who is near and who can SEE (HasDetectionLOS is Papyrus
	// only); Watchers decides everything else.
	float Papyrus_WatchRadius(std::monostate)
	{
		return RP::Watchers::GetSingleton().SweepRadius();
	}

	std::int32_t Papyrus_WatchFirstID(std::monostate)
	{
		return static_cast<std::int32_t>(RP::Watchers::GetSingleton().SweepFirst());
	}

	std::int32_t Papyrus_WatchSecondID(std::monostate)
	{
		return static_cast<std::int32_t>(RP::Watchers::GetSingleton().SweepSecond());
	}

	// True: turn this actor's head to the scene (they noticed it).
	bool Papyrus_NoteWatcher(std::monostate, std::int32_t a_formID, bool a_sees)
	{
		return RP::Watchers::GetSingleton().Note(static_cast<std::uint32_t>(a_formID), a_sees);
	}

	void Papyrus_EndWatchSweep(std::monostate)
	{
		try {
			RP::Watchers::GetSingleton().EndSweep();
		} catch (const std::exception& e) {
			logger::critical("EndWatchSweep threw: {} - the poll would have died here silently", e.what());
		}
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
	void Papyrus_RequeueOrder(
		std::monostate, std::int32_t a_kind, std::int32_t a_formID,
		RE::BSFixedString a_setID, RE::BSFixedString a_extra)
	{
		RP::PapyrusLink::GetSingleton().RequeueStranded(
			static_cast<RP::Order::Kind>(a_kind),
			static_cast<std::uint32_t>(a_formID),
			a_setID.empty() ? "" : a_setID.c_str(),
			a_extra.empty() ? "" : a_extra.c_str());
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

	void Papyrus_PollMark(std::monostate, std::int32_t a_step)
	{
		RP::PapyrusLink::GetSingleton().NotePollStep(a_step);
	}

	void Papyrus_NoteMedicBeat(std::monostate)
	{
		RP::PapyrusLink::GetSingleton().NoteMedicBeat();
	}

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

	// ---- the relationship store (R-1): the API every consumer reads and writes ----
	// Rapport keeps the number; what it is WORTH is the consumer's curve (R-10).
	float Papyrus_PairBond(std::monostate, std::int32_t a_first, std::int32_t a_second)
	{
		return RP::Ledger::GetSingleton().Bond(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second));
	}

	// Returns the new bond. a_reason: 3 dialogue, 4 gift, 5 any other addon (1 and 2
	// are Rapport's own - scenes and the vanilla import - and are not accepted here).
	float Papyrus_AddBond(std::monostate, std::int32_t a_first, std::int32_t a_second, float a_amount, std::int32_t a_reason)
	{
		using Reason = RP::Ledger::BondReason;
		const auto reason = a_reason == 3 ? Reason::kDialogue : a_reason == 4 ? Reason::kGift : Reason::kAddon;
		return RP::Ledger::GetSingleton().AddBond(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second),
			a_amount, reason);
	}

	bool Papyrus_IsPairSeeded(std::monostate, std::int32_t a_first, std::int32_t a_second)
	{
		return RP::Ledger::GetSingleton().IsSeeded(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second));
	}

	float Papyrus_SeedBond(std::monostate, std::int32_t a_rank, bool a_partner)
	{
		return RP::Ledger::SeedValue(a_rank, a_partner);
	}

	// ---- the Narrator (roadmap 11) and the faithfulness trait -------------------

	// An addon's own share of a pair's score, just before it asks for the scene.
	void Papyrus_NarrateBonus(std::monostate, std::int32_t a_first, std::int32_t a_second, RE::BSFixedString a_label,
		float a_value)
	{
		RP::Narrator::GetSingleton().AddBonus(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second),
			a_label.empty() ? "" : a_label.c_str(), a_value);
	}

	// An addon passed on a likely pair; a_why is a clause with no names in it.
	void Papyrus_NarrateNearMiss(std::monostate, std::int32_t a_first, std::int32_t a_second, RE::BSFixedString a_why,
		float a_score, float a_bar)
	{
		RP::Narrator::GetSingleton().OnNearMiss(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second),
			a_why.empty() ? "" : a_why.c_str(), a_score, a_bar);
	}

	RE::BSFixedString Papyrus_NarratorHistory(std::monostate)
	{
		return RP::Narrator::GetSingleton().History();
	}

	// An addon's own moment in its own words; {first} and {second} become the names.
	void Papyrus_NarrateLine(std::monostate, std::int32_t a_first, std::int32_t a_second, RE::BSFixedString a_headline,
		RE::BSFixedString a_numbers)
	{
		RP::Narrator::GetSingleton().OnAddonLine(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second),
			a_headline.empty() ? "" : a_headline.c_str(), a_numbers.empty() ? "" : a_numbers.c_str());
	}

	// Names for the nameless: the new name, or "" if they have one already.
	RE::BSFixedString Papyrus_Introduce(std::monostate, RE::Actor* a_who)
	{
		return RP::Names::GetSingleton().Introduce(a_who);
	}

	float Papyrus_FaithfulnessOf(std::monostate, std::int32_t a_formID)
	{
		return RP::Traits::Faithfulness(static_cast<std::uint32_t>(a_formID));
	}

	// Staged, not written: it becomes true only when the scene starts.
	void Papyrus_NoteAffair(std::monostate, std::int32_t a_first, std::int32_t a_second)
	{
		RP::PapyrusLink::GetSingleton().StageAffair(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second));
	}

	// The bond a consumer should rank a pair by (Rapport:Relations.BondBetween).
	float Papyrus_PreviewBond(std::monostate, std::int32_t a_first, std::int32_t a_second, std::int32_t a_rank, bool a_partner)
	{
		return RP::Ledger::GetSingleton().PreviewBond(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second),
			a_rank, a_partner);
	}

	// Read before and after a multi-call snapshot of the candidate list.
	std::int32_t Papyrus_CandidateGeneration(std::monostate)
	{
		return static_cast<std::int32_t>(RP::Candidates::GetSingleton().Generation());
	}

	// major*10000 + minor*100 + patch. An addon checks this on connect and says so
	// plainly when Rapport is too old, instead of failing native by native.
	std::int32_t Papyrus_ApiVersion(std::monostate)
	{
		return RP_VERSION_MAJOR * 10000 + RP_VERSION_MINOR * 100 + RP_VERSION_PATCH;
	}

	void Papyrus_SetLovers(std::monostate, std::int32_t a_first, std::int32_t a_second, bool a_lovers)
	{
		RP::Ledger::GetSingleton().SetLovers(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second), a_lovers);
	}

	bool Papyrus_AreLovers(std::monostate, std::int32_t a_first, std::int32_t a_second)
	{
		return RP::Ledger::GetSingleton().AreLovers(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second));
	}

	std::int32_t Papyrus_LoverOf(std::monostate, std::int32_t a_actor)
	{
		return static_cast<std::int32_t>(RP::Ledger::GetSingleton().LoverOf(static_cast<std::uint32_t>(a_actor)));
	}

	// Every lover, one at a time: two plain Ints rather than an array, so the
	// binding is the shape every other native here already has.
	std::int32_t Papyrus_LoverCount(std::monostate, std::int32_t a_actor)
	{
		return static_cast<std::int32_t>(RP::Ledger::GetSingleton().LoversOf(static_cast<std::uint32_t>(a_actor)).size());
	}

	std::int32_t Papyrus_LoverAt(std::monostate, std::int32_t a_actor, std::int32_t a_index)
	{
		const auto all = RP::Ledger::GetSingleton().LoversOf(static_cast<std::uint32_t>(a_actor));
		return a_index >= 0 && static_cast<std::size_t>(a_index) < all.size() ? static_cast<std::int32_t>(all[a_index]) : 0;
	}

	bool Papyrus_IsAffairPair(std::monostate, std::int32_t a_first, std::int32_t a_second)
	{
		return RP::Ledger::GetSingleton().IsAffair(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second));
	}

	bool Papyrus_IsIncestPair(std::monostate, std::int32_t a_first, std::int32_t a_second)
	{
		return RP::Ledger::GetSingleton().IsIncest(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second));
	}

	// Called by the bridge when it starts a request, while it holds both Actors: the
	// engine's rank, blood tie and partnership, imported once per pair (R-2).
	void Papyrus_NoteVanillaRelationship(std::monostate, std::int32_t a_first, std::int32_t a_second, std::int32_t a_rank,
		bool a_blood, bool a_partner)
	{
		RP::Ledger::GetSingleton().SeedFromVanilla(static_cast<std::uint32_t>(a_first),
			static_cast<std::uint32_t>(a_second), a_rank, a_blood, a_partner);
	}

	bool Papyrus_IsPartnerPair(std::monostate, std::int32_t a_first, std::int32_t a_second)
	{
		return RP::Ledger::GetSingleton().IsPartner(static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second));
	}

	// The persona (R-7) - derived from the form id, or the owner's override.
	// How many people could see this actor right now.
	//
	// Overture asks it to decide whether a room is public (O-4's place recoil),
	// and the number is deliberately the SAME one Rapport's own pairing score
	// uses: ActorScan's observer positions, which are loaded, alive, people only
	// and never children, measured against the same observerRadius. A caller
	// gets what Rapport would act on, not a second opinion that drifts from it.
	//
	// The actor themselves is in that list and is subtracted. The PLAYER is not:
	// somebody being propositioned by the player is not thereby in public, and
	// counting the asker as an audience made every private room read as crowded.
	std::int32_t Papyrus_ObserversNear(std::monostate, std::int32_t a_formID)
	{
		const auto id = static_cast<std::uint32_t>(a_formID);
		const auto actor = RE::TESForm::GetFormByID(id) ? RE::TESForm::GetFormByID(id)->As<RE::Actor>() : nullptr;
		if (!actor) {
			return -1;   // not an actor we can see: not the same as "nobody is watching"
		}

		// The actor is left out BY ID. A position match against a snapshot up to a
		// scan old let anyone who had moved count themselves as their own audience
		// (microscope 2026-09-23). The player is never in the population.
		return RP::Crowd::GetSingleton().Near(actor->GetPosition(), id);
	}

	RE::BSFixedString Papyrus_PersonaOf(std::monostate, std::int32_t a_formID)
	{
		// R-11: the player has no persona. A hash of 0x14 would be an invented one.
		if (a_formID == 0x14) {
			return std::string{};
		}
		return std::string{ RP::Barks::GetSingleton().PersonaOf(static_cast<std::uint32_t>(a_formID)) };
	}

	// An addon's MCM setting from the files (McmSettings::ModSetting), or the default.
	float Papyrus_ModSettingFloat(std::monostate, RE::BSFixedString a_mod, RE::BSFixedString a_key, float a_default)
	{
		const auto v = RP::McmSettings::ModSetting(a_mod.c_str(), a_key.c_str());
		return v ? static_cast<float>(*v) : a_default;
	}

	std::int32_t Papyrus_ModSettingInt(std::monostate, RE::BSFixedString a_mod, RE::BSFixedString a_key, std::int32_t a_default)
	{
		const auto v = RP::McmSettings::ModSetting(a_mod.c_str(), a_key.c_str());
		return v ? static_cast<std::int32_t>(std::llround(*v)) : a_default;
	}

	bool Papyrus_ModSettingBool(std::monostate, RE::BSFixedString a_mod, RE::BSFixedString a_key, bool a_default)
	{
		const auto v = RP::McmSettings::ModSetting(a_mod.c_str(), a_key.c_str());
		return v ? *v != 0.0 : a_default;
	}

	// R-27. The player has none (R-11), so "" for 0x14, like PersonaOf.
	RE::BSFixedString Papyrus_OrientationOf(std::monostate, std::int32_t a_formID)
	{
		if (a_formID == 0x14) {
			return std::string{};
		}
		return std::string{ RP::Orientation::Name(RP::Orientation::GetSingleton().Of(static_cast<std::uint32_t>(a_formID))) };
	}

	// R-27: would the first agree to sex with the second, by orientation alone.
	bool Papyrus_Attracted(std::monostate, std::int32_t a_who, std::int32_t a_with)
	{
		return RP::Orientation::GetSingleton().Attracted(RE::TESForm::GetFormByID<RE::Actor>(static_cast<std::uint32_t>(a_who)),
			RE::TESForm::GetFormByID<RE::Actor>(static_cast<std::uint32_t>(a_with)));
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
		// THE PAUSE HAS TO BE HERE, not only in the scheduler.
		//
		// It was in the scheduler's stand-in branch, which sits AFTER the "an
		// addon owns the decision" branch -- so with Chemistry installed that
		// branch is never reached, and pause reported success while Chemistry went
		// on starting scenes through this native. Measured: paused at 02:42:56,
		// Chemistry started one at 02:43:24.
		//
		// This is the door ADDONS come through, so this is where holding them off
		// belongs. The mailbox calls PapyrusLink::RequestScene directly and is
		// deliberately not gated: the whole point of pausing is to stop autonomy
		// taking the slot from a deliberate test.
		// It holds off AUTONOMY. A pair with the player in it is the player's own
		// request -- Overture's yes -- which is exactly what a pause clears the way
		// for, so it is never refused here (microscope pass 2).
		const bool playersOwn = (a_first && a_first->GetFormID() == 0x14) || (a_second && a_second->GetFormID() == 0x14);
		if (RP::PapyrusLink::GetSingleton().AutonomyPaused() && !playersOwn) {
			logger::info("request refused: autonomy is paused, so an addon may not start a scene");
			RP::PapyrusLink::GetSingleton().NoteRefusal("autonomy is paused (mailbox) - no addon may start a scene");
			return false;
		}

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
			// retry loop instead of an error-handling tree. One that wants the
			// words anyway asks LastRefusal().
			logger::info(
				"papyrus: an addon asked for \"{}\" and was turned down - see the line above for "
				"why",
				scenario);
		}
		return ok;
	}

	void Papyrus_ReservePlayerScene(std::monostate, RE::Actor* a_with, float a_seconds)
	{
		RP::PapyrusLink::GetSingleton().ReservePlayerScene(a_with ? a_with->GetFormID() : 0u, a_seconds);
	}

	bool Papyrus_PlayerHoldsSlot(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().PlayerHoldsSlot();
	}

	std::int32_t Papyrus_InFlightRequest(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().InFlightRequest();
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
		a_vm->BindNativeMethod(kCoreScript, "ObserverTolerance"sv, Papyrus_ObserverTolerance, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NeedsHandshake"sv, Papyrus_NeedsHandshake, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteEvent"sv, Papyrus_NoteEvent, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteActorBusy"sv, Papyrus_NoteActorBusy, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteSceneLive"sv, Papyrus_NoteSceneLive, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteSceneEnded"sv, Papyrus_NoteSceneEnded, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ForeignSceneStarted"sv, Papyrus_ForeignSceneStarted, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ForeignSceneAnimation"sv, Papyrus_ForeignSceneAnimation, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ForeignSceneEnded"sv, Papyrus_ForeignSceneEnded, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "OwnSceneEnded"sv, Papyrus_OwnSceneEnded, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ActorInFront"sv, Papyrus_ActorInFront, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DebugSceneWith"sv, Papyrus_DebugSceneWith, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DebugSceneFor"sv, Papyrus_DebugSceneFor, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DebugScenePair"sv, Papyrus_DebugScenePair, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "LastRefusal"sv, Papyrus_LastRefusal, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneExcludeTags"sv, Papyrus_SceneExcludeTags, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneIncludeTags"sv, Papyrus_SceneIncludeTags, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneSpot"sv, Papyrus_SceneSpot, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ActorsExclude"sv, Papyrus_ActorsExclude, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteBridgeConnected"sv, Papyrus_NoteBridgeConnected, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "BlockFaces"sv, Papyrus_BlockFaces, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneRefused"sv, Papyrus_SceneRefused, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "RefusedOurScene"sv, Papyrus_RefusedOurScene, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteSceneTags"sv, Papyrus_NoteSceneTags, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteScenePosition"sv, Papyrus_NoteScenePosition, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteActorSex"sv, Papyrus_NoteActorSex, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteSceneSlots"sv, Papyrus_NoteSceneSlots, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "Pump"sv, Papyrus_Pump, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneToStop"sv, Papyrus_SceneToStop, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteStopAsked"sv, Papyrus_NoteStopAsked, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteAAFStatus"sv, Papyrus_NoteAAFStatus, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteAAFVersion"sv, Papyrus_NoteAAFVersion, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ScenePosition"sv, Papyrus_ScenePosition, std::nullopt, false);
		a_vm->BindNativeMethod(
			kCoreScript, "NoteAAFRevivalChoice"sv, Papyrus_NoteAAFRevivalChoice, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeOverlayOrder"sv, Papyrus_TakeOverlayOrder, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "PendingOrders"sv, Papyrus_PendingOrders, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "OrderActorID"sv, Papyrus_OrderActorID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "OrderSetID"sv, Papyrus_OrderSetID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "OrderExtra"sv, Papyrus_OrderExtra, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "OrderVoice"sv, Papyrus_OrderVoice, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "WatchRadius"sv, Papyrus_WatchRadius, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "WatchFirstID"sv, Papyrus_WatchFirstID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "WatchSecondID"sv, Papyrus_WatchSecondID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteWatcher"sv, Papyrus_NoteWatcher, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "EndWatchSweep"sv, Papyrus_EndWatchSweep, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeMoisturizerOrder"sv, Papyrus_TakeMoisturizerOrder, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerActorID"sv, Papyrus_MoisturizerActorID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "MoisturizerRegions"sv, Papyrus_MoisturizerRegions, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "DeferOrder"sv, Papyrus_DeferOrder, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "RequeueOrder"sv, Papyrus_RequeueOrder, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "FormIdText"sv, Papyrus_FormIdText, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "BridgeSilentTicks"sv, Papyrus_BridgeSilentTicks, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "PollMark"sv, Papyrus_PollMark, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteMedicBeat"sv, Papyrus_NoteMedicBeat, std::nullopt, false);
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
		a_vm->BindNativeMethod(kCoreScript, "PairBond"sv, Papyrus_PairBond, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "AddBond"sv, Papyrus_AddBond, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "IsIncestPair"sv, Papyrus_IsIncestPair, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NarrateBonus"sv, Papyrus_NarrateBonus, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NarrateNearMiss"sv, Papyrus_NarrateNearMiss, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NarratorHistory"sv, Papyrus_NarratorHistory, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NarrateLine"sv, Papyrus_NarrateLine, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "Introduce"sv, Papyrus_Introduce, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "FaithfulnessOf"sv, Papyrus_FaithfulnessOf, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteAffair"sv, Papyrus_NoteAffair, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "PreviewBond"sv, Papyrus_PreviewBond, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CandidateGeneration"sv, Papyrus_CandidateGeneration, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ApiVersion"sv, Papyrus_ApiVersion, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "IsAffairPair"sv, Papyrus_IsAffairPair, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "IsPairSeeded"sv, Papyrus_IsPairSeeded, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SeedBond"sv, Papyrus_SeedBond, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "IsPartnerPair"sv, Papyrus_IsPartnerPair, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NoteVanillaRelationship"sv, Papyrus_NoteVanillaRelationship, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "PersonaOf"sv, Papyrus_PersonaOf, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "OrientationOf"sv, Papyrus_OrientationOf, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "Attracted"sv, Papyrus_Attracted, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ModSettingFloat"sv, Papyrus_ModSettingFloat, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ModSettingInt"sv, Papyrus_ModSettingInt, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ModSettingBool"sv, Papyrus_ModSettingBool, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ObserversNear"sv, Papyrus_ObserversNear, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "GetNeed"sv, Papyrus_GetNeed, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SetNeed"sv, Papyrus_SetNeed, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeOverDecisions"sv, Papyrus_TakeOverDecisions, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "Busy"sv, Papyrus_Busy, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "RequestScene"sv, Papyrus_RequestScene, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "ReservePlayerScene"sv, Papyrus_ReservePlayerScene, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "PlayerHoldsSlot"sv, Papyrus_PlayerHoldsSlot, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "InFlightRequest"sv, Papyrus_InFlightRequest, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SetLovers"sv, Papyrus_SetLovers, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "AreLovers"sv, Papyrus_AreLovers, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "LoverOf"sv, Papyrus_LoverOf, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "LoverCount"sv, Papyrus_LoverCount, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "LoverAt"sv, Papyrus_LoverAt, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "CanRun"sv, Papyrus_CanRun, std::nullopt, false);

		// No count: it was a hand-kept "60" that stopped being true long ago.
		logger::info("papyrus: native functions bound on {}", kCoreScript);
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
		RE::Actor* a_first, RE::Actor* a_second, float a_duration, std::string_view a_scenario,
		bool a_bypassHold, std::string* a_why)
	{
		// Every refusal says why, twice: into a_why for a caller that asked (the debug
		// triggers' HUD line), and into LastRefusal for an addon that only got false
		// back. The log lines stay exactly as they were: fo4-mcp reads them.
		const auto refuse = [&](std::string a_reason) {
			if (a_why) {
				*a_why = a_reason;
			}
			NoteRefusal(std::move(a_reason));
			return false;
		};

		if (!a_first || !a_second) {
			return refuse("one of the two is nobody");
		}
		if (!_bridgeReady.load()) {
			logger::warn("scene wanted before the bridge reported ready");
			return refuse("the bridge is not ready yet - give it a few seconds after a load");
		}
		// THE PLAYER'S LANE, here in the funnel every request passes -- addons and
		// Rapport's own stand-in alike. It used to sit in the addon door only, and
		// the stand-in (the autonomy of an install with Overture but no Chemistry)
		// walked straight past it (microscope 2026-09-23).
		if (!a_bypassHold) {
			if (auto held = HeldFrom(a_first->GetFormID(), a_second->GetFormID()); !held.empty()) {
				logger::info("request refused: {}", held);
				return refuse(std::move(held));
			}
		}
		// THE DOOR re-checks what the scan checked. A pair is picked from a list
		// published up to twenty seconds ago, and by the time it is asked for, one of
		// them may be talking to the player, in an ambient conversation, dead or gone
		// -- and AAF walks them off mid-sentence, or refuses (microscope pass 2). The
		// player's own request is exempt from the dialogue test: the NPC was talking
		// to the player a moment ago because that is where its yes came from.
		const bool playersOwn = a_first->GetFormID() == 0x14 || a_second->GetFormID() == 0x14;
		for (auto* actor : { a_first, a_second }) {
			if (actor->GetFormID() == 0x14) {
				continue;
			}
			std::string_view why;
			// Adults only (DESIGN hard rule), HERE, in the funnel every request passes:
			// every addon, the stand-in, the player's lane and the debug triggers. It was
			// left to each caller's own filters, and one caller (a debug trigger) had none
			// (review of R-23, 2026-09-24).
			if (actor->IsChild()) {
				why = "is a child - never";
			} else if (actor->IsDead(true)) {
				why = "is dead";
			} else if (!actor->Get3D()) {
				why = "is not loaded";
			} else if (!playersOwn && actor->talkingToPlayer) {
				why = "is talking to the player";
			} else if (actor->boolFlags.any(RE::Actor::BOOL_FLAGS::kInRandomScene)) {
				why = "is in the middle of a conversation";
			}
			if (!why.empty()) {
				logger::info("request refused: {:08X} {}", actor->GetFormID(), why);
				const char* name = actor->GetDisplayFullName();
				return refuse(name && *name ? std::format("{} ({:08X}) {}", name, actor->GetFormID(), why)
				                            : std::format("{:08X} {}", actor->GetFormID(), why));
			}
		}
		// ORIENTATION (R-27), here in the funnel, for every addon as well as our own pairing
		// (owner, 2026-09-25: "enforce for addons"). A third-party mod asking for a pair that
		// one of them would never want is refused like any other rule. Only the forced test
		// doors -- a forced debug hotkey, fo4-mcp's mailbox -- step past it, the way they step
		// past the player's hold: they exist to start the scene you asked for.
		if (!a_bypassHold && !Orientation::GetSingleton().Mutual(a_first, a_second)) {
			auto why = Orientation::GetSingleton().WhyNot(a_first, a_second);
			logger::info("request refused: {}", why);
			return refuse(std::move(why));
		}
		if (_sceneInFlight.exchange(true)) {
			// Said now, where it used to be silent: the addon door's "see the line
			// above" had no line above for this one.
			logger::info("request refused: a scene is already in flight");
			return refuse("a scene is already in flight - one at a time");
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
		Narrator::GetSingleton().OnRequestAccepted(a_first->GetFormID(), a_second->GetFormID());
		ClearReservation(a_first->GetFormID(), a_second->GetFormID());
		NoteRefusal({});
		return true;
	}

	std::string PapyrusLink::LastRefusal() const
	{
		std::scoped_lock lock{ _refusalLock };
		return _lastRefusal;
	}

	void PapyrusLink::NoteRefusal(std::string a_why)
	{
		std::scoped_lock lock{ _refusalLock };
		_lastRefusal = std::move(a_why);
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

	void PapyrusLink::StageAffair(std::uint32_t a_first, std::uint32_t a_second)
	{
		NamedLock lock{ _counter, "request counter" };
		const auto f = static_cast<std::uint32_t>(_inFlightFirst);
		const auto s = static_cast<std::uint32_t>(_inFlightSecond);
		if ((f == a_first && s == a_second) || (f == a_second && s == a_first)) {
			_stagedAffair = 1;
		} else {
			logger::info("relationship: an affair was noted for {:08X} + {:08X}, who are not the pair in flight - ignored",
				a_first, a_second);
		}
	}

	void PapyrusLink::ReservePlayerScene(std::uint32_t a_with, float a_seconds)
	{
		NamedLock lock{ _reserveLock, "reserve" };
		// !(> 0) and not (<= 0): NaN is a release, never a hold of undefined length.
		if (!(a_seconds > 0.0f)) {
			if (_reservedWith != 0 && _reservedWith == a_with) {
				logger::info("reserve: the player's hold with {:08X} let go", _reservedWith);
				_reservedWith = 0;
			}
			return;
		}
		if (a_with == 0 || a_with == 0x14) {
			logger::warn("reserve: refused - a hold is for the player AND one other person, and {:08X} is not that",
				a_with);
			return;
		}
		const auto seconds = (std::min)(a_seconds, 120.0f);
		if (_reservedWith != 0 && _reservedWith != a_with && std::chrono::steady_clock::now() < _reservedUntil) {
			logger::warn("reserve: the player's hold with {:08X} is replaced by one with {:08X} - one hold at a time",
				_reservedWith, a_with);
		}
		_reservedWith = a_with;
		_reservedUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds{ static_cast<int>(seconds * 1000.0f) };
		logger::info("reserve: the scene slot is held for the player and {:08X} for {:.0f}s", a_with, seconds);
	}

	std::string PapyrusLink::HeldFrom(std::uint32_t a_first, std::uint32_t a_second)
	{
		NamedLock lock{ _reserveLock, "reserve" };
		if (_reservedWith == 0) {
			return {};
		}
		const auto now = std::chrono::steady_clock::now();
		if (now >= _reservedUntil) {
			logger::info("reserve: the player's hold with {:08X} ran out unused", _reservedWith);
			_reservedWith = 0;
			return {};
		}
		const bool held = (a_first == 0x14 && a_second == _reservedWith) || (a_second == 0x14 && a_first == _reservedWith);
		if (held) {
			return {};
		}
		const auto left = std::chrono::duration_cast<std::chrono::seconds>(_reservedUntil - now).count();
		return std::format("the slot is held for the player's request with {:08X} ({}s left)", _reservedWith, left);
	}

	std::int32_t PapyrusLink::InFlightRequest()
	{
		NamedLock lock{ _counter, "request counter" };
		return _sceneInFlight.load() ? _inFlightRequest : 0;
	}

	bool PapyrusLink::PlayerHoldsSlot()
	{
		NamedLock lock{ _reserveLock, "reserve" };
		if (_reservedWith != 0 && std::chrono::steady_clock::now() >= _reservedUntil) {
			logger::info("reserve: the player's hold with {:08X} ran out unused", _reservedWith);
			_reservedWith = 0;
		}
		return _reservedWith != 0;
	}

	void PapyrusLink::ClearReservation(std::uint32_t a_first, std::uint32_t a_second)
	{
		NamedLock lock{ _reserveLock, "reserve" };
		if (_reservedWith == 0) {
			return;
		}
		const bool held = (a_first == 0x14 && a_second == _reservedWith) || (a_second == 0x14 && a_first == _reservedWith);
		if (held) {
			logger::info("reserve: the player's scene with {:08X} was accepted - hold released", _reservedWith);
			_reservedWith = 0;
		}
	}

	void PapyrusLink::OnGameLoading()
	{
		// A hold belongs to the world it was made in.
		{
			NamedLock lock{ _reserveLock, "reserve" };
			_reservedWith = 0;
		}
		// A SAVE LOAD IS A NEW WORLD. The plugin lives for the whole game session and
		// Papyrus does not, so anything held here about a scene belongs to the world
		// being left behind - and nothing else ever let go of it. 2026-09-21: a save
		// was loaded while request 6 was running; every request after it was refused
		// 'a scene was already in flight', autonomy with it, and the only thing that
		// would ever have cleared it was the watchdog, 13 minutes later.
		//
		// STATE ONLY. No Release, no face clear, no overlay removal: those would be
		// orders about actors in a world that is gone, delivered to the one that is
		// arriving. A save made mid-scene carries its own in-flight pair in the
		// co-save, and RestoreInFlightPair releases THOSE actors properly.
		const bool inFlight = _sceneInFlight.load();
		const auto request = _inFlightRequest;
		std::size_t dropped = 0;
		{
			NamedLock lock{ _orderLock, "order queue" };
			dropped = _orders.size() + _stranded.size() + _cmkzOrders.size();
			_orders.clear();
			_stranded.clear();
			_cmkzOrders.clear();
		}
		{
			NamedLock lock{ _counter, "request counter" };
			_pending = Pending{};
		}
		// Each only clears its own state for the scene (Barks drops a waiting reply,
		// Watchers its rolls, Scenarios its stage clock); none of them queues anything.
		Barks::GetSingleton().OnSceneEnded();
		Watchers::GetSingleton().OnSceneEnded();
		Scenarios::GetSingleton().End();
		// Somebody else's scenes too (R-22): forgotten, not cleared -- their faces are
		// on the wearing list, which the arriving save's own record governs.
		ForeignScenes::GetSingleton().Reset();
		FaceAuthority::GetSingleton().Reset();
		ClearInFlight();
		_sceneInFlight.store(false);

		// The published pairs and the crowd were measured in the world being left,
		// and the first tick of the new one is a whole warm-up away (60 s). Served
		// in between they are answers about people who are not here. 2026-09-23:
		// 40 s after a load into the Third Rail, Chemistry took two Fourville
		// Residents off the old list and AAF was asked to stage two actors who were
		// not loaded; ObserversNear said "0 watching" in a full bar because it was
		// still counting the bunkhouse. So: no list, and a crowd of "unknown" (-1),
		// until the new world has been scanned.
		Candidates::GetSingleton().Clear();
		Crowd::GetSingleton().Reset();
		// Pending clears were for the world being left; the load sweep (main.cpp)
		// covers whoever the arriving save carries.
		Morphs::GetSingleton().Forget();

		if (inFlight || dropped) {
			logger::warn("loading a save: {}{} order(s) for the world being left dropped",
				inFlight ? std::format("request {} was still in flight and is forgotten, ", request) : std::string{},
				dropped);
		} else {
			logger::info("loading a save: nothing in flight, no orders to drop");
		}
	}

	bool PapyrusLink::AbandonInFlight(std::string_view a_why)
	{
		if (!_sceneInFlight.load()) {
			return false;
		}

		logger::error("giving up on the scene in flight: {} - releasing", a_why);
		std::uint32_t first = 0;
		std::uint32_t second = 0;
		bool          started = false;
		{
			NamedLock lock{ _counter, "request counter" };
			_pending = Pending{};
			first = static_cast<std::uint32_t>(_inFlightFirst);
			second = static_cast<std::uint32_t>(_inFlightSecond);
			started = _sceneRunning;
		}
		// Deliberately no ledger entry. Giving up means we do not know what
		// happened, and a guess written into a save outlives the session that made
		// it. The face and the busy flags are a different matter: those are state
		// we put on somebody, and not knowing what happened is exactly when they
		// have to come off.
		Expressions::GetSingleton().OnSceneEnded();
		Barks::GetSingleton().OnSceneEnded();
		Watchers::GetSingleton().OnSceneEnded();

		// And the scenario. Without this it keeps its stage clock running against a
		// scene that is gone, advancing through the rest of its stages and asking a
		// dead AAF to change position for each one -- forever, because nothing else
		// ever ends a scenario. The expression layer was already being cleaned up
		// here; this one was missed when scenarios were added.
		Scenarios::GetSingleton().End();

		Release(static_cast<std::uint32_t>(_inFlightFirst));
		Release(static_cast<std::uint32_t>(_inFlightSecond));
		// A morph AAF left behind is the same kind of state as the busy flag.
		Morphs::GetSingleton().OnSceneEnded(static_cast<std::uint32_t>(_inFlightFirst),
			static_cast<std::uint32_t>(_inFlightSecond));
		ClearInFlight();
		_sceneInFlight.store(false);
		_heals.fetch_add(1);
		// A scene with the player that never started is a yes the player is still
		// waiting on -- the same as a failed one, and said the same way. One that
		// started and was cut short played; there is nothing to take back.
		if (!started && (first == 0x14 || second == 0x14)) {
			Narrator::GetSingleton().OnPlayerSceneFailed(first, second);
		}
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

	std::string PapyrusLink::SceneExcludeTags(std::string_view a_given)
	{
		// The same snapshot ChooseScenePosition takes, for the same request: the
		// bridge asks for the position first and, only when there is none, this.
		// The scenario is COPIED under the lock, for ChooseScenePosition's reason.
		std::uint32_t first = 0;
		std::uint32_t second = 0;
		std::string   scenario;
		{
			NamedLock lock{ _counter, "request counter" };
			first = static_cast<std::uint32_t>(_inFlightFirst);
			second = static_cast<std::uint32_t>(_inFlightSecond);
			scenario = _inFlightScenario;
		}
		if (first == 0 || second == 0) {
			return {};
		}
		return Scenarios::GetSingleton().ExcludeTagsFor(a_given, first, second, scenario);
	}

	std::string PapyrusLink::SceneIncludeTags()
	{
		std::uint32_t first = 0;
		std::uint32_t second = 0;
		std::string   scenario;
		{
			NamedLock lock{ _counter, "request counter" };
			first = static_cast<std::uint32_t>(_inFlightFirst);
			second = static_cast<std::uint32_t>(_inFlightSecond);
			scenario = _inFlightScenario;
		}
		if (first == 0 || second == 0) {
			return {};
		}
		return Scenarios::GetSingleton().IncludeTagsFor(first, second, scenario);
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

		// What only a scene that PLAYED may record or say. The request was a wish: the
		// bridge and AAF can still refuse it, and an affair or a narration for a scene
		// that never happened is a lie the attitude layer would one day believe.
		// Every door leads here - Rapport's stand-in, every addon, the dev channel.
		{
			std::uint32_t first = 0, second = 0;
			std::uint64_t affair = 0;
			std::string   scenario;
			{
				NamedLock lock{ _counter, "request counter" };
				first = static_cast<std::uint32_t>(_inFlightFirst);
				second = static_cast<std::uint32_t>(_inFlightSecond);
				scenario = _inFlightScenario;
				affair = std::exchange(_stagedAffair, 0);
			}
			if (affair != 0) {
				Ledger::GetSingleton().NoteAffair(first, second);
			}
			Narrator::GetSingleton().OnSceneRequested(first, second, scenario);
		}

		// Below the guard, not above it. A stale start clearing this would let the
		// next scene ask for furniture the room has already refused once.
		Scenarios::GetSingleton().NoteSceneStarted(
			static_cast<std::uint32_t>(_inFlightFirst), static_cast<std::uint32_t>(_inFlightSecond));

		// The clock starts HERE, not when the request was made: AAF walks the pair
		// to each other first, and that walk is not the scene. Measured at 12.5
		// seconds across an open market -- 40% of a thirty-second scene.
		_sceneStartedAt = std::chrono::steady_clock::now();
		_sceneRunning = true;
		_stopAsked = false;

		// One actor, one AAF scene: a record of somebody else's scene still holding
		// either of these two missed its end, and the poll would go on putting its
		// faces over ours (R-22). Before the expression layer takes them.
		ForeignScenes::GetSingleton().OwnSceneStarted(
			static_cast<std::uint32_t>(_inFlightFirst),
			static_cast<std::uint32_t>(_inFlightSecond));

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

		// R-9. Last, so nothing above waits on it. The first actor is the one the
		// addon's decision started from, so they open and the second answers.
		Watchers::GetSingleton().OnSceneStarted(a_request,
			static_cast<std::uint32_t>(_inFlightFirst),
			static_cast<std::uint32_t>(_inFlightSecond));
		Barks::GetSingleton().OnSceneStarted(a_request,
			static_cast<std::uint32_t>(_inFlightFirst),
			static_cast<std::uint32_t>(_inFlightSecond),
			_inFlightScenario);
	}

	std::int32_t PapyrusLink::RunningRequest() const
	{
		NamedLock lock{ _counter, "request counter" };
		return _sceneRunning ? _inFlightRequest : 0;
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

		// Rapport rules the faces it holds past the engine's merge, when Anatomy is
		// there: every face and every line passes here, so nothing escapes it.
		FaceAuthority::GetSingleton().OnOrder(a_order);

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

	void PapyrusLink::RequeueStranded(
		Order::Kind a_kind, std::uint32_t a_formID, std::string_view a_setID, std::string_view a_extra)
	{
		NamedLock lock{ _orderLock, "order queue" };

		// THE ORDER IS PASSED IN, not read from the latch, and the first version
		// of this got that wrong.
		//
		// It took "the order you were just handed" from _orderActor/_orderSet,
		// reasoning that asking the bridge to describe it again invited it to
		// describe it differently. But the drain loop dispatches DoOrder with
		// CallFunctionNoWait, up to eight per poll -- so it takes the next order,
		// and the next, OVERWRITING the latch, long before the first DoOrder has
		// run and asked to requeue. The latch is only stable when exactly one
		// order is in flight, which is why it looked correct in testing.
		//
		// DoOrder's own parameters are the only description that cannot have moved
		// on, so they are the one to trust.

		// Only what is still true whenever the actor comes back. A removal is: the
		// sweat is on them and should not be. An application is not: the face was
		// chosen for a stage of a scene that has since ended, and putting it on
		// them when they walk back into the cell would be a bug wearing the
		// costume of a fix.
		switch (a_kind) {
		case Order::Kind::kRemoveOverlay:
		case Order::Kind::kRelease:
		case Order::Kind::kClearExpression:
			break;
		default:
			return;
		}

		if (a_formID == 0) {
			return;
		}

		// The same clear asked for twice is the same clear. Without this an actor
		// who stays away accumulates one entry per scene they were never cleared
		// from, and the list is the thing that has to stay small.
		for (const auto& held : _stranded) {
			if (held.formID == a_formID && held.kind == a_kind && held.setID == a_setID) {
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

		_stranded.push_back(Order{ a_kind, a_formID, std::string{ a_setID }, std::string{ a_extra } });
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
			_orderVoice = 0;
			return 0;
		}

		const auto order = _orders.front();
		_orders.pop_front();

		_orderActor = static_cast<std::int32_t>(order.formID);
		_orderSet = order.setID;
		_orderExtra = order.extra;
		_orderVoice = static_cast<std::int32_t>(order.voice);
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
		// Not in the ledger -- their scene never ended -- so the load sweep would
		// miss them, and an interrupted scene is exactly how a morph gets left.
		Morphs::GetSingleton().OnSceneEnded(a_first, a_second);
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

	namespace
	{
		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
			    .count();
		}

		// Rapport:Bridge.OnTimer's PollMark steps, in order.
		std::string_view PollStepName(std::int32_t a_step)
		{
			switch (a_step) {
			case 1: return "entered (next poll already scheduled)";
			case 2: return "handshake checked";
			case 3: return "asked AAF its version";
			case 4: return "asked AAF its status (next: Pump)";
			case 5: return "Pump returned";
			case 6: return "the scene clock checked (StopScene asked if due)";
			case 7: return "stale requests dropped";
			case 8: return "a new request begun (StartScene asked if one was taken)";
			case 9: return "watchers swept";
			case 10: return "orders drained - the poll finished";
			default: return "never marked";
			}
		}
	}

	void PapyrusLink::NotePollStep(std::int32_t a_step)
	{
		if (a_step == 1) {
			_pollEntries.fetch_add(1);
		}
		_pollStep.store(a_step);
		_pollStepAtMs.store(NowMs());
	}

	void PapyrusLink::NoteMedicBeat()
	{
		_medicBeats.fetch_add(1);
		_medicBeatAtMs.store(NowMs());
	}

	// The three causes a silent poll has, told apart:
	//   - polls still ENTER but never reach Pump: a stack hangs at the named step,
	//     and a fresh one hangs there again every PollSeconds;
	//   - no entries, the medic still beats: the bridge's own clock or lock is gone
	//     (the medic's re-arm is the cure);
	//   - no entries and no medic beat: no Papyrus timer runs at all - a menu that
	//     pauses the game, or the VM itself.
	std::string PapyrusLink::StallEvidence() const
	{
		const auto now = NowMs();
		const auto ago = [now](std::int64_t a_at) {
			return a_at == 0 ? std::string{ "never" } : std::format("{:.0f}s ago", (now - a_at) / 1000.0);
		};
		const auto step = _pollStep.load();
		std::string menus;
		if (const auto ui = RE::UI::GetSingleton()) {
			for (const auto name : { "PauseMenu"sv, "PipboyMenu"sv, "Console"sv, "LoadingMenu"sv, "DialogueMenu"sv,
					 "ContainerMenu"sv, "BarterMenu"sv, "WorkshopMenu"sv, "LooksMenu"sv, "MessageBoxMenu"sv }) {
				if (ui->GetMenuOpen(RE::BSFixedString{ name })) {
					menus += menus.empty() ? "" : ", ";
					menus += name;
				}
			}
			menus = std::format("menuMode {}, freezeFramePause {}, open: {}", ui->menuMode, ui->freezeFramePause,
				menus.empty() ? "none of the pausing ones" : menus);
		} else {
			menus = "UI not available";
		}
		return std::format(
			"evidence: polls entered since the last Pump {}, last step {} '{}' {}; medic {} beat(s), last {}; {}",
			_pollEntries.load() - _entriesAtLastPump, step, PollStepName(step), ago(_pollStepAtMs.load()),
			_medicBeats.load(), ago(_medicBeatAtMs.load()), menus);
	}

	void PapyrusLink::CheckBridgeAlive()
	{
		const auto pumps = _pumps.load();
		const auto moved = pumps != _pumpsAtLastTick;
		_pumpsAtLastTick = pumps;

		if (moved) {
			if (_stallReported) {
				logger::info("the bridge is answering again after {} poll(s) total - {}", pumps, StallEvidence());
				_stallReported = false;
			}
			_entriesAtLastPump = _pollEntries.load();
			_silentTicks = 0;
			return;
		}

		if (!_bridgeReady.load()) {
			return;
		}
		if (_stallReported) {
			// Said again every third tick (about a minute) while it lasts: the first line
			// shows the moment it went quiet, these show whether a menu closing, the
			// medic's re-arm or nothing at all changed it.
			if (++_stallTicksSinceReport % 3 == 0) {
				logger::warn("the bridge is still silent - {}", StallEvidence());
			}
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
		_stallTicksSinceReport = 0;
		logger::error("{}", StallEvidence());
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

		// The stranded list, ALWAYS, INCLUDING WHEN IT IS EMPTY.
		//
		// A requeue that is working and a requeue that was never reached both
		// write nothing until an actor comes back, so "no stranded line in the
		// log" was two different facts wearing one appearance -- which is the
		// exact confusion this health block exists to end, reintroduced by the
		// person who wrote that sentence. Saying "holding 0" is the whole point:
		// it is what makes a later "holding 4" mean something.
		logger::info(
			"health: holding {} cleanup order(s) for actors who were not loaded, {} heal(s) so far",
			StrandedOrders(), _heals.load());

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
			Morphs::GetSingleton().OnSceneEnded(first, second);
		}
		Expressions::GetSingleton().OnSceneEnded();
		Barks::GetSingleton().OnSceneEnded();
		Watchers::GetSingleton().OnSceneEnded();
		Scenarios::GetSingleton().End();
		_sceneInFlight.store(false);
	}

	// One place that lets a request go, so the five fields cannot drift apart
	// again. Under the lock, and touching nothing that takes another one.
	void PapyrusLink::ClearInFlight()
	{
		NamedLock lock{ _counter, "request counter" };
		_stagedAffair = 0;
		_inFlightFirst = 0;
		_inFlightSecond = 0;
		_inFlightRequest = 0;
		_inFlightDuration = 0.0f;
		_inFlightScenario.clear();
		_sceneRunning = false;
		_stopAsked = false;
	}

	bool PapyrusLink::RefusedOurScene(std::string_view a_why)
	{
		// AAF reports a refusal through the SAME event it reports a scene start
		// with, in a four-argument form, and until now nothing did anything about
		// it beyond writing a line. The request stayed in flight -- so both actors
		// stayed flagged busy in AAF, unusable by every AAF mod, and every tick
		// reported "a scene is already running" -- until the 780-second watchdog
		// noticed. One refusal cost THIRTEEN MINUTES of the framework doing
		// nothing, which from the outside is indistinguishable from a wedge, and
		// was mistaken for one.
		//
		// Only ever called for a refusal carrying OUR meta tag, so this cannot
		// tear down a scene somebody else's mod was refused.
		if (!_sceneInFlight.load()) {
			return false;
		}
		OnRequestFailed(_inFlightRequest, a_why);
		return true;
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
		Scenarios::GetSingleton().NoteSceneRefused(a_why);

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
		// AAF may have got as far as walking them in before it failed.
		Morphs::GetSingleton().OnSceneEnded(first, second);
		Expressions::GetSingleton().OnSceneEnded();
		Barks::GetSingleton().OnSceneEnded();
		Watchers::GetSingleton().OnSceneEnded();

		// A scene with the PLAYER in it that fails after the request was taken is
		// something the player is waiting for: say so, rather than let a yes vanish
		// without a word (Overture O-9). NPC scenes fail quietly, as they always have.
		if (first == 0x14 || second == 0x14) {
			Narrator::GetSingleton().OnPlayerSceneFailed(first, second);
		}

		ClearInFlight();
		_sceneInFlight.store(false);
	}
}
