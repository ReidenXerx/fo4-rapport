#pragma once

#include "NamedLock.h"
#include "Orders.h"

namespace RP
{
	// AAF scenes Rapport did not start: the AAF menu's, and every other mod's (R-22).
	//
	// Why this exists: every scene feature used to follow Rapport's own request and
	// nothing else, while the takeover (A-11) stops CumOverlays' and Moisturizer's AAF
	// listeners for EVERY scene. The owner started scenes from AAF's own menu and saw
	// stony faces -- and those scenes got no cum at all either, because the listeners
	// that would have given it were ours to switch off and nothing of ours replaced
	// them. The owner, 2026-09-24: "we need have our ruling all the time just also do
	// it on ALL aaf scenes" -- forced scenes included, which Rapport's pleasure turns
	// into consensual play (R-22's second poll).
	//
	// Several can run at once, and alongside Rapport's own, so everything here is per
	// scene, keyed by AAF's LOCATION form id -- the one value every event of a scene
	// carries ([3] on OnSceneInit, [5] on the rest), and the one the bridge has always
	// matched its own scenes by. A ground scene's location is one of its actors.
	//
	// Stage A of R-22: the face for the act AAF is playing, building with the scene's
	// progress and winning outright on a climax tag; the afterglow and the clearing; and
	// the aftermath, decided from the scene's own tags when it ends. Skin heat is the
	// expression layer's, per ACTOR (Expressions::RaiseHeat), because an overlay is worn
	// by a body, not by a scene. The voices and the relationship records are later
	// stages of the same decision.
	//
	// Every entry point runs on the game's main thread -- the natives are bound
	// frame-synced, and the poll is one of them -- so they never overlap each other.
	class ForeignScenes
	{
	public:
		[[nodiscard]] static ForeignScenes& GetSingleton() noexcept;

		// One actor of a scene, as the bridge's Var opened on the Papyrus thread.
		struct Member
		{
			std::uint32_t formID{ 0 };
			std::int32_t  sex{ -1 };   // 0 male, 1 female, -1 unknown
			bool          player{ false };
			bool          child{ false };
			// A race Rapport does not dress (races.json, A-4) is never given a face or
			// cum, but stays a MEMBER: who the act landed on is decided with them there.
			bool          raceAllowed{ true };
			// Wearing this record's current face. Per member, because a member read
			// late (unloaded when an earlier event came) has to be given the face the
			// others already wear, and only a member who wears one has one to clear.
			bool          wearing{ false };
		};

		// a_duration is AAF's own [6]: the scene's length in seconds, or <= 0 for none
		// (a looping scene). A timed scene with no tree ENDS on it (aaf-sot, finding H,
		// measured 29.83 s against 30), so it is the scene's real length.
		void Started(std::int32_t a_location, std::vector<Member> a_members, std::string a_position,
			std::string a_tags, std::string a_meta, bool a_npcControlled, float a_duration);
		void Animation(std::int32_t a_location, std::vector<Member> a_members, std::string a_position,
			std::string a_tags);
		void Ended(std::int32_t a_location, std::vector<Member> a_members, std::string a_position,
			std::string a_tags);

		// Rapport's OWN scene, which the bridge claims and never forwards. It started
		// with these two: a record of somebody else's still holding either is over --
		// one actor, one AAF scene -- and its end was missed; left alone, the poll
		// would go on putting its faces over ours.
		void OwnSceneStarted(std::uint32_t a_first, std::uint32_t a_second);

		// Rapport's own scene at this location ended, and was claimed. Remembered like
		// any end, so a duplicate end -- which the bridge would forward, the request
		// being released -- cannot put a second layer of cum on the same people.
		void OwnSceneEnded(std::int32_t a_location, const std::vector<Member>& a_members);

		// Called on every poll: a timed scene's build, the afterglow's end, a timed
		// scene so far past its length that its end event must have been missed, and
		// a scene that never began to animate at all.
		void Pump();

		// A load or a new game. No scene survives one, and the faces they left are the
		// wearing list's to clear -- this only forgets.
		void Reset();

	private:
		using Clock = std::chrono::steady_clock;

		struct Scene
		{
			std::int32_t        location{ 0 };
			std::vector<Member> members;       // grows, never shrinks: Retire clears everybody it dressed
			std::string         meta;
			bool                npcControlled{ false };
			float               duration{ 0.0f };   // AAF's [6]; <= 0 means none
			std::uint32_t       stages{ 0 };        // the tree's, found ONCE at the start; 0 = no tree
			std::uint32_t       steps{ 0 };         // steps TAKEN: the entry animation is step 0
			bool                animating{ false }; // the first animation has arrived
			Clock::time_point   animatingAt{};
			bool                started{ false };   // a real OnSceneInit, not a join placeholder
			std::string         position;           // the one playing now, for the override file
			std::string         liveAct;            // the last tags that named an act
			std::string         allTags;            // every animation's, for the aftermath and the log
			std::string         lastActTags;        // the last tags an aftermath rule matched
			std::string         face;               // the BASE set on everybody now, "" for none
			bool                climaxSeen{ false };
			Clock::time_point   startedAt{};        // the record's, init or placeholder
			Clock::time_point   lastEventAt{};
			Clock::time_point   endedAt{};
			bool                ended{ false };
		};

