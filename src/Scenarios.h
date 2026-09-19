#pragma once

#include "NamedLock.h"
#include "TreeIndex.h"
#include "Orders.h"

namespace RP
{
	// Sex as a story rather than an action.
	//
	// A quickie is one stage. Sex at home has a prelude, a middle and a finish,
	// and each of those is a ROLE that many animations could fill. So a stage
	// carries TAGS, not a position id, and AAF's ChangePosition picks the
	// animation -- which means a scenario written here works with whatever packs
	// somebody has rather than only with the pack it was written against.
	//
	// AAF's own positionTreeData is a different thing and not a replacement: a
	// tree chains specific positions from ONE pack, is authored by that pack, and
	// there are ten of them across this entire install. A scenario is narrative
	// and pack-agnostic.
	//
	// Rapport does NOT choose which scenario runs. The framework executes one an
	// addon names; the scheduler names one only while it is standing in for
	// Chemistry, and that is marked where it happens.
	class Scenarios
	{
	public:
		struct Stage
		{
			std::string id;
			float       seconds{ 30.0f };
			// ALTERNATIVES, tried one at a time. AAF's includeTags is an AND: it
			// wants one animation carrying every tag given, so sending five means
			// asking for something that is all five at once, which nothing is.
			std::vector<std::string> options;
			std::string include;   // the authored list, for the log
			std::string exclude;   // tags it must avoid
			// How far along the story this stage is, 1..3. The ACT decides which
			// family of face fits; this decides how hard they are feeling it.
			//
			// `face` is now only a fallback, used before AAF has reported any
			// animation. It cannot be the answer on its own: a stage named "oral"
			// carried Rapport_Oral and got it applied over "Impregnate Cowgirl",
			// because a stage has not chosen the act since ChangePosition went.
			int         intensity{ 2 };
			std::string face;      // opening face, before any act is known
			bool        playable{ true };   // false when nothing installed matches

			// Ask AAF for NOTHING and let whatever is playing carry on.
			//
			// AAF's packs ship position trees -- 85 of them here, 22 ending in an
			// explicit Climax branch -- and a position that declares one walks its
			// own stages on the pack author's timings:
			//
			//     Play Stage 1 -> ... -> Play Stage 6 -> Climax -> Finish
			//
			// Which means our stage clock was CUTTING THOSE OFF: a stage lands on a
			// tree-bearing position, AAF starts walking it, and thirty seconds later
			// the next stage yanks it away long before Climax. A climax cannot be
			// asked for either -- UAP hides every standalone climax position and
			// nulls its animation, deliberately, because the tree is how you are
			// meant to reach one.
			//
			// So the last stage stops asking. Rapport keeps the face and the
			// aftermath, which are its own, and AAF finishes the story it started.
			// Costs nothing when the position has no tree: it simply keeps playing.
			bool        handover{ false };

			// Choose a TREE for this stage instead of taking whatever the tags
			// return, then hand over to it. The stage's include/exclude describe the
			// KIND of ending wanted; Rapport resolves that against the trees this
			// install actually has, so the scenario stays pack-agnostic while the
			// ending stops being luck.
			//
			// Degrades to a plain handover when nothing qualifies or the index is
			// empty: the scene carries on rather than being cut short.
			bool        tree{ false };

			// Only trees that reach a climax or an orgasm are eligible. On one real
			// install 9 of 40 entry positions lead to a tree that simply stops, and
			// an addon that asked for an ending must not be handed one of those.
			bool        requireEnding{ true };
		};

		struct Scenario
		{
			std::string        id;
			std::vector<Stage> stages;

			// Total of the stages that can actually be played. A skipped stage
			// costs nothing, so this is what the scene should be asked to run for.
			[[nodiscard]] float PlayableSeconds() const
			{
				float total = 0.0f;
				for (const auto& stage : stages) {
					if (stage.playable) {
						total += stage.seconds;
					}
				}
				return total;
			}
		};

		[[nodiscard]] static Scenarios& GetSingleton() noexcept;

		void Load();

		[[nodiscard]] const Scenario* Find(std::string_view a_id) const;

