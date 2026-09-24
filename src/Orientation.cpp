#include "Orientation.h"

#include "Traits.h"

namespace RP
{
	namespace
	{
		constexpr std::uint32_t kPlayer = 0x14;

		// R-27's mix: the first 70% straight, the next 20% bi, the last 10% gay.
		constexpr float kStraightShare = 0.70f;
		constexpr float kBiShare = 0.20f;

		std::filesystem::path PinPath()
		{
			return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "personas.json";
		}

		// 0..1 from the form id. Its own mixer: PersonaOf multiplies by the golden ratio,
		// Faithfulness runs murmur3's finaliser from 0x5BD1E995, Expressions takes % 6.
		// A trait that correlated with another would make "reticent" secretly mean
		// "straight", which nobody decided. This one is the lowbias32 finaliser from its
		// own seed.
		[[nodiscard]] float Draw(std::uint32_t a_formID) noexcept
		{
			auto h = Traits::StableID(a_formID) ^ 0x2C1B3C6Du;
			h ^= h >> 16;
			h *= 0x7FEB352Du;
			h ^= h >> 15;
			h *= 0x846CA68Bu;
			h ^= h >> 16;
			return static_cast<float>(h >> 8) / static_cast<float>(1u << 24);
		}

		[[nodiscard]] std::optional<bool> Female(RE::Actor* a_actor)
		{
			auto* npc = a_actor ? a_actor->GetNPC() : nullptr;   // not const: GetSex() is not
			if (!npc) {
				return std::nullopt;
			}
			return npc->GetSex() == RE::SEX::kFemale;
		}

		[[nodiscard]] std::string ActorName(RE::Actor* a_actor)
		{
			const char* name = a_actor ? a_actor->GetDisplayFullName() : nullptr;
			return name && *name ? std::string{ name } : std::format("{:08X}", a_actor ? a_actor->GetFormID() : 0u);
		}

		[[nodiscard]] std::optional<Orientation::Kind> Parse(std::string_view a_text)
		{
			if (a_text == "straight"sv) {
				return Orientation::Kind::kStraight;
			}
			if (a_text == "bi"sv) {
				return Orientation::Kind::kBi;
			}
			if (a_text == "gay"sv) {
				return Orientation::Kind::kGay;
			}
			return std::nullopt;
		}
	}

	Orientation& Orientation::GetSingleton() noexcept
	{
		static Orientation singleton;
		return singleton;
	}

	void Orientation::Load()
	{
		std::unordered_map<std::uint32_t, Kind> pins;
		std::unordered_set<std::uint32_t>       playersexual;
		std::ifstream                           file{ PinPath() };
		if (file) {
			nlohmann::json document;
			try {
				file >> document;
			} catch (const std::exception& e) {
				logger::error("orientation: {} is not valid json ({}) - nobody is pinned", PinPath().string(), e.what());
				document = nullptr;
			}
			const auto handler = RE::TESDataHandler::GetSingleton();
			const auto list = document.is_object() ? document.find("overrides") : document.end();
			if (handler && document.is_object() && list != document.end() && list->is_array()) {
				for (const auto& entry : *list) {
					// One bad entry is skipped, never thrown: the file is hand-edited, and an
					// exception here would cross into F4SE at data ready.
					try {
						const auto orientation = entry.value("orientation", std::string{});
						const bool open = entry.value("playersexual", false);
						if (orientation.empty() && !open) {
							continue;   // a persona pin only
						}
						const auto    plugin = entry.value("plugin", std::string{});
						std::uint32_t id = 0;
						if (const auto raw = entry.find("id"); raw != entry.end()) {
							id = raw->is_string() ? static_cast<std::uint32_t>(std::stoul(raw->get<std::string>(), nullptr, 16))
							                      : raw->get<std::uint32_t>();
						}
						const auto kind = Parse(orientation);
						if (!orientation.empty() && !kind) {
							logger::warn("orientation: pin for {} {:06X} names \"{}\" - straight, bi or gay; skipped", plugin,
								id, orientation);
							continue;
						}
						const auto* form = handler->LookupForm(id, plugin);
						if (!form) {
							logger::debug("orientation: pin {} {:06X} is not loaded - skipped", plugin, id);
							continue;
						}
						if (!form->As<RE::Actor>() && !form->As<RE::TESNPC>()) {
							logger::warn("orientation: pin {} {:06X} is not an actor or NPC - skipped", plugin, id);
							continue;
						}
						if (kind) {
							pins[form->GetFormID()] = *kind;
						}
						if (open) {
							playersexual.insert(form->GetFormID());
						}
					} catch (const std::exception& e) {
						logger::warn("orientation: a pin in personas.json is malformed ({}) - skipped", e.what());
					}
				}
			}
		}
		const auto pinned = pins.size();
		const auto open = playersexual.size();
		{
			std::scoped_lock lock{ _lock };
			_pins = std::move(pins);
			_playersexual = std::move(playersexual);
		}
		logger::info("orientation: {} pinned, {} playersexual toward the player; everyone else derived from the form id "
					 "({:.0f}% straight, {:.0f}% bi, {:.0f}% gay)",
			pinned, open, kStraightShare * 100.0f, kBiShare * 100.0f, (1.0f - kStraightShare - kBiShare) * 100.0f);
	}

