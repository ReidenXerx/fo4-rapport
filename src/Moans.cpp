#include "Moans.h"

#include "Barks.h"
#include "Compat.h"
#include "FaceAuthority.h"

namespace RP
{
	namespace
	{
		constexpr std::uint32_t kPlay = 0x52464150;        // 'RFAP': play an SNDR at an actor's head
		constexpr std::uint32_t kPlayFeature = 1u << 12;   // Anatomy's hello: it plays RFAP
		constexpr std::uint32_t kCutPrevious = 1u << 0;    // RFAP flags: stop this actor's last one first
#ifdef RP_RUNTIME_DATABASE
		constexpr const char* kPeer = nullptr;   // to every listener, as FaceAuthority sends (F4SEPlugin_Load says why)
#else
		constexpr const char* kPeer = "OCBPC plugin";
#endif
		// RFAE kinds (fo4-anatomy A-67).
		constexpr std::uint32_t kBegan = 1, kThrust = 2, kImpact = 3, kEnded = 4;

		// The tempo rule (owner, 2026-10-01: "faster -> shorter"), on the stroke's length.
		constexpr std::uint32_t kFastStrokeMs = 600;
		constexpr std::uint32_t kSlowStrokeMs = 1200;
		// Two gasps no closer than this: a pounding is not a gasp per stroke.
		constexpr auto kImpactGap = std::chrono::milliseconds{ 1200 };
		// A take whose length the table does not give: assume a short moan's.
		constexpr float kUnknownLength = 2.0f;

#pragma pack(push, 4)
		struct EventV2
		{
			std::uint32_t version, formID, partner, kind;
			float         depth, speed;
			std::uint32_t strokeMs;   // v2 only; a v1 message (24 bytes) leaves it 0
		};
#pragma pack(pop)
		static_assert(sizeof(EventV2) == 28);
		constexpr std::uint32_t kV1Size = 24;

		constexpr std::array<std::string_view, 6> kKindNames{ "breath", "short", "medium", "long", "impact", "climax" };
	}

	Moans& Moans::GetSingleton() noexcept
	{
		static Moans singleton;
		return singleton;
	}

	void Moans::Load()
	{
		const auto    path = std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "moans.json";
		std::ifstream file{ path };
		if (!file) {
			logger::info("moans: no {} - Rapport plays no scene moans", path.string());
			return;
		}
		std::unordered_map<std::string, std::array<Sound, 6>> characters;
		std::size_t                                           sounds = 0;
		// The WHOLE parse guarded: a hand-edited table must cost the moans, never the game.
		try {
			nlohmann::json doc;
			file >> doc;
			for (const auto& [who, kinds] : doc.at("characters").items()) {
				auto& slots = characters[who];
				for (std::size_t k = 0; k < kKindNames.size(); ++k) {
					const auto it = kinds.find(std::string{ kKindNames[k] });
					if (it == kinds.end()) {
						continue;
					}
					Sound s;
					s.local = static_cast<std::uint32_t>(std::stoul(it->at("form").get<std::string>(), nullptr, 16));
					for (const auto& sec : it->value("seconds", nlohmann::json::array())) {
						s.longest = (std::max)(s.longest, sec.get<float>());
					}
					if (s.longest <= 0.0f) {
						s.longest = kUnknownLength;
					}
					slots[k] = s;
					++sounds;
				}
			}
		} catch (const std::exception& e) {
			logger::error("moans: {} could not be read ({}) - no scene moans", path.string(), e.what());
			return;
		}
		std::lock_guard lock{ _lock };
		_characters = std::move(characters);
		ResolveForms();
		_loaded = sounds > 0;
		logger::info("moans: {} sound(s) for {} character(s) - played through Anatomy when it offers RFAP", sounds,
			_characters.size());
	}