		// ---- running one ------------------------------------------------------
		// Begins at the first playable stage. Returns false when the scenario is
		// unknown or has nothing playable at all, which leaves the scene to behave
		// as an ordinary single-animation one.
		bool Begin(std::string_view a_id, std::uint32_t a_first, std::uint32_t a_second);

		// The position a scene should START on, chosen from the tree catalogue
		// before StartScene is called. Empty when nothing qualifies, and then the
		// scene starts unconstrained as it always did.
		//
		// This is the whole mechanism now. ChangePosition is abandoned: it was
		// refused 26 times out of 26 with tags, and refused again when handed a
		// position id and no filters at all -- AAF answered "there are no FEMALE
		// HUMAN + MALE HUMAN animations" with includeTags(NONE), for a pair it
		// starts scenes for constantly. Nothing about that is a content problem
		// and nothing we pass changes it.
		//
		// StartScene takes the same position id and works, so the choice moves to
		// the one moment that can act on it.
		[[nodiscard]] std::string ChooseSceneStart(
			std::string_view a_id, std::uint32_t a_first, std::uint32_t a_second);

		// How well a scenario would play for a specific pair, RIGHT NOW, without
		// starting anything. An addon walks two actors across a room before it can
		// ask for a scene, and finding out then that nothing fits wastes the walk
		// and strands them mid-floor.
		//
		// Ordered worst to best is deliberate: an addon comparing two scenarios
		// can just take the larger, and a new value can only be added at the ends
		// without renumbering what an already-compiled addon believes.
		enum class Quality : std::int32_t
		{
			kUnknownScenario = -1,  // the id is not in scenarios.json

			// The scenario wanted a tree and nothing in the catalogue fits this
			// pair. The scene STILL PLAYS -- AAF chooses the position and there
			// is no guaranteed ending -- so this is a weaker promise, never a
			// refusal. On this install it is what two women get for every
			// tree-bearing scenario, because no f_f position enters a tree.
			kNothingFits = 0,

			// The scenario asks for no tree at all (quickie). It will play; AAF
			// chooses, and it may be short.
			kUnconstrained = 1,

			// A tree was found, but only by giving up the guaranteed climax.
			kNoGuaranteedEnding = 2,

			// A tree that matches and is known to reach an ending.
			kGood = 3,
		};

		[[nodiscard]] Quality Preflight(
			std::string_view a_id, std::uint32_t a_first, std::uint32_t a_second) const;

		// How long the chosen tree is authored to run, or 0 when none was chosen.
		[[nodiscard]] float ChosenSeconds() const;

		// AAF's tree moved to its next position. THIS is what advances the story,
		// not a clock.
		//
		// There is no timer in this any more, and that is the point. The stage
		// clock was scaling declared branch times onto wall time, and the declared
		// time is not wall time: measured, a tree declaring 120s ran 179.9s in one
		// scene and 226.3s in another -- 1.50x and 1.89x, so not even a constant to
		// correct by. Everything downstream of that guess was wrong by the same
		// margin, most visibly the orgasm face arriving 58 to 63 seconds before the
		// orgasm animation.
		//
		// AAF knows exactly when it steps, and says so. Counting those steps is
		// exact and needs no arithmetic about durations at all.
		//
		// Note what is NOT used: OnStageEvent. The bridge registers for it and it
		// fired zero times across three complete scenes, so the hook whose name
		// promises this is silent. OnAnimationChange is the one that works, and its
		// count matched the catalogue's stage count for all three trees.
		void NoteAnimationAdvanced();

		// A scene would not start. If its tree needed furniture, the next one will
		// not ask for any.
		void NoteSceneRefused();

		// One started, so the room is clearly fine.
		void NoteSceneStarted();

		// Advances when the current stage has run its seconds. Called on the same
		// poll as everything else.
		void Pump();

		void End();

		// The length the scene should be asked to run for: the sum of the stages
		// that can actually be played.
		[[nodiscard]] float SecondsFor(std::string_view a_id) const;

