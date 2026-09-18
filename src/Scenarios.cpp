#include "Scenarios.h"

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
				if (stage.id.empty() || stage.include.empty()) {
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
				stage.playable = AnyContentFor(stage.include);
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

		logger::info(
			"scenario \"{}\": stage \"{}\" for {:.0f}s, any of [{}]{}",
			_running->id, stage.id, stage.seconds, stage.include,
			stage.exclude.empty() ? "" : std::format(" avoiding [{}]", stage.exclude));

		SendCurrentOption(a_out);

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

		logger::info(
			"scenario \"{}\": trying \"{}\" for stage \"{}\" ({} of {})",
			_running->id, stage.options[_option], stage.id, _option + 1, stage.options.size());
		a_out.push_back(
			Order{ Order::Kind::kChangePosition, _first, stage.options[_option], stage.exclude });
	}

	void Scenarios::OnRefused(std::string_view a_why)
	{
		std::vector<Order> outgoing;
		{
			NamedLock lock{ _lock, "scenarios" };
			if (!_running || _stage >= _running->stages.size()) {
				return;
			}

			const auto& stage = _running->stages[_stage];
			++_option;

			if (_option < stage.options.size()) {
				logger::info(
					"scenario \"{}\": AAF refused \"{}\" for stage \"{}\" - trying the next one ({})",
					_running->id, stage.options[_option - 1], stage.id, a_why);
				SendCurrentOption(outgoing);
			} else {
				logger::warn(
					"scenario \"{}\": nothing in stage \"{}\" works for this pair here - moving on ({})",
					_running->id, stage.id, a_why);
				EnterStage(_stage + 1, outgoing);
			}
		}
		PapyrusLink::GetSingleton().QueueOrders(outgoing);
	}

	void Scenarios::Pump()
	{
		std::vector<Order> outgoing;
		{
			NamedLock lock{ _lock, "scenarios" };
			if (!_running || _stage >= _running->stages.size()) {
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
		_running = nullptr;
		_stage = 0;
		_first = 0;
		_second = 0;
	}
}