	// Main thread (Load at data-ready, Reset after a load): every SNDR to its runtime id, so the
	// event thread never asks the data handler anything.
	void Moans::ResolveForms()
	{
		const auto handler = RE::TESDataHandler::GetSingleton();
		std::size_t missing = 0;
		for (auto& [who, slots] : _characters) {
			for (auto& s : slots) {
				s.form = 0;
				if (s.local && handler) {
					if (const auto* form = handler->LookupForm(s.local, "Rapport.esp"sv)) {
						s.form = form->GetFormID();
					}
				}
				if (s.local && !s.form) {
					++missing;
				}
			}
		}
		if (missing) {
			logger::warn("moans: {} sound record(s) are not in this Rapport.esp - those stay silent", missing);
		}
	}

	void Moans::Register(const std::vector<std::uint32_t>& a_actors)
	{
		// Main thread: the bridge's natives. Game state is read HERE, never on the event thread.
		std::vector<std::pair<std::uint32_t, std::string>> found;
		for (const auto id : a_actors) {
			auto*      actor = id ? RE::TESForm::GetFormByID<RE::Actor>(id) : nullptr;
			const auto sex = Compat::Sex(actor ? actor->GetNPC() : nullptr);
			if (sex < 0) {
				continue;
			}
			auto persona = Barks::GetSingleton().PersonaOf(id);
			if (persona.empty()) {
				persona = "romantic";   // no persona data loaded: the gentlest of the four
			}
			found.emplace_back(id, std::string{ sex == 1 ? "female_" : "male_" } + persona);
		}
		std::lock_guard lock{ _lock };
		for (auto& [id, who] : found) {
			_actors[id].character = std::move(who);
		}
	}

	std::optional<Moans::Message> Moans::Pick(std::uint32_t a_actor, Kind a_kind, bool a_cut, std::string_view a_why)
	{
		if (!(FaceAuthority::GetSingleton().PeerFeatures() & kPlayFeature)) {
			if (!_saidNoPeer) {
				_saidNoPeer = true;
				logger::info("moans: Anatomy does not offer RFAP (hello bit 12) - no scene moans this session");
			}
			return std::nullopt;
		}
		auto& state = _actors[a_actor];
		if (state.character.empty()) {
			if (!_saidUnknown) {
				_saidUnknown = true;
				logger::info("moans: {:08X} sent a body event before its scene told Rapport who they are - skipped", a_actor);
			}
			return std::nullopt;
		}
		const auto it = _characters.find(state.character);
		if (it == _characters.end()) {
			return std::nullopt;
		}
		const auto& sound = it->second[static_cast<std::size_t>(a_kind)];
		if (!sound.form) {
			return std::nullopt;
		}
		// Quiet until it has ended, plus a breath: moans are a rhythm, not a drone.
		const auto pause = std::uniform_int_distribution<int>{ 200, 1200 }(_rng);
		state.quietUntil = std::chrono::steady_clock::now() +
		                   std::chrono::milliseconds{ static_cast<int>(sound.longest * 1000.0f) + pause };
		if (++_played <= 3 || _played % 100 == 0) {
			logger::info("moans: {:08X} {} {} ({}) - {} played so far", a_actor, state.character,
				kKindNames[static_cast<std::size_t>(a_kind)], a_why, _played);
		}
		return Message{ 1, a_actor, sound.form, 1.0f, a_cut ? kCutPrevious : 0u };
	}

	void Moans::Send(const std::vector<Message>& a_out)
	{
		if (a_out.empty()) {
			return;
		}
		if (const auto messaging = F4SE::GetMessagingInterface()) {
			for (auto message : a_out) {
				messaging->Dispatch(kPlay, &message, sizeof(message), kPeer);
			}
		}
	}

