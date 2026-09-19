#pragma once

namespace RP
{
	// What endings the installed packs can actually deliver, and how to ask for one.
	//
	// AAF's packs ship POSITION TREES: a chain of positions with authored timings
	// that walks itself to an ending.
	//
	//     Play Stage 1 -> ... -> Play Stage 6 -> Climax -> Finish
	//
	// There is no API for them -- the word "tree" appears nowhere in AAF's Papyrus,
	// because trees run in the SWF -- but a POSITION may declare one with
	// positionTree="...", and positions are what ChangePosition takes. So naming a
	// position IS choosing a tree, and that is the whole mechanism here.
	//
	// It matters because a climax cannot be requested any other way. UAP hides
	// every standalone climax position and nulls its animation, on purpose, because
	// the tree is the intended route. Asking by tag returns nothing and is right to.
	//
	// Measured on one real install: 83 trees, of which 42 end in an Orgasm branch
	// and 22 in a Climax branch, reachable from 40 visible female+male positions --
	// and 9 of those 40 lead somewhere with no ending at all, which is exactly what
	// an addon relying on a climax must not be handed.
	class TreeIndex
	{
	public:
		// Ordered deliberately: a later value is a better ending, so comparing them
		// ranks candidates without a separate table.
		enum class Ending
		{
			kNone = 0,      // no terminal branch worth the name
			kFinishOnly,    // it stops, but nothing happens first
			kOrgasm,
			kClimax
		};

		struct Entry
		{
			std::string                     positionID;   // what we name to AAF
			std::string                     treeID;
			std::unordered_set<std::string> tags;         // the ENTRY position's, lowercased

			// The tags of the position the tree ENDS on, which is a different
			// animation from the one it starts on and usually a different act.
			// Measured here: 54 of 66 entry/ending tag sets differ -- "Pit Doggy"
			// enters doggy and from-behind and ends blowjob, climax and from-front.
			// A scenario asking to END a certain way was being matched on the
			// opening, so it could ask for doggy and get a facial.
			//
			// Empty when the tree names no terminal position. Selection then falls
			// back to the entry tags, because that is the only thing there is.
			std::unordered_set<std::string> endingTags;

			// Does any position this tree can reach actually carry a climax tag?
			//
			// `ending` is graded from a BRANCH NAME, and measured here that is a
			// promise 27 times out of 61: "rxl_bp70_impregnate_mish_Tree" has
			// branches called Orgasm and Finish, and every position it can reach
			// carries the same eleven tags with no climax among them. Observed in
			// game -- that tree ran a full scene and never emitted a climax tag, so
			// no climax face could fire and the aftermath had nothing to key on.
			//
			// 34 of 74 entries have a climax-tagged position somewhere; only 32 have
			// one at the exit, so this asks ANYWHERE. AAF chooses its own branch and
			// a climax reached mid-tree still counts.
			bool climaxTagged{ false };
			Ending                          ending{ Ending::kNone };

			// Sum of the branches' authored time. ZERO MEANS UNKNOWN, not instant:
			// some trees carry no time attributes at all and AAF paces them some
			// other way. Treating 0 as a duration would rank those as the shortest
			// thing available, which is the wrong answer in the dangerous direction.
			float         seconds{ 0.0f };
			std::uint32_t stages{ 0 };

			[[nodiscard]] bool LengthKnown() const noexcept { return seconds > 0.0f; }
		};

		[[nodiscard]] static TreeIndex& GetSingleton() noexcept;

		void Load();

		// The best entry whose tags include at least one of a_include, none of
		// a_exclude, and whose tree reaches a real ending when a_requireEnding.
		//
		// a_budgetSeconds is how long the caller can give it; an entry of unknown
		// length is allowed but ranked below one that is known to fit, because a
		// tree cut off before its climax is the failure this exists to prevent.
		//
		// Returns nullptr when nothing qualifies, which is a real answer: the
		// caller should then leave the scene alone rather than pick badly.
		// a_composition is AAF's pair tag -- "f_m", "m_m", "f_f" -- and an entry
		// that does not carry it is not eligible at all. An empty string means the
		// sexes were never reported, and then it is not filtered on: a missing fact
		// must not silently narrow the catalogue to nothing.
		// a_noFurnitureOnly drops every tree that needs a couch, a bed, a desk or
		// anything else that has to exist nearby. Normally furniture is only a
		// preference -- AAF does find it indoors -- but after a scene has actually
		// failed to start, asking for it again is asking for the same failure.
		[[nodiscard]] const Entry* Choose(
			std::string_view a_include,
			std::string_view a_exclude,
			std::string_view a_composition,
			bool             a_requireEnding,
			bool             a_noFurnitureOnly,
			float            a_budgetSeconds) const;

		// Does this entry need furniture that has to be there already?
		[[nodiscard]] static bool NeedsFurniture(const Entry& a_entry);

		[[nodiscard]] const Entry* Find(std::string_view a_positionID) const;

		[[nodiscard]] std::size_t Size() const noexcept { return _entries.size(); }
		[[nodiscard]] std::size_t WithEnding() const noexcept { return _withEnding; }

		// An empty index means the scan found nothing -- no AAF folder, no files, no
		// read access. That is not evidence that trees are absent, so callers fall
		// back to their old behaviour rather than concluding the install has none.
		[[nodiscard]] bool Usable() const noexcept { return !_entries.empty(); }

		[[nodiscard]] static std::string_view Describe(Ending a_ending);

	private:
		std::vector<Entry> _entries;
		std::size_t        _withEnding{ 0 };
	};
}