		// ---- the tag index ---------------------------------------------------
		// Every tag the installed animation packs actually use, read straight off
		// disk at startup.
		//
		// This exists so "skip a stage nothing can fill" is decided BEFORE a scene
		// starts. The alternative is FindMatchingAnimations, which is asynchronous
		// and whose argument layout has never been observed in this project -- and
		// a stage that stalls waiting for an answer is worse than one that was
		// quietly dropped.
		[[nodiscard]] bool AnyContentFor(std::string_view a_includeTags) const;

		[[nodiscard]] static std::filesystem::path ConfigPath();
		[[nodiscard]] static std::filesystem::path AAFDataPath();

	private:
		void IndexInstalledTags();
		void MarkPlayableStages();

		void EnterStage(std::size_t a_index, std::vector<Order>& a_out);
		void SendCurrentOption(std::vector<Order>& a_out);

	public:
		// AAF refused what the current stage asked for. Try this stage's next
		// alternative; when they run out, move on. Its refusal is better evidence
		// than the tag index, which knows a tag exists somewhere but not whether
		// an animation exists for THIS pair.
		void OnRefused(std::string_view a_why);


	private:

		mutable std::timed_mutex _lock;

		// ---- the scene in progress -------------------------------------------
		const Scenario* _running{ nullptr };
		std::size_t     _stage{ 0 };
		std::size_t     _option{ 0 };


		// The tree this scene was started on, and what it is authored to run for.
		// Set when a scene we chose a furniture tree for failed to start, cleared
		// the moment one starts. The next choice then takes NoFurn only, because
		// asking again for a couch that is not there asks for the same failure.
		// One rung-by-rung run of the selection ladder, decided and not yet acted
		// on, so the real start and the pre-flight cannot answer differently.
		struct Selection
		{
			const TreeIndex::Entry* entry{ nullptr };
			const Scenario*         scenario{ nullptr };
			bool                    relaxed{ false };        // gave up the ending
			bool                    unconstrained{ false };  // asks for no tree
			std::string_view        why;                     // when entry is null
		};

		// Resolve the face for where the story and the animation both are, and queue
		// it if it changed. Caller holds _lock.
		//
		// Shared by the poll and by the tree-step notification on purpose. Doing it
		// only on the poll left the face up to a whole poll interval behind the
		// animation -- which defeats the reason for listening to the steps at all.
		void ResolveFaceLocked(int a_intensity, std::string_view a_stageID, std::vector<Order>& a_out);

		[[nodiscard]] Selection SelectLocked(
			std::string_view a_id,
			std::uint32_t    a_first,
			std::uint32_t    a_second,
			bool             a_avoidFurniture) const;

		bool        _avoidFurniture{ false };

		// The declared stage seconds are WEIGHTS, and this scales them onto the
		// length the chosen tree is actually authored for. 1.0 when that length is
		// unknown, which is the old behaviour and the honest answer.
		float       _stageScale{ 1.0f };

		// How many positions the chosen tree walks, from the catalogue, and how
		// many steps AAF has actually taken. The scenario's stages are spread over
		// the first; the second says where we are.
		//
		// _treeSteps is 0 when no tree was chosen -- an unconstrained scene, which
		// is what quickie asks for by design and what every female/female pair gets
		// on this install. Such a scene takes no tree steps at all, so it keeps the
		// clock. That is not a redundant second mechanism; it is the only one
		// available when there is nothing stepping.
		std::uint32_t _treeSteps{ 0 };
		std::uint32_t _stepsSeen{ 0 };

		// The face we last asked for, so an unchanged answer queues nothing.
		std::string _faceApplied;

		// Has this scene reached a climax yet? Once it has, the face never drops
		// back below the top intensity.
		//
		// Observed: "Prone Bone" climaxes at step 2 of 5 and keeps going, so the
		// climax tag vanished from step 3 and the face fell from Rapport_Climax to
		// Rapport_Pleasure_2 -- MILDER than the step before it. She visibly
		// un-orgasmed and then calmed down mid-scene.
		bool _climaxSeen{ false };

		std::string _chosenPosition;
		float       _chosenSeconds{ 0.0f };
		std::uint32_t   _first{ 0 };
		std::uint32_t   _second{ 0 };
		std::chrono::steady_clock::time_point _stageStartedAt{};

		std::vector<Scenario>                      _scenarios;
		std::unordered_set<std::string>            _tags;   // lowercased
	};
}