	void Moans::OnEvent(const void* a_data, std::uint32_t a_length)
	{
		if (!a_data || (a_length != kV1Size && a_length != sizeof(EventV2))) {
			return;
		}
		EventV2 e{};
		std::memcpy(&e, a_data, a_length);   // a v1 leaves strokeMs 0: unknown tempo
		if (e.version < 1 || e.version > 2 || e.formID == 0) {
			return;
		}
		std::vector<Message> out;
		{
			std::lock_guard lock{ _lock };
			if (!_loaded) {
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			auto&      state = _actors[e.formID];
			const bool quiet = now >= state.quietUntil;
			std::optional<Message> m;
			switch (e.kind) {
			case kBegan:
				// A new penetration: whatever climaxed before was another scene (a foreign end that
				// never came must not keep them "already climaxed" forever).
				state.climaxed = false;
				[[fallthrough]];
			case kEnded:
				if (quiet) {
					m = Pick(e.formID, Kind::kBreath, false, e.kind == kBegan ? "began" : "ended");
				}
				break;
			case kThrust:
				if (quiet) {
					const auto kind = e.strokeMs == 0            ? Kind::kMedium
					                  : e.strokeMs < kFastStrokeMs ? Kind::kShort
					                  : e.strokeMs < kSlowStrokeMs ? Kind::kMedium
					                                               : Kind::kLong;
					m = Pick(e.formID, kind, false, "thrust");
				}
				break;
			case kImpact:
				if (now - state.lastImpact >= kImpactGap && !state.climaxed) {
					state.lastImpact = now;
					m = Pick(e.formID, Kind::kImpact, true, "impact");
				}
				break;
			default:
				break;
			}
			if (m) {
				out.push_back(*m);
			}
		}
		Send(out);   // after the lock: a listener answering on this thread cannot deadlock us
	}

	bool Moans::ClimaxTag(std::string_view a_tags)
	{
		std::string lower{ a_tags };
		std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return lower.find("climax") != std::string::npos || lower.find("orgasm") != std::string::npos;
	}

	std::optional<Moans::Message> Moans::Climax(std::uint32_t a_actor)
	{
		if (a_actor == 0) {
			return std::nullopt;
		}
		auto& state = _actors[a_actor];
		if (state.climaxed) {
			return std::nullopt;
		}
		state.climaxed = true;
		return Pick(a_actor, Kind::kClimax, true, "climax tag");
	}

	void Moans::OwnSceneStarted(std::uint32_t a_first, std::uint32_t a_second)
	{
		Register({ a_first, a_second });
		std::lock_guard lock{ _lock };
		_ownFirst = a_first;
		_ownSecond = a_second;
		_actors[a_first].climaxed = false;
		_actors[a_second].climaxed = false;
	}

	void Moans::OwnSceneEnded(std::uint32_t a_first, std::uint32_t a_second)
	{
		std::lock_guard lock{ _lock };
		// Only the pair it belongs to: a late end of the LAST scene must not wipe the new one's.
		if (_ownFirst != a_first || _ownSecond != a_second) {
			return;
		}
		_actors.erase(a_first);
		_actors.erase(a_second);
		_ownFirst = _ownSecond = 0;
	}

	void Moans::OwnSceneTags(std::string_view a_tags)
	{
		if (!ClimaxTag(a_tags)) {
			return;
		}
		std::vector<Message> out;
		{
			std::lock_guard lock{ _lock };
			if (!_loaded || !_ownFirst) {
				return;
			}
			for (const auto actor : { _ownFirst, _ownSecond }) {
				if (auto m = Climax(actor)) {
					out.push_back(*m);
				}
			}
		}
		Send(out);
	}

	void Moans::SceneTags(const std::vector<std::uint32_t>& a_actors, std::string_view a_tags)
	{
		Register(a_actors);
		if (!ClimaxTag(a_tags)) {
			return;
		}
		std::vector<Message> out;
		{
			std::lock_guard lock{ _lock };
			if (!_loaded) {
				return;
			}
			for (const auto actor : a_actors) {
				if (auto m = Climax(actor)) {
					out.push_back(*m);
				}
			}
		}
		Send(out);
	}

	void Moans::SceneEnded(const std::vector<std::uint32_t>& a_actors)
	{
		std::lock_guard lock{ _lock };
		for (const auto actor : a_actors) {
			_actors.erase(actor);
		}
	}

	void Moans::Reset()
	{
		std::lock_guard lock{ _lock };
		_actors.clear();
		_ownFirst = _ownSecond = 0;
		ResolveForms();   // main thread; a new world may hold a different load order
	}
}
