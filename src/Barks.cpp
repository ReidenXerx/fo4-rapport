#include "Barks.h"

#include "Aftermath.h"
#include "McmSettings.h"
#include "Traits.h"
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

		// Hand-kept, never generated: build-barks-table.py rewrites barks.json, and an
		// owner's choice of who Ivy IS must not vanish the next time lines are added.
		std::filesystem::path OverridePath()
		{
			return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "personas.json";
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

		McmSettings::Overlay("Barks", document);
		_responderDelay = document.value("responderDelaySeconds", 4.5f);
		if (const auto personas = document.find("personas"); personas != document.end() && personas->is_array()) {
			for (const auto& persona : *personas) {
				_personas.push_back(persona.get<std::string>());
			}
		}

		if (const auto lines = document.find("lines"); lines != document.end() && lines->is_array()) {
			for (const auto& entry : *lines) {
				// Both kinds: pair lines carry a scenario and a role, observer lines
				// (R-12) an audience. Each must have its own fields or it is skipped.
				const auto kind = entry.value("kind", std::string{});
				if (kind != "pair" && kind != "observer") {
					continue;
				}
				Line line;
				line.id = entry.value("id", std::string{});
				line.observer = kind == "observer";
				line.persona = entry.value("persona", std::string{});
				line.scenario = entry.value("scenario", std::string{});
				line.role = entry.value("role", std::string{});
				line.audience = entry.value("audience", std::string{});
				line.sight = entry.value("sight", false);
				line.topic = entry.value("topic", 0u);
				const auto gender = entry.value("gender", std::string{});
				line.gender = gender.empty() ? 0 : gender.front();
				const bool shaped = line.observer ? !line.audience.empty()
				                                  : !line.scenario.empty() && !line.role.empty();
				if (line.topic == 0 || line.persona.empty() || !shaped) {
					logger::warn("barks: line \"{}\" is missing a field - skipped", line.id);
					continue;
				}
				_lines.push_back(std::move(line));
			}
		}

		LoadOverrides();

		if (_personas.empty() || _lines.empty()) {
			logger::warn(
				"barks: {} persona(s) and {} pair line(s) read - nothing could be said, so barks are off",
				_personas.size(), _lines.size());
			return;
		}

		_enabled = McmSettings::ReadBool(document, "enabled", true);
		const auto observers = std::ranges::count_if(_lines, [](const Line& a_line) { return a_line.observer; });
		logger::info("barks: {} pair + {} observer lines across {} personas, responder answers after {:.1f}s{}",
			_lines.size() - observers, observers, _personas.size(), _responderDelay,
			_enabled ? "" : " - but switched OFF in barks.json");
	}

	void Barks::LoadOverrides()
	{
		// R-13: personas are DERIVED, and the owner can pin one by hand. Keyed by
		// plugin + file-relative id, so the pin survives a load-order change, and
		// matched against the actor's reference OR its base NPC: a unique NPC has one
		// of each, and whichever the owner looked up in xEdit has to work.
		_overrides.clear();
		std::ifstream file{ OverridePath() };
		if (!file) {
			return;
		}
		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			logger::error("barks: {} is not valid json ({}) - no persona is pinned", OverridePath().string(), e.what());
			return;
		}
		const auto handler = RE::TESDataHandler::GetSingleton();
		const auto list = document.find("overrides");
		if (!handler || list == document.end() || !list->is_array()) {
			return;
		}
		for (const auto& entry : *list) {
			// One bad entry is skipped, never thrown: this file is hand-edited, and an
			// exception here would cross into F4SE at data ready and take the game down.
			try {
			const auto plugin = entry.value("plugin", std::string{});
			const auto persona = entry.value("persona", std::string{});
			std::uint32_t id = 0;
			if (const auto raw = entry.find("id"); raw != entry.end()) {
				id = raw->is_string() ? static_cast<std::uint32_t>(std::stoul(raw->get<std::string>(), nullptr, 16))
				                      : raw->get<std::uint32_t>();
			}
			if (std::ranges::find(_personas, persona) == _personas.end()) {
				logger::warn("barks: persona pin for {} {:06X} names \"{}\", which is not a persona - skipped", plugin, id,
					persona);
				continue;
			}
			const auto* form = handler->LookupForm(id, plugin);
			if (!form) {
				// A mod this player does not have: normal, not an error.
				logger::debug("barks: persona pin {} {:06X} is not loaded - skipped", plugin, id);
				continue;
			}
			// Only a person can have a persona. 2026-09-21 the first pin named Ivy's
			// VOICE TYPE (000801, the id next door in the voice overrides) and was
			// "pinned" without a word while Ivy stayed what the hash made her.
			if (!form->As<RE::Actor>() && !form->As<RE::TESNPC>()) {
				logger::warn("barks: persona pin {} {:06X} is not an actor or NPC (form type {}) - skipped", plugin, id,
					static_cast<int>(form->GetFormType()));
				continue;
			}
			_overrides[form->GetFormID()] = persona;
			logger::info("barks: {:08X} ({} {:06X}) is pinned to the {} persona", form->GetFormID(), plugin, id, persona);
			} catch (const std::exception& e) {
				logger::warn("barks: a persona pin in personas.json is malformed ({}) - skipped", e.what());
			}
		}
	}

	std::string Barks::PersonaOf(std::uint32_t a_formID) const
	{
		NamedLock lock{ _lock, "barks" };
		if (_personas.empty()) {
			return {};
		}
		if (!_overrides.empty()) {
			if (const auto it = _overrides.find(a_formID); it != _overrides.end()) {
				return it->second;
			}
			const auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_formID);
			const auto* npc = actor ? actor->GetNPC() : nullptr;
			if (npc) {
				if (const auto it = _overrides.find(npc->GetFormID()); it != _overrides.end()) {
					return it->second;
				}
			}
		}
		// Mixed before the modulo, deliberately. Expressions already styles faces
		// by formID % 6, and a plain formID % 4 shares a factor of two with that --
		// every even-styled face would lean toward two of the four personas. A
		// multiplicative hash keeps the two derivations independent while staying
		// just as stable: same id, same persona, forever.
		const auto mixed = static_cast<std::uint32_t>(Traits::StableID(a_formID) * 2654435761u) >> 16;
		return _personas[mixed % _personas.size()];
	}

	const Barks::Line* Barks::Pick(
		std::string_view a_persona, std::string_view a_scenario, std::string_view a_role, std::int32_t a_sex)
	{
		// Caller holds _lock.
		const char sex = a_sex == 0 ? 'm' : a_sex == 1 ? 'f' : 0;

		std::vector<const Line*> fits;
		for (const auto& line : _lines) {
			if (line.observer || line.persona != a_persona || line.scenario != a_scenario || line.role != a_role) {
				continue;
			}
			// A gendered line names the speaker's own body. An unknown sex gets
			// only the lines that do not, rather than a coin flip on anatomy.
			if (line.gender != 0 && line.gender != sex) {
				continue;
			}
			fits.push_back(&line);
		}
		return Choose(std::move(fits), std::format("{}|{}|{}", a_persona, a_scenario, a_role));
	}

	const Barks::Line* Barks::Choose(std::vector<const Line*> a_fits, const std::string& a_key)
	{
		if (a_fits.empty()) {
			return nullptr;
		}
		const auto& last = _lastPicked[a_key];
		if (a_fits.size() > 1 && !last.empty()) {
			std::erase_if(a_fits, [&](const Line* a_line) { return a_line->id == last; });
		}
		const auto* chosen = a_fits[std::uniform_int_distribution<std::size_t>{ 0, a_fits.size() - 1 }(_rng)];
		_lastPicked[a_key] = chosen->id;
		return chosen;
	}

	std::pair<std::uint32_t, std::string> Barks::PickObserver(
		std::string_view a_persona, std::string_view a_audience, std::int32_t a_sex, bool a_heardOnly)
	{
		NamedLock lock{ _lock, "barks" };
		if (!_enabled) {
			return {};
		}
		const char               sex = a_sex == 0 ? 'm' : a_sex == 1 ? 'f' : 0;
		std::vector<const Line*> fits;
		for (const auto& line : _lines) {
			if (line.observer && line.persona == a_persona && line.audience == a_audience &&
				(line.gender == 0 || line.gender == sex) && !(a_heardOnly && line.sight)) {
				fits.push_back(&line);
			}
		}
		const auto* chosen = Choose(std::move(fits),
			std::format("observer|{}|{}|{}", a_persona, a_audience, a_heardOnly ? "heard" : "seen"));
		return chosen ? std::pair{ chosen->topic, chosen->id } : std::pair<std::uint32_t, std::string>{};
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

		// The PLAYER never barks. R-11: the player has no persona, and a persona
		// hashed from 0x14 would put an NPC's line in their mouth. Overture's O-2/O-3:
		// the player's side is text they chose, never a voice picked for them. The
		// NPC keeps its own role's line: it answers after a beat when the player
		// opened, and opens itself when the player is the one who answers.
		constexpr std::uint32_t kPlayer = 0x14;
		const bool playerOpens = a_initiator == kPlayer;
		const bool playerAnswers = a_responder == kPlayer;

		// Sexes first, outside our lock: Aftermath takes its own.
		const auto initiatorSex = Aftermath::GetSingleton().SexOf(a_initiator);
		const auto responderSex = Aftermath::GetSingleton().SexOf(a_responder);
		const auto initiatorPersona = playerOpens ? std::string{} : std::string{ PersonaOf(a_initiator) };
		const auto responderPersona = playerAnswers ? std::string{} : std::string{ PersonaOf(a_responder) };

		std::uint32_t opening = 0;
		std::string   openingID;
		bool          answers = false;
		{
			NamedLock lock{ _lock, "barks" };
			_reply.reset();
			if (!_enabled) {
				return;
			}

			const auto* first = playerOpens ? nullptr : Pick(initiatorPersona, a_scenario, "initiator", initiatorSex);
			const auto* second = playerAnswers ? nullptr : Pick(responderPersona, a_scenario, "responder", responderSex);
			if ((!first && !playerOpens) || (!second && !playerAnswers)) {
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
			a_initiator, playerOpens ? "the player, silent" : initiatorPersona, openingID.empty() ? "nothing" : openingID,
			a_responder, playerAnswers ? "the player, silent" : responderPersona, answers ? "after a beat" : "with nothing");
		if (opening != 0) {
			Voices::GetSingleton().Speak(a_initiator, a_responder, opening);
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
		Voices::GetSingleton().Speak(due->speaker, due->target, due->topic);
	}
}
