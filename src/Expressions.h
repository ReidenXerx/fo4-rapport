#pragma once

#include "NamedLock.h"
#include "Orders.h"

namespace RP
{
	// The face during a scene.
	//
	// Why this exists at all: across the whole AAF install there are ten mfgSet
	// references, and the pack that was playing when the owner first watched a
	// Rapport scene has exactly one of them -- Sleep_EyesClosed. The faces are
	// blank because nothing asks them to be anything else. That is a content gap
	// every animation pack shares, and a framework is the right place to fill it.
	//
	// Unlike the overlays this layer depends on nobody. A morph id is an index
	// into the engine's own facial morph table, so there is no texture to ship
	// and no mod whose assets we are borrowing.
	//
	// The one thing it must never do is leave a face behind. An expression is
	// applied with lock="true", which holds it against anything else that would
	// move it, so a scene that ends badly -- or a save taken in the middle of one
	// -- would otherwise leave a settler wearing it for the rest of the
	// playthrough. Everyone currently wearing one is therefore written into the
	// save, and cleared on the next load.
	class Expressions
	{
	public:
		[[nodiscard]] static Expressions& GetSingleton() noexcept;

		void Load();
		[[nodiscard]] bool Enabled() const noexcept { return _enabled; }

		// A scene of ours began. Duration is what we asked AAF for, and the steps
		// below are placed as fractions of it.
		void OnSceneStarted(std::uint32_t a_first, std::uint32_t a_second, float a_durationSeconds);

		// The tags of one animation, as AAF reported them. They decide which
		// progression the scene gets -- kissing is not the same face as sex.
		void NoteTags(std::string_view a_tags);

		// The position AAF is playing, by name. Kept for the resolver: it is both an
		// override key and, failing everything else, words to read.
		void NotePosition(std::string_view a_position);

		// The act AAF is playing RIGHT NOW, as its own tags. Empty until an
		// animation naming an act has been reported.
		//
		// Only advances on an animation that names an act: a transition or a
		// standing idle between two positions must not erase where the scene
		// actually is.
		[[nodiscard]] std::string LiveAct() const;

		// Which face fits the act being played, at the arousal the story has
		// reached. The ACT picks the family and the STAGE picks the level.
		//
		// The two are genuinely different questions and conflating them is what
		// went wrong: a scenario stage named "oral" put Rapport_Oral on an actor
		// while AAF played "Impregnate Cowgirl" -- an oral face, mid-vaginal,
		// observed in game. A stage can no longer choose the act, so it can no
		// longer name the face; all it knows is how far along the story is.
		//
		// A climax tag wins outright. It is the one moment whose timing we used to
		// get badly wrong in the other direction: the stage clock put
		// Rapport_Climax on at 77s of a scene whose climax animation began at 140s
		// and which ran for 180s, so both actors wore an orgasm face for over a
		// minute and a half before there was one.
		//
		// a_intensity is 1..3. Returns empty to mean "leave the face alone",
		// which is the honest answer before any animation has been reported.
		// a_position is the position id AAF reported, which is consulted FIRST
		// against the override file and LAST as words to read. Empty is fine and
		// means "tags only".
		// How many styles tools/make_mfg.py generates per expression. The two MUST
		// agree: this appends a number to a set name, and a name nothing defines is
		// a face that silently never appears. The generator prints every set it
		// wrote so the pair can be checked by eye.
		static constexpr std::uint32_t kStyles = 6;

		// The set name for THIS actor.
		//
		// Everybody gets the same expressions; the style decides how they wear them.
		// Derived from the form id, so an actor keeps theirs for the whole
		// playthrough and it reads as character rather than the game shuffling faces
		// at them -- and it costs no storage, so it survives a save with nothing
		// written anywhere.
		//
		// Rapport_Clear has no variants: there is only one way to put a face back.
		[[nodiscard]] static std::string VariantFor(
			std::string_view a_setID, std::uint32_t a_formID);

		[[nodiscard]] static std::string_view FaceForAct(
			std::string_view a_actTags, std::string_view a_position, int a_intensity);

		// The face for ONE member, from the scene's face. The oral face belongs to the
		// MOUTH, not to the scene: in a blowjob the man is not sucking anything, and he
		// wore Rapport_Oral until the owner saw it (2026-09-24). In a mixed pair whose act
		// names which side the mouth is on, the mouth's owner keeps Rapport_Oral and the
		// other wears Rapport_Pleasure_<a_otherLevel>. A same-sex pair, a group, or an act
		// that does not say (69, rimming) keeps it on everyone -- nothing tells them apart.
		[[nodiscard]] static std::string FaceFor(std::string_view a_sceneFace, std::string_view a_actTags,
			std::string_view a_position, std::uint32_t a_actor, const std::vector<std::uint32_t>& a_members,
			int a_otherLevel);

