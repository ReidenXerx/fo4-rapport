#pragma once

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
			std::string include;   // tags AAF may choose from
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

		std::vector<Scenario>                      _scenarios;
		std::unordered_set<std::string>            _tags;   // lowercased
	};
}
