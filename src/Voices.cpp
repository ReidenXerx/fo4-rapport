#include "Voices.h"

#include "PapyrusLink.h"

namespace RP
{
	namespace
	{
		std::filesystem::path ConfigPath()
		{
			return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "voices.json";
		}
	}

	Voices& Voices::GetSingleton() noexcept
	{
		static Voices singleton;
		return singleton;
	}

	std::uint32_t Voices::Resolve(const nlohmann::json& a_ref, std::string_view a_what)
	{
		if (!a_ref.is_object()) {
			return 0;
		}
		const auto master = a_ref.value("master", std::string{});
		const auto id = a_ref.value("id", 0u);
		const auto handler = RE::TESDataHandler::GetSingleton();
		const auto* voice = handler ? handler->LookupForm<RE::BGSVoiceType>(id, master) : nullptr;
		if (!voice) {
			// A missing master (a DLC this player does not own) is normal, not an
			// error: the voices it would have mapped simply never appear.
			logger::debug("voices: {} {:06X} in {} is not loaded - skipped", a_what, id, master);
			return 0;
		}
		return voice->GetFormID();
	}

	void Voices::Load()
	{
		NamedLock lock{ _lock, "voices" };
		_own.clear();
		_borrow.clear();
		_enabled = false;

		const auto    path = ConfigPath();
		std::ifstream file{ path };
		if (!file) {
			logger::warn("voices: no {} - unique voices will be silent with a subtitle", path.string());
			return;
		}
		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			logger::error("voices: {} is not valid json ({}) - no voice fallback", path.string(), e.what());
			return;
		}

		for (const auto& entry : document.value("own", nlohmann::json::array())) {
			if (const auto id = Resolve(entry, "own voice")) {
				_own.insert(id);
			}
		}

		std::size_t silent = 0;
		for (const auto& entry : document.value("borrow", nlohmann::json::array())) {
			const auto from = Resolve(entry, "unique voice");
			if (from == 0) {
				continue;
			}
			// "as": null is a deliberate silence -- measured too far from anything
			// we have, or vetoed by the owner. Recorded, so it is not treated as
			// unmapped and handed the sex default below.
			const bool vetoed = entry.contains("as") && entry["as"].is_null();
			const auto as = entry.contains("as") && !vetoed ? Resolve(entry["as"], "borrowed voice") : 0u;
			if (as == 0 && !vetoed) {
				// The voice it borrows is from a DLC this player does not own. That is
				// not the owner's silence: leave it unmapped so the sex default speaks.
				continue;
			}
			_borrow[from] = as;
			silent += as == 0 ? 1 : 0;
		}

		_speakingRaces.clear();
		for (const auto& entry : document.value("speakingRaces", nlohmann::json::array())) {
			const auto master = entry.value("master", std::string{});
			const auto handler = RE::TESDataHandler::GetSingleton();
			if (const auto* race = handler ? handler->LookupForm<RE::TESRace>(entry.value("id", 0u), master) : nullptr) {
				_speakingRaces.insert(race->GetFormID());
			}
		}

		const auto unmapped = document.value("unmapped", nlohmann::json::object());
		_unmappedFemale = Resolve(unmapped.value("female", nlohmann::json{}), "unmapped female default");
		_unmappedMale = Resolve(unmapped.value("male", nlohmann::json{}), "unmapped male default");

		_dialoguePlugins.clear();
		_dialogue.clear();
		if (const auto dialogue = document.find("dialogue"); dialogue != document.end() && dialogue->is_object()) {
			for (const auto& plugin : dialogue->value("plugins", nlohmann::json::array())) {
				auto name = plugin.get<std::string>();
				std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				_dialoguePlugins.push_back(std::move(name));
			}
			for (const auto& entry : dialogue->value("borrow", nlohmann::json::array())) {
				const auto from = Resolve(entry, "dialogue voice");
				const auto as = from ? Resolve(entry.value("as", nlohmann::json{}), "dialogue borrowed voice") : 0u;
				if (from && as) {
					_dialogue[from] = as;
				}
			}
			logger::info("voices: dialogue lines of {} plugin(s) borrow for {} voice type(s)", _dialoguePlugins.size(),
				_dialogue.size());
		}

