#include "Watchers.h"

#include "Barks.h"
#include "Voices.h"

namespace RP
{
	namespace
	{
		std::filesystem::path ConfigPath()
		{
			return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "barks.json";
		}

		// The same hard filters ActorScan applies to candidates, plus the ones a
		// SPEAKER needs. Called outside every lock this service holds.
		bool Eligible(std::uint32_t a_id)
		{
			const auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_id);
			if (!actor || actor == RE::PlayerCharacter::GetSingleton() || actor->IsChild() || actor->IsDead(true) || actor->IsInCombat()) {
				return false;
			}
			return Voices::GetSingleton().CanSpeak(a_id);
		}
	}

	Watchers& Watchers::GetSingleton() noexcept
	{
		static Watchers singleton;
		return singleton;
	}

	void Watchers::Load()
	{
		NamedLock lock{ _lock, "watchers" };
		_enabled = false;

		std::ifstream  file{ ConfigPath() };
		nlohmann::json document;
		try {
			if (file) {
				file >> document;
			}
		} catch (const std::exception& e) {
			logger::error("watchers: barks.json is not valid json ({}) - no observer reactions", e.what());
			return;
		}
		const auto settings = document.value("observers", nlohmann::json::object());
		_enabled = settings.value("enabled", false);
		_radius = settings.value("radius", 900.0f);
		_chance = std::clamp(settings.value("chance", 0.33f), 0.0f, 1.0f);
		_startAfter = settings.value("startAfterSeconds", 10.0f);
		_gap = settings.value("gapSeconds", 6.0f);
		_cooldown = settings.value("cooldownSeconds", 300.0f);
		logger::info(
			"watchers: {} - radius {:.0f}, chance {:.0f}%, from {:.0f}s into a scene, {:.0f}s between lines, "
			"{:.0f}s per-actor cooldown",
			_enabled ? "on" : "OFF", _radius, _chance * 100.0f, _startAfter, _gap, _cooldown);
	}

	void Watchers::OnSceneStarted(std::int32_t a_request, std::uint32_t a_first, std::uint32_t a_second)
	{
		NamedLock lock{ _lock, "watchers" };
		_request = a_request;
		_first = a_first;
		_second = a_second;
		_startedAt = Clock::now();
		_rolled.clear();
		_sweep.clear();
	}

	void Watchers::OnSceneEnded()
	{
		NamedLock lock{ _lock, "watchers" };
		if (_request != 0) {
			logger::info("request {}: watchers - {} bystander(s) rolled this scene", _request, _rolled.size());
		}
		_request = 0;
		_first = _second = 0;
		_rolled.clear();
		_sweep.clear();
	}

	float Watchers::SweepRadius() const
	{
		NamedLock lock{ _lock, "watchers" };
		if (!_enabled || _request == 0) {
			return 0.0f;
		}
		const auto elapsed = std::chrono::duration<float>{ Clock::now() - _startedAt }.count();
		return elapsed < _startAfter ? 0.0f : _radius;
	}

	std::uint32_t Watchers::SweepFirst() const
	{
		NamedLock lock{ _lock, "watchers" };
		return _first;
	}

	std::uint32_t Watchers::SweepSecond() const
	{
		NamedLock lock{ _lock, "watchers" };
		return _second;
	}

	void Watchers::Note(std::uint32_t a_actor, bool a_sees)
	{
		NamedLock lock{ _lock, "watchers" };
		if (_request != 0 && a_actor != _first && a_actor != _second) {
			_sweep.emplace_back(a_actor, a_sees);
		}
	}

	void Watchers::EndSweep()
	{
		// 1. Snapshot under the lock.
		std::vector<std::pair<std::uint32_t, bool>> sweep;
		std::int32_t                                request = 0;
		std::uint32_t                               first = 0;
		{
			NamedLock lock{ _lock, "watchers" };
			sweep.swap(_sweep);
			request = _request;
			first = _first;
		}
		if (request == 0 || sweep.empty()) {
			return;
		}

		// 2. Judge outside it: these ask Voices and the engine, and Voices has its
		// own lock. Only watchers who SEE are eligible to be rolled at all.
		std::vector<std::uint32_t> seeing;
		for (const auto& [id, sees] : sweep) {
			if (sees && Eligible(id)) {
				seeing.push_back(id);
			}
		}
		if (seeing.empty()) {
			return;
		}
		const std::string_view audience = seeing.size() >= 2 ? "crowd" : "alone";

		// 3. Commit under the lock: who is rolled now, and who (if anyone) speaks.
		std::uint32_t speaker = 0;
		{
			NamedLock lock{ _lock, "watchers" };
			if (_request != request) {
				return;   // the scene ended while we were judging
			}
			const auto now = Clock::now();
			const bool gapOK = std::chrono::duration<float>{ now - _lastLine }.count() >= _gap;
			for (const auto id : seeing) {
				if (!_rolled.insert(id).second) {
					continue;   // already had their one roll this scene
				}
				if (const auto spoke = _spokeAt.find(id);
					spoke != _spokeAt.end() && std::chrono::duration<float>{ now - spoke->second }.count() < _cooldown) {
					logger::info("request {}: watcher {:08X} sees it - on cooldown, no roll", request, id);
					continue;
				}
				const bool wins = std::uniform_real_distribution<float>{ 0.0f, 1.0f }(_rng) < _chance;
				logger::info("request {}: watcher {:08X} sees it ({}) - rolled {}", request, id, audience,
					wins ? "a line" : "silence");
				// One line per sweep, and none inside the gap. A second winner in the
				// same sweep has still spent their roll: the rule is one roll per
				// scene, and a win that could not be voiced is a roll like any other.
				if (wins && speaker == 0 && gapOK) {
					speaker = id;
				}
			}
			if (speaker != 0) {
				_lastLine = now;
				_spokeAt[speaker] = now;
			}
		}
		if (speaker == 0) {
			return;
		}

		// 4. Speak, outside every lock. The persona is the watcher's own.
		const auto* actor = RE::TESForm::GetFormByID<RE::Actor>(speaker);
		auto*       npc = actor ? actor->GetNPC() : nullptr;
		const auto  sex = npc ? static_cast<std::int32_t>(npc->GetSex()) : -1;
		const auto  persona = std::string{ Barks::GetSingleton().PersonaOf(speaker) };
		const auto [topic, id] = Barks::GetSingleton().PickObserver(persona, audience, sex);
		if (topic == 0) {
			logger::warn("request {}: watcher {:08X} won a line but the bank has none for {} / {}", request,
				speaker, persona, audience);
			return;
		}
		logger::info("request {}: watcher {:08X} ({}, {}) says {}", request, speaker, persona, audience, id);
		Voices::GetSingleton().Speak(speaker, first, topic);
	}
}
