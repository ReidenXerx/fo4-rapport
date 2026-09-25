#include "FaceAuthority.h"

#include "Barks.h"
#include "McmSettings.h"

namespace RP
{
	namespace
	{
		constexpr std::uint32_t kSet = 0x52464153;     // 'RFAS'
		constexpr std::uint32_t kClear = 0x52464143;   // 'RFAC'
		constexpr std::uint32_t kDeep = 0x52464144;    // 'RFAD': the held face at full depth
		constexpr std::uint32_t kKnobs = 0x5246414B;   // 'RFAK': Anatomy's knobs from Rapport's MCM
		constexpr std::uint32_t kGlance = 0x52464147;  // 'RFAG': a glance into the partner's eyes
		constexpr std::uint32_t kVersion = 1;
		constexpr const char*   kPeer = "OCBPC plugin";   // Anatomy's cbp.dll, by its F4SE name

		// Hello feature bits, as the anatomy session defined them.
		constexpr std::uint32_t kEngineLines = 1u << 1;   // lines keep the mouth, for their real length
		constexpr std::uint32_t kDepthBlend = 1u << 3;    // blends toward our deep face by depth
		constexpr std::uint32_t kGlances = 1u << 4;       // turns the eyes to a partner on 'RFAG'

		// A glance's partner stands this close (the two of a scene are an arm's length apart).
		constexpr float kPartnerReach = 220.0f;

		// How long a line of Rapport's keeps the speaker's mouth for their lip sync.
		// The C++ side does not know a line's length -- the bridge says it later, and
		// only as "said" -- so this covers the poll's delay before it is said (up to
		// 3 s) and a bark (measured 1-5 s). ASSUMED.
		constexpr auto kSpeakingWindow = std::chrono::seconds{ 9 };

		// Exactly the layout agreed with the anatomy session: 232 bytes, natural
		// alignment, owned at offset 8.
		struct SetMessage
		{
			std::uint32_t version;
			std::uint32_t formID;
			std::uint64_t owned;
			float         value[54];
		};
		static_assert(sizeof(SetMessage) == 232);

		// Agreed with the anatomy session, 2026-09-25: 32 bytes, append-only after version.
		struct KnobMessage
		{
			std::uint32_t version;
			std::uint32_t enabled;
			float         lipClearance;
			float         lipSpeed;
			float         shaftScale;
			float         headMin;
			float         headMax;
			float         reactScale;
		};
		static_assert(sizeof(KnobMessage) == 32);

		// Agreed with the anatomy session, 2026-09-25: 24 bytes.
		struct GlanceMessage
		{
			std::uint32_t version;
			std::uint32_t looker;
			std::uint32_t target;
			std::uint32_t durationMs;
			float         lidsOpen;
			std::uint32_t flags;
		};
		static_assert(sizeof(GlanceMessage) == 24);

		// How a persona looks at the one they are with, ASSUMED for the owner to judge in game:
		// seconds between glances (min, max), and how long one lasts (min, max).
		struct GlanceStyle
		{
			float everyMin, everyMax, forMin, forMax;
		};
		[[nodiscard]] GlanceStyle StyleFor(std::string_view a_persona, bool a_oral)
		{
			// The owner's example: during a blowjob, time to time, for 1-2 seconds.
			GlanceStyle style{ 6.0f, 15.0f, 1.0f, 2.0f };
			if (a_persona == "vulgar"sv) {
				style = { 5.0f, 11.0f, 1.5f, 3.0f };   // holds your eyes, wants you to see it
			} else if (a_persona == "reticent"sv) {
				style = { 12.0f, 25.0f, 0.6f, 1.2f };  // a quick look, then away
			} else if (a_persona == "romantic"sv) {
				style = { 7.0f, 14.0f, 1.2f, 2.5f };
			}
			if (!a_oral) {
				// Face to face or not, other acts look less often than the one where she looks UP.
				style.everyMin *= 1.5f;
				style.everyMax *= 1.5f;
			}
			return style;
		}

		struct ClearMessage
		{
			std::uint32_t version;
			std::uint32_t formID;
		};
		static_assert(sizeof(ClearMessage) == 8);