		_enabled = document.value("enabled", true) && !_own.empty();
		logger::info(
			"voices: {} own, {} unique mapped ({} of them deliberately silent), {} speaking race(s), "
			"unmapped default {}/{}{}",
			_own.size(), _borrow.size(), silent, _speakingRaces.size(),
			_unmappedFemale ? "female" : "-", _unmappedMale ? "male" : "-",
			_enabled ? "" : " - fallback is OFF");
	}

	bool Voices::IsDialoguePlugin(std::string_view a_plugin) const
	{
		if (a_plugin.empty()) {
			return false;
		}
		NamedLock lock{ _lock, "voices" };
		if (!lock) {
			return false;   // on the engine's thread: no answer is "the engine's own voice"
		}
		return std::ranges::any_of(_dialoguePlugins, [&](const std::string& a_name) {
			return a_name.size() == a_plugin.size() && _strnicmp(a_name.data(), a_plugin.data(), a_name.size()) == 0;
		});
	}

	std::uint32_t Voices::DialogueBorrow(std::uint32_t a_voiceType) const
	{
		NamedLock lock{ _lock, "voices" };
		if (!lock || !_enabled) {
			return 0;
		}
		const auto found = _dialogue.find(a_voiceType);
		return found == _dialogue.end() ? 0u : found->second;
	}

	std::uint32_t Voices::BorrowFor(std::uint32_t a_speaker) const
	{
		const auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_speaker);
		auto*       npc = actor ? actor->GetNPC() : nullptr;
		const auto* voice = npc ? npc->voiceType : nullptr;
		if (!voice) {
			return 0;
		}
		const auto own = voice->GetFormID();

		NamedLock lock{ _lock, "voices" };
		if (!_enabled || _own.contains(own)) {
			return 0;
		}
		if (const auto found = _borrow.find(own); found != _borrow.end()) {
			return found->second;
		}
		// A voice type the similarity pass never heard -- almost always one a mod
		// added. The owner's rule is "most similar we have"; with nothing measured,
		// the sex default is the closest honest answer, and voices.json can turn
		// it off by leaving "unmapped" empty.
		//
		// Only for a speaking race. Unmeasured means unknown, and the offline map
		// silences robots, creatures and every child voice by RACE; a mod's robot
		// or a mod's child race must not slip past that by being new. Anything not
		// human or ghoul keeps the silence V-8 already gives it.
		if (!actor->race || !_speakingRaces.contains(actor->race->GetFormID())) {
			return 0;
		}
		return npc->GetSex() == RE::SEX::kFemale ? _unmappedFemale : _unmappedMale;
	}

	bool Voices::CanSpeak(std::uint32_t a_speaker) const
	{
		const auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_speaker);
		auto*       npc = actor ? actor->GetNPC() : nullptr;
		const auto* voice = npc ? npc->voiceType : nullptr;
		if (!voice) {
			return false;
		}
		{
			NamedLock lock{ _lock, "voices" };
			if (_own.contains(voice->GetFormID())) {
				return true;
			}
		}
		return BorrowFor(a_speaker) != 0;
	}

	void Voices::Speak(std::uint32_t a_speaker, std::uint32_t a_target, std::uint32_t a_topic) const
	{
		Order order{ Order::Kind::kSayTopic, a_speaker, std::to_string(a_topic),
			// As a SIGNED int: the bridge reads it with `as Int`, and an FE/FF id above
			// 0x7FFFFFFF would not survive an unsigned decimal.
			a_target ? std::to_string(static_cast<std::int32_t>(a_target)) : std::string{} };
		order.voice = BorrowFor(a_speaker);
		if (order.voice != 0) {
			logger::info("voices: {:08X} has a voice we did not render - borrowing {:08X} for this line",
				a_speaker, order.voice);
		}
		// Not under _lock: QueueOrder takes the order lock, and a lock taken while
		// holding another is the cycle this codebase keeps its lock rules for.
		PapyrusLink::GetSingleton().QueueOrder(std::move(order));
	}
}