		// What finishing a scene hands the expression layer and the aftermath, decided
		// under our lock and carried out after it.
		struct Retired
		{
			std::int32_t               location{ 0 };
			std::vector<std::uint32_t> release;   // faces to clear: members no other scene dresses
			std::vector<std::uint32_t> actors;    // for the aftermath, in AAF's slot order
			std::vector<std::int32_t>  sexes;
			std::vector<bool>          dressable; // beside actors: false for a race Rapport does not dress
			std::string                allTags;
			std::string                lastActTags;
			bool                       aftermath{ false };
			std::string                why;
		};

		// A scene finished at a location -- its end processed, or the scene given up on
		// because another took its actor or its place -- remembered for a while with who
		// was in it. It is what answers a LATE or a DUPLICATE end: one naming only its
		// cast, and not the cast of the scene now at the place, finishes nothing and
		// leaves no second layer of cum (A-22).
		//
		// By WHO, not by the position an end names: the actors are read from AAF's own
		// array, while what an end's position means for a tree, or after the player
		// changes position, has never been measured. Where the casts are the same, the
		// end is the live scene's: swallowing a live scene's real end would strand its
		// face and lose its cum, which is worse than a rare second layer.
		//
		// Kept even when a new scene takes the place: a ground scene's location is one
		// of its actors, so back-to-back scenes on the same victim share it, and the old
		// scene's end can be processed after the new one's start.
		struct Finished
		{
			std::int32_t               location{ 0 };
			std::vector<std::uint32_t> actors;
			Clock::time_point          at{};
		};

		// A record retired with no end event: its end, if it ever comes, finds nothing
		// left to decide. Kept until that end or a new scene at the place -- not on a
		// timer, because a scene we gave up on can still be running long after. A new
		// scene there turns it into a Finished, so the old end is still recognised.
		enum class Quiet
		{
			kOverdue,        // past its length: aftermath decided then; its events are ignored
			kNeverAnimated   // never began: nothing to leave behind; an animation revives it
		};

		struct QuietMark
		{
			Quiet                      kind{ Quiet::kOverdue };
			std::vector<std::uint32_t> actors;
		};

		// The face this scene should be wearing now, as a BASE set id ("" = leave it),
		// and what decided it, for the log.
		// Expressions::IntensitySchedule, read before our lock: (fraction, level) in order.
		using Schedule = std::vector<std::pair<float, int>>;
		[[nodiscard]] std::pair<std::string, std::string> FaceNow(Scene& a_scene, const Schedule& a_schedule) const;

		// a_face on everybody in the scene, into a_out; the heat level it calls for, per
		// actor, into a_heat (the expression layer climbs each actor's own); everybody it
		// put a face on into a_held.
		void Wear(Scene& a_scene, const std::pair<std::string, std::string>& a_face, std::vector<Order>& a_out,
			std::vector<std::pair<std::uint32_t, int>>& a_heat, std::vector<std::uint32_t>& a_held);

		// Under our lock: takes a scene out and says what finishing it means. a_keep:
		// members the scene replacing this one puts its own face on in the same call
		// -- not cleared, so the two orders do not fight. Anybody else it wore a face on
		// is released, unless another live record has put one on them since.
		[[nodiscard]] Retired Retire(Scene& a_scene, bool a_withAftermath, std::string_view a_why,
			const std::vector<Member>* a_keep) const;

		// Outside every lock: the clears, then the aftermath.
		static void Carry(std::vector<Retired>& a_retired);

		// One actor is in one AAF scene at a time. A member who turns up in a scene at
		// another location means every other live record holding them missed its end.
		void EvictElsewhere(std::int32_t a_location, const std::vector<Member>& a_members,
			std::vector<Retired>& a_out, const std::vector<Member>* a_keep, std::string_view a_where);

		// Takes the record at this location out: a new scene began there, or a child
		// turned up in it. A started scene whose end is still owed is remembered as
		// finished, and leaves its aftermath when a_aftermathIfOwed says it may.
		void RetireAt(std::int32_t a_location, std::vector<Retired>& a_out, std::string_view a_why,
			bool a_aftermathIfOwed, const std::vector<Member>* a_keep);

		void Remember(std::int32_t a_location, std::vector<std::uint32_t> a_actors, Clock::time_point a_now);

		// Is this end a finished scene's late or second one, rather than the end of the
		// scene now at the place? Says which, for the log; "" means it is not.
		[[nodiscard]] std::string_view EndOfFinished(std::int32_t a_location, const std::vector<Member>& a_members,
			Clock::time_point a_now) const;

		// A finished scene remembered at this location, still within its time.
		[[nodiscard]] const Finished* FinishedAt(std::int32_t a_location, Clock::time_point a_now) const;

		// Does a finished scene remembered here hold every one of these (and they are
		// somebody)? The test that tells an old scene's late event from a new one's.
		[[nodiscard]] bool FitsFinished(std::int32_t a_location, const std::vector<Member>& a_members,
			Clock::time_point a_now) const;

		mutable std::timed_mutex                     _lock;
		std::unordered_map<std::int32_t, Scene>      _scenes;
		std::unordered_set<std::int32_t>             _ignored;   // ours, or holding a child
		std::vector<Finished>                        _finished;
		std::unordered_map<std::int32_t, QuietMark>  _quiet;
	};
}