		// How wet and how flushed, from the face that was chosen. Sweat follows
		// the expression rather than the clock for the same reason the face does:
		// AAF's tree steps are what actually advance a scene, and a body that is
		// dripping while the face is still anticipating reads as two mods.
		//
		// Returns -1 to LEAVE whatever is on -- which is what the afterglow wants,
		// since a flush outlasts the act that caused it -- 0 for none, else 1..3.
		[[nodiscard]] static int HeatLevelFor(std::string_view a_faceSetID);

		// "" for level <= 0, else the overlaySet id in Rapport_overlayData.xml.
		[[nodiscard]] static std::string HeatSetFor(int a_level);

		// For the SCENARIO path, which pushes its own expression orders and never
		// goes through Collect. Takes this subsystem's lock, so it is called from
		// Scenarios exactly as LiveAct already is -- scenarios then expressions,
		// never the reverse, which is what keeps that ordering a rule rather than
		// a coincidence.
		void CollectHeat(std::string_view a_faceSetID, std::uint32_t a_first,
			std::uint32_t a_second, std::vector<Order>& a_out);
		static constexpr int kHeatLevels = 3;

		// What AAF is playing, by name. Empty until an animation has been reported.
		[[nodiscard]] std::string LivePosition() const;

		// Data/F4SE/Plugins/Rapport/act-overrides.json, if it exists. Position id ->
		// one of oral / pleasure / kiss / climax / none.
		//
		// Small on purpose. The pack's own tags classify 98.8% of the positions on
		// the reference install and reading the position's NAME takes that to 99.2%,
		// so this file is for the handful left and for anything an author tagged
		// wrongly. A full hand-built inventory would be 1131 entries here, 99% of
		// them a second copy of data that ships with the pack and goes stale the
		// moment that pack updates.
		void LoadOverrides();
		[[nodiscard]] std::size_t OverrideCount() const;

		// Says, once at load, every position this build cannot read. That list IS
		// the override file's contents, and it comes from the classifier that
		// actually runs rather than from a tool that reimplements it -- so the two
		// can never disagree about what needs overriding.
		void ReportUnclassified() const;

		void OnSceneEnded();

		// A scenario is driving the faces for this scene, so the percentage
		// schedule must not also run. Everything else stays: the wearing list, the
		// clearing and the co-save record are still this layer's, because a face
		// put on by a stage still has to come off at the end.
		void StandDown();

	private:
		std::string _liveAct;
		std::string _livePosition;

		// Lowercased position id -> face set. Static after Load; read from the
		// resolver, which is why it is a plain map behind the same lock.
		static inline std::unordered_map<std::string, std::string> _overrides;

	public:

		// Called on every poll. Queues the next expression when its moment has
		// come, and the clearing one when the afterglow is over.
		void Pump();

		// ---- the save --------------------------------------------------------
		// Form ids currently wearing a Rapport expression. Small, and only
		// non-empty during and just after a scene -- which is exactly when a save
		// is most likely to strand one.
		//
		// a_spareForeign: leave alone anybody a scene Rapport did not start is still
		// playing on (R-22). Only our own afterglow passes it -- that clear sweeps
		// the whole list on purpose, and must not take a face off somebody else's
		// scene mid-act. A load and the panic switch clear everybody.
		void CollectClear(std::vector<Order>& a_out, std::string_view a_why, bool a_spareForeign = false);

		// ---- scenes Rapport did not start (R-22) ------------------------------
		// ForeignScenes drives those faces. This layer keeps their actors on the
		// wearing list -- so a save taken mid-scene clears them on the load, exactly
		// as for our own -- and spares them in our own afterglow's clear.
		void HoldForeign(std::uint32_t a_formID);

		// Clears these actors' faces and whatever heat they WEAR, takes them off the
		// list, and appends the orders to a_out for the caller to send. Whether or not
		// they are still on the list: a load or the panic switch may have swept it while
		// their scene still had a face on them, and a missed clear is a face locked for
		// good where a redundant one costs two calls. An actor our own scene has taken
		// since is left to it.
		void ReleaseForeign(const std::vector<std::uint32_t>& a_formIDs, std::vector<Order>& a_out,
			std::string_view a_why);