		[[nodiscard]] bool IsClear(std::string_view a_setID)
		{
			return a_setID.empty() || a_setID == "Rapport_Clear"sv;
		}
	}

	FaceAuthority& FaceAuthority::GetSingleton() noexcept
	{
		static FaceAuthority singleton;
		return singleton;
	}

	void FaceAuthority::Load()
	{
		const auto path = std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "faces.json";
		std::ifstream file{ path };
		if (!file) {
			logger::warn("face authority: no {} - Rapport's faces stay on the AAF path alone", PathText(path));
			return;
		}
		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			logger::error("face authority: {} is not valid json ({}) - the AAF path alone", PathText(path), e.what());
			return;
		}

		std::unordered_map<std::string, std::array<float, kSlots>> sets;
		std::unordered_map<std::string, Deep>                      deep;
		std::uint64_t                                              mouth = 0;
		std::uint32_t                                              morphs = 50;
		try {
			morphs = (std::min)(document.value("morphs", 50u), 50u);
			for (const auto& id : document.value("mouth", nlohmann::json::array())) {
				const auto bit = id.get<std::uint32_t>();
				if (bit < 64) {
					mouth |= std::uint64_t{ 1 } << bit;
				}
			}
			for (const auto& [setID, settings] : document.at("sets").items()) {
				std::array<float, kSlots> values{};
				for (const auto& [morph, intensity] : settings.items()) {
					const auto id = static_cast<std::size_t>(std::stoul(morph));
					if (id < morphs) {
						values[id] = std::clamp(intensity.get<float>() / 100.0f, 0.0f, 1.0f);
					}
				}
				sets.emplace(setID, values);
			}
			// Optional: a faces.json from before the deep faces has none, and blends nothing.
			for (const auto& [setID, settings] : document.value("deep", nlohmann::json::object()).items()) {
				Deep entry;
				for (const auto& [morph, intensity] : settings.items()) {
					const auto id = static_cast<std::size_t>(std::stoul(morph));
					if (id < morphs) {
						entry.values[id] = std::clamp(intensity.get<float>() / 100.0f, 0.0f, 1.0f);
						entry.mask |= std::uint64_t{ 1 } << id;
					}
				}
				if (entry.mask != 0) {
					deep.emplace(setID, entry);
				}
			}
		} catch (const std::exception& e) {
			logger::error("face authority: {} has a shape this plugin cannot read ({}) - the AAF path alone",
				PathText(path), e.what());
			return;
		}

