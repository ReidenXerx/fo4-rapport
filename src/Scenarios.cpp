#include "Scenarios.h"

#include "PapyrusLink.h"
#include "Aftermath.h"
#include "Expressions.h"
#include "Config.h"
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

	// "A, B ,C" -> {"a","b","c"}. Empty entries dropped, so a trailing comma or a
	// deliberately empty exclude list costs nothing.
	[[nodiscard]] std::vector<std::string> Split(std::string_view a_list)
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
}

namespace RP
{
	Scenarios& Scenarios::GetSingleton() noexcept
	{
		static Scenarios singleton;
		return singleton;
	}

	std::filesystem::path Scenarios::ConfigPath()
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "scenarios.json";
	}

	std::filesystem::path Scenarios::AAFDataPath()
	{
		return std::filesystem::path{ "Data" } / "AAF";
	}

	void Scenarios::Load()
	{
		_scenarios.clear();

		IndexInstalledTags();
		TreeIndex::GetSingleton().Load();

		const auto    path = ConfigPath();
		std::ifstream file{ path };
		if (!file) {
			logger::info("scenarios: no {} - scenes are single animations, as before", path.string());
			return;
		}

		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			logger::error("scenarios: {} is not valid json ({}) - none are available", path.string(), e.what());
			return;
		}

		const auto list = document.find("scenarios");
		if (list == document.end() || !list->is_array()) {
			return;
		}

		for (const auto& entry : *list) {
			Scenario scenario;
			scenario.id = entry.value("id", std::string{});
			if (scenario.id.empty()) {
				logger::warn("scenarios: an entry has no id - skipped");
				continue;
			}

			const auto stages = entry.find("stages");
			if (stages == entry.end() || !stages->is_array()) {
				logger::warn("scenarios: \"{}\" has no stages - skipped", scenario.id);
				continue;
			}

			for (const auto& raw : *stages) {
				Stage stage;
				stage.id = raw.value("id", std::string{});
				stage.seconds = raw.value("seconds", 30.0f);
				stage.include = raw.value("include", std::string{});
				stage.options = Split(stage.include);
				stage.exclude = raw.value("exclude", std::string{});
				stage.face = raw.value("face", std::string{});

				// Authors wrote `face`, not `intensity`, so derive it from what the
				// face they picked implies rather than making every scenario file
				// wrong at once. An explicit intensity overrides.
				stage.intensity = 2;
				if (stage.face.ends_with("_1") || stage.face.ends_with("Anticipation")) {
					stage.intensity = 1;
				} else if (stage.face.ends_with("_3") || stage.face.ends_with("Climax")) {
					stage.intensity = 3;
				}
				stage.intensity = raw.value("intensity", stage.intensity);
				stage.intensity = std::clamp(stage.intensity, 1, 3);
				stage.handover = raw.value("handover", false);
				stage.tree = raw.value("tree", false);
				stage.requireEnding = raw.value("requireEnding", true);

				// A handover stage asks AAF for nothing, so it needs no tags. Every
				// other stage does: without them there is nothing to request and the
				// stage would silently do nothing at all.
				if (stage.id.empty() || (stage.include.empty() && !stage.handover && !stage.tree)) {
					logger::warn(
						"scenarios: a stage of \"{}\" has no id or no include tags - skipped",
						scenario.id);
					continue;
				}
				scenario.stages.push_back(std::move(stage));
			}

			if (scenario.stages.empty()) {
				continue;
			}
			_scenarios.push_back(std::move(scenario));
		}

		MarkPlayableStages();

		for (const auto& scenario : _scenarios) {
			std::string shape;
			for (const auto& stage : scenario.stages) {
				if (!shape.empty()) {
					shape += " -> ";
				}
				shape += stage.playable
				             ? std::format("{} {:.0f}s", stage.id, stage.seconds)
				             : std::format("({} SKIPPED, nothing installed matches)", stage.id);
			}
			logger::info(
				"scenarios: \"{}\" = {} ({:.0f}s playable)",
				scenario.id, shape, scenario.PlayableSeconds());
		}
		logger::info(
			"scenarios: {} loaded, {} distinct tag(s) indexed from the installed packs",
			_scenarios.size(), _tags.size());
	}

	// Reads the tags out of AAF's own XML rather than asking AAF.
	//
	// Deliberately crude -- a regex over `tags="..."` rather than an XML parse.
	// The data is flat, every pack writes it the same way, and tools/tagaudit.py
	// has been reading it this way accurately since it was written. An XML parser
	// would be more correct and would buy nothing.
	void Scenarios::IndexInstalledTags()
	{
		_tags.clear();

		const auto      folder = AAFDataPath();
		std::error_code ec;
		if (!std::filesystem::exists(folder, ec)) {
			logger::warn(
				"scenarios: {} does not exist, so no stage can be checked for content and every "
				"stage will be attempted",
				folder.string());
			return;
		}

		std::uint32_t files = 0;
		for (const auto& entry : std::filesystem::directory_iterator{ folder, ec }) {
			if (!entry.is_regular_file(ec)) {
				continue;
			}
			const auto name = entry.path().filename().string();
			if (!name.ends_with(".xml") && !name.ends_with(".XML")) {
				continue;
			}

			std::ifstream file{ entry.path(), std::ios::binary };
			if (!file) {
				continue;
			}
			++files;

			std::string text{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
			for (std::size_t at = text.find("tags=\""); at != std::string::npos;
			     at = text.find("tags=\"", at + 1)) {
				const auto open = at + 6;
				const auto close = text.find('"', open);
				if (close == std::string::npos) {
					break;
				}
				for (auto& tag : Split(std::string_view{ text }.substr(open, close - open))) {
					_tags.insert(std::move(tag));
				}
			}
		}

		logger::info("scenarios: indexed {} tag(s) from {} AAF xml file(s)", _tags.size(), files);
	}

	bool Scenarios::AnyContentFor(std::string_view a_includeTags) const
	{
		// An empty index means the scan found nothing -- no AAF folder, no files,
		// no read access. That is not evidence that content is absent, so every
		// stage is attempted rather than every stage being skipped.
		if (_tags.empty()) {
			return true;
		}
		for (const auto& tag : Split(a_includeTags)) {
			if (_tags.contains(tag)) {
				return true;
			}
		}
		return false;
	}

	void Scenarios::MarkPlayableStages()
	{
		for (auto& scenario : _scenarios) {
			for (auto& stage : scenario.stages) {
				// A handover stage requests nothing, so there is nothing it could
				// fail to find. It is always playable.
				stage.playable = stage.handover || stage.tree || AnyContentFor(stage.include);
				if (!stage.playable) {
					logger::warn(
						"scenarios: \"{}\" stage \"{}\" will be skipped - nothing installed carries "
						"any of [{}]",
						scenario.id, stage.id, stage.include);
				}
			}
		}
	}

	const Scenarios::Scenario* Scenarios::Find(std::string_view a_id) const
	{
		for (const auto& scenario : _scenarios) {
			if (scenario.id == a_id) {
				return &scenario;
			}
		}
		return nullptr;
	}

	// ---- running one --------------------------------------------------------

	float Scenarios::SecondsFor(std::string_view a_id) const
	{
		const auto scenario = Find(a_id);
		return scenario ? scenario->PlayableSeconds() : 0.0f;
	}

	bool Scenarios::Begin(std::string_view a_id, std::uint32_t a_first, std::uint32_t a_second)
	{
		std::vector<Order> outgoing;
		{
			NamedLock lock{ _lock, "scenarios" };

			const auto scenario = Find(a_id);
			if (!scenario) {
				logger::warn("scenarios: nothing named \"{}\" - the scene runs as a single animation", a_id);
				return false;
			}
			if (scenario->PlayableSeconds() <= 0.0f) {
				logger::warn(
					"scenarios: \"{}\" has no stage the installed packs can fill - the scene runs as "
					"a single animation",
					a_id);
				return false;
			}

			_running = scenario;
			_first = a_first;
			_second = a_second;
			_stage = scenario->stages.size();   // EnterStage moves it to the first playable one

			// The stages are a proportion of the scene, not a number of seconds.
			//
			// AAF stages the scene now, and the chosen tree runs for as long as its
			// author made it -- 120s for the good ones. A scenario that declares 285
			// seconds of stages against a 120-second tree never reaches its last two:
			// the tree hits its Climax branch and exits, OnSceneEnded fires, and the
			// climax FACE is scheduled for a moment the scene never gets to.
			//
			// That is the same mistake the redesign was for, one layer up. The
			// clock used to cut off AAF's climax; left alone it would cut off ours.
			// So the declared seconds are read as weights and scaled onto what is
			// actually going to play. Unknown tree length falls back to 1.0, which
			// is the old behaviour and the honest answer when nothing is known.
			_stageScale = 1.0f;
			if (_chosenSeconds > 0.0f) {
				const auto declared = scenario->PlayableSeconds();
				if (declared > 0.0f) {
					_stageScale = _chosenSeconds / declared;
				}
			}

			logger::info(
				"scenario \"{}\": {:08X} and {:08X}, {} stage(s) over {:.0f}s{}",
				scenario->id, a_first, a_second, scenario->stages.size(),
				scenario->PlayableSeconds() * _stageScale,
				_stageScale == 1.0f
					? " (the tree's length is unknown, so the stages keep their own)"
					: std::format(" - the chosen tree is authored for {:.0f}s, so the stages are "
								  "scaled to {:.2f} of their declared length",
						  _chosenSeconds, _stageScale));

			EnterStage(0, outgoing);
		}
		PapyrusLink::GetSingleton().QueueOrders(outgoing);
		return true;
	}

	// Enters the first PLAYABLE stage at or after a_index. A stage nothing can
	// fill costs nothing and is stepped over here rather than being started and
	// then found empty.
	void Scenarios::EnterStage(std::size_t a_index, std::vector<Order>& a_out)
	{
		if (!_running) {
			return;
		}

		auto index = a_index;
		while (index < _running->stages.size() && !_running->stages[index].playable) {
			logger::info("scenario \"{}\": skipping \"{}\" - nothing installed fills it",
				_running->id, _running->stages[index].id);
			++index;
		}

		if (index >= _running->stages.size()) {
			logger::info("scenario \"{}\": no stages left - the scene plays out its last one", _running->id);
			_stage = _running->stages.size();
			return;
		}

		const auto& stage = _running->stages[index];
		_stage = index;
		_option = 0;
		_stageStartedAt = std::chrono::steady_clock::now();

		if (stage.tree) {
			// The tree was chosen at StartScene and the scene is walking it. There
			// is nothing to do here and nothing to decide.
			//
			// This used to run the WHOLE selection again and log what it "would"
			// pick -- a 74-entry scan with scoring and a random draw, on the
			// Papyrus poll path, inside this lock, discarded immediately. Worse
			// than wasteful: it passed the stage's own seconds as the budget and
			// never avoided furniture, so it answered a DIFFERENT question from
			// the one the start asked and then printed the answer as though it
			// were the same one. Observed live picking "Prone Bone" while the
			// scene had been correctly started on "Impregnate Cowgirl" -- two
			// trees, one scene, and a log that looked like a disagreement.
			logger::info(
				"scenario \"{}\": stage \"{}\" over {:.0f}s - the scene is on the tree chosen when "
				"it started and AAF is staging it",
				_running->id, stage.id, stage.seconds * _stageScale);
		} else if (stage.handover) {
			logger::info(
				"scenario \"{}\": stage \"{}\" over {:.0f}s - handing the ending to AAF. No "
				"position change is asked for, so a position that declares a tree walks it to its "
				"own climax",
				_running->id, stage.id, stage.seconds * _stageScale);
		} else {
			// "for 35s, any of [Kissing,...]" was wrong twice over. The clock runs
			// on the SCALED seconds -- measured live, this stage advanced after
			// 16.6s while the log said 35 -- and the tag list is not asked for
			// anything: a stage without tree=true makes no request of AAF at all.
			// Printing it read as a request that was being made and refused.
			logger::info(
				"scenario \"{}\": stage \"{}\" over {:.0f}s, face only - AAF keeps staging the "
				"tree, and these tags choose nothing from here",
				_running->id, stage.id, stage.seconds * _stageScale);

			SendCurrentOption(a_out);
		}

		// Only the OPENING face, and only while nothing better is known.
		//
		// This used to fire on every stage boundary, which now fights the act: it
		// would overwrite a correctly chosen face with the stage's guess, and Pump
		// would not put it back -- _faceApplied already matched, so the wrong face
		// would sit there until the act itself changed. Once AAF has told us what
		// is playing, the act decides and the stage only supplies the intensity.
		if (!stage.face.empty() && _faceApplied.empty()) {
			_faceApplied = stage.face;
			for (const auto formID : { _first, _second }) {
				if (formID != 0) {
					a_out.push_back(Order{ Order::Kind::kApplyExpression, formID, stage.face, {} });
				}
			}
		}
	}

	// ONE tag, not the whole list. AAF's includeTags is an AND.
	void Scenarios::SendCurrentOption(std::vector<Order>& a_out)
	{
		if (!_running || _stage >= _running->stages.size()) {
			return;
		}
		const auto& stage = _running->stages[_stage];
		if (_option >= stage.options.size()) {
			return;
		}

		// NO POSITION ORDER. ChangePosition does not work -- 26 refusals out of 26
		// with tags, and refused again with a position id and no filters at all --
		// so a stage no longer tries to move the scene. The tree chosen at
		// StartScene does the staging, and a stage is now a MOOD: its face, for its
		// share of the story.
		logger::info(
			"scenario \"{}\": stage \"{}\" - {}",
			_running->id, stage.id,
			stage.face.empty() ? "no face of its own" : std::format("face {}", stage.face));

		// And ask AAF what it thinks it has for that same tag. Apples to apples:
		// the answer arrives on OnAnimationQueryResult and is logged raw, so a
		// refusal can finally be read as either "AAF cannot see this content" or
		// "AAF can see it and our ChangePosition is wrong".
		if (Config::GetSingleton().diagnoseStageTags) {
			a_out.push_back(
				Order{ Order::Kind::kQueryAnimations, _first, stage.options[_option], stage.exclude });

			// And the same question with NO filter at all, once per stage. Every
			// tagged query has come back 0 -- including PenisToVagina, which 2320
			// positions carry -- and a zero is only meaningful against a baseline.
			// Non-zero here means the query works and tag matching is what fails.
			// Zero here means the query itself is wrong and its other answers said
			// nothing about the content.
			if (_option == 0) {
				a_out.push_back(Order{ Order::Kind::kQueryAnimations, _first, {}, {} });
			}
		}
	}

	// A refusal can no longer be about one of our stages, so this no longer moves
	// the story. It only records that the tree we picked was turned down.
	//
	// The bridge registers for AAF's API events on the API SINGLETON, so every
	// refusal in the game arrives here -- including other mods' -- and the refusal
	// branch carries no request id to filter on. While a stage still asked AAF for
	// positions, this walked the option list and then called EnterStage(_stage+1),
	// so six refusals from a DIFFERENT mod's failed scene silently advanced our
	// story a whole stage and lost that stage's face. A stage now asks for nothing
	// at all, which means by construction no refusal is ever about one.
	void Scenarios::OnRefused(std::string_view a_why)
	{
		NamedLock lock{ _lock, "scenarios" };

		// The one refusal that IS ours to act on: between choosing a tree and the
		// scene starting, a refusal is very likely AAF turning down the position we
		// named. Recording it lets the furniture fallback fire; it changes no
		// stage, and the worst a foreign refusal in that window can do is make the
		// next choice avoid furniture once.
		if (!_running && !_chosenPosition.empty()) {
			logger::info(
				"scenarios: a scene was refused while \"{}\" was waiting to start ({})",
				_chosenPosition, a_why);
			return;
		}

		if (_running) {
			logger::info(
				"scenario \"{}\": AAF refused something during stage \"{}\" ({}) - ignored, because "
				"a stage asks AAF for nothing and no refusal can be about one",
				_running->id,
				_stage < _running->stages.size() ? _running->stages[_stage].id : "(past the end)",
				a_why);
		}
	}

	// The entire selection, with no side effects, so the pre-flight an addon runs
	// before it stages anything asks the exact question the real start will ask.
	// Two copies of this ladder would drift, and a pre-flight that disagrees with
	// the start it is predicting is worse than having none.
	//
	// Caller holds _lock.
	Scenarios::Selection Scenarios::SelectLocked(
		std::string_view a_id,
		std::uint32_t    a_first,
		std::uint32_t    a_second,
		bool             a_avoidFurniture) const
	{
		Selection out;

		const auto scenario = Find(a_id);
		if (!scenario) {
			out.why = "no such scenario";
			return out;
		}
		out.scenario = scenario;

		// The stage that ASKED for a tree describes the ending, and a scenario that
		// asks for none gets none.
		//
		// This used to take the last stage with any include tags at all, which is a
		// different question with a different answer. "quickie" is one stage --
		// somewhere public, no time, no undressing, and no `tree` -- and its tags
		// made it the last non-empty include, so it was handed a six-stage climax
		// tree and ran for four minutes. Its thirty seconds were decorative,
		// because nothing stops a scene on the scenario's clock any more.
		//
		// Inferring a requirement from a field being non-empty is how a scenario
		// gets the opposite of what it asked for while the log reads like success.
		const Stage* ending = nullptr;
		for (auto it = scenario->stages.rbegin(); it != scenario->stages.rend(); ++it) {
			if (it->tree) {
				ending = &*it;
				break;
			}
		}
		if (!ending) {
			logger::info(
				"scenario \"{}\": no stage asks for a tree, so the scene starts unconstrained - "
				"AAF picks, and it is free to be short",
				scenario->id);
			out.unconstrained = true;
			out.why = "scenario asks for no tree";
			return out;
		}
		if (ending->include.empty()) {
			logger::warn(
				"scenario \"{}\": stage \"{}\" asks for a tree but names no tags to choose one by - "
				"starting unconstrained rather than picking arbitrarily",
				scenario->id, ending->id);
			out.unconstrained = true;
			out.why = "the ending stage names no tags";
			return out;
		}

		const auto& index = TreeIndex::GetSingleton();
		if (!index.Usable()) {
			logger::warn(
				"scenario \"{}\": no tree catalogue, so the scene starts unconstrained and its "
				"ending is whatever AAF picks",
				scenario->id);
			out.why = "no tree catalogue";
			return out;
		}

		// The budget is the WHOLE scene, not the ending stage's share of it.
		//
		// The tree starts walking the moment the scene starts and has every second
		// of it. Passing the finish stage's 120s instead of athome's 285 understated
		// the budget 2.4x, so a 150s tree scored -12 as "will be cut off" where it
		// should have scored +8 -- a 20-point swing against exactly the long,
		// well-authored trees this whole feature exists to reach, and wider than the
		// 10-point band, so they were erased rather than merely demoted.
		const auto  budget = scenario->PlayableSeconds();
		const auto  composition = Aftermath::GetSingleton().CompositionOf(a_first, a_second);
		const auto* chosen = index.Choose(
			ending->include, ending->exclude, composition, ending->requireEnding,
			a_avoidFurniture, budget);

		// Nothing without furniture fits either, so take the furniture one back --
		// a scene that might not start beats no scene at all, and the refusal that
		// set this flag may have been about something else entirely.
		if (!chosen && a_avoidFurniture) {
			logger::info(
				"scenario \"{}\": nothing without furniture fits, so trying one that wants it "
				"after all",
				scenario->id);
			chosen = index.Choose(
				ending->include, ending->exclude, composition, ending->requireEnding, false,
				budget);
		}

		// Last rung: stop insisting on a guaranteed climax.
		//
		// Requiring the ending is the right default and a hard requirement is the
		// wrong way to hold it. Measured on this install it recovers athome from
		// 24 candidates to 25 and tender from 22 to 24 -- small here, and the
		// point is the install where it is not: a thin pack set can leave a
		// scenario with a perfectly good match that happens not to be graded, and
		// refusing it buys nothing.
		//
		// It does NOT rescue female+female, which is what it was first proposed
		// for. Two women have 24 positions on this install and NOT ONE of them
		// enters a tree, so there is nothing for any rung to relax TO. What makes
		// f_f playable is the unconstrained path below -- no position named, AAF
		// chooses freely, Rapport still keeps the faces and the aftermath. That
		// is a content gap in AAF's packs, not something selection can fix.
		//
		// Loud, because "it finished somehow" is not what the scenario asked for
		// and the log is where that difference has to be visible.
		if (!chosen && ending->requireEnding) {
			chosen = index.Choose(
				ending->include, ending->exclude, composition, false, false, budget);
			if (chosen) {
				out.relaxed = true;
				logger::warn(
					"scenario \"{}\": no {} tree on this install both matches [{}] and reaches a "
					"climax, so relaxing to \"{}\" - it will play out, but nothing guarantees it "
					"finishes",
					scenario->id, composition.empty() ? "any-pair" : composition, ending->include,
					chosen->positionID);
			}
		}

		if (!chosen) {
			// Said out loud, because a scene without a guaranteed ending is exactly
			// what the owner asked to be sure of, and silence here would read as
			// success.
			logger::warn(
				"scenario \"{}\": no {} tree reaches a climax with [{}] on this install, out of "
				"{} indexed ({} with a real ending). The scene will still run, but nothing "
				"guarantees how it finishes",
				scenario->id, composition.empty() ? "any-pair" : composition, ending->include,
				index.Size(), index.WithEnding());
			out.why = "nothing in the catalogue matches";
			return out;
		}

		out.entry = chosen;
		return out;
	}

	std::string Scenarios::ChooseSceneStart(
		std::string_view a_id, std::uint32_t a_first, std::uint32_t a_second)
	{
		NamedLock lock{ _lock, "scenarios" };

		_chosenPosition.clear();
		_chosenSeconds = 0.0f;

		const auto picked = SelectLocked(a_id, a_first, a_second, _avoidFurniture);
		if (!picked.entry) {
			return {};
		}

		const auto* chosen = picked.entry;
		_chosenPosition = chosen->positionID;
		_chosenSeconds = chosen->seconds;
		_treeSteps = chosen->stages;
		_stepsSeen = 0;

		logger::info(
			"scenario \"{}\": starting on \"{}\" - a {}-stage tree ending in a {}{}{}. AAF stages it "
			"from here; Rapport keeps the faces and the aftermath",
			picked.scenario->id, chosen->positionID, chosen->stages,
			TreeIndex::Describe(chosen->ending),
			chosen->climaxTagged
				? ""
				: " that NO position in it is tagged for, so expect no climax face",
			chosen->LengthKnown() ? std::format(" and authored for {:.0f}s", chosen->seconds)
			                      : " of unrecorded length");
		return _chosenPosition;
	}

	// What an addon asks BEFORE it walks two actors across a room: would this
	// scenario play for this pair, and how well?
	//
	// Deliberately the same call the start makes, including its logging -- a
	// pre-flight that takes a cheaper shortcut is a pre-flight that says yes to
	// something the start then refuses.
	Scenarios::Quality Scenarios::Preflight(
		std::string_view a_id, std::uint32_t a_first, std::uint32_t a_second) const
	{
		NamedLock lock{ _lock, "scenarios" };

		const auto picked = SelectLocked(a_id, a_first, a_second, _avoidFurniture);
		if (!picked.scenario) {
			return Quality::kUnknownScenario;
		}
		if (picked.entry) {
			return picked.relaxed ? Quality::kNoGuaranteedEnding : Quality::kGood;
		}

		// A scenario that asks for no tree is not a failure -- quickie is exactly
		// that by design, and AAF picking freely is the intended behaviour. It is
		// simply a weaker promise than a chosen tree.
		return picked.unconstrained ? Quality::kUnconstrained : Quality::kNothingFits;
	}

	void Scenarios::ResolveFaceLocked(
		int a_intensity, std::string_view a_stageID, std::vector<Order>& a_out)
	{
		if (!_running) {
			return;
		}
		const auto act = Expressions::GetSingleton().LiveAct();
		const auto want = Expressions::FaceForAct(act, a_intensity);
		if (want.empty() || want == _faceApplied) {
			return;
		}

		logger::info(
			"scenario \"{}\": stage \"{}\" is at intensity {} and AAF is playing [{}] - face {}",
			_running->id, a_stageID, a_intensity, act, want);
		_faceApplied.assign(want);
		for (const auto formID : { _first, _second }) {
			if (formID != 0) {
				a_out.push_back(Order{ Order::Kind::kApplyExpression, formID, _faceApplied, {} });
			}
		}
	}

	bool Scenarios::OnATree() const
	{
		NamedLock lock{ _lock, "scenarios" };
		return _running != nullptr && _treeSteps > 0;
	}

	void Scenarios::NoteAnimationAdvanced()
	{
		std::vector<Order> outgoing;
		{
			NamedLock lock{ _lock, "scenarios" };
			if (!_running || _treeSteps == 0) {
				return;
			}

			// The entry animation is step 0 and arrives as one of these too, so the
			// count is of steps TAKEN and the first call leaves us on stage 0.
			const auto step = _stepsSeen;
			if (_stepsSeen < _treeSteps) {
				++_stepsSeen;
			}

			// Spread the scenario's stages over the tree's steps. A 5-stage scenario
			// on a 6-step tree gives the opening stage the extra one:
			//   step 0,1 -> prelude   2 -> oral   3 -> main   4 -> build   5 -> finish
			const auto count = static_cast<std::uint32_t>(_running->stages.size());
			const auto want = static_cast<std::size_t>(
				std::min<std::uint64_t>(count - 1, static_cast<std::uint64_t>(step) * count / _treeSteps));

			logger::info(
				"scenario \"{}\": AAF took tree step {} of {} - stage {} of {}",
				_running->id, step + 1, _treeSteps, want + 1, count);

			if (want > _stage || _stage >= _running->stages.size()) {
				EnterStage(want, outgoing);
			}

			// Immediately, on this same call. The act was updated a line before this
			// one in the native, so by the time we are here _liveAct is already the
			// animation AAF has just moved to -- including its climax tag. Waiting
			// for the next poll would put the face up to three seconds behind the
			// orgasm it is meant to land on.
			const auto at = std::min<std::size_t>(_stage, _running->stages.size() - 1);
			ResolveFaceLocked(_running->stages[at].intensity, _running->stages[at].id, outgoing);
		}
		PapyrusLink::GetSingleton().QueueOrders(outgoing);
	}

	void Scenarios::NoteSceneRefused()
	{
		NamedLock lock{ _lock, "scenarios" };
		if (_chosenPosition.empty() || _avoidFurniture) {
			return;
		}

		// Only when the tree we picked actually wanted a room feature. A refusal
		// for any other reason says nothing about the furniture and should not
		// narrow the catalogue.
		const auto& index = TreeIndex::GetSingleton();
		if (const auto* entry = index.Find(_chosenPosition);
			entry && TreeIndex::NeedsFurniture(*entry)) {
			_avoidFurniture = true;
			logger::warn(
				"scenarios: the scene would not start on \"{}\", which needs furniture that has to "
				"be there already - the next one will ask for none",
				_chosenPosition);
		}
	}

	void Scenarios::NoteSceneStarted()
	{
		NamedLock lock{ _lock, "scenarios" };
		_avoidFurniture = false;
	}

	float Scenarios::ChosenSeconds() const
	{
		NamedLock lock{ _lock, "scenarios" };
		return _chosenSeconds;
	}

	void Scenarios::Pump()
	{
		std::vector<Order> outgoing;
		{
			NamedLock lock{ _lock, "scenarios" };
			if (!_running) {
				return;
			}

			// PAST the last stage the scene is still running -- for a long time.
			// Measured: athome's stages finished at 128s, AAF's climax animation
			// began at 140s and the scene ended at 180s. Returning here, which is
			// what this did, abandoned the face for the whole ending, including the
			// orgasm. The story has no more stages to give, so the intensity is
			// pinned at its maximum and the act keeps driving the face.
			const bool past = _stage >= _running->stages.size();
			const int  intensity = past ? 3 : _running->stages[_stage].intensity;
			const std::string_view stageID = past ? "after the last"sv
			                                      : std::string_view{ _running->stages[_stage].id };

			// The face follows the ACT, and the act changes when AAF's tree steps --
			// which is inside a stage, not at its boundary. Resolving only on a
			// stage change is how an orgasm face arrived 63 seconds before the
			// orgasm animation and stayed on for the 103 seconds after it.
			//
			// Cheap: a tag split and a few comparisons, and it only queues an order
			// when the answer actually changed.
			ResolveFaceLocked(intensity, stageID, outgoing);

			// Only when nothing is stepping. A scene on a tree advances on AAF's
			// steps, which is exact; running the clock as well would race it and
			// win sometimes, putting the story ahead of the animation again.
			if (!past && _treeSteps == 0) {
				const auto elapsed = std::chrono::duration<float>{
					std::chrono::steady_clock::now() - _stageStartedAt
				}.count();
				if (elapsed >= _running->stages[_stage].seconds * _stageScale) {
					EnterStage(_stage + 1, outgoing);
				}
			}
		}
		PapyrusLink::GetSingleton().QueueOrders(outgoing);
	}

	void Scenarios::End()
	{
		NamedLock lock{ _lock, "scenarios" };
		if (_running) {
			logger::info("scenario \"{}\": over", _running->id);
		}
		_running = nullptr;
		_stage = 0;
		_option = 0;
		_first = 0;
		_second = 0;

		// The chosen tree dies with the scene. Leaving it behind meant a later,
		// unrelated failure asked "did the position we chose need furniture?" and
		// got the answer for the PREVIOUS scene -- banning furniture on the
		// evidence of a scene that had started perfectly.
		//
		// _avoidFurniture deliberately survives: it is cross-scene by design, and
		// NoteSceneStarted clears it.
		_chosenPosition.clear();
		_chosenSeconds = 0.0f;
		_faceApplied.clear();
		_treeSteps = 0;
		_stepsSeen = 0;
	}
}
