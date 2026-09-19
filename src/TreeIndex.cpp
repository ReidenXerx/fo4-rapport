#include "TreeIndex.h"

namespace
{
	[[nodiscard]] std::string Lower(std::string_view a_text)
	{
		std::string out{ a_text };
		std::ranges::transform(out, out.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return out;
	}

	[[nodiscard]] std::unordered_set<std::string> SplitTags(std::string_view a_list)
	{
		std::unordered_set<std::string> out;
		std::string                     current;
		for (const char c : a_list) {
			if (c == ',') {
				if (!current.empty()) {
					out.insert(Lower(current));
					current.clear();
				}
			} else if (c != ' ' && c != '\t') {
				current.push_back(c);
			}
		}
		if (!current.empty()) {
			out.insert(Lower(current));
		}
		return out;
	}

	[[nodiscard]] std::vector<std::string> SplitList(std::string_view a_list)
	{
		std::vector<std::string> out;
		std::string              current;
		for (const char c : a_list) {
			if (c == ',') {
				if (!current.empty()) {
					out.push_back(Lower(current));
					current.clear();
				}
			} else if (c != ' ' && c != '\t') {
				current.push_back(c);
			}
		}
		if (!current.empty()) {
			out.push_back(Lower(current));
		}
		return out;
	}

	// One attribute out of an element's text. Deliberately crude, like the tag
	// scan it sits beside: the data is flat, every pack writes it the same way,
	// and tools/tagaudit.py has read it this way accurately since it was written.
	[[nodiscard]] std::string Attr(std::string_view a_element, std::string_view a_name)
	{
		std::string needle{ a_name };
		needle += "=\"";
		const auto at = a_element.find(needle);
		if (at == std::string_view::npos) {
			return {};
		}
		const auto open = at + needle.size();
		const auto close = a_element.find('"', open);
		if (close == std::string_view::npos) {
			return {};
		}
		return std::string{ a_element.substr(open, close - open) };
	}

	// The span of one element, from a_from to the '>' that closes its open tag.
	[[nodiscard]] std::string_view Element(std::string_view a_text, std::size_t a_from)
	{
		const auto end = a_text.find('>', a_from);
		return end == std::string_view::npos
		           ? a_text.substr(a_from)
		           : a_text.substr(a_from, end - a_from);
	}
}

namespace RP
{
	TreeIndex& TreeIndex::GetSingleton() noexcept
	{
		static TreeIndex singleton;
		return singleton;
	}

	std::string_view TreeIndex::Describe(Ending a_ending)
	{
		switch (a_ending) {
		case Ending::kClimax:
			return "climax";
		case Ending::kOrgasm:
			return "orgasm";
		case Ending::kFinishOnly:
			return "stops without one";
		default:
			return "no ending";
		}
	}

