#include "FaceAuthority.h"

namespace RP
{
	namespace
	{
		constexpr std::uint32_t kSet = 0x52464153;     // 'RFAS'
		constexpr std::uint32_t kClear = 0x52464143;   // 'RFAC'
		constexpr std::uint32_t kVersion = 1;
		constexpr const char*   kPeer = "OCBPC plugin";   // Anatomy's cbp.dll, by its F4SE name

		// Hello feature bits, as the anatomy session defined them.
		constexpr std::uint32_t kEngineLines = 1u << 1;   // lines keep the mouth, for their real length

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
		} catch (const std::exception& e) {
			logger::error("face authority: {} has a shape this plugin cannot read ({}) - the AAF path alone",
				PathText(path), e.what());
			return;
		}

		{
			NamedLock lock{ _lock, "face authority" };
			_sets = std::move(sets);
			_mouth = mouth;
			_morphs = morphs;
			_unknown.clear();
		}
		_loaded.store(true);
		logger::info("face authority: {} set(s) of {} morph(s), {} of them the mouth; Anatomy {}", _sets.size(), morphs,
			std::popcount(mouth),
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
				if (ValuesOf(held.setID, send.values)) {
					sends.push_back(send);
				}
			}
		}
		for (const auto& send : sends) {
			Dispatch(send);
		}
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
		if (a_send.clear) {
			ClearMessage message{ kVersion, a_send.formID };
			messaging->Dispatch(kClear, &message, sizeof(message), kPeer);
			return;
		}
		SetMessage message{ kVersion, a_send.formID, a_send.owned, {} };
		std::ranges::copy(a_send.values, message.value);
		messaging->Dispatch(kSet, &message, sizeof(message), kPeer);
	}
}