	std::optional<Orientation::Kind> Orientation::PinOf(std::uint32_t a_formID) const
	{
		std::scoped_lock lock{ _lock };
		if (_pins.empty()) {
			return std::nullopt;
		}
		if (const auto it = _pins.find(a_formID); it != _pins.end()) {
			return it->second;
		}
		const auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_formID);
		const auto* npc = actor ? actor->GetNPC() : nullptr;
		if (npc) {
			if (const auto it = _pins.find(npc->GetFormID()); it != _pins.end()) {
				return it->second;
			}
		}
		return std::nullopt;
	}

	bool Orientation::Playersexual(std::uint32_t a_formID) const
	{
		std::scoped_lock lock{ _lock };
		if (_playersexual.contains(a_formID)) {
			return true;
		}
		const auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_formID);
		const auto* npc = actor ? actor->GetNPC() : nullptr;
		return npc && _playersexual.contains(npc->GetFormID());
	}

	Orientation::Kind Orientation::Of(std::uint32_t a_formID) const
	{
		if (const auto pin = PinOf(a_formID)) {
			return *pin;
		}
		const float draw = Draw(a_formID);
		return draw < kStraightShare                ? Kind::kStraight :
		       draw < kStraightShare + kBiShare     ? Kind::kBi :
		                                              Kind::kGay;
	}

	std::string_view Orientation::Name(Kind a_kind) noexcept
	{
		switch (a_kind) {
		case Kind::kStraight:
			return "straight"sv;
		case Kind::kBi:
			return "bi"sv;
		case Kind::kGay:
			return "gay"sv;
		}
		return "?"sv;
	}

	bool Orientation::Attracted(RE::Actor* a_who, RE::Actor* a_with) const
	{
		if (!a_who || !a_with || a_who->GetFormID() == kPlayer) {
			return true;
		}
		if (a_with->GetFormID() == kPlayer && Playersexual(a_who->GetFormID())) {
			return true;
		}
		const auto mine = Female(a_who);
		const auto theirs = Female(a_with);
		if (!mine || !theirs) {
			return true;
		}
		switch (Of(a_who->GetFormID())) {
		case Kind::kBi:
			return true;
		case Kind::kStraight:
			return *mine != *theirs;
		case Kind::kGay:
			return *mine == *theirs;
		}
		return true;
	}

	std::string Orientation::WhyNot(RE::Actor* a_first, RE::Actor* a_second) const
	{
		for (auto [who, with] : { std::pair{ a_first, a_second }, std::pair{ a_second, a_first } }) {
			if (!Attracted(who, with)) {
				return std::format("{} is {} and {} is a {}", ActorName(who), Name(Of(who->GetFormID())), ActorName(with),
					Female(with).value_or(false) ? "woman" : "man");
			}
		}
		return {};
	}
}
