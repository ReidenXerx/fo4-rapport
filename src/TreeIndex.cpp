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
		};
		std::unordered_map<std::string, Tree> trees;

		struct Pending
		{
			std::string                     positionID;
			std::string                     treeID;
			std::unordered_set<std::string> tags;
		};
		std::vector<Pending> pending;

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

				Tree tree;
				for (auto b = body.find("<branch"); b != std::string::npos;
				     b = body.find("<branch", b + 1)) {
					const auto branch = Element(body, b);
					++tree.stages;

					const auto branchID = Lower(Attr(branch, "id"));
					if (branchID.find("climax") != std::string::npos) {
						tree.ending = std::max(tree.ending, Ending::kClimax);
					} else if (branchID.find("orgasm") != std::string::npos) {
						tree.ending = std::max(tree.ending, Ending::kOrgasm);
					} else if (branchID.find("finish") != std::string::npos) {
						tree.ending = std::max(tree.ending, Ending::kFinishOnly);
					}

					if (const auto seconds = Attr(branch, "time"); !seconds.empty()) {
						try {
							tree.seconds += std::stof(seconds);
						} catch (const std::exception&) {
							// A branch with an unreadable time contributes nothing
							// rather than poisoning the whole tree's length.
						}
					}
				}
				trees.insert_or_assign(std::move(id), tree);
			}

			// ---- positions that enter one -----------------------------------
			for (auto at = text.find("<position"); at != std::string::npos;
			     at = text.find("<position", at + 1)) {
				const auto element = Element(text, at);

				auto treeID = Attr(element, "positionTree");
				if (treeID.empty()) {
					continue;
				}

				// Hidden, or stubbed out with a null animation, means AAF will not
				// select it -- which is exactly how UAP retires the standalone
				// climax positions. Either way it is not an entry point.
				const auto hiddenAttr = Attr(element, "isHidden");
				const bool hidden = hiddenAttr.empty() ? hiddenByDefault
				                                       : Lower(hiddenAttr) == "true";
				if (hidden || Lower(Attr(element, "animation")) == "null") {
					continue;
				}

				auto id = Attr(element, "id");
				if (id.empty()) {
					continue;
				}

				pending.push_back(Pending{ std::move(id), std::move(treeID),
					SplitTags(Attr(element, "tags")) });
			}
		}

		// ---- join -----------------------------------------------------------
		std::uint32_t orphaned = 0;
		for (auto& item : pending) {
			const auto found = trees.find(item.treeID);
			if (found == trees.end()) {
				++orphaned;
				continue;
			}

			Entry made;
			made.positionID = std::move(item.positionID);
			made.treeID = std::move(item.treeID);
			made.tags = std::move(item.tags);
			made.ending = found->second.ending;
			made.seconds = found->second.seconds;
			made.stages = found->second.stages;

			if (made.ending >= Ending::kOrgasm) {
				++_withEnding;
			}
			_entries.push_back(std::move(made));
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

	const TreeIndex::Entry* TreeIndex::Choose(
		std::string_view a_include,
		std::string_view a_exclude,
		std::string_view a_composition,
		bool             a_requireEnding,
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
			if (std::ranges::any_of(exclude, [&](const auto& tag) { return entry.tags.contains(tag); })) {
				continue;
			}

			// ANY of the wanted tags, not all: a stage's list has always been
			// alternatives, and AAF's own includeTags being an AND is what made
			// the first version of this ask for something that is all of them.
			const auto matched = static_cast<float>(std::ranges::count_if(
				include, [&](const auto& tag) { return entry.tags.contains(tag); }));
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

		static std::mt19937 rng{ std::random_device{}() };
		std::uniform_int_distribution<std::size_t> pick{ 0, candidates.size() - 1 };
		const auto* chosen = candidates[pick(rng)].first;

		logger::info(
			"trees: {} of {} indexed position(s) qualified within reach of the best score; chose "
			"\"{}\"",
			candidates.size(), _entries.size(), chosen->positionID);
		return chosen;
	}
}
