#include "Moans.h"

#include "Barks.h"
#include "Compat.h"
#include "Config.h"
#include "FaceAuthority.h"
#include "TreeIndex.h"

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
		// RFAE kinds (fo4-anatomy A-67). 5 = a deep oral stroke, if Anatomy sends it as a kind.
		constexpr std::uint32_t kBegan = 1, kThrust = 2, kImpact = 3, kEnded = 4, kDeep = 5;
		// RFAE v3 flags, if Anatomy sends them on the event instead.
		// v3 (fo4-anatomy ec72ff4, c1a4b87): bit 0 ORAL (a shaft in a mouth, on both partners' events),
		// bit 1 DEEP (the stroke reached the deep face's depth, any opening), bit 2 RECEIVER (this actor's
		// opening -- mouth, vagina or anus -- is the one entered; from geometry, any sex).
		constexpr std::uint32_t kFlagOral = 1u << 0, kFlagDeep = 1u << 1, kFlagReceiver = 1u << 2;
		// More kinds of contact (fo4-anatomy, 2026-10-01), still on v3's 32 bytes: bit 3 ANAL (the opening
		// is the anus), bit 4 LICK (a mouth on her, no shaft: beats every 450-850 ms), bit 5 HAND (fingers,
		// a fist, rubbing, or a handjob on his shaft), bit 6 TOY, bit 7 SELF (masturbation). The receiver's
		// events carry RECEIVER; the other actor's carry the contact bit alone.
		constexpr std::uint32_t kFlagAnal = 1u << 3, kFlagLick = 1u << 4;
		// Two gags no closer than this.
		constexpr auto kGagGap = std::chrono::milliseconds{ 1500 };
		// The GULPS are switched OFF (owner, 2026-10-01: "lets disable gulp sounds for a while we return to
		// it in next versions"). Their takes and SNDRs stay built; a deep oral stroke leaves the mouth
		// silent until this is true again.
		constexpr bool kGulpsEnabled = false;
		constexpr std::uint64_t kSummaryEvery = 200;

		// The tempo rule (owner, 2026-10-01: "faster -> shorter"), on the stroke's length.
		constexpr std::uint32_t kFastStrokeMs = 600;
		constexpr std::uint32_t kSlowStrokeMs = 1200;
		// Two gasps no closer than this: a pounding is not a gasp per stroke.
		constexpr auto kImpactGap = std::chrono::milliseconds{ 1200 };
		// A take whose length the table does not give: assume a short moan's.
		constexpr float kUnknownLength = 2.0f;
		// One climax per scene: a second climax tag within this long is the same climax (a position change
		// re-sends tags). Past it -- a long scene's second round, or a foreign scene whose end never came --
		// a new one may play.
		constexpr auto kClimaxWindow = std::chrono::seconds{ 90 };
		// An "ended" breath waits this long, and is dropped if contact resumes (a position change, a brief
		// withdrawal): a sigh on every withdrawal sounded wrong (review, 2026-10-01).
		constexpr auto kEndedBreathDelay = std::chrono::milliseconds{ 1500 };

#pragma pack(push, 4)
		struct EventV3
		{
			std::uint32_t version, formID, partner, kind;
			float         depth, speed;
			std::uint32_t strokeMs;   // v2 on; a v1 message (24 bytes) leaves it 0
			std::uint32_t flags;      // v3 only; v1 and v2 leave it 0
		};