		// Each actor's heat climbs to the level given if it is higher than what that
		// actor wears -- per ACTOR, because the overlay is on a body, not a scene, and a
		// body can walk out of one scene still sweating into the next (R-22).
		void RaiseHeat(const std::vector<std::pair<std::uint32_t, int>>& a_targets, std::vector<Order>& a_out);

		// expressions.json's steps read as levels: (fraction, 0 anticipation .. 3). A
		// timed climax step reads as 3 -- only a climax TAG gives the climax face.
		[[nodiscard]] std::vector<std::pair<float, int>> IntensitySchedule() const;

		[[nodiscard]] std::string AfterSet() const;
		[[nodiscard]] float       DazedSeconds() const;
		static void Send(const std::vector<Order>& a_orders);

		[[nodiscard]] std::vector<std::uint32_t> Wearing() const;
		void RestoreWearing(std::vector<std::uint32_t> a_wearing);

		// Queues the clearing set for everyone on the list and empties it. Used on
		// load and by the panic switch.
		void ClearEveryone(std::string_view a_why);

		void Reset();

		[[nodiscard]] static std::filesystem::path ConfigPath();

	private:
		struct Step
		{
			float       at{ 0.0f };   // fraction of the scene's length
			std::string set;
		};

		// Records who should wear a_setID and appends the orders to a_out. It does
		// NOT send them: sending reaches into PapyrusLink, and nothing here may
		// call another subsystem while holding this one's lock. The poll died
		// inside exactly that call.
		void Collect(std::string_view a_setID, std::vector<Order>& a_out);

		// Under _lock: a_formID's heat climbs to a_level if that is higher than what it
		// wears; the set it wears comes off first, because AAF stacks overlays.
		void ClimbHeat(std::uint32_t a_formID, int a_level, std::vector<Order>& a_out);

		// Under _lock: the heat a_formID wears comes off. With no record -- after a load
		// -- every level is swept, because an overlay nothing removes is on for good.
		void TakeOffHeat(std::uint32_t a_formID, std::vector<Order>& a_out);

		// "Rapport_Heat_2" -> 2; anything else 0.
		[[nodiscard]] static int LevelOfHeat(std::string_view a_heatSet);

		mutable std::timed_mutex _lock;

		bool        _enabled{ true };
		float       _dazedSeconds{ 20.0f };
		std::string _clearSet{ "Rapport_Clear" };
		std::string _afterSet{ "Rapport_Dazed" };
		std::string _kissSet{ "Rapport_Kiss" };
		std::vector<Step>        _steps;
		std::vector<std::string> _kissOnlyTags;

		// ---- the scene in progress ------------------------------------------
		std::uint32_t _first{ 0 };
		std::uint32_t _second{ 0 };
		float         _duration{ 0.0f };
		std::chrono::steady_clock::time_point _startedAt{};
		std::chrono::steady_clock::time_point _endedAt{};
		bool          _running{ false };
		bool          _stoodDown{ false };
		bool          _dazing{ false };
		bool          _clearPending{ false };   // a save was made mid-scene
		std::size_t   _nextStep{ 0 };
		std::string   _tags;
		bool          _sawSexTag{ false };

		// The heat set currently on both actors, "" for none. Only ever one at a
		// time: AAF stacks what it is given, so a level change must take the old one
		// off before putting the new one on.
		std::string _heatApplied;

		// Heat only ever CLIMBS within a scene, so this is the high-water mark and
		// not simply the current level.
		//
		// The face legitimately falls -- an expression eases off when a stage is
		// gentler, and that reads correctly. Heat inherited that and it was wrong:
		// measured in game, a `tender` scene went Heat_2 (prelude, intensity 2) ->
		// Heat_1 (main, intensity 1) -> Heat_3 (finish), so the body got LESS sweaty
		// halfway through. Sweat accumulates; nothing about a gentler minute dries
		// anybody off. Reset by CollectClear when the scene's afterglow ends.
		int _heatLevel{ 0 };

		std::vector<std::uint32_t> _wearing;

		// The subset of _wearing a foreign scene holds right now (R-22).
		std::vector<std::uint32_t> _foreignHeld;

		// The heat overlay each actor wears now, whoever put it on. Not saved.
		std::unordered_map<std::uint32_t, std::string> _heatOn;

		// Actors whose heat nobody tracked this session: the wearing list a save brought
		// back. Only THEY are swept at every level; anybody else with no _heatOn entry is
		// known to wear none, and sweeping them cost three calls for nothing.
		std::unordered_set<std::uint32_t> _heatUnknown;
	};
}
