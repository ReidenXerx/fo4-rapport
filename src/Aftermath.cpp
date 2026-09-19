#include "Aftermath.h"

#include "Ledger.h"
#include "PapyrusLink.h"

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

	// AAF hands tags over as an array printed into one string. Splitting on
	// anything that is not part of a tag means the same code reads
	// "A,B,C", "[A, B, C]" and "\"A\",\"B\"" without caring which it got.
	[[nodiscard]] std::vector<std::string> SplitTags(std::string_view a_text)
	{
		std::vector<std::string> tags;
		std::string              current;
		for (const char c : a_text) {
			if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
				current.push_back(c);
			} else if (!current.empty()) {
				tags.push_back(Lower(current));
				current.clear();
			}
		}
		if (!current.empty()) {
			tags.push_back(Lower(current));
		}
		return tags;
	}
}

namespace RP
{
	Aftermath& Aftermath::GetSingleton() noexcept
	{
		static Aftermath singleton;
		return singleton;
	}

	std::string_view Aftermath::Name(Backend a_backend) noexcept
	{
		switch (a_backend) {
		case Backend::kMoisturizer:
			return "Commonwealth Moisturizer (worn meshes)"sv;
		default:
			return "none"sv;
		}
	}

	std::filesystem::path Aftermath::ConfigPath()
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "aftermath.json";
	}

	void Aftermath::Load()
	{
		NamedLock lock{ _lock, "aftermath" };
		_rules.clear();
		_regions.clear();

		const auto  path = ConfigPath();
		std::ifstream file{ path };
		if (!file) {
			_enabled = false;
			logger::warn("aftermath: no {} - scenes will leave nothing behind", path.string());
			return;
		}

		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			_enabled = false;
			logger::error("aftermath: {} is not valid json ({}) - the feature is off", path.string(), e.what());
			return;
		}

		_enabled = document.value("enabled", true);
		_hours = document.value("hours", 12.0f);
		_layers = (std::max)(1, document.value("layers", 3));
		_requireClimax = document.value("requireClimax", false);

		if (const auto regions = document.find("regions"); regions != document.end() && regions->is_object()) {
			for (const auto& [setID, letters] : regions->items()) {
				if (letters.is_string()) {
					_regions.emplace_back(setID, letters.get<std::string>());
				}
			}
		}

		if (const auto tags = document.find("tags"); tags != document.end() && tags->is_object()) {
			for (const auto& [tag, sets] : tags->items()) {
				if (!sets.is_array()) {
					continue;
				}
				std::vector<std::string> ids;
				for (const auto& set : sets) {
					if (set.is_string()) {
						ids.push_back(set.get<std::string>());
					}
				}
				if (!ids.empty()) {
					_rules.emplace_back(Lower(tag), std::move(ids));
				}
			}
		}

		if (!_enabled) {
			logger::info("aftermath: switched off in aftermath.json");
			return;
		}
		if (_rules.empty()) {
			_enabled = false;
			logger::warn("aftermath: no tag rules were read - nothing could ever be applied, so it is off");
			return;
		}

		ChooseBackend(document.value("backend", std::string{ "auto" }));

		logger::info(
			"aftermath: {} tag rule(s), {:.0f} game hours, {} layer(s){}",
			_rules.size(), _hours, _layers, _requireClimax ? ", climax required" : "");
	}

	void Aftermath::ChooseBackend(const std::string& a_wanted)
	{
		const auto handler = RE::TESDataHandler::GetSingleton();
		const auto installed = [&](std::string_view a_plugin) {
			return handler && handler->LookupModByName(a_plugin) != nullptr;
		};

		const auto hasMoisturizer = installed("ComMoisturizer.esp"sv);
		const auto hasOverlays = installed("CumOverlays.esp"sv);

		// A config asking for the backend that no longer exists is answered rather
		// than ignored. Silently doing something else is how a player spends an
		// evening wondering why their setting does nothing.
		if (a_wanted == "overlay") {
			logger::warn(
				"aftermath: \"backend\": \"overlay\" is no longer a thing - CumOverlays support "
				"was removed. Commonwealth Moisturizer is the only backend; falling through to it");
		}

		_backend = hasMoisturizer ? Backend::kMoisturizer : Backend::kNone;

		if (_backend == Backend::kNone) {
			_enabled = false;
			logger::warn(
				"aftermath: Commonwealth Moisturizer (ComMoisturizer.esp) is NOT INSTALLED, so "
				"there is no aftermath backend and scenes will leave nothing behind.{} "
				"Everything else -- scenes, faces, sweat, autonomy -- is unaffected.",
				hasOverlays
					? " CumOverlays is installed but is no longer driven: it paints a flat texture"
					  " and cannot do a face, and the mesh path is the only one that was ever"
					  " actually exercised."
					: "");
			return;
		}

		logger::info("aftermath: using {}", Name(_backend));
		if (_backend == Backend::kMoisturizer) {
			CheckMoisturizerMorphs();
		}
		if (hasOverlays) {
			logger::info(
				"aftermath: CumOverlays is installed and is NOT driven any more, but it is still "
				"SILENCED - a mod left listening to AAF goes on painting the same bodies on its "
				"own schedule, and its flat textures would land on top of the meshes");
		}
	}

	void Aftermath::CheckMoisturizerMorphs()
	{
		// A worn mesh is built to ONE body shape. LooksMenu can then morph it per
		// actor at runtime -- but only from a .tri of morph data sitting beside the
		// .nif, and that file is produced by a BodySlide build with "Build Morphs"
		// ticked. Moisturizer ships the .nif alone for every body except
		// AtomicMuscle, so a fresh install has a mesh that CANNOT morph and will
		// fit the base body rather than the player's.
		//
		// Nothing in the game says this. The mesh simply looks wrong, and the
		// obvious conclusion -- that the mod is broken -- is the wrong one.
		const std::filesystem::path meshes{ "Data/Meshes/kziitd/semen" };
		std::error_code ec;
		if (!std::filesystem::exists(meshes, ec)) {
			logger::warn(
				"aftermath: Commonwealth Moisturizer is installed but {} does not exist - its FOMOD's "
				"body option was not installed, so there is no mesh to put on anybody",
				meshes.string());
			return;
		}

		for (const auto& name : { "kzSemen_Female"sv, "kzSemen_Male"sv }) {
			const auto nif = meshes / (std::string{ name } + ".nif");
			if (!std::filesystem::exists(nif, ec)) {
				continue;
			}
			const auto tri = meshes / (std::string{ name } + ".tri");
			if (std::filesystem::exists(tri, ec)) {
				logger::info("aftermath: {}.nif has morph data - it will follow each actor's body", name);
			} else {
				logger::warn(
					"aftermath: {}.nif has NO morph data beside it ({} is missing). It is built to one "
					"body shape and cannot follow anybody's: on a body that is not the one it was "
					"built to, it will not line up. Build the semen outfit in BodySlide against your "
					"own preset with \"Build Morphs\" ticked - that build is what produces the .tri.",
					name, tri.filename().string());
			}
		}
	}

	[[nodiscard]] std::string Aftermath::RegionsFor(const std::vector<std::string>& a_sets) const
	{
		std::string regions;
		for (const auto& set : a_sets) {
			for (const auto& [id, letters] : _regions) {
				if (id != set) {
					continue;
				}
				for (const char letter : letters) {
					if (regions.find(letter) == std::string::npos) {
						regions.push_back(letter);
					}
				}
			}
		}
		return regions;
	}

	void Aftermath::NoteSex(std::uint32_t a_formID, std::int32_t a_sex)
	{
		NamedLock lock{ _lock, "aftermath" };
		_sex[a_formID] = a_sex;
	}

	std::int32_t Aftermath::SexOf(std::uint32_t a_formID) const
	{
		NamedLock lock{ _lock, "aftermath" };
		const auto found = _sex.find(a_formID);
		return found == _sex.end() ? -1 : found->second;
	}

	std::string Aftermath::CompositionOf(std::uint32_t a_first, std::uint32_t a_second) const
	{
		const auto first = SexOf(a_first);
		const auto second = SexOf(a_second);
		if (first < 0 || second < 0) {
			return {};   // unknown is not a constraint, it is an absence of one
		}

		const auto females = (first == 1 ? 1 : 0) + (second == 1 ? 1 : 0);
		switch (females) {
		case 2:
			return "f_f";
		case 1:
			return "f_m";
		default:
			return "m_m";
		}
	}

	void Aftermath::NoteSlots(std::uint32_t a_slot0, std::uint32_t a_slot1)
	{
		NamedLock lock{ _lock, "aftermath" };
		_slot0 = a_slot0;
		_slot1 = a_slot1;
	}

	// Which of the two it landed on.
	//
	// AAF's own `role` attribute is dead -- zero occurrences across every installed
	// pack -- so this is worked out from what the packs DO populate. Every act tag
	// is "<giver part>To<receiver part>", and per-actor `gender` is used
	// everywhere: 760 F, 1260 M.
	std::vector<std::uint32_t> Aftermath::ReceiversOf(
		std::string_view a_tags, std::uint32_t a_first, std::uint32_t a_second) const
	{
		const auto lowered = Lower(a_tags);
		const auto has = [&](std::string_view a_part) {
			return lowered.find(a_part) != std::string::npos;
		};

		const auto onAFemalePart = has("tovagina"sv) || has("tonipples"sv) ||
		                           has("cunnilingus"sv) || has("vaginato"sv);
		const auto onEitherPart = has("tomouth"sv) || has("toanus"sv) ||
		                          has("blowjob"sv) || has("analingus"sv) || has("anusto"sv);
		if (!onAFemalePart && !onEitherPart) {
			return {};
		}

		const auto sexOf = [&](std::uint32_t a_formID) {
			const auto found = _sex.find(a_formID);
			return found == _sex.end() ? -1 : found->second;
		};
		const auto firstSex = sexOf(a_first);
		const auto secondSex = sexOf(a_second);

		// A mixed pair answers itself, and that is 559 of the 562 two-actor
		// animations that name both genders.
		if (firstSex == 1 && secondSex == 0) {
			return { a_first };
		}
		if (firstSex == 0 && secondSex == 1) {
			return { a_second };
		}

		// Nothing vaginal happens between two actors of the same sex, so a
		// female-only part with no female pairing is a tag that names nobody here.
		if (onAFemalePart && !onEitherPart) {
			return {};
		}

		// Same sex, or a sex nobody told us. Nothing available separates them:
		// AAF's `role` is dead, sex says nothing, and the slot order that would
		// settle it is locked inside a packed Var that Papyrus cannot open.
		//
		// So both, which is the owner's call and the better arithmetic -- neither
		// leaves two people wrong, both leaves one. The regions are still only the
		// ones this scene's tags actually named, so nobody gets a hole that was
		// never touched.
		return { a_first, a_second };
	}

	void Aftermath::NoteTags(std::string_view a_tags)
	{
		NamedLock lock{ _lock, "aftermath" };
		if (!_sceneTags.empty()) {
			_sceneTags.push_back(',');
		}
		_sceneTags.append(a_tags);

		// Only an animation that names an act replaces the last one. A kiss or a
		// transition playing after the sex must not erase where the scene actually
		// got to.
		if (!SetsFor(a_tags).empty()) {
			_lastActTags.assign(a_tags);
		}
	}

	std::vector<std::string> Aftermath::SetsFor(std::string_view a_tags) const
	{
		const auto tags = SplitTags(a_tags);
		if (tags.empty()) {
			return {};
		}

		if (_requireClimax) {
			const auto climaxed = std::ranges::any_of(tags, [](const std::string& tag) {
				return tag.rfind("climax", 0) == 0;
			});
			if (!climaxed) {
				return {};
			}
		}

		std::vector<std::string> sets;
		for (const auto& [tag, ids] : _rules) {
			if (std::ranges::find(tags, tag) == tags.end()) {
				continue;
			}
			for (const auto& id : ids) {
				if (std::ranges::find(sets, id) == sets.end()) {
					sets.push_back(id);
				}
			}
		}
		return sets;
	}

	void Aftermath::OnSceneEnded(std::uint32_t a_first, std::uint32_t a_second)
	{
		NamedLock lock{ _lock, "aftermath" };

		const auto tags = std::exchange(_sceneTags, {});
		const auto act = std::exchange(_lastActTags, {});
		if (!_enabled) {
			return;
		}

		if (tags.empty()) {
			// Not a silent nothing: a scene we heard nothing about is a different
			// state from a scene that was only kissing, and only one of them is a bug.
			logger::warn(
				"aftermath: the scene ended without a single animation tag reaching us - "
				"nothing applied, and that is a gap in what we were told, not a decision");
			return;
		}

		// WHO, before what. Applying to both is what put cum on the neck of a
		// Diamond City guard who was on the giving end of it.
		const auto receivers = ReceiversOf(act, a_first, a_second);
		_slot0 = 0;
		_slot1 = 0;
		if (receivers.empty()) {
			logger::info(
				"aftermath: the tags name no act that leaves anything on anybody (tags: {})", tags);
			return;
		}

		// The subject on its own; the explanation goes AFTER the sentence, not
		// inside its subject, which is how the first version of this line came out
		// as "00002F0B; 000F61B6 was on the other end and keeps nothing keep(s)".
		std::string who;
		for (const auto formID : receivers) {
			if (!who.empty()) {
				who += " and ";
			}
			who += std::format("{:08X}", formID);
		}
		const std::string because =
			receivers.size() == 1
				? std::format(
					  " - {:08X} was on the other end and keeps nothing",
					  receivers.front() == a_first ? a_second : a_first)
				: std::string{ " - a same-sex pair, and nothing available says which of them "
				               "received, so both do" };

		// The LAST act, not every act. A scene that went from vaginal to a blowjob
		// finishes on the blowjob.
		const auto sets = SetsFor(act);
		if (sets.empty()) {
			logger::info("aftermath: nothing to leave behind (tags: {})", tags);
			return;
		}

		const auto now = Ledger::GameHours();
		if (now < 0.0f) {
			logger::warn("aftermath: no game clock yet - nothing applied");
			return;
		}
		const auto expires = now + _hours;

		// One mark per actor, not one per set: Moisturizer puts everything on in a
		// single call and takes it all off in a single call, so a mark per set
		// would queue one removal too many and the extra would strip what a later
		// scene had just applied.
		const auto regions = RegionsFor(sets);
		if (regions.empty()) {
			logger::info(
				"aftermath: the sets for this scene name no place Moisturizer knows (tags: {})",
				tags);
			return;
		}
		for (const auto formID : receivers) {
			Apply(formID, "CMkz:" + regions, expires);
		}
		logger::info(
			"aftermath: {} keep(s) Moisturizer [{}] until hour {:.1f} (now {:.1f}){}",
			who, regions, expires, now, because);

		// ASK NOW, not on the next scheduler tick.
		//
		// Tick is driven by the scheduler, which runs every 20 seconds, so
		// aftermath arrived anywhere from 5 to 18 seconds after the scene it
		// belongs to -- measured across a live session. That is long enough to
		// read as unrelated to what just happened, which is the whole point of it.
		//
		// The wait bought nothing. Tick's only extra condition is that the owner
		// is loaded, and the two people who have just finished a scene in front of
		// the player are the most certainly-present actors in the game. Anything
		// restored from a save still goes through Tick, where that check does earn
		// its keep.
		for (const auto formID : receivers) {
			for (auto& mark : _marks) {
				if (mark.formID == formID && !mark.asked) {
					PapyrusLink::GetSingleton().QueueOrder(
						Order{ Order::Kind::kApplyMoisturizer, mark.formID, mark.setID });
					mark.asked = true;
				}
			}
		}
	}

	void Aftermath::Apply(std::uint32_t a_formID, const std::string& a_setID, float a_expiresAt)
	{
		// Already wearing this set: push the hour out rather than stacking a
		// second copy. Two marks for one set would queue two removals, and the
		// second would strip an overlay a later scene had just re-applied.
		for (auto& mark : _marks) {
			if (mark.formID != a_formID) {
				continue;
			}

			// On the mesh backend an actor has exactly one mark, because the mod
			// has exactly one state per actor. A second scene REPLACES the regions
			// rather than adding a mark, and the union is what gets applied.
			if (_backend == Backend::kMoisturizer && mark.setID.starts_with("CMkz:")) {
				for (const char letter : a_setID) {
					if (letter != ':' && mark.setID.find(letter) == std::string::npos) {
						mark.setID.push_back(letter);
					}
				}
				mark.expiresAt = (std::max)(mark.expiresAt, a_expiresAt);
				mark.asked = false;
				return;
			}

			if (mark.setID == a_setID) {
				mark.expiresAt = (std::max)(mark.expiresAt, a_expiresAt);
				mark.asked = false;   // ask again: a fresh scene should look fresh
				return;
			}
		}

		_marks.push_back(Mark{ a_formID, a_expiresAt, a_setID, false });
	}

	void Aftermath::Defer(std::uint32_t a_formID)
	{
		NamedLock lock{ _lock, "aftermath" };
		for (auto& mark : _marks) {
			if (mark.formID == a_formID) {
				mark.asked = false;
			}
		}
	}

	void Aftermath::Tick(const std::vector<std::uint32_t>& a_here)
	{
		const auto now = Ledger::GameHours();
		if (now < 0.0f) {
			return;
		}

		NamedLock lock{ _lock, "aftermath" };

		std::uint32_t expired = 0;
		std::uint32_t asked = 0;
		std::uint32_t waiting = 0;

		for (auto mark = _marks.begin(); mark != _marks.end();) {
			if (now >= mark->expiresAt) {
				// Removal goes out whether or not they are here. An overlay left on
				// someone who wandered off is the failure this whole feature exists
				// to prevent, and AAF takes the call either way.
				PapyrusLink::GetSingleton().QueueOrder(
					Order{ mark->setID.starts_with("CMkz:") ? Order::Kind::kClearMoisturizer
					                                        : Order::Kind::kRemoveOverlay,
					       mark->formID, mark->setID });
				mark = _marks.erase(mark);
				++expired;
				continue;
			}

			// Anything not yet asked for in this session is asked for as soon as
			// its owner is in front of us. That covers the first application and
			// the re-application after a load with one line, because they are the
			// same thing: the plugin has no memory of having asked.
			if (!mark->asked) {
				if (std::ranges::find(a_here, mark->formID) != a_here.end()) {
					PapyrusLink::GetSingleton().QueueOrder(
						Order{ mark->setID.starts_with("CMkz:") ? Order::Kind::kApplyMoisturizer
						                                        : Order::Kind::kApplyOverlay,
						       mark->formID, mark->setID });
					mark->asked = true;
					++asked;
				} else {
					++waiting;
				}
			}
			++mark;
		}

		if (expired > 0 || asked > 0) {
			logger::info(
				"aftermath: hour {:.1f} - {} applied, {} expired, {} still standing, {} waiting for "
				"their owner to be nearby",
				now, asked, expired, _marks.size(), waiting);
		}
	}

	std::vector<Aftermath::Mark> Aftermath::Marks() const
	{
		NamedLock lock{ _lock, "aftermath" };
		return _marks;
	}

	void Aftermath::Restore(std::vector<Mark> a_marks)
	{
		NamedLock lock{ _lock, "aftermath" };
		_marks = std::move(a_marks);

		// Whether a restored mark needs asking for again depends entirely on the
		// backend, and getting this wrong ADDS cum on every reload.
		//
		// An OVERLAY is LooksMenu state applied at runtime; it may or may not have
		// survived, and asking twice is free because AAF will not apply the same
		// overlay to an actor twice. So: ask again.
		//
		// A MOISTURIZER mark is an equipped armour piece with object mods, and
		// ActorValues recording which slots are used. All of that is ordinary game
		// state that came back with the save. Asking again does not refresh it --
		// its picker SKIPS the slots already set and fills new ones, so a reload
		// would stack another full set of layers, and another, until the region
		// filled and wiped. Already on, already correct: leave it.
		for (auto& mark : _marks) {
			mark.asked = mark.setID.starts_with("CMkz:");
		}
	}

	void Aftermath::Clear()
	{
		NamedLock lock{ _lock, "aftermath" };
		_marks.clear();
		_sceneTags.clear();
	}

	void Aftermath::RemoveEverything(std::string_view a_why)
	{
		NamedLock lock{ _lock, "aftermath" };
		if (_marks.empty()) {
			return;
		}

		auto& link = PapyrusLink::GetSingleton();
		for (const auto& mark : _marks) {
			link.QueueOrder(
				Order{ mark.setID.starts_with("CMkz:") ? Order::Kind::kClearMoisturizer
				                                       : Order::Kind::kRemoveOverlay,
				       mark.formID, mark.setID });
		}
		logger::warn("aftermath: removing all {} standing overlay(s) - {}", _marks.size(), a_why);
		_marks.clear();
	}

	std::size_t Aftermath::Size() const
	{
		NamedLock lock{ _lock, "aftermath" };
		return _marks.size();
	}
}
