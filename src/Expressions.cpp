#include "Expressions.h"

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
		_dazing = false;
		_nextStep = 0;
		_tags.clear();
		_sawSexTag = false;

		logger::info("expressions: driving {:08X} and {:08X} over {:.0f}s", a_first, a_second, _duration);
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
	}

	void Expressions::Collect(std::string_view a_setID, std::vector<Order>& a_out)
	{
		for (const auto formID : { _first, _second }) {
			if (formID == 0) {
				continue;
			}
			a_out.push_back(Order{ Order::Kind::kApplyExpression, formID, std::string{ a_setID } });

			if (std::ranges::find(_wearing, formID) == _wearing.end()) {
				_wearing.push_back(formID);
			}
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

			if (_running) {
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
		}
		logger::info("expressions: clearing {} face(s) - {}", _wearing.size(), a_why);
		_wearing.clear();
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
