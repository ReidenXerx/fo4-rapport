#include "FaceAuthority.h"

#include "McmSettings.h"

namespace RP
{
	namespace
	{
		constexpr std::uint32_t kSet = 0x52464153;     // 'RFAS'
		constexpr std::uint32_t kClear = 0x52464143;   // 'RFAC'
		constexpr std::uint32_t kDeep = 0x52464144;    // 'RFAD': the held face at full depth
		constexpr std::uint32_t kKnobs = 0x5246414B;   // 'RFAK': Anatomy's knobs from Rapport's MCM
		constexpr std::uint32_t kVersion = 1;
		constexpr const char*   kPeer = "OCBPC plugin";   // Anatomy's cbp.dll, by its F4SE name

		// Hello feature bits, as the anatomy session defined them.
		constexpr std::uint32_t kEngineLines = 1u << 1;   // lines keep the mouth, for their real length
		constexpr std::uint32_t kDepthBlend = 1u << 3;    // blends toward our deep face by depth

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
		logger::info("face authority: depth blend {}", (a_features & kDepthBlend)
														   ? "ON - a held oral face follows the depth in the mouth"
														   : "not offered by this cbp.dll - the oral face holds still");
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