	void TreeIndex::Load()
	{
		_entries.clear();
		_withEnding = 0;

		const auto      folder = std::filesystem::path{ "Data" } / "AAF";
		std::error_code ec;
		if (!std::filesystem::exists(folder, ec)) {
			logger::warn(
				"trees: {} does not exist, so no ending can be chosen and scenarios keep their "
				"old behaviour",
				folder.string());
			return;
		}

		struct Tree
		{
			Ending        ending{ Ending::kNone };
			float         seconds{ 0.0f };
			std::uint32_t stages{ 0 };

			// The position the tree ends ON, taken from its exit branch. Every
			// branch in this install carries a positionID (530/530), so an exit
			// branch always names one -- this is not a best-effort field.
			std::string endPositionID;
			Ending      endGrade{ Ending::kNone };  // which exit won, when several

			// Every position any branch names. Needed to tell a tree that really
			// reaches a tagged climax from one that only calls a branch "Orgasm".
			std::vector<std::string> reachable;
		};
		std::unordered_map<std::string, Tree> trees;

		// Every declaration of a position id, merged. `retired` is sticky so a
		// later -- or earlier -- override that hides or nulls it wins regardless of
		// the order the files are read in. `order` keeps the catalogue stable
		// across runs rather than leaving it to the hash map.
		struct Decl
		{
			std::string                     treeID;
			std::unordered_set<std::string> tags;
			bool                            retired{ false };
			bool                            seen{ false };
		};
		std::unordered_map<std::string, Decl> declared;
		std::vector<std::string>              order;

		std::uint32_t files = 0;
		for (const auto& entry : std::filesystem::directory_iterator{ folder, ec }) {
			if (!entry.is_regular_file(ec)) {
				continue;
			}
			const auto name = Lower(entry.path().filename().string());
			if (!name.ends_with(".xml")) {
				continue;
			}

			std::ifstream file{ entry.path(), std::ios::binary };
			if (!file) {
				continue;
			}
			++files;
			const std::string text{ std::istreambuf_iterator<char>{ file },
				                    std::istreambuf_iterator<char>{} };

			// ---- the file's default hidden flag -----------------------------
			// A <defaults> element applies to every position in the file, and
			// missing it is how a whole pack of positions reads as selectable when
			// it is not. UAP's override file hides all 23 of its positions this
			// way and nothing else says so.
			bool hiddenByDefault = false;
			if (const auto at = text.find("<defaults"); at != std::string::npos) {
				hiddenByDefault = Lower(Attr(Element(text, at), "isHidden")) == "true";
			}

			// ---- trees ------------------------------------------------------
			for (auto at = text.find("<tree"); at != std::string::npos;
			     at = text.find("<tree", at + 1)) {
				const auto head = Element(text, at);
				auto       id = Attr(head, "id");
				if (id.empty()) {
					continue;
				}

				const auto close = text.find("</tree>", at);
				const auto body = text.substr(at, close == std::string::npos ? std::string::npos
				                                                             : close - at);

				// Branches NEST. Only one root-to-leaf path is ever played, so the
				// length and the stage count are the longest PATH, not the totals.
				//
				// Summing every branch over-stated nine of the eighty-five trees
				// here, worst 225s against an actual 120s path -- and since the
				// budget term is a 20-point swing on a 10-point band, those trees
				// were not demoted for a length they never run, they were erased.
				// They are the six-stage orgasm trees the budget was written for.
				Tree                tree;
				std::vector<float>  running;   // cumulative time down the current path
				bool                hasExit = false;

				for (std::size_t b = body.find("<branch"); b != std::string::npos;) {
					const auto nextOpen = body.find("<branch", b + 1);
					const auto nextClose = body.find("</branch>", b + 1);

					const auto branch = Element(body, b);
					const auto selfClosing = !branch.empty() && branch.back() == '/';

					float seconds = 0.0f;
					if (const auto attr = Attr(branch, "time"); !attr.empty()) {
						try {
							seconds = std::stof(attr);
						} catch (const std::exception&) {
							// Unreadable time contributes nothing rather than
							// poisoning the whole tree's length.
						}
					}

					const auto total = (running.empty() ? 0.0f : running.back()) + seconds;
					running.push_back(total);

					tree.stages = std::max(tree.stages, static_cast<std::uint32_t>(running.size()));
					tree.seconds = std::max(tree.seconds, total);

					if (auto onID = Attr(branch, "positionID"); !onID.empty()) {
						tree.reachable.push_back(std::move(onID));
					}

					const auto branchID = Lower(Attr(branch, "id"));

					auto grade = Ending::kNone;
					if (branchID.find("climax") != std::string::npos) {
						grade = Ending::kClimax;
					} else if (branchID.find("orgasm") != std::string::npos) {
						grade = Ending::kOrgasm;
					} else if (branchID.find("finish") != std::string::npos) {
						grade = Ending::kFinishOnly;
					}
					tree.ending = std::max(tree.ending, grade);

					if (Lower(Attr(branch, "isExit")) == "true") {
						hasExit = true;

						// Several exits can end one tree. Take the best-graded --
						// the scenario asked to END a certain way, so the branch
						// that actually gets there is the one that describes it.
						if (tree.endPositionID.empty() || grade > tree.endGrade) {
							if (auto onID = Attr(branch, "positionID"); !onID.empty()) {
								tree.endPositionID = std::move(onID);
								tree.endGrade = grade;
							}
						}
					}

					if (selfClosing) {
						running.pop_back();
					}

					// Walk the document in order, popping a level for every close
					// that lands before the next open.
					b = nextOpen;
					for (auto c = nextClose; c != std::string::npos && (b == std::string::npos || c < b);
					     c = body.find("</branch>", c + 1)) {
						if (!running.empty()) {
							running.pop_back();
						}
					}
				}

				// A tree with no exit branch does not end itself, whatever its
				// branches are called.
				//
				// Two trees here have exactly one ending-shaped branch and it is
				// named "Stage 4 (No orgasm)" -- so a substring test read them as
				// REACHING an orgasm on the strength of the branch that says there
				// is not one, and they were the entire result set for some tags
				// under requireEnding. Every one of the 81 real exit branches in
				// this install is marked isExit, so the marker is the fact and the
				// name is only the grade.
				if (!hasExit) {
					tree.ending = Ending::kNone;
				}

				trees.insert_or_assign(std::move(id), tree);
			}

			// ---- positions that enter one -----------------------------------
			//
			// AAF merges position declarations BY ID across files, and a pack can
			// retire another pack's position by re-declaring it. UAP does exactly
			// that: a second declaration with animation="Null", under a <defaults>
			// that hides the whole file, and crucially WITHOUT positionTree.
			//
			// Reading each declaration independently therefore kept the base pack's
			// live one and never saw the retirement -- eleven of eighty-five
			// entries here were positions AAF is guaranteed to refuse, and for some
			// tags they were the entire result set.
			//
			// So declarations are collected per id and retirement is STICKY: any
			// declaration that hides or nulls an id retires it, whatever order the
			// files happen to be read in. That fails in the safe direction -- drop
			// a position rather than hand AAF one it will not play -- and does not
			// depend on filesystem enumeration order, which is what decided it
			// before and only gave the right answer here by luck of collation.
			for (auto at = text.find("<position"); at != std::string::npos;
			     at = text.find("<position", at + 1)) {
				const auto element = Element(text, at);

				auto id = Attr(element, "id");
				if (id.empty()) {
					continue;
				}

				auto& decl = declared[id];

				// Before the hidden test: a climax position is hidden BY DESIGN
				// (UAP hides all of them) and its tags are exactly what a scenario
				// asking for an ending needs to match against. Only overwrite with
				// a non-empty set, so a bare retiring re-declaration cannot erase
				// the real pack's tags.
				if (auto declTags = SplitTags(Attr(element, "tags")); !declTags.empty()) {
					decl.tags = std::move(declTags);
				}

				const auto hiddenAttr = Attr(element, "isHidden");
				const bool hidden = hiddenAttr.empty() ? hiddenByDefault
				                                       : Lower(hiddenAttr) == "true";
				if (hidden || Lower(Attr(element, "animation")) == "null") {
					decl.retired = true;
					continue;
				}

				if (auto treeID = Attr(element, "positionTree"); !treeID.empty()) {
					decl.treeID = std::move(treeID);
					if (!decl.seen) {
						decl.seen = true;
						order.push_back(id);
					}
				}
			}
		}

		// ---- join -----------------------------------------------------------
		std::uint32_t orphaned = 0;
		std::uint32_t retired = 0;
		for (const auto& id : order) {
			auto& decl = declared[id];

			// Retired by an override somewhere. Not an orphan and not a fault --
			// a pack deliberately taking another pack's position out of play.
			if (decl.retired) {
				++retired;
				continue;
			}

			const auto found = trees.find(decl.treeID);
			if (found == trees.end()) {
				++orphaned;
				continue;
			}

			Entry made;
			made.positionID = id;
			made.treeID = std::move(decl.treeID);
			made.tags = decl.tags;
			made.ending = found->second.ending;

			if (const auto& endID = found->second.endPositionID; !endID.empty()) {
				if (const auto endDecl = declared.find(endID); endDecl != declared.end()) {
					made.endingTags = endDecl->second.tags;
				}
			}

			made.climaxTagged = std::ranges::any_of(
				found->second.reachable, [&](const std::string& pos) {
					const auto it = declared.find(pos);
					return it != declared.end() &&
					       std::ranges::any_of(it->second.tags, [](const std::string& tag) {
						       return tag.starts_with("climax");
					       });
				});
			made.seconds = found->second.seconds;
			made.stages = found->second.stages;

			if (made.ending >= Ending::kOrgasm) {
				++_withEnding;
			}
			_entries.push_back(std::move(made));
		}

		if (retired > 0) {
			logger::info(
				"trees: {} position(s) were retired by an override and are not in the catalogue - "
				"AAF would refuse them",
				retired);
		}

		logger::info(
			"trees: {} tree(s) in {} file(s); {} selectable entry position(s), {} of them reaching "
			"a climax or an orgasm",
			trees.size(), files, _entries.size(), _withEnding);

		if (orphaned > 0) {
			logger::warn(
				"trees: {} position(s) name a tree that is not installed - those endings are not "
				"available",
				orphaned);
		}
		if (_entries.empty()) {
			logger::warn(
				"trees: nothing was indexed. Scenarios keep their old behaviour rather than "
				"assuming this install has no trees");
		}
	}

