#include "Expressions.h"

#include "PapyrusLink.h"
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

	// Split on ANY non-alphanumeric, which is what makes this work on what
	// actually arrives. Papyrus hands us `akArgs[3] as String` -- the coercion of
	// a Var holding a string array -- so the text carries brackets, quotes and
	// commas. Treating all of them as separators is why the same parser reads it
	// correctly in Aftermath, where it has been doing so in game.
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

	// Any tag naming an act. A scene that only ever shows these is kissing, and
	// kissing does not want the face that sex wants.
	[[nodiscard]] bool LooksLikeSex(std::string_view a_tags)
	{
		static constexpr std::array kSex{
			"penisto"sv, "mouthto"sv, "handto"sv, "vaginal"sv, "anal"sv, "blowjob"sv,
			"cunnilingus"sv, "masturbation"sv, "straponto"sv, "dildoto"sv, "climax"sv,
			"analingus"sv, "anusto"sv, "vaginato"sv, "footto"sv
		};
		const auto lowered = Lower(a_tags);
		return std::ranges::any_of(kSex, [&](std::string_view needle) {
			return lowered.find(needle) != std::string::npos;
		});
	}
}

namespace RP
{
	Expressions& Expressions::GetSingleton() noexcept
	{
		static Expressions singleton;
		return singleton;
	}

