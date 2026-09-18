#pragma once

#include "NamedLock.h"
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
			std::string face;      // the expression set for this moment
			bool        playable{ true };   // false when nothing installed matches
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

		// Advances when the current stage has run its seconds. Called on the same
		// poll as everything else.
		void Pump();

		void End();

		[[nodiscard]] bool Running() const;

		// The length the scene should be asked to run for: the sum of the stages
		// that can actually be played.
		[[nodiscard]] float SecondsFor(std::string_view a_id) const;
		[[nodiscard]] std::size_t Count() const noexcept { return _scenarios.size(); }

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
		[[nodiscard]] std::size_t KnownTags() const noexcept { return _tags.size(); }

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
		std::uint32_t   _first{ 0 };
		std::uint32_t   _second{ 0 };
		std::chrono::steady_clock::time_point _stageStartedAt{};

		std::vector<Scenario>                      _scenarios;
		std::unordered_set<std::string>            _tags;   // lowercased
	};
}
