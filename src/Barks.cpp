#include "Barks.h"

#include "Aftermath.h"
#include "PapyrusLink.h"
#include "Voices.h"

namespace RP
{
	namespace
	{
		std::filesystem::path ConfigPath()
		{
			return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "barks.json";
		}
	}

	Barks& Barks::GetSingleton() noexcept
	{
		static Barks singleton;
		return singleton;
	}

	void Barks::Load()
	{
		NamedLock lock{ _lock, "barks" };
		_lines.clear();
		_personas.clear();
		_enabled = false;

		const auto    path = ConfigPath();
		std::ifstream file{ path };
		if (!file) {
			logger::warn("barks: no {} - nobody will speak during scenes", path.string());
			return;
		}

		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			logger::error("barks: {} is not valid json ({}) - barks are off", path.string(), e.what());
			return;
		}

		_responderDelay = document.value("responderDelaySeconds", 4.5f);
		if (const auto personas = document.find("personas"); personas != document.end() && personas->is_array()) {
			for (const auto& persona : *personas) {
				_personas.push_back(persona.get<std::string>());
			}
		}

		if (const auto lines = document.find("lines"); lines != document.end() && lines->is_array()) {
			for (const auto& entry : *lines) {
				// Observer lines are in the same table for R-12 and are skipped here
				// until that lands: they carry an audience, not a scenario and role.
				if (entry.value("kind", std::string{}) != "pair") {
					continue;
				}
				Line line;
				line.id = entry.value("id", std::string{});
				line.persona = entry.value("persona", std::string{});
				line.scenario = entry.value("scenario", std::string{});
				line.role = entry.value("role", std::string{});
				line.topic = entry.value("topic", 0u);
				const auto gender = entry.value("gender", std::string{});
				line.gender = gender.empty() ? 0 : gender.front();
				if (line.topic == 0 || line.persona.empty() || line.scenario.empty() || line.role.empty()) {
					logger::warn("barks: line \"{}\" is missing a field - skipped", line.id);
					continue;
				}
				_lines.push_back(std::move(line));
			}
		}

		if (_personas.empty() || _lines.empty()) {
			logger::warn(
				"barks: {} persona(s) and {} pair line(s) read - nothing could be said, so barks are off",
				_personas.size(), _lines.size());
			return;
		}

