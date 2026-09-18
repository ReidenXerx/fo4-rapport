#include "Scenarios.h"

#include "PapyrusLink.h"
#include "Aftermath.h"
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

namespace
{
	// How long the pair is given to be walked somewhere else. AAF's walk was
	// measured at 12.5 seconds across an open market, and a restart adds its own
	// delay, so this is generous on purpose: giving up early would skip the stage
	// the move exists to make possible.
	constexpr float kMoveGraceSeconds = 45.0f;
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

	bool Scenarios::Running() const
	{
		NamedLock lock{ _lock, "scenarios" };
		return _running != nullptr;
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
			_movedThisScene = false;
			_awaitingMove = false;
			_first = a_first;
			_second = a_second;
			_stage = scenario->stages.size();   // EnterStage moves it to the first playable one

			logger::info(
				"scenario \"{}\": {:08X} and {:08X}, {:.0f}s over {} stage(s)",
				scenario->id, a_first, a_second, scenario->PlayableSeconds(), scenario->stages.size());

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
			// The whole point of the catalogue: pick the ending, do not hope for it.
			const auto& index = TreeIndex::GetSingleton();
			const auto composition = Aftermath::GetSingleton().CompositionOf(_first, _second);
			const auto* chosen = index.Choose(
				stage.include, stage.exclude, composition, stage.requireEnding, stage.seconds);

			if (chosen) {
				// Named for the log only. The scene was already STARTED on a tree
				// chosen the same way, and nothing can move it afterwards: naming a
				// position mid-scene is refused exactly like naming tags was.
				logger::info(
					"scenario \"{}\": stage \"{}\" - the ending it would pick here is \"{}\" ({} "
					"stages, {}). The scene is already on its tree and AAF is staging it",
					_running->id, stage.id, chosen->positionID, chosen->stages,
					TreeIndex::Describe(chosen->ending));
			} else {
				// Said out loud rather than silently behaving like a handover: "no
				// tree qualified" and "this stage never wanted one" look identical
				// from the outside, and only one of them is a content problem.
				logger::warn(
					"scenario \"{}\": stage \"{}\" wanted a {} tree ending in a climax matching "
					"[{}]{} and this install has none of the {} indexed. Letting the scene play "
					"on instead",
					_running->id, stage.id,
					composition.empty() ? "any-pair" : composition, stage.include,
					stage.exclude.empty() ? "" : std::format(" avoiding [{}]", stage.exclude),
					index.Size());
			}
		} else if (stage.handover) {
			logger::info(
				"scenario \"{}\": stage \"{}\" for {:.0f}s - handing the ending to AAF. No position "
				"change is asked for, so a position that declares a tree walks it to its own climax",
				_running->id, stage.id, stage.seconds);
		} else {
			logger::info(
				"scenario \"{}\": stage \"{}\" for {:.0f}s, any of [{}]{}",
				_running->id, stage.id, stage.seconds, stage.include,
				stage.exclude.empty() ? "" : std::format(" avoiding [{}]", stage.exclude));

			SendCurrentOption(a_out);
		}