#pragma pack(pop)
		static_assert(sizeof(EventV3) == 32);
		constexpr std::uint32_t kV1Size = 24, kV2Size = 28;

		// AAF's tags as lowercase TOKENS ("Anal, Rough" -> {"anal", "rough"}): a substring match took "pain"
		// out of any tag that merely contained it.
		std::vector<std::string> Tokens(std::string_view a_tags)
		{
			std::vector<std::string> out;
			std::string              cur;
			const auto               flush = [&] {
                if (!cur.empty()) {
                    out.push_back(cur);
                    cur.clear();
                }
			};
			for (const unsigned char c : a_tags) {
				if (c == ',' || c == ';' || c == ' ' || c == '\t') {
					flush();
				} else {
					cur.push_back(static_cast<char>(std::tolower(c)));
				}
			}
			flush();
			return out;
		}

		constexpr std::array<std::string_view, 11> kKindNames{ "breath", "short", "medium", "long", "impact", "climax",
			"pain_short", "pain_medium", "pain_long", "pain_impact", "gag" };
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
		std::unordered_map<std::string, std::array<Sound, static_cast<std::size_t>(Kind::kCount)>> characters;
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

	void Moans::ForgetWho(std::uint32_t a_actor)
	{
		std::lock_guard lock{ _lock };
		if (const auto it = _actors.find(a_actor); it != _actors.end()) {
			it->second.character.clear();
		}
	}

	void Moans::Register(const std::vector<std::uint32_t>& a_actors)
	{
		// Main thread: the bridge's natives. Game state is read HERE, never on the event thread.
		std::vector<std::pair<std::uint32_t, std::string>> found;
		std::vector<std::uint32_t>                          silent;
		bool                                                child = false;
		for (const auto id : a_actors) {
			auto*      actor = id ? RE::TESForm::GetFormByID<RE::Actor>(id) : nullptr;
			const auto sex = Compat::Sex(actor ? actor->GetNPC() : nullptr);
			// The same screen ForeignScenes gives faces and cum: a child anywhere leaves
			// the whole scene alone, and a race Rapport does not dress (a creature) has
			// no human voice to give. Before this, an AAF-menu scene that the face and
			// cum paths refused still got moans.
			if (actor && actor->IsChild()) {
				child = true;
			}
			const bool raceOk = actor && (actor == RE::PlayerCharacter::GetSingleton() ||
			                              Config::GetSingleton().IsRaceAllowed(actor->race));
			if (sex < 0 || !raceOk) {
				silent.push_back(id);
				continue;
			}
			auto persona = Barks::GetSingleton().PersonaOf(id);
			if (persona.empty()) {
				persona = "romantic";   // no persona data loaded: the gentlest of the four
			}
			found.emplace_back(id, std::string{ sex == 1 ? "female_" : "male_" } + persona);
		}
		std::lock_guard lock{ _lock };
		if (child) {
			for (const auto id : a_actors) {
				if (const auto it = _actors.find(id); it != _actors.end()) {
					it->second.character.clear();
				}
			}
			logger::info("moans: a child is in this scene - nobody in it is voiced");
			return;
		}
		for (const auto id : silent) {
			if (const auto it = _actors.find(id); it != _actors.end()) {
				it->second.character.clear();
			}
		}
		for (auto& [id, who] : found) {
			_actors[id].character = std::move(who);
		}
	}

	std::optional<Moans::Message> Moans::Pick(std::uint32_t a_actor, Kind a_kind, bool a_cut, std::string_view a_why)
	{
		// The MCM switch: off means none of Rapport's moans either, the climax included -- the packs' own
		// sounds play then, and a moan of ours on top would double them.
		if (!FaceAuthority::GetSingleton().SoundOverride()) {
			return std::nullopt;
		}
		if (!(FaceAuthority::GetSingleton().PeerFeatures() & kPlayFeature)) {
			if (!_saidNoPeer) {
				_saidNoPeer = true;
				logger::info("moans: Anatomy does not offer RFAP (hello bit 12) - no scene moans this session");
			}
			++_skipNoPeer;
			return std::nullopt;
		}
		auto& state = _actors[a_actor];
		if (state.character.empty()) {
			if (!_saidUnknown) {
				_saidUnknown = true;
				logger::info("moans: {:08X} sent a body event before its scene told Rapport who they are - learning it", a_actor);
			}
			++_skipUnknown;
			if (std::ranges::find(_unknown, a_actor) == _unknown.end()) {
				_unknown.push_back(a_actor);   // the next Pump, on the main thread, learns who they are
			}
			return std::nullopt;
		}
		const auto it = _characters.find(state.character);
		const Sound* sound = it == _characters.end() ? nullptr : &it->second[static_cast<std::size_t>(a_kind)];
		if (!sound || !sound->form) {
			if (++_skipNoSound == 1) {
				logger::warn("moans: no sound record for {} {} - is Rapport.esp the moans build?", state.character,
					kKindNames[static_cast<std::size_t>(a_kind)]);
			}
			return std::nullopt;
		}
		// Quiet until it has ended, plus a breath: moans are a rhythm, not a drone.
		const auto pause = std::uniform_int_distribution<int>{ 200, 1200 }(_rng);
		state.quietUntil = std::chrono::steady_clock::now() +
		                   std::chrono::milliseconds{ static_cast<int>(sound->longest * 1000.0f) + pause };
		if (++_played <= 3 || _played % 100 == 0) {
			logger::info("moans: {:08X} {} {} ({}) - {} played so far", a_actor, state.character,
				kKindNames[static_cast<std::size_t>(a_kind)], a_why, _played);
		}
		return Message{ 1, a_actor, sound->form, 1.0f, a_cut ? kCutPrevious : 0u };
	}

	void Moans::Summary(std::string_view a_when)
	{
		logger::info("moans: {} - {} body event(s) from Anatomy, {} voice(s) sent; skipped: {} still playing, {} mouth full, "
		             "{} no RFAP, {} unknown actor, {} no record, {} before moans.json",
			a_when, _received, _played, _skipBusy, _skipMouth, _skipNoPeer, _skipUnknown, _skipNoSound, _skipNotLoaded);
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
		// Forward-compatible: later versions only APPEND (the contract), so anything from 24 bytes on is
		// read up to the fields this build knows; a rejected event is said once, never dropped silently.
		EventV3 e{};
		if (a_data && a_length >= kV1Size) {
			std::memcpy(&e, a_data, (std::min)(a_length, static_cast<std::uint32_t>(sizeof(EventV3))));
		}
		if (!a_data || a_length < kV1Size || e.version < 1 || e.formID == 0) {
			std::lock_guard lock{ _lock };
			if (!_saidBadEvent) {
				_saidBadEvent = true;
				logger::warn("moans: an RFAE of {} byte(s), version {} was not readable - ignored", a_length, e.version);
			}
			return;
		}
		std::vector<Message> out;
		{
			std::lock_guard lock{ _lock };
			if (++_received == 1) {
				logger::info("moans: first body event from Anatomy (RFAE v{}, {:08X}, kind {}, flags {:X}) - listening",
					e.version, e.formID, e.kind, e.flags);
			}
			if (_received % kSummaryEvery == 0) {
				Summary("so far");
			}
			if (!_loaded) {
				++_skipNotLoaded;
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			auto&      state = _actors[e.formID];
			const bool quiet = now >= state.quietUntil;
			std::optional<Message> m;
			// ORAL (v3 flags bit 0) rides on both partners' events; RECEIVER (bit 2, fo4-anatomy c1a4b87,
			// decided from geometry, any sex) says this actor's opening is the one entered. So the MOUTH's
			// owner is ORAL | RECEIVER, and the shaft's owner is ORAL alone.
			const bool receiver = (e.flags & kFlagReceiver) != 0;
			const bool mouth = (e.flags & kFlagOral) && receiver;
			// The LICKER's mouth is busy too (LICK without RECEIVER): no moan from it.
			const bool licking = (e.flags & kFlagLick) && !receiver;
			// ANAL, on the one receiving it -- a shaft, fingers or a toy: the painful-pleasure set (owner:
			// "anal should use pain-pleasure sounds rapport did specifically for this"). Everything else --
			// licking, hands, toys, self, a handjob on the shaft's owner -- the pleasure moans, by beat or
			// stroke tempo the same way (owner: "moans could be pleasure moans we have").
			// A rough scene's tags count for the receiver only: the giver moans pleasure.
			const bool rough = receiver && (state.rough || (e.flags & kFlagAnal));
			// The mouth's state outlives the event: in a spitroast her vagina's track moans while her mouth
			// is full, so a busy mouth silences every track of hers, and the climax too.
			if (mouth || licking) {
				state.mouthBusy = e.kind != kEnded;
			}
			// Contact resumed: a held "ended" breath is dropped.
			if (e.kind == kBegan || e.kind == kThrust) {
				state.breathAt = {};
			}
			// A deep stroke in the mouth: its owner gags, cutting whatever was playing. DEEP (bit 1)
			// is set for any opening, so only with ORAL.
			if (kGulpsEnabled && mouth && (e.kind == kDeep || (e.flags & kFlagDeep))) {
				if (now - state.lastGag >= kGagGap) {
					state.lastGag = now;
					m = Pick(e.formID, Kind::kGag, true, "deep");
				}
				if (m) {
					out.push_back(*m);
				}
				goto send;
			}
			// A full mouth makes no moan. The shaft's owner moans as ever: he is the one pleasured.
			if (mouth || licking || state.mouthBusy) {
				++_skipMouth;
				goto send;
			}
			switch (e.kind) {
			case kBegan:
				if (quiet) {
					m = Pick(e.formID, Kind::kBreath, false, "began");
				}
				break;
			case kEnded:
				// Held, and played by Pump only if no contact resumes first.
				state.breathAt = now + kEndedBreathDelay;
				break;
			case kThrust:
				if (quiet) {
					const auto tier = e.strokeMs == 0 ? 1 : e.strokeMs < kFastStrokeMs ? 0 : e.strokeMs < kSlowStrokeMs ? 1 : 2;
					static constexpr std::array<Kind, 3> kSweet{ Kind::kShort, Kind::kMedium, Kind::kLong };
					static constexpr std::array<Kind, 3> kPain{ Kind::kPainShort, Kind::kPainMedium, Kind::kPainLong };
					m = Pick(e.formID, (rough ? kPain : kSweet)[tier], false, rough ? "rough thrust" : "thrust");
				}
				break;
			case kImpact:
				// Never over a climax still playing (it was cut-flagged; a gasp would cut it).
				if (now - state.lastImpact >= kImpactGap && now >= state.climaxUntil) {
					state.lastImpact = now;
					m = Pick(e.formID, rough ? Kind::kPainImpact : Kind::kImpact, true, "impact");
				}
				break;
			default:
				break;
			}
			if (m) {
				out.push_back(*m);
			} else if (!quiet && e.kind != kEnded) {
				++_skipBusy;
			}
		send:;
		}
		Send(out);   // after the lock: a listener answering on this thread cannot deadlock us
	}

	bool Moans::ClimaxTag(std::string_view a_tags)
	{
		return std::ranges::any_of(Tokens(a_tags), [](const std::string& t) {
			return t.starts_with("climax") || t.starts_with("orgasm");
		});
	}

	// Rough, aggressive or BDSM: the painful-pleasure set for the one RECEIVING (owner, 2026-10-01).
	// Whole words, not prefixes ("pain" took "painting"), and no "anal": the engine's ANAL bit
	// says which opening, and a scene tag would have put the giver on the pain set too.
	bool Moans::RoughTag(std::string_view a_tags)
	{
		static constexpr std::array<std::string_view, 19> kWords{ "aggressive", "rough", "bdsm", "bondage", "bound", "gagged",
			"strangled", "spanktobutt", "garrotepole",
			"spank", "spanking", "whip", "whipping", "choke", "choking", "pain", "painful", "forced", "punishment" };
		return std::ranges::any_of(Tokens(a_tags), [](const std::string& t) {
			return std::ranges::find(kWords, std::string_view{ t }) != kWords.end();
		});
	}

	void Moans::NoteRough(const std::vector<std::uint32_t>& a_actors, bool a_rough)
	{
		for (const auto actor : a_actors) {
			if (actor) {
				_actors[actor].rough = a_rough;
			}
		}
	}

	std::optional<Moans::Message> Moans::Climax(std::uint32_t a_actor)
	{
		if (a_actor == 0) {
			return std::nullopt;
		}
		auto&      state = _actors[a_actor];
		const auto now = std::chrono::steady_clock::now();
		if ((state.climaxed && now - state.climaxAt < kClimaxWindow) || state.mouthBusy) {
			return std::nullopt;
		}
		auto m = Pick(a_actor, Kind::kClimax, true, "climax tag");
		if (m) {
			// Marked only when it PLAYED: a skipped one (no RFAP yet, unknown actor) may still come.
			state.climaxed = true;
			state.climaxAt = now;
			state.climaxUntil = state.quietUntil;
		}
		return m;
	}

	void Moans::OwnSceneStarted(std::uint32_t a_first, std::uint32_t a_second)
	{
		Register({ a_first, a_second });
		std::lock_guard lock{ _lock };
		_ownFirst = a_first;
		_ownSecond = a_second;
		for (const auto id : { a_first, a_second }) {
			auto& s = _actors[id];
			s.climaxed = false;
			s.rough = false;
			s.mouthBusy = false;
			s.breathAt = {};
		}
	}

	void Moans::OwnSceneEnded(std::uint32_t a_first, std::uint32_t a_second)
	{
		std::lock_guard lock{ _lock };
		// Only the pair it belongs to: a late end of the LAST scene must not wipe the new one's.
		if (_ownFirst != a_first || _ownSecond != a_second) {
			return;
		}
		for (const auto id : { a_first, a_second }) {
			if (const auto it = _actors.find(id); it != _actors.end()) {
				it->second.climaxed = it->second.rough = it->second.mouthBusy = false;
			}
		}
		_ownFirst = _ownSecond = 0;
		_ownPosition.clear();
	}

	void Moans::OwnScenePosition(std::string_view a_position)
	{
		std::lock_guard lock{ _lock };
		_ownPosition = a_position;
	}

	void Moans::OwnSceneTags(std::string_view a_tags)
	{
		const bool rough = RoughTag(a_tags);
		std::vector<Message> out;
		{
			std::lock_guard lock{ _lock };
			if (!_loaded || !_ownFirst) {
				return;
			}
			NoteRough({ _ownFirst, _ownSecond }, rough);
			if (!ClimaxTag(a_tags) && !TreeIndex::GetSingleton().IsClimaxPosition(_ownPosition)) {
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

	void Moans::SceneTags(const std::vector<std::uint32_t>& a_actors, std::string_view a_position,
		std::string_view a_tags)
	{
		Register(a_actors);
		const bool rough = RoughTag(a_tags);
		std::vector<Message> out;
		{
			std::lock_guard lock{ _lock };
			if (!_loaded) {
				return;
			}
			NoteRough(a_actors, rough);
			if (!ClimaxTag(a_tags) && !TreeIndex::GetSingleton().IsClimaxPosition(a_position)) {
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
		// Who they are is KEPT -- a sex and a persona do not change -- so an end arriving after the next
		// scene's start cannot leave its actors unknown and silent. Only this scene's state is cleared.
		std::lock_guard lock{ _lock };
		for (const auto actor : a_actors) {
			if (const auto it = _actors.find(actor); it != _actors.end()) {
				it->second.climaxed = it->second.rough = it->second.mouthBusy = false;
			}
		}
	}

	void Moans::Reset()
	{
		std::lock_guard lock{ _lock };
		if (_received) {
			Summary("at the load");
		}
		_actors.clear();
		_unknown.clear();
		_ownFirst = _ownSecond = 0;
		_ownPosition.clear();
		ResolveForms();   // main thread; a new world may hold a different load order
	}

	void Moans::Pump()
	{
		std::vector<std::uint32_t> learn;
		{
			std::lock_guard lock{ _lock };
			learn.swap(_unknown);
		}
		if (!learn.empty()) {
			Register(learn);   // main thread: the bridge's poll
		}
		std::vector<Message> out;
		{
			std::lock_guard lock{ _lock };
			if (!_loaded) {
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			for (auto& [id, s] : _actors) {
				if (s.breathAt != std::chrono::steady_clock::time_point{} && now >= s.breathAt) {
					s.breathAt = {};
					if (now >= s.quietUntil && !s.mouthBusy) {
						if (auto m = Pick(id, Kind::kBreath, false, "ended")) {
							out.push_back(*m);
						}
					}
				}
			}
		}
		Send(out);
	}
}