	const TreeIndex::Entry* TreeIndex::Find(std::string_view a_positionID) const
	{
		const auto found = std::ranges::find(_entries, a_positionID, &Entry::positionID);
		return found == _entries.end() ? nullptr : &*found;
	}

	bool TreeIndex::NeedsFurniture(const Entry& a_entry)
	{
		if (a_entry.tags.contains("nofurn")) {
			return false;
		}
		// Named rather than inferred: a tag that is not one of these is not a claim
		// about the room. Measured here, 13 of the 31 eligible female+male trees
		// with a real ending want one of them.
		static constexpr std::array kFurniture{ "desk", "couch", "doublebed", "singlebed",
			"bed", "chair", "counter", "bench", "table", "wall" };
		return std::ranges::any_of(
			kFurniture, [&](const auto* tag) { return a_entry.tags.contains(tag); });
	}

	const TreeIndex::Entry* TreeIndex::Choose(
		std::string_view a_include,
		std::string_view a_exclude,
		std::string_view a_composition,
		bool             a_requireEnding,
		bool             a_noFurnitureOnly,
		float            a_budgetSeconds) const
	{
		const auto include = SplitList(a_include);
		const auto exclude = SplitList(a_exclude);
		const auto composition = Lower(a_composition);

		// Every candidate worth having, not just the single highest. Deterministic
		// scoring picked the same tree for every scenario on this install -- which
		// would end every scene ever played with the same animation, the exact
		// opposite of what a scenario is for.
		std::vector<std::pair<const Entry*, float>> candidates;

		for (const auto& entry : _entries) {
			if (a_requireEnding && entry.ending < Ending::kOrgasm) {
				continue;
			}

			// Who it is FOR. Without this the catalogue happily offers a gay
			// position to a mixed pair: "Gay Romantic Missionary" scored level with
			// the straight one because both are six-stage climax trees tagged
			// missionary and loving.
			if (!composition.empty() && !entry.tags.contains(composition)) {
				continue;
			}
			if (a_noFurnitureOnly && NeedsFurniture(entry)) {
				continue;
			}
			// Excluded on either end. A tag the scenario does not want is not
			// acceptable merely because the tree saves it for the last stage.
			if (std::ranges::any_of(exclude, [&](const auto& tag) {
				    return entry.tags.contains(tag) || entry.endingTags.contains(tag);
			    })) {
				continue;
			}

			// Match the scenario's wanted tags against where the tree ENDS, not
			// where it starts -- a scenario names the act it wants to finish on,
			// and 54 of the 66 trees here end on a different act from the one they
			// open with. "Pit Doggy" enters doggy and from-behind and ends
			// blowjob, climax and from-front: asking for doggy was getting a
			// facial, and asking for a facial was matching nothing.
			//
			// Falling back to the entry tags for a tree with no exit position is
			// not a compromise -- it is the only description that tree has.
			const auto& matchAgainst = entry.endingTags.empty() ? entry.tags : entry.endingTags;
			const auto  matched = static_cast<float>(std::ranges::count_if(
                include, [&](const auto& tag) { return matchAgainst.contains(tag); }));
			if (!include.empty() && matched == 0.0f) {
				continue;
			}

			// A climax outranks an orgasm, more matching tags outrank fewer, and a
			// tree known to fit the time outranks one of unknown length -- which in
			// turn outranks one that will be cut off before it finishes.
			float score = matched * 10.0f;
			score += static_cast<float>(entry.ending) * 5.0f;

			// Strongly prefer a tree that needs no furniture.
			//
			// This position is where the scene STARTS now, not somewhere it moves
			// to later, so a tree that wants a couch is asking for a couch to exist
			// wherever these two happen to be standing. Thirteen of the 31 eligible
			// female+male trees here need one -- couch, double bed, single bed,
			// bench, desk -- and naming one that is not there risks the scene not
			// starting at all, which is worse than a plainer ending.
			//
			// A preference and not a filter: AAF finds the furniture often enough
			// indoors, and excluding half the catalogue to avoid a maybe would cost
			// more variety than it buys.
			if (entry.tags.contains("nofurn")) {
				score += 15.0f;
			}
			// A tree whose content is actually MARKED as reaching a climax, over one
			// that only names a branch for it. Owner's call: prefer, do not exclude.
			//
			// 25 against a 10-point variety band means that at equal tag match a
			// tagged tree erases an untagged one outright -- said plainly because
			// that is close to exclusion in practice. The 27 untagged ones stay
			// reachable where composition, furniture or budget rules the tagged out,
			// which is what keeps two women and thin pack sets playable.
			if (entry.climaxTagged) {
				score += 25.0f;
			}

			if (entry.LengthKnown()) {
				score += entry.seconds <= a_budgetSeconds ? 8.0f : -12.0f;
			}

			candidates.emplace_back(&entry, score);
		}

		if (candidates.empty()) {
			return nullptr;
		}

		const auto bestScore = std::ranges::max_element(
			candidates, {}, &std::pair<const Entry*, float>::second)->second;

		// Anything within a tag's worth of the best is as good as the best. A wider
		// band would start trading the ending away for variety; a narrower one puts
		// us back to one tree.
		std::erase_if(candidates, [&](const auto& item) { return item.second < bestScore - 10.0f; });

		// thread_local, not static. Every path here goes through Scenarios' lock,
		// but NamedLock gives up after a second and lets the caller proceed
		// UNLOCKED -- and Papyrus reaches this on two distinct stacks -- so a
		// shared mt19937 is a genuine data race in a game process.
		thread_local std::mt19937 rng{ std::random_device{}() };
		std::uniform_int_distribution<std::size_t> pick{ 0, candidates.size() - 1 };
		const auto* chosen = candidates[pick(rng)].first;

		logger::info(
			"trees: {} of {} indexed position(s) qualified within reach of the best score; chose "
			"\"{}\"",
			candidates.size(), _entries.size(), chosen->positionID);
		return chosen;
	}
}