		{
			NamedLock lock{ _lock, "face authority" };
			_sets = std::move(sets);
			_deep = std::move(deep);
			_mouth = mouth;
			_morphs = morphs;
			_unknown.clear();
		}
		_loaded.store(true);
		logger::info("face authority: {} set(s) of {} morph(s), {} of them the mouth, {} with a deep face; Anatomy {}",
			_sets.size(), morphs, std::popcount(mouth), _deep.size(),
			_peer.load() ? "answered - Rapport rules the faces it holds"
						 : "has not answered (yet) - the AAF path alone until it does");
	}

	void FaceAuthority::OnHello(std::uint32_t a_version, std::uint32_t a_features)
	{
		if (a_version < kVersion) {
			logger::warn("face authority: Anatomy speaks protocol v{}, Rapport needs v{} - the AAF path alone", a_version,
				kVersion);
			return;
		}
		{
			NamedLock lock{ _lock, "face authority" };
			_peerVersion = a_version;
		}
		_peerFeatures.store(a_features);
		_peer.store(true);
		logger::info("face authority: Anatomy's cbp.dll answered (protocol v{}, features {:08X}) - {}; lines: {}",
			a_version, a_features,
			_loaded.load() ? "Rapport rules the faces it holds" : "authority begins once faces.json is read",
			(a_features & kEngineLines) ? "the engine's own, for their real length"
										: "Rapport gives the mouth back for 9 s per line");
		SendKnobs();
		logger::info("face authority: depth blend {}; glances {}",
			(a_features & kDepthBlend) ? "ON" : "not offered by this cbp.dll",
			(a_features & kGlances) ? "ON - held faces look at their partner now and then"
									: "not offered by this cbp.dll yet");
	}

	bool FaceAuthority::ValuesOf(const std::string& a_setID, std::array<float, kSlots>& a_out)
	{
		if (const auto found = _sets.find(a_setID); found != _sets.end()) {
			a_out = found->second;
			return true;
		}
		if (_unknown.insert(a_setID).second) {
			logger::warn("face authority: faces.json has no \"{}\" - that face goes by the AAF path alone "
						 "(tools/make_mfg.py writes both from one table)",
				a_setID);
		}
		return false;
	}

	void FaceAuthority::DeepOf(const std::string& a_setID, Send& a_send) const
	{
		if (!(_peerFeatures.load() & kDepthBlend)) {
			return;
		}
		if (const auto found = _deep.find(a_setID); found != _deep.end()) {
			a_send.deep = true;
			a_send.deepMask = found->second.mask;
			a_send.deepValues = found->second.values;
		}
	}

	std::uint64_t FaceAuthority::MaskFor(const Held& a_held) const noexcept
	{
		const auto every = _morphs >= 64 ? ~std::uint64_t{ 0 } : (std::uint64_t{ 1 } << _morphs) - 1;
		return a_held.speaking ? (every & ~_mouth) : every;
	}

	void FaceAuthority::OnOrder(const Order& a_order)
	{
		if (!Available() || a_order.formID == 0) {
			return;
		}
		const bool clears = a_order.kind == Order::Kind::kClearExpression ||
		                    (a_order.kind == Order::Kind::kApplyExpression && IsClear(a_order.setID));
		Send send;
		send.formID = a_order.formID;
		bool stopGlance = false;
		{
			NamedLock lock{ _lock, "face authority" };
			if (!lock) {
				return;
			}
			if (clears) {
				// Sent whether or not we think we hold them: a clear that finds nothing on
				// the other side costs one message, a hold left there costs a frozen face.
				_held.erase(a_order.formID);
				send.clear = true;
				stopGlance = true;
			} else if (a_order.kind == Order::Kind::kApplyExpression) {
				if (!ValuesOf(a_order.setID, send.values)) {
					return;
				}
				auto& held = _held[a_order.formID];
				held.setID = a_order.setID;
				send.owned = MaskFor(held);
				DeepOf(held.setID, send);
			} else if (a_order.kind == Order::Kind::kSayTopic) {
				// Their side hands the mouth to the line itself, for as long as the engine
				// plays it -- better than any guess here, and any line, not just ours.
				if (_peerFeatures.load() & kEngineLines) {
					return;
				}
				// Only a face we hold has a mouth of ours to give back; anybody else's
				// lips were never ours.
				const auto found = _held.find(a_order.formID);
				if (found == _held.end()) {
					return;
				}
				auto& held = found->second;
				held.speakingUntil = Clock::now() + kSpeakingWindow;
				if (held.speaking || !ValuesOf(held.setID, send.values)) {
					return;   // already speaking: the window was only extended
				}
				held.speaking = true;
				send.owned = MaskFor(held);
				DeepOf(held.setID, send);
			} else {
				return;
			}
		}
		// Outside our lock: the receiver takes its own.
		Dispatch(send);
		// A face let go ends any glance it was in the middle of ('RFAG' target 0 = stop).
		if (stopGlance && (_peerFeatures.load() & kGlances)) {
			Dispatch(Glance{ a_order.formID, 0, 0, 0.0f });
		}
	}

	void FaceAuthority::Pump()
	{
		if (!Available()) {
			return;
		}
		std::vector<Send> sends;
		{
			NamedLock lock{ _lock, "face authority" };
			if (!lock) {
				return;
			}
			const auto now = Clock::now();
			for (auto& [formID, held] : _held) {
				if (!held.speaking || now < held.speakingUntil) {
					continue;
				}
				held.speaking = false;
				Send send;
				send.formID = formID;
				send.owned = MaskFor(held);
				DeepOf(held.setID, send);
				if (ValuesOf(held.setID, send.values)) {
					sends.push_back(send);
				}
			}
		}
		for (const auto& send : sends) {
			Dispatch(send);
		}
		if (_peerFeatures.load() & kGlances) {
			for (const auto& glance : DueGlances(Clock::now())) {
				Dispatch(glance);
			}
		}
	}

	std::vector<FaceAuthority::Glance> FaceAuthority::DueGlances(Clock::time_point a_now)
	{
		struct Seen
		{
			std::uint32_t formID;
			bool          oral;
			bool          speaking;
		};
		std::vector<Seen> seen;
		{
			NamedLock lock{ _lock, "face authority" };
			if (!lock) {
				return {};
			}
			for (const auto& [formID, held] : _held) {
				seen.push_back(Seen{ formID, held.setID.starts_with("Rapport_Oral"sv), held.speaking });
			}
		}
		std::vector<Glance> out;
		NamedLock           glanceLock{ _glanceLock, "glances" };
		if (!glanceLock) {
			return out;
		}
		std::erase_if(_nextGlance, [&](const auto& a_entry) {
			return std::ranges::none_of(seen, [&](const Seen& a_seen) { return a_seen.formID == a_entry.first; });
		});
		// Where everyone is, once: the partner test compares every pair.
		std::vector<std::pair<std::uint32_t, RE::NiPoint3>> where;
		for (const auto& one : seen) {
			const auto* actor = RE::TESForm::GetFormByID<RE::Actor>(one.formID);
			if (actor && actor->Get3D()) {
				where.emplace_back(one.formID, actor->GetPosition());
			}
		}
		const auto nearestTo = [&](std::uint32_t a_id) -> std::uint32_t {
			const auto self = std::ranges::find(where, a_id, &std::pair<std::uint32_t, RE::NiPoint3>::first);
			if (self == where.end()) {
				return 0;
			}
			std::uint32_t best = 0;
			float         bestD = kPartnerReach;
			for (const auto& [id, pos] : where) {
				if (id == a_id) {
					continue;
				}
				const float d = std::hypot(pos.x - self->second.x, pos.y - self->second.y, pos.z - self->second.z);
				if (d < bestD) {
					bestD = d;
					best = id;
				}
			}
			return best;
		};
		const auto randomMs = [&](float a_lo, float a_hi) {
			return std::chrono::milliseconds(
				static_cast<int>(std::uniform_real_distribution<float>{ a_lo, a_hi }(_dice) * 1000.0f));
		};
		for (const auto& [formID, oral, speaking] : seen) {
			// The player's eyes are the camera's; a speaking face is busy with its line.
			if (formID == 0x14 || speaking) {
				continue;
			}
			auto [next, fresh] = _nextGlance.try_emplace(formID, a_now);
			if (!fresh && a_now < next->second) {
				continue;   // not due: no persona asked, no lock of Barks' taken
			}
			const auto style = StyleFor(Barks::GetSingleton().PersonaOf(formID), oral);
			if (fresh) {
				// The first look comes after a while, not the moment the face is put on.
				next->second = a_now + randomMs(style.everyMin, style.everyMax);
				continue;
			}
			const auto  partner = nearestTo(formID);
			const float seconds = std::uniform_real_distribution<float>{ style.forMin, style.forMax }(_dice);
			next->second = a_now + std::chrono::milliseconds(static_cast<int>(seconds * 1000.0f)) +
			               randomMs(style.everyMin, style.everyMax);
			if (partner == 0 || nearestTo(partner) != formID) {
				continue;   // nobody close, or the closest one is someone else's partner
			}
			out.push_back(Glance{ formID, partner, static_cast<std::uint32_t>(seconds * 1000.0f), oral ? 0.7f : 0.5f });
		}
		return out;
	}

	void FaceAuthority::Dispatch(const Glance& a_glance)
	{
		const auto messaging = F4SE::GetMessagingInterface();
		if (!messaging) {
			return;
		}
		GlanceMessage message{ kVersion, a_glance.looker, a_glance.target, a_glance.durationMs, a_glance.lidsOpen, 0 };
		messaging->Dispatch(kGlance, &message, sizeof(message), kPeer);
		logger::debug("glance: {:08X} looks at {:08X} for {} ms", a_glance.looker, a_glance.target, a_glance.durationMs);
	}

	void FaceAuthority::SendKnobs()
	{
		if (!_peer.load()) {
			return;   // nobody to tell; the hello sends them
		}
		const auto     path = std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "anatomy.json";
		nlohmann::json doc = nlohmann::json::object();
		std::ifstream  file{ path };
		if (!file) {
			logger::info("anatomy knobs: no {} - Anatomy keeps its own ini", PathText(path));
			return;
		}
		try {
			file >> doc;
		} catch (const std::exception& e) {
			logger::error("anatomy knobs: {} is not valid json ({}) - Anatomy keeps its own ini", PathText(path), e.what());
			return;
		}
		McmSettings::Overlay("Anatomy", doc);
		const auto number = [&](const char* a_key, float a_default, float a_lo, float a_hi) {
			const auto  it = doc.find(a_key);
			const float v = it != doc.end() && it->is_number() ? it->get<float>() : a_default;
			return std::clamp(v, a_lo, a_hi);
		};
		KnobMessage message{};
		message.version = kVersion;
		constexpr const char* kBits[] = { "aim", "shape", "lipFit", "faceReaction", "deepFace" };
		for (std::uint32_t i = 0; i < std::size(kBits); ++i) {
			if (McmSettings::ReadBool(doc, kBits[i], true)) {
				message.enabled |= 1u << i;
			}
		}
		message.lipClearance = number("lipClearance", 0.05f, 0.0f, 0.2f);
		message.lipSpeed = number("lipSpeed", 1.0f, 0.25f, 3.0f);
		message.shaftScale = number("shaftScale", 0.85f, 0.6f, 1.2f);
		message.headMin = number("headMin", 1.2f, 1.0f, 2.0f);
		message.headMax = (std::max)(message.headMin, number("headMax", 1.4f, 1.0f, 2.0f));
		message.reactScale = number("reactScale", 1.0f, 0.0f, 2.0f);
		if (const auto messaging = F4SE::GetMessagingInterface()) {
			messaging->Dispatch(kKnobs, &message, sizeof(message), kPeer);
		}
		logger::info("anatomy knobs: sent (enabled {:05b}, clearance {:.2f}, lip speed {:.2f}, shaft {:.2f}, head "
					 "{:.2f}..{:.2f}, reaction {:.2f})",
			message.enabled, message.lipClearance, message.lipSpeed, message.shaftScale, message.headMin,
			message.headMax, message.reactScale);
	}

	void FaceAuthority::Reset()
	{
		{
			NamedLock lock{ _lock, "face authority" };
			_held.clear();
		}
		// And tell the other side, formID 0 = everyone. It drops every hold on a load by
		// itself; this is the anatomy session's belt-and-braces (2026-09-24), for a hold
		// that would otherwise outlive the world it was made in.
		if (Available()) {
			Send send;
			send.clear = true;
			Dispatch(send);
		}
	}

	void FaceAuthority::Dispatch(const Send& a_send)
	{
		const auto messaging = F4SE::GetMessagingInterface();
		if (!messaging) {
			return;
		}
		// One message, or one RFAS+RFAD pair, at a time: a clear from another thread may not
		// land between an RFAS and its RFAD. Not _lock -- the receiver takes its own lock.
		static std::mutex sending;
		const std::scoped_lock one{ sending };
		if (a_send.clear) {
			ClearMessage message{ kVersion, a_send.formID };
			messaging->Dispatch(kClear, &message, sizeof(message), kPeer);
			return;
		}
		SetMessage message{ kVersion, a_send.formID, a_send.owned, {} };
		std::ranges::copy(a_send.values, message.value);
		messaging->Dispatch(kSet, &message, sizeof(message), kPeer);
		// Right after its RFAS, which (as agreed) drops whatever deep face that actor had:
		// a face with no deep one is simply never blended.
		if (a_send.deep) {
			SetMessage deep{ kVersion, a_send.formID, a_send.deepMask, {} };
			std::ranges::copy(a_send.deepValues, deep.value);
			messaging->Dispatch(kDeep, &deep, sizeof(deep), kPeer);
		}
	}
}