	std::filesystem::path Expressions::ConfigPath()
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "expressions.json";
	}

	void Expressions::Load()
	{
		NamedLock lock{ _lock, "expressions" };
		_steps.clear();

		const auto    path = ConfigPath();
		std::ifstream file{ path };
		if (!file) {
			_enabled = false;
			logger::warn("expressions: no {} - faces are left to the animation packs", path.string());
			return;
		}

		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			_enabled = false;
			logger::error("expressions: {} is not valid json ({}) - the feature is off", path.string(), e.what());
			return;
		}

		_enabled = document.value("enabled", true);
		_dazedSeconds = document.value("dazedSeconds", 20.0f);
		_clearSet = document.value("clearSet", std::string{ "Rapport_Clear" });
		_afterSet = document.value("afterSet", std::string{ "Rapport_Dazed" });
		_kissSet = document.value("kissSet", std::string{ "Rapport_Kiss" });

		if (const auto steps = document.find("steps"); steps != document.end() && steps->is_array()) {
			for (const auto& step : *steps) {
				Step entry;
				entry.at = step.value("at", -1.0f);
				entry.set = step.value("set", std::string{});
				if (entry.at < 0.0f || entry.set.empty()) {
					logger::warn("expressions: a step is missing \"at\" or \"set\" - skipped");
					continue;
				}
				_steps.push_back(std::move(entry));
			}
		}

		// Out of order in the file would mean steps silently never firing, because
		// the pump only ever looks at the next one.
		std::ranges::sort(_steps, [](const Step& a, const Step& b) { return a.at < b.at; });

		if (!_enabled) {
			logger::info("expressions: switched off in expressions.json");
			return;
		}
		if (_steps.empty()) {
			_enabled = false;
			logger::warn("expressions: no steps were read - nothing could ever be applied, so it is off");
			return;
		}

		std::string schedule;
		for (const auto& step : _steps) {
			if (!schedule.empty()) {
				schedule += ", ";
			}
			schedule += std::format("{:.0f}% {}", step.at * 100.0f, step.set);
		}
		logger::info("expressions: {}; then {} for {:.0f}s, then {}",
			schedule, _afterSet, _dazedSeconds, _clearSet);
	}

	void Expressions::OnSceneStarted(std::uint32_t a_first, std::uint32_t a_second, float a_durationSeconds)
	{
		NamedLock lock{ _lock, "expressions" };
		if (!_enabled) {
			return;
		}

		_first = a_first;
		_second = a_second;
		_duration = (std::max)(a_durationSeconds, 1.0f);
		_startedAt = std::chrono::steady_clock::now();
		_running = true;
		_stoodDown = false;
		_dazing = false;
		_nextStep = 0;
		_tags.clear();
		_sawSexTag = false;
		// This scene's own high-water mark. One started in the last one's afterglow
		// was measured against the old level, and a scenario's heat (CollectHeat
		// gates on it) never rose above it -- no sweat at all.
		_heatApplied.clear();
		_heatLevel = 0;
		// One actor, one AAF scene: whatever scene of somebody else's held these two
		// is over (ForeignScenes::OwnSceneStarted), and their faces are ours now.
		// Left listed, our own afterglow would spare them for good.
		std::erase(_foreignHeld, a_first);
		std::erase(_foreignHeld, a_second);

		logger::info("expressions: driving {:08X} and {:08X} over {:.0f}s", a_first, a_second, _duration);
	}

	void Expressions::NotePosition(std::string_view a_position)
	{
		NamedLock lock{ _lock, "expressions" };
		_livePosition.assign(a_position);
	}

	void Expressions::NoteTags(std::string_view a_tags)
	{
		NamedLock lock{ _lock, "expressions" };
		if (!_tags.empty()) {
			_tags.push_back(',');
		}
		_tags.append(a_tags);
		if (!_sawSexTag && LooksLikeSex(a_tags)) {
			_sawSexTag = true;
		}

		// Only an animation that names an act moves this. AAF sends a tag list for
		// transitions and idles too, and letting one of those overwrite the act
		// would blank the face in the middle of the scene.
		if (!FaceForAct(a_tags, _livePosition, 2).empty()) {
			_liveAct.assign(a_tags);
		}
	}

	std::string Expressions::LiveAct() const
	{
		NamedLock lock{ _lock, "expressions" };
		return _liveAct;
	}

	std::string Expressions::VariantFor(std::string_view a_setID, std::uint32_t a_formID)
	{
		if (a_setID.empty() || a_setID == "Rapport_Clear"sv) {
			return std::string{ a_setID };
		}
		// A set an author wrote by hand in act-overrides.json, or any name that
		// already carries a style, is used as given rather than having a second
		// number stapled on.
		// NO "already styled" GUARD. There used to be one -- if the id ended in
		// _1.._9 it was returned unchanged, on the theory that it already carried
		// a style. It cannot tell a style suffix from a BASE NAME THAT ENDS IN A
		// DIGIT, and three of the eight base names do:
		//
		//     Rapport_Pleasure_1  ->  returned as-is  ->  no such mfgSet
		//     Rapport_Pleasure_2  ->  returned as-is  ->  no such mfgSet
		//     Rapport_Pleasure_3  ->  returned as-is  ->  no such mfgSet
		//
		// The generator emits Rapport_Pleasure_3_1/_2/_3 and nothing called plain
		// Rapport_Pleasure_3, so AAF was asked for a set that does not exist and
		// the three faces covering the whole MIDDLE of every scene silently never
		// applied. Only Climax, Oral, Kiss, Anticipation and Dazed -- the five
		// whose names do not end in a digit -- ever worked. Caught by reading
		// "face: asked AAF for Rapport_Pleasure_3" in a live log and noticing it
		// had no style suffix where Rapport_Climax_2 on the line above did.
		//
		// The guard was never needed: every caller passes a BASE id from
		// FaceForAct, and the one id that must not be styled is Rapport_Clear,
		// which is handled above by name.
		return std::format("{}_{}", a_setID, (a_formID % kStyles) + 1);
	}

	std::string Expressions::FaceFor(std::string_view a_sceneFace, std::string_view a_actTags,
		std::string_view a_position, std::uint32_t a_actor, const std::vector<std::uint32_t>& a_members,
		int a_otherLevel)
	{
		if (a_sceneFace != "Rapport_Oral"sv || a_members.size() != 2) {
			return std::string{ a_sceneFace };
		}
		// Sex, read now: 1 female, 0 male, -1 unknown.
		const auto sexOf = [](std::uint32_t a_id) {
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_id);
			auto* npc = actor ? actor->GetNPC() : nullptr;   // not const: GetSex() is not
			return npc ? static_cast<int>(RP::Compat::Female(npc)) : -1;
		};
		const int s0 = sexOf(a_members[0]);
		const int s1 = sexOf(a_members[1]);
		if (s0 < 0 || s1 < 0 || s0 == s1) {
			return std::string{ a_sceneFace };
		}
		const auto female = s0 == 1 ? a_members[0] : a_members[1];
		const auto male = s0 == 1 ? a_members[1] : a_members[0];

		// "<giver part>To<receiver part>": the mouth receives in PenisToMouth, gives in
		// MouthToVagina. Named acts say it too. Both kinds at once (69) settles nothing.
		std::string text{ a_actTags };
		text += ',';
		text += a_position;
		std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const auto has = [&](std::initializer_list<std::string_view> a_needles) {
			return std::ranges::any_of(a_needles, [&](std::string_view n) { return text.find(n) != std::string::npos; });
		};
		const bool herMouth = has({ "penistomouth"sv, "blowjob"sv, "fellatio"sv, "irrumatio"sv, "deepthroat"sv,
			"mouthtopenis"sv, "tonguetopenis"sv });
		const bool hisMouth = has({ "cunnilingus"sv, "mouthtovagina"sv, "tonguetovagina"sv, "vaginatomouth"sv });
		if (herMouth == hisMouth) {
			return std::string{ a_sceneFace };
		}
		const auto mouth = herMouth ? female : male;
		if (a_actor == mouth) {
			return std::string{ a_sceneFace };
		}
		return std::format("Rapport_Pleasure_{}", std::clamp(a_otherLevel, 1, 3));
	}

	std::string_view Expressions::FaceForAct(
		std::string_view a_actTags, std::string_view a_position, int a_intensity)
	{
		const auto pleasureFor = [](int a_level) -> std::string_view {
			switch (a_level) {
			case 1:
				return "Rapport_Pleasure_1"sv;
			case 2:
				return "Rapport_Pleasure_2"sv;
			default:
				return "Rapport_Pleasure_3"sv;
			}
		};

		// FIRST, and it beats everything including a climax tag: somebody looked at
		// this position and said what it is. Nothing derived should overrule that.
		if (!a_position.empty()) {
			if (const auto it = _overrides.find(Lower(a_position)); it != _overrides.end()) {
				const auto& want = it->second;
				if (want == "oral") {
					return "Rapport_Oral"sv;
				}
				if (want == "kiss") {
					return "Rapport_Kiss"sv;
				}
				if (want == "climax") {
					return "Rapport_Climax"sv;
				}
				if (want == "pleasure") {
					return pleasureFor(a_intensity);
				}
				if (want == "none") {
					return {};
				}
				// An unknown word is a typo in the file, and silently ignoring it
				// would leave somebody wondering why their override does nothing.
				logger::warn(
					"expressions: act-overrides.json gives \"{}\" for \"{}\", which is not one of "
					"oral / pleasure / kiss / climax / none - ignoring it",
					want, a_position);
			}
		}

		const auto tags = SplitTags(a_actTags);

		// SUBSTRING, not equality. Pack authors do not agree on tag spelling and
		// never will: tonight's log carried "SEUKissing" -- one mod prefixing its
		// own name onto a standard tag -- and exact matching only found the act
		// because a bare "KISSING" happened to sit beside it. Elsewhere the same
		// act appears as CLIMAX and CLIMAXM.
		//
		// So match loosely and let a near miss land on the generous side. The cost
		// of the two directions is not symmetric: a slightly-too-expressive face
		// during sex reads as enthusiasm, and a stony calm one reads as broken.
		const auto any = [&](std::initializer_list<std::string_view> needles) {
			return std::ranges::any_of(tags, [&](const std::string& tag) {
				return std::ranges::any_of(needles, [&](std::string_view n) {
					return tag.find(n) != std::string::npos;
				});
			});
		};
		const auto pleasure = [&]() -> std::string_view {
			switch (a_intensity) {
			case 1:
				return "Rapport_Pleasure_1"sv;
			case 2:
				return "Rapport_Pleasure_2"sv;
			default:
				return "Rapport_Pleasure_3"sv;
			}
		};

		// A climax outranks everything, including the stage. This is the moment the
		// whole scene is for and AAF is the only thing that knows when it arrives.
		if (any({ "climax"sv, "orgasm"sv })) {
			return "Rapport_Climax"sv;
		}

		// Mouth working around something, and the TARGET is what decides it.
		//
		// This used to match any tag containing "mouthto", with a special case to
		// rescue MouthToMouth from being read as a blowjob. That was the wrong test
		// twice over: it needed the special case at all, and it still caught
		// MouthToNipples -- observed putting an open-jawed oral face on a missionary
		// scene whose tags also said PenisToVagina.
		//
		// A census of every mouth tag in the install decides it instead. Genital and
		// anal targets are oral sex: penistomouth 151, mouthtovagina 44, mouthtoanus
		// 28, tonguetoanus 4, mouthtopenis 1, anustomouth 1. The rest are foreplay or
		// flourish and must fall through to whatever else is happening:
		// mouthtomouth 27, mouthtofoot 8, mouthtonipples 4, mouthtoarmpit 1.
		//
		// Listing the targets makes MouthToMouth fall out naturally, so the special
		// case is gone rather than merely corrected.
		//
		// The reverse order is not available: "penistomouth" contains "penisto", so
		// checking penetration first would turn every blowjob into a pleasure face.
		// NAME THE TARGET, never a fragment of it. "tomouth" was here to catch
		// penistomouth (151) and anustomouth (1) -- and it also catches
		// MOUTHTOMOUTH (27), which put an open-jawed blowjob face on a kissing
		// animation. Observed live on a scene tagged
		// SEUKissing, KISSING, MOUTHTOMOUTH, SFW.
		//
		// That is the SECOND time this exact shape has bitten: the special case
		// that used to rescue MouthToMouth was deleted precisely because listing
		// targets was supposed to make it fall out naturally -- and then the
		// fragment that broke it was left in the list. A substring of a target is
		// not a target. "tongueto" is narrowed for the same reason before it finds
		// a tonguetomouth in some pack nobody here has installed.
		if (any({ "blowjob"sv, "cunnilingus"sv, "analingus"sv, "fellatio"sv, "irrumatio"sv,
				  "oral"sv, "69"sv, "rimjob"sv, "rimming"sv, "licking"sv,
				  "penistomouth"sv, "anustomouth"sv, "vaginatomouth"sv,
				  "mouthtovagina"sv, "mouthtopenis"sv, "mouthtoanus"sv,
				  "tonguetoanus"sv, "tonguetopenis"sv, "tonguetovagina"sv })) {
			return "Rapport_Oral"sv;
		}

		// "scissor" and "feetto" are here because a live f_f scene tagged
		// SCISSOR, FEETTOVAGINA, STIM9 fell through every family and landed on a
		// KISS face. "footto" was in this list; the pack spells it FEETto. That is
		// the whole lesson again in one tag.
		if (any({ "penisto"sv, "vaginal"sv, "anal"sv, "vaginato"sv, "anusto"sv, "strapon"sv,
				  "dildo"sv, "handjob"sv, "handto"sv, "footto"sv, "feetto"sv, "fingering"sv,
				  "titfuck"sv, "masturbat"sv, "spanking"sv, "scissor"sv, "tribb"sv, "grinding"sv,
				  "doggy"sv })) {
			return pleasure();
		}

		// Foreplay with no act named yet.
		//
		// "tease" was in this list and had to come OUT. It is a STAGE MODIFIER, not
		// an act: the install pairs it with real ones -- "LOVE5, TEASE,
		// PenisToVagina" is a BP70 entry animation. It only ever decided anything
		// when nothing else matched, and then it decided wrongly, putting a kiss
		// face on a scissoring scene at STIM9. A word that is right only when it is
		// never reached is not a rule, it is a trap.
		if (any({ "kissing"sv, "mouthtomouth"sv, "foreplay"sv, "grope"sv, "fondle"sv, "cuddle"sv })) {
			return "Rapport_Kiss"sv;
		}

		// Nothing named an act we know -- but if the tags look sexual AT ALL, that
		// is a pack spelling something in a way nobody anticipated, not a scene
		// where nothing is happening. Give a pleasure face at the story's own
		// intensity rather than leaving a blank one on during sex.
		if (LooksLikeSex(a_actTags)) {
			return pleasure();
		}

		// Last resort, and the most author-independent signal there is: AAF's own
		// arousal number. Nearly every position in this install carries Stim0..Stim9
		// regardless of which pack wrote it, so a high one says these two are
		// worked up even when no tag names what they are doing -- and the install
		// has positions like "F_M, DoubleBed, FromFront, Stim3, Love5" with no act
		// tag at all, which is plainly sex and was getting a blank face.
		//
		// SFW and NonSex veto it outright. A cuddle at Stim1 is meant to look calm,
		// and overriding an author who said so is the one place this tolerance would
		// do harm rather than good.
		if (!any({ "sfw"sv, "nonsex"sv })) {
			for (const auto& tag : tags) {
				if (!tag.starts_with("stim") || tag.size() < 5) {
					continue;
				}
				const auto digit = tag[4];
				if (digit >= '3' && digit <= '9') {
					return pleasure();
				}
			}
		}

		// LAST: read the position's own NAME.
		//
		// Not a guess -- measured. Fourteen positions on this install carry no act in
		// their tags at all, and five of them say it plainly in the title: "Gay
		// Blowjob (Couch)", "Toilet Blowjob 1". The author knew what it was; they
		// only wrote it in the one place nothing was reading.
		//
		// Last rather than first because a name is incidental and tags are authored
		// data. "Impregnate Cowgirl" ends on a climax position whose NAME still says
		// cowgirl, so a name consulted early would have overruled the climax tag.
		if (!a_position.empty()) {
			const auto words = SplitTags(a_position);
			const auto named = [&](std::initializer_list<std::string_view> needles) {
				return std::ranges::any_of(words, [&](const std::string& word) {
					return std::ranges::any_of(needles, [&](std::string_view n) {
						return word.find(n) != std::string::npos;
					});
				});
			};
			if (named({ "blowjob"sv, "cunnilingus"sv, "rimjob"sv, "facial"sv, "deepthroat"sv })) {
				return "Rapport_Oral"sv;
			}
			if (named({ "missionary"sv, "cowgirl"sv, "doggy"sv, "spooning"sv, "pronebone"sv,
						"prone"sv, "grind"sv, "anal"sv, "impregnate"sv, "fuck"sv, "sex"sv,
						"scissor"sv, "handjob"sv })) {
				return pleasureFor(a_intensity);
			}
			if (named({ "kiss"sv, "cuddle"sv, "hug"sv, "hold"sv })) {
				return "Rapport_Kiss"sv;
			}
		}

		// Genuinely says nothing sexual -- a walk, an idle, a transition. Change
		// nothing, because here a neutral face is the correct one.
		return {};
	}

	void Expressions::LoadOverrides()
	{
		_overrides.clear();

		const auto path =
			std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "act-overrides.json";
		std::error_code ec;
		if (!std::filesystem::exists(path, ec)) {
			// Absent is the normal case. The pack's own tags classify almost
			// everything; this file exists for the handful they do not.
			return;
		}

		std::ifstream file{ path };
		if (!file) {
			logger::error("expressions: {} exists but could not be opened", path.string());
			return;
		}

		nlohmann::json json;
		try {
			file >> json;
		} catch (const std::exception& e) {
			logger::error(
				"expressions: {} is not valid JSON ({}) - every position falls back to its tags",
				path.string(), e.what());
			return;
		}

		const auto acts = json.find("acts");
		if (acts == json.end() || !acts->is_object()) {
			logger::warn("expressions: {} has no \"acts\" object - nothing overridden", path.string());
			return;
		}

		for (const auto& [position, want] : acts->items()) {
			if (!want.is_string() || position.empty()) {
				continue;
			}
			_overrides.insert_or_assign(Lower(position), Lower(want.get<std::string>()));
		}
		logger::info(
			"expressions: {} position(s) overridden by hand from act-overrides.json",
			_overrides.size());
	}

	void Expressions::ReportUnclassified() const
	{
		// Straight from the classifier that actually runs, rather than from a tool
		// that reimplements it -- so what this prints IS what needs overriding, and
		// the two cannot drift apart.
		const auto& index = TreeIndex::GetSingleton();
		if (!index.Usable()) {
			return;
		}

		// AllPositions, not Entries: an unconstrained scene plays whatever AAF picks,
		// and most of what it picks never enters a tree. Walking the tree catalogue
		// reported one position and silently ignored the other thousand.
		std::vector<std::string> unreadable;
		for (const auto& declared : index.AllPositions()) {
			if (FaceForAct(declared.tags, declared.positionID, 2).empty()) {
				unreadable.push_back(declared.positionID);
			}
		}

		if (unreadable.empty()) {
			logger::info(
				"expressions: every indexed position's act can be read from its tags or its name");
			return;
		}

		logger::info(
			"expressions: {} of {} position(s) whose act cannot be read from tags OR name. Put them "
			"in "
			"Data/F4SE/Plugins/Rapport/act-overrides.json under \"acts\", as "
			"\"<position id>\": \"oral|pleasure|kiss|climax|none\":",
			unreadable.size(), index.AllPositions().size());
		for (const auto& id : unreadable) {
			logger::info("expressions:   \"{}\": \"pleasure\",", id);
		}
	}

	std::string Expressions::LivePosition() const
	{
		NamedLock lock{ _lock, "expressions" };
		return _livePosition;
	}

	std::size_t Expressions::OverrideCount() const
	{
		return _overrides.size();
	}

	int Expressions::HeatLevelFor(std::string_view a_faceSetID)
	{
		// Matched on the BASE set id, before VariantFor appends a style number --
		// which is what both callers pass.
		if (a_faceSetID == "Rapport_Climax"sv || a_faceSetID == "Rapport_Pleasure_3"sv) {
			return 3;
		}
		if (a_faceSetID == "Rapport_Pleasure_2"sv || a_faceSetID == "Rapport_Oral"sv) {
			return 2;
		}
		if (a_faceSetID == "Rapport_Pleasure_1"sv) {
			return 1;
		}
		// Nothing has happened yet. A flush before the first touch is the tell
		// that this is a timer and not a reaction.
		if (a_faceSetID == "Rapport_Anticipation"sv || a_faceSetID == "Rapport_Kiss"sv ||
			a_faceSetID == "Rapport_Clear"sv) {
			return 0;
		}
		// Rapport_Dazed and anything unrecognised: leave the skin alone. Sweat
		// does not evaporate the moment a scene ends, and an unknown set is not a
		// reason to strip somebody.
		return -1;
	}

	std::string Expressions::HeatSetFor(int a_level)
	{
		if (a_level <= 0 || a_level > kHeatLevels) {
			return {};
		}
		return std::format("Rapport_Heat_{}", a_level);
	}

	void Expressions::CollectHeat(std::string_view a_faceSetID, std::uint32_t a_first,
		std::uint32_t a_second, std::vector<Order>& a_out)
	{
		NamedLock lock{ _lock, "expressions" };

		const int level = HeatLevelFor(a_faceSetID);
		// A high-water mark: -1 leaves it alone, and anything at or below what is
		// already on is ignored rather than applied downward.
		if (level <= _heatLevel) {
			return;
		}
		std::string heat = HeatSetFor(level);

		for (const auto formID : { a_first, a_second }) {
			if (formID == 0) {
				continue;
			}
			// Per actor: what comes off is what THIS body wears, which may be a level
			// another scene left on it (R-22), not the level this scene last set.
			ClimbHeat(formID, level, a_out);
			// Registered here as well as in Collect. Without this a scenario that
			// ended badly -- before OnSceneEnded ran its Dazed pass, which is the
			// only other thing that adds them -- leaves two actors wearing an
			// overlay that nothing on the clear list will ever take off.
			if (std::ranges::find(_wearing, formID) == _wearing.end()) {
				_wearing.push_back(formID);
			}
		}

		logger::info("expressions: skin {} -> {} (scenario)",
			_heatApplied.empty() ? "(none)" : _heatApplied.c_str(),
			heat.empty() ? "(none)" : heat.c_str());
		_heatApplied = std::move(heat);
		_heatLevel = level;
	}

	void Expressions::Collect(std::string_view a_setID, std::vector<Order>& a_out)
	{
		const int   level = HeatLevelFor(a_setID);
		std::string heat  = HeatSetFor(level);
		const bool  shift = level > _heatLevel;   // climbs only -- see _heatLevel

		for (const auto formID : { _first, _second }) {
			if (formID == 0) {
				continue;
			}
			// The oral face goes on the mouth only; the partner gets pleasure (FaceFor).
			const auto mine = FaceFor(a_setID, _liveAct, _livePosition, formID, { _first, _second }, 2);
			a_out.push_back(
				Order{ Order::Kind::kApplyExpression, formID, VariantFor(mine, formID) });

			// Per actor (R-22): an actor can arrive wearing a level another scene left,
			// and what comes off must be what is ON them. Off before on inside, because
			// AAF has no notion of replacing an overlay set -- applying a second one
			// leaves both.
			ClimbHeat(formID, level, a_out);

			if (std::ranges::find(_wearing, formID) == _wearing.end()) {
				_wearing.push_back(formID);
			}
		}

		if (shift) {
			logger::info("expressions: skin {} -> {}",
				_heatApplied.empty() ? "(none)" : _heatApplied.c_str(),
				heat.empty() ? "(none)" : heat.c_str());
			_heatApplied = std::move(heat);
			_heatLevel = level;
		}
	}

	void Expressions::Pump()
	{
		// Everything decided under the lock; nothing SENT under it. Calling into
		// PapyrusLink while holding this mutex is where the poll stopped, and the
		// rule that prevents it is simply: never call another subsystem while
		// holding your own lock.
		std::vector<Order> outgoing;

		{
			NamedLock lock{ _lock, "expressions" };
			if (!_enabled) {
				return;
			}

			const auto now = std::chrono::steady_clock::now();

			// Before anything else: a face left on somebody by a session that is
			// over. This is the whole reason the wearing list is in the save.
			if (_clearPending) {
				_clearPending = false;
				// Sparing the scenes of somebody else's that started SINCE the load: a
				// load forgets every hold, so any listed now is this session's own face.
				CollectClear(outgoing, "a save was made while a scene was running", true);
			}

			if (_running && !_stoodDown) {
				const auto elapsed =
					std::chrono::duration<float>{ now - _startedAt }.count();
				const auto fraction = elapsed / _duration;

				// A scene with no act tag at all is kissing or foreplay, and gets
				// one face for its whole length rather than a build to a climax it
				// is never going to have. Waiting for the first step's moment
				// before deciding gives the animation time to say what it is.
				if (!_sawSexTag && !_tags.empty()) {
					if (_nextStep == 0 && fraction >= _steps.front().at) {
						logger::info("expressions: no act tag in this scene - holding {}", _kissSet);
						Collect(_kissSet, outgoing);
						_nextStep = _steps.size();   // nothing further; hold this face
					}
				} else {
					while (_nextStep < _steps.size() && fraction >= _steps[_nextStep].at) {
						// Logged BEFORE it is queued. If this ever stops working
						// again, the log says which side of the queue it died on
						// rather than leaving it to be reasoned about.
						logger::info(
							"expressions: {:.0f}% through - {}",
							fraction * 100.0f, _steps[_nextStep].set);
						Collect(_steps[_nextStep].set, outgoing);
						++_nextStep;
					}
				}
			} else if (_dazing) {
				if (std::chrono::duration<float>{ now - _endedAt }.count() >= _dazedSeconds) {
					_dazing = false;
					// Clear EVERYONE on the list, not just this scene's two. If an
					// earlier scene ended badly its actors are still on it, and
					// this is the only thing that ever takes a face off. Everyone
					// but those a scene of somebody else's is still playing on
					// (R-22): their scene takes its own faces off when it ends.
					CollectClear(outgoing, "the afterglow is over", true);
				}
			}
		}

		Send(outgoing);
	}

	void Expressions::Send(const std::vector<Order>& a_orders)
	{
		auto& link = PapyrusLink::GetSingleton();
		for (const auto& order : a_orders) {
			link.QueueOrder(order);
		}
	}

	void Expressions::CollectClear(std::vector<Order>& a_out, std::string_view a_why, bool a_spareForeign)
	{
		if (_wearing.empty()) {
			return;
		}
		std::vector<std::uint32_t> spared;
		for (const auto formID : _wearing) {
			if (a_spareForeign && std::ranges::find(_foreignHeld, formID) != _foreignHeld.end()) {
				spared.push_back(formID);
				continue;
			}
			a_out.push_back(Order{ Order::Kind::kClearExpression, formID, _clearSet });
			TakeOffHeat(formID, a_out);
		}
		_heatApplied.clear();
		_heatLevel = 0;
		logger::info("expressions: clearing {} face(s) - {}{}", _wearing.size() - spared.size(), a_why,
			spared.empty() ? std::string{}
						   : std::format("; {} spared, still in a scene Rapport did not start", spared.size()));
		_wearing = std::move(spared);
		if (!a_spareForeign) {
			_foreignHeld.clear();
		}
	}

	void Expressions::HoldForeign(std::uint32_t a_formID)
	{
		if (a_formID == 0) {
			return;
		}
		NamedLock lock{ _lock, "expressions" };
		if (std::ranges::find(_foreignHeld, a_formID) == _foreignHeld.end()) {
			_foreignHeld.push_back(a_formID);
		}
		if (std::ranges::find(_wearing, a_formID) == _wearing.end()) {
			_wearing.push_back(a_formID);
		}
	}

	void Expressions::ReleaseForeign(const std::vector<std::uint32_t>& a_formIDs, std::vector<Order>& a_out,
		std::string_view a_why)
	{
		NamedLock lock{ _lock, "expressions" };
		std::size_t cleared = 0;
		for (const auto formID : a_formIDs) {
			std::erase(_foreignHeld, formID);

			// Freed by that scene and taken by one of ours since: the face is ours now,
			// and ours -- with the heat -- comes off with our own afterglow.
			if ((_running || _dazing) && (formID == _first || formID == _second)) {
				continue;
			}
			// Cleared whether or not the list still names them: a load or the panic
			// switch may have swept the list while their scene had a face on them.
			std::erase(_wearing, formID);
			a_out.push_back(Order{ Order::Kind::kClearExpression, formID, _clearSet });
			TakeOffHeat(formID, a_out);
			++cleared;
		}
		if (cleared > 0) {
			logger::info("expressions: clearing {} face(s) - {}", cleared, a_why);
		}
	}

	void Expressions::RaiseHeat(const std::vector<std::pair<std::uint32_t, int>>& a_targets, std::vector<Order>& a_out)
	{
		if (a_targets.empty()) {
			return;
		}
		NamedLock lock{ _lock, "expressions" };
		for (const auto& [formID, level] : a_targets) {
			if (formID == 0) {
				continue;
			}
			ClimbHeat(formID, level, a_out);
			// On the list for as long as they wear it, so a save taken now clears it on
			// the load -- the same reason every face is.
			if (std::ranges::find(_wearing, formID) == _wearing.end()) {
				_wearing.push_back(formID);
			}
		}
	}

	std::vector<std::pair<float, int>> Expressions::IntensitySchedule() const
	{
		NamedLock lock{ _lock, "expressions" };
		std::vector<std::pair<float, int>> out;
		for (const auto& step : _steps) {
			int level = 1;
			if (step.set == "Rapport_Anticipation"sv) {
				level = 0;
			} else if (step.set == "Rapport_Pleasure_1"sv) {
				level = 1;
			} else if (step.set == "Rapport_Pleasure_2"sv) {
				level = 2;
			} else if (step.set == "Rapport_Pleasure_3"sv || step.set == "Rapport_Climax"sv) {
				// A climax on a timer arrived a minute early once (section 17): only
				// a climax TAG gives the climax face, so its step is simply the top.
				level = 3;
			}
			out.emplace_back(step.at, level);
		}
		return out;
	}

	int Expressions::LevelOfHeat(std::string_view a_heatSet)
	{
		for (int level = kHeatLevels; level >= 1; --level) {
			if (a_heatSet == HeatSetFor(level)) {
				return level;
			}
		}
		return 0;
	}

	void Expressions::ClimbHeat(std::uint32_t a_formID, int a_level, std::vector<Order>& a_out)
	{
		if (a_formID == 0 || a_level <= 0) {
			return;   // -1 leaves the skin alone, and 0 puts nothing on
		}
		const auto on = _heatOn.find(a_formID);
		const int  wearing = on != _heatOn.end() ? LevelOfHeat(on->second) : 0;
		if (a_level <= wearing) {
			return;   // heat climbs and never falls (docs/aaf-under-the-hood.md, section 21)
		}
		if (on != _heatOn.end()) {
			a_out.push_back(Order{ Order::Kind::kRemoveOverlay, a_formID, on->second });
		}
		auto set = HeatSetFor(a_level);
		a_out.push_back(Order{ Order::Kind::kApplyOverlay, a_formID, set });
		_heatOn.insert_or_assign(a_formID, std::move(set));
	}

	void Expressions::TakeOffHeat(std::uint32_t a_formID, std::vector<Order>& a_out)
	{
		// The one that is ON when we know which, and only sweep all levels when we do
		// not.
		//
		// "Removing a set that was never applied costs an order and does nothing" was
		// wrong about the cost: every AAF call costs a poll's worth of exposure, and a
		// fast travel landing mid-drain once stopped the bridge (section 24). So the
		// exact set when this layer knows it -- and the sweep only in the case it was
		// written for: after a LOAD the record is empty while an overlay may still be
		// on the actor, and an overlay nothing removes is on them for good.
		//
		// The sweep FIRST: a level put on since the load is known, but the save's may
		// still be under it, and removing only the known one left that for good.
		if (_heatUnknown.erase(a_formID) > 0) {
			for (int level = 1; level <= kHeatLevels; ++level) {
				a_out.push_back(Order{ Order::Kind::kRemoveOverlay, a_formID, HeatSetFor(level) });
			}
			_heatOn.erase(a_formID);
			return;
		}
		if (const auto on = _heatOn.find(a_formID); on != _heatOn.end()) {
			a_out.push_back(Order{ Order::Kind::kRemoveOverlay, a_formID, on->second });
			_heatOn.erase(on);
		}
		// Otherwise tracked this session and wearing none: nothing to take off.
	}

	std::string Expressions::AfterSet() const
	{
		NamedLock lock{ _lock, "expressions" };
		return _afterSet;
	}

	float Expressions::DazedSeconds() const
	{
		NamedLock lock{ _lock, "expressions" };
		return _dazedSeconds;
	}

	void Expressions::StandDown()
	{
		NamedLock lock{ _lock, "expressions" };
		_stoodDown = true;
		logger::info("expressions: a scenario is driving the faces this scene - schedule stood down");
	}

	void Expressions::OnSceneEnded()
	{
		std::vector<Order> outgoing;
		{
			NamedLock lock{ _lock, "expressions" };
			if (!_enabled || !_running) {
				return;
			}

			_running = false;
			_endedAt = std::chrono::steady_clock::now();
			_dazing = true;
			logger::info("expressions: scene over - {} for {:.0f}s, then clearing", _afterSet, _dazedSeconds);
			Collect(_afterSet, outgoing);
		}
		Send(outgoing);
	}

	void Expressions::ClearEveryone(std::string_view a_why)
	{
		std::vector<Order> outgoing;
		{
			NamedLock lock{ _lock, "expressions" };
			CollectClear(outgoing, a_why);
		}
		Send(outgoing);
	}

	std::vector<std::uint32_t> Expressions::Wearing() const
	{
		NamedLock lock{ _lock, "expressions" };
		return _wearing;
	}

	void Expressions::RestoreWearing(std::vector<std::uint32_t> a_wearing)
	{
		NamedLock lock{ _lock, "expressions" };
		_wearing = std::move(a_wearing);
		_clearPending = !_wearing.empty();
		// Nothing this session says what heat these wear: the clear sweeps every level.
		_heatUnknown.clear();
		_heatUnknown.insert(_wearing.begin(), _wearing.end());
		// Deliberately not queued here: the bridge is not listening yet. The first
		// pump after the handshake does it, and the log says why.
		if (!_wearing.empty()) {
			logger::warn(
				"expressions: {} actor(s) were wearing a Rapport face when this save was made - "
				"they will be cleared as soon as the bridge is listening",
				_wearing.size());
		}
	}

	void Expressions::Reset()
	{
		NamedLock lock{ _lock, "expressions" };
		_running = false;
		_dazing = false;
		_clearPending = false;
		_nextStep = 0;
		_first = 0;
		_second = 0;
		_tags.clear();
		_sawSexTag = false;
		_wearing.clear();
		_foreignHeld.clear();
		// The heat bookkeeping too: a load while our scene was running or dazing left
		// _heatApplied/_heatLevel naming the old world's level, so the next scene's heat
		// was measured against it and the load's clear removed the wrong set.
		_heatApplied.clear();
		_heatLevel = 0;
		_heatOn.clear();
		_heatUnknown.clear();
	}
}