		_enabled = document.value("enabled", true);
		logger::info("barks: {} pair lines across {} personas, responder answers after {:.1f}s{}",
			_lines.size(), _personas.size(), _responderDelay, _enabled ? "" : " - but switched OFF in barks.json");
	}

	std::string_view Barks::PersonaOf(std::uint32_t a_formID) const
	{
		NamedLock lock{ _lock, "barks" };
		if (_personas.empty()) {
			return {};
		}
		// Mixed before the modulo, deliberately. Expressions already styles faces
		// by formID % 6, and a plain formID % 4 shares a factor of two with that --
		// every even-styled face would lean toward two of the four personas. A
		// multiplicative hash keeps the two derivations independent while staying
		// just as stable: same id, same persona, forever.
		const auto mixed = static_cast<std::uint32_t>(a_formID * 2654435761u) >> 16;
		return _personas[mixed % _personas.size()];
	}

	const Barks::Line* Barks::Pick(
		std::string_view a_persona, std::string_view a_scenario, std::string_view a_role, std::int32_t a_sex)
	{
		// Caller holds _lock.
		const char sex = a_sex == 0 ? 'm' : a_sex == 1 ? 'f' : 0;

		std::vector<const Line*> fits;
		for (const auto& line : _lines) {
			if (line.persona != a_persona || line.scenario != a_scenario || line.role != a_role) {
				continue;
			}
			// A gendered line names the speaker's own body. An unknown sex gets
			// only the lines that do not, rather than a coin flip on anatomy.
			if (line.gender != 0 && line.gender != sex) {
				continue;
			}
			fits.push_back(&line);
		}
		if (fits.empty()) {
			return nullptr;
		}

		const auto key = std::format("{}|{}|{}", a_persona, a_scenario, a_role);
		const auto& last = _lastPicked[key];
		if (fits.size() > 1 && !last.empty()) {
			std::erase_if(fits, [&](const Line* a_line) { return a_line->id == last; });
		}

		const auto* chosen = fits[std::uniform_int_distribution<std::size_t>{ 0, fits.size() - 1 }(_rng)];
		_lastPicked[key] = chosen->id;
		return chosen;
	}

	void Barks::Say(std::uint32_t a_speaker, std::uint32_t a_target, std::uint32_t a_topic)
	{
		// Not under _lock: QueueOrder takes the order lock, and a lock taken while
		// holding another is the cycle this codebase keeps its lock rules for.
		Order order{ Order::Kind::kSayTopic, a_speaker, std::to_string(a_topic), std::to_string(a_target) };
		// Which VOICE says it is not a bark decision -- it is the framework's, and
		// every speaker of a Rapport line gets the same answer (V-25).
		order.voice = Voices::GetSingleton().BorrowFor(a_speaker);
		if (order.voice != 0) {
			logger::info("bark: {:08X} has a voice we did not render - borrowing {:08X} for this line",
				a_speaker, order.voice);
		}
		PapyrusLink::GetSingleton().QueueOrder(std::move(order));
	}

	void Barks::OnSceneStarted(
		std::int32_t a_request, std::uint32_t a_initiator, std::uint32_t a_responder, std::string_view a_scenario)
	{
		if (a_initiator == 0 || a_responder == 0) {
			return;
		}
		if (a_scenario.empty()) {
			// The bank is written per scenario. A single animation with no story
			// has no line that fits it, and a line from the wrong story is worse
			// than none.
			logger::info("request {}: no scenario, so no barks", a_request);
			return;
		}

		// Sexes first, outside our lock: Aftermath takes its own.
		const auto initiatorSex = Aftermath::GetSingleton().SexOf(a_initiator);
		const auto responderSex = Aftermath::GetSingleton().SexOf(a_responder);
		const auto initiatorPersona = std::string{ PersonaOf(a_initiator) };
		const auto responderPersona = std::string{ PersonaOf(a_responder) };

		std::uint32_t opening = 0;
		std::string   openingID;
		bool          answers = false;
		{
			NamedLock lock{ _lock, "barks" };
			_reply.reset();
			if (!_enabled) {
				return;
			}

			const auto* first = Pick(initiatorPersona, a_scenario, "initiator", initiatorSex);
			const auto* second = Pick(responderPersona, a_scenario, "responder", responderSex);
			if (!first || !second) {
				logger::warn(
					"request {}: no {} line for {} {} ({}) in scenario {} - the table does not cover it",
					a_request, first ? "responder" : "initiator",
					first ? responderPersona : initiatorPersona,
					first ? "responder" : "initiator",
					first ? responderSex : initiatorSex, a_scenario);
			}
			if (first) {
				opening = first->topic;
				openingID = first->id;
			}
			if (second) {
				answers = true;
				_reply = Reply{ a_request, a_responder, a_initiator, second->topic, second->id,
					std::chrono::steady_clock::now() +
						std::chrono::milliseconds{ static_cast<int>(_responderDelay * 1000.0f) } };
			}
		}

		logger::info("request {}: bark - {:08X} ({}) opens with {}, {:08X} ({}) answers {}", a_request,
			a_initiator, initiatorPersona, openingID.empty() ? "nothing" : openingID,
			a_responder, responderPersona, answers ? "after a beat" : "with nothing");
		if (opening != 0) {
			Say(a_initiator, a_responder, opening);
		}
	}

	void Barks::OnSceneEnded()
	{
		NamedLock lock{ _lock, "barks" };
		if (_reply) {
			logger::info("request {}: scene ended before {:08X} answered - {} dropped",
				_reply->request, _reply->speaker, _reply->id);
			_reply.reset();
		}
	}

	void Barks::Pump()
	{
		std::optional<Reply> due;
		{
			NamedLock lock{ _lock, "barks" };
			if (!_reply || std::chrono::steady_clock::now() < _reply->due) {
				return;
			}
			due = std::move(_reply);
			_reply.reset();
		}

		// The end paths cancel a reply, and this is the backstop for one that
		// did not: a reply whose request is no longer the running one belongs to
		// a scene that is gone.
		if (PapyrusLink::GetSingleton().RunningRequest() != due->request) {
			logger::info("request {}: no longer running - {:08X}'s reply {} dropped",
				due->request, due->speaker, due->id);
			return;
		}

		logger::info("request {}: bark - {:08X} answers with {}", due->request, due->speaker, due->id);
		Say(due->speaker, due->target, due->topic);
	}
}
