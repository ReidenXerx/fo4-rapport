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
		if (a_setID.size() > 2 && a_setID[a_setID.size() - 2] == '_' &&
			a_setID.back() >= '1' && a_setID.back() <= '9') {
			return std::string{ a_setID };
		}
		return std::format("{}_{}", a_setID, (a_formID % kStyles) + 1);
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
		if (any({ "blowjob"sv, "cunnilingus"sv, "analingus"sv, "fellatio"sv, "irrumatio"sv,
				  "oral"sv, "tomouth"sv, "69"sv, "tongueto"sv, "rimjob"sv, "rimming"sv,
				  "licking"sv, "mouthtovagina"sv, "mouthtopenis"sv, "mouthtoanus"sv })) {
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

		const int   level = HeatLevelFor(a_faceSetID);
		std::string heat  = HeatSetFor(level);
		if (level < 0 || heat == _heatApplied) {
			return;
		}

		for (const auto formID : { a_first, a_second }) {
			if (formID == 0) {
				continue;
			}
			if (!_heatApplied.empty()) {
				a_out.push_back(Order{ Order::Kind::kRemoveOverlay, formID, _heatApplied });
			}
			if (!heat.empty()) {
				a_out.push_back(Order{ Order::Kind::kApplyOverlay, formID, heat });
			}
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
	}

	void Expressions::Collect(std::string_view a_setID, std::vector<Order>& a_out)
	{
		const int   level = HeatLevelFor(a_setID);
		std::string heat  = HeatSetFor(level);
		const bool  shift = level >= 0 && heat != _heatApplied;

		for (const auto formID : { _first, _second }) {
			if (formID == 0) {
				continue;
			}
			a_out.push_back(
				Order{ Order::Kind::kApplyExpression, formID, VariantFor(a_setID, formID) });

			if (shift) {
				// Off before on. AAF has no notion of replacing an overlay set --
				// applying a second one leaves both, and the sets differ only in
				// how heavy they are, so the result is the sum of every level the
				// scene passed through rather than the one it is at.
				if (!_heatApplied.empty()) {
					a_out.push_back(Order{ Order::Kind::kRemoveOverlay, formID, _heatApplied });
				}
				if (!heat.empty()) {
					a_out.push_back(Order{ Order::Kind::kApplyOverlay, formID, heat });
				}
			}

			if (std::ranges::find(_wearing, formID) == _wearing.end()) {
				_wearing.push_back(formID);
			}
		}

		if (shift) {
			logger::info("expressions: skin {} -> {}",
				_heatApplied.empty() ? "(none)" : _heatApplied.c_str(),
				heat.empty() ? "(none)" : heat.c_str());
			_heatApplied = std::move(heat);
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
				CollectClear(outgoing, "a save was made while a scene was running");
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
					// this is the only thing that ever takes a face off.
					CollectClear(outgoing, "the afterglow is over");
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

	void Expressions::CollectClear(std::vector<Order>& a_out, std::string_view a_why)
	{
		if (_wearing.empty()) {
			return;
		}
		for (const auto formID : _wearing) {
			a_out.push_back(Order{ Order::Kind::kClearExpression, formID, _clearSet });
			// EVERY level, not just the one we think is on. After a save and a
			// reload _heatApplied is empty while the overlay is still on the
			// actor, and an overlay nothing removes is on them for good -- the
			// exact failure the sets were written without a duration to avoid.
			// Removing a set that was never applied costs an order and does
			// nothing.
			for (int level = 1; level <= kHeatLevels; ++level) {
				a_out.push_back(Order{ Order::Kind::kRemoveOverlay, formID, HeatSetFor(level) });
			}
		}
		_heatApplied.clear();
		logger::info("expressions: clearing {} face(s) - {}", _wearing.size(), a_why);
		_wearing.clear();
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
	}
}