		// The face belongs to the stage, not to a percentage of the clock.
		if (!stage.face.empty()) {
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

	void Scenarios::OnRefused(std::string_view a_why)
	{
		std::vector<Order> outgoing;
		{
			NamedLock lock{ _lock, "scenarios" };
			if (!_running || _stage >= _running->stages.size()) {
				return;
			}

			// A handover stage asked for nothing, so a refusal cannot be about it.
			// Something else in the scene was refused; advancing the option counter
			// here would skip the handover and end the story early.
			if (_running->stages[_stage].tree) {
				logger::warn(
					"scenario \"{}\": AAF refused the tree chosen for stage \"{}\" ({}) - letting "
					"the scene play on rather than starting over",
					_running->id, _running->stages[_stage].id, a_why);
				return;
			}

			if (_running->stages[_stage].handover) {
				logger::info(
					"scenario \"{}\": AAF refused something during the handover stage \"{}\" ({}) - "
					"ignoring it, this stage asked for nothing",
					_running->id, _running->stages[_stage].id, a_why);
				return;
			}

			const auto& stage = _running->stages[_stage];
			++_option;

			if (_option < stage.options.size()) {
				logger::info(
					"scenario \"{}\": AAF refused \"{}\" for stage \"{}\" - trying the next one ({})",
					_running->id, stage.options[_option - 1], stage.id, a_why);
				SendCurrentOption(outgoing);
			} else if (!_movedThisScene) {
				// Where they ARE is the problem, not the stage. AAF's ChangePosition
				// cannot leave the furniture a scene started on, so a desk with one
				// missionary animation on it refuses everything a prelude asks for.
				// Rather than skip the stage, the pair gets up and carries on
				// somewhere without furniture -- which is where the variety is.
				_movedThisScene = true;
				_awaitingMove = true;
				_moveRequestedAt = std::chrono::steady_clock::now();
				logger::info(
					"scenario \"{}\": nothing in stage \"{}\" works WHERE THEY ARE ({}) - moving "
					"them somewhere without furniture and trying this stage again",
					_running->id, stage.id, a_why);
				PapyrusLink::GetSingleton().BeginRelocation();
				outgoing.push_back(Order{ Order::Kind::kRelocate, _first, {}, {} });
			} else {
				logger::warn(
					"scenario \"{}\": nothing in stage \"{}\" works for this pair here, and they "
					"have already moved once this scene - skipping it ({})",
					_running->id, stage.id, a_why);
				EnterStage(_stage + 1, outgoing);
			}
		}
		PapyrusLink::GetSingleton().QueueOrders(outgoing);
	}

	// The scene came back somewhere else. Re-run the stage that could not be
	// filled, from its first option: the old refusals were about the old place.
	void Scenarios::OnRelocated()
	{
		std::vector<Order> outgoing;
		{
			NamedLock lock{ _lock, "scenarios" };
			if (!_running || _stage >= _running->stages.size()) {
				return;
			}
			logger::info(
				"scenario \"{}\": they have moved - trying stage \"{}\" again from the top",
				_running->id, _running->stages[_stage].id);
			_awaitingMove = false;
			_option = 0;
			_stageStartedAt = std::chrono::steady_clock::now();
			SendCurrentOption(outgoing);
		}
		PapyrusLink::GetSingleton().QueueOrders(outgoing);
	}

	std::string Scenarios::ChooseSceneStart(
		std::string_view a_id, std::uint32_t a_first, std::uint32_t a_second)
	{
		NamedLock lock{ _lock, "scenarios" };

		_chosenPosition.clear();
		_chosenSeconds = 0.0f;

		const auto scenario = Find(a_id);
		if (!scenario) {
			return {};
		}

		// The LAST stage describes the ending this scenario wants, and the ending
		// is what the whole tree is chosen for: the pack author staged everything
		// before it to arrive there.
		const Stage* ending = nullptr;
		for (auto it = scenario->stages.rbegin(); it != scenario->stages.rend(); ++it) {
			if (!it->include.empty()) {
				ending = &*it;
				break;
			}
		}
		if (!ending) {
			return {};
		}

		const auto& index = TreeIndex::GetSingleton();
		if (!index.Usable()) {
			logger::warn(
				"scenario \"{}\": no tree catalogue, so the scene starts unconstrained and its "
				"ending is whatever AAF picks",
				scenario->id);
			return {};
		}

		const auto  composition = Aftermath::GetSingleton().CompositionOf(a_first, a_second);
		const auto* chosen = index.Choose(
			ending->include, ending->exclude, composition, ending->requireEnding, ending->seconds);

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
			return {};
		}

		_chosenPosition = chosen->positionID;
		_chosenSeconds = chosen->seconds;

		logger::info(
			"scenario \"{}\": starting on \"{}\" - a {}-stage tree ending in a {}{}. AAF stages it "
			"from here; Rapport keeps the faces and the aftermath",
			scenario->id, chosen->positionID, chosen->stages,
			TreeIndex::Describe(chosen->ending),
			chosen->LengthKnown() ? std::format(" and authored for {:.0f}s", chosen->seconds)
			                      : " of unrecorded length");
		return _chosenPosition;
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
			if (!_running || _stage >= _running->stages.size()) {
				return;
			}

			// Stopped while they walk. The stage this move was made for has not had
			// its chance yet, and counting through the walk would step over it.
			if (_awaitingMove) {
				const auto waiting = std::chrono::duration<float>{
					std::chrono::steady_clock::now() - _moveRequestedAt
				}.count();
				if (waiting < kMoveGraceSeconds) {
					return;
				}

				// It never came back. Give the flag up rather than stalling the
				// story here forever, and give up the plugin's one too -- left set,
				// it reads every later scene end as a move and nothing is recorded.
				logger::warn(
					"scenario \"{}\": the move was asked for {:.0f}s ago and no scene has started "
					"- carrying on without it",
					_running->id, waiting);
				_awaitingMove = false;
				PapyrusLink::GetSingleton().CancelRelocation();
				EnterStage(_stage + 1, outgoing);
				return;
			}

			const auto elapsed = std::chrono::duration<float>{
				std::chrono::steady_clock::now() - _stageStartedAt
			}.count();
			if (elapsed < _running->stages[_stage].seconds) {
				return;
			}

			EnterStage(_stage + 1, outgoing);
		}
		PapyrusLink::GetSingleton().QueueOrders(outgoing);
	}

	void Scenarios::End()
	{
		NamedLock lock{ _lock, "scenarios" };
		if (_running) {
			logger::info("scenario \"{}\": over", _running->id);
		}
		if (_awaitingMove) {
			_awaitingMove = false;
			PapyrusLink::GetSingleton().CancelRelocation();
		}
		_running = nullptr;
		_stage = 0;
		_first = 0;
		_second = 0;
	}
}
