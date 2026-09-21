#include "Ledger.h"

#include "Narrator.h"

#include "Aftermath.h"
#include "Config.h"
#include "Expressions.h"
#include "PapyrusLink.h"

namespace
{
	// F4SE identifies a plugin's data by a four-character code, and a record
	// inside it the same way. Both are ours alone.
	constexpr std::uint32_t FourCC(const char (&a_code)[5])
	{
		return static_cast<std::uint32_t>(a_code[0]) |
		       (static_cast<std::uint32_t>(a_code[1]) << 8) |
		       (static_cast<std::uint32_t>(a_code[2]) << 16) |
		       (static_cast<std::uint32_t>(a_code[3]) << 24);
	}

	constexpr auto kPluginID = FourCC("RPRT");
	constexpr auto kActorRecord = FourCC("ACTR");
	constexpr auto kOverlayRecord = FourCC("OVRL");
	constexpr auto kFaceRecord = FourCC("FACE");
	constexpr auto kSceneRecord = FourCC("SCNE");
	constexpr auto kPairRecord = FourCC("PAIR");
	constexpr std::uint32_t kVersion = 1;

	// The pair table has its OWN version. kVersion is shared by every record and a
	// mismatch refuses the record outright, so bumping it for the relationship
	// store would have wiped every existing save's whole ledger. v1 still loads.
	constexpr std::uint32_t kPairVersion = 2;

	// What one completed scene is worth: the fraction of the remaining distance to
	// +1. 0.15 is the size of Chemistry's old per-scene C-3 bonus, so the first
	// scene means what it meant before - and the fortieth means far less.
	constexpr float kBondPerScene = 0.15f;

	// Below this a pair has no relationship worth keeping past its scene history.
	constexpr float kBondFloor = 0.01f;

	// The engine's relationship rank as a starting bond, logged on every seed.
	//
	// MEASURED (R-4, 2026-09-21) for the positive half: strangers 0, a barmaid and
	// her employers 1, brothers 3, a mother and son 3, a married couple (the
	// Codmans) 4 - Skyrim's scale (Friend 1, Ally 3, Lover 4). No negative pair was
	// at hand, so the negative side assumes the same symmetry. A partner starts
	// closer on top: a spouse is not just an ally. Blood adds nothing either way.
	[[nodiscard]] float VanillaSeed(std::int32_t a_rank, bool a_partner) noexcept
	{
		const auto rank = std::clamp(static_cast<float>(a_rank) * 0.15f, -0.6f, 0.6f);
		return std::clamp(rank + (a_partner ? 0.20f : 0.0f), -1.0f, 1.0f);
	}

	// A corrupt length could otherwise ask for an enormous allocation before the
	// arithmetic below catches it. The real world is a few hundred.
	constexpr std::uint32_t kMaxEntries = 100000;

	// Runtime ceilings, enforced whatever PruneHours says. Far above any real save:
	// 2000 actor records is 56 KB and 4000 pairs is 112 KB (28-byte entries).
	constexpr std::size_t kMaxActorRecords = 2000;
	constexpr std::size_t kMaxPairRecords = 4000;
}

namespace RP
{
	Ledger& Ledger::GetSingleton() noexcept
	{
		static Ledger singleton;
		return singleton;
	}

	bool Ledger::Register(const F4SE::SerializationInterface* a_intfc)
	{
		if (!a_intfc) {
			logger::critical(
				"ledger: F4SE gave us no serialization interface - NOTHING WILL BE REMEMBERED "
				"between saves");
			return false;
		}

		a_intfc->SetUniqueID(kPluginID);
		a_intfc->SetSaveCallback(OnSave);
		a_intfc->SetLoadCallback(OnLoad);
		a_intfc->SetRevertCallback(OnRevert);
		logger::info("ledger: registered with the save game");
		return true;
	}

	float Ledger::GameHours()
	{
		const auto calendar = RE::Calendar::GetSingleton();
		if (!calendar || !calendar->gameDaysPassed) {
			// Before a save is loaded there is no game time. Calendar::GetHoursPassed
			// answers 24.0 here, which reads as a real hour and is not one, so we
			// say "unknown" instead and let every caller decide.
			return -1.0f;
		}
		return calendar->gameDaysPassed->GetValue() * 24.0f;
	}

	void Ledger::RecordScene(std::uint32_t a_first, std::uint32_t a_second)
	{
		float         before = 0.0f;
		float         after = 0.0f;
		std::uint32_t together = 0;
		RecordSceneLocked(a_first, a_second, before, after, together);
		// Outside the ledger lock: the Narrator takes its own and reads the ledger.
		Narrator::GetSingleton().OnBondChanged(a_first, a_second, before, after, together, true);
	}

	void Ledger::RecordSceneLocked(std::uint32_t a_first, std::uint32_t a_second, float& a_before, float& a_after,
		std::uint32_t& a_together)
	{
		const auto now = GameHours();
		NamedLock lock{ _lock, "ledger" };
		if (_dead.contains(a_first) || _dead.contains(a_second)) {
			logger::info("ledger: {:08X} + {:08X} - one of them died during the scene, nothing recorded", a_first, a_second);
			return;
		}

		auto& first = _records[a_first];
		first.lastSceneAt = now;
		first.lastPartner = a_second;
		++first.scenes;

		auto& second = _records[a_second];
		second.lastSceneAt = now;
		second.lastPartner = a_first;
		++second.scenes;

		// And the pair itself, which the two actor records cannot reconstruct once
		// either of them has been with anybody else.
		auto& pair = _pairs[PairKey(a_first, a_second)];
		pair.lastSceneAt = now;
		++pair.scenes;
		// R-10: a completed scene raises the relationship, with diminishing returns.
		a_before = pair.bond;
		pair.bond += kBondPerScene * (1.0f - pair.bond);
		a_after = pair.bond;
		a_together = pair.scenes;
		pair.lastTouchedAt = now;
		pair.lastReason = BondReason::kScene;

		logger::info(
			"ledger: {:08X} and {:08X} have now had {} and {} scene(s), at hour {:.1f}; together {} - bond {:+.3f}",
			a_first, a_second, first.scenes, second.scenes, now, pair.scenes, pair.bond);
	}

	float Ledger::Bond(std::uint32_t a_first, std::uint32_t a_second) const
	{
		NamedLock lock{ _lock, "ledger" };
		const auto it = _pairs.find(PairKey(a_first, a_second));
		return it == _pairs.end() ? 0.0f : it->second.bond;
	}

	Ledger::BondReason Ledger::LastBondReason(std::uint32_t a_first, std::uint32_t a_second) const
	{
		NamedLock lock{ _lock, "ledger" };
		const auto it = _pairs.find(PairKey(a_first, a_second));
		return it == _pairs.end() ? BondReason::kNone : it->second.lastReason;
	}

	float Ledger::AddBond(std::uint32_t a_first, std::uint32_t a_second, float a_amount, BondReason a_reason)
	{
		// !isfinite: NaN passes "== 0" and clamp, and would sit in the save forever.
		if (a_first == 0 || a_second == 0 || a_first == a_second || a_amount == 0.0f || !std::isfinite(a_amount)) {
			return Bond(a_first, a_second);
		}
		const auto    amount = std::clamp(a_amount, -1.0f, 1.0f);
		const auto    now = GameHours();
		float         before = 0.0f;
		float         after = 0.0f;
		std::uint32_t scenes = 0;
		{
			NamedLock lock{ _lock, "ledger" };
			if (_dead.contains(a_first) || _dead.contains(a_second)) {
				return 0.0f;
			}
			// R-5: THIS is the interaction that creates the record, never mere proximity.
			auto& pair = _pairs[PairKey(a_first, a_second)];
			before = pair.bond;
			// Toward +1 or -1 by that fraction of the distance left: bounded, and
			// diminishing for every source alike.
			pair.bond += amount > 0.0f ? amount * (1.0f - pair.bond) : amount * (1.0f + pair.bond);
			pair.bond = std::clamp(pair.bond, -1.0f, 1.0f);
			pair.lastTouchedAt = now;
			pair.lastReason = a_reason;
			after = pair.bond;
			scenes = pair.scenes;
		}
		logger::info("relationship: {:08X} + {:08X} bond {:+.3f} -> {:+.3f} (asked {:+.3f}, reason {})",
			a_first, a_second, before, after, amount, static_cast<int>(a_reason));
		// Outside the ledger lock: the Narrator takes its own and reads the ledger.
		Narrator::GetSingleton().OnBondChanged(a_first, a_second, before, after, scenes, false);
		return after;
	}

	void Ledger::NoteAffair(std::uint32_t a_first, std::uint32_t a_second)
	{
		if (a_first == 0 || a_second == 0 || a_first == a_second) {
			return;
		}
		NamedLock lock{ _lock, "ledger" };
		auto& pair = _pairs[PairKey(a_first, a_second)];
		if (!pair.affair) {
			pair.affair = true;
			pair.lastTouchedAt = GameHours();
			logger::info("relationship: {:08X} + {:08X} - an affair: one of them is partnered elsewhere", a_first, a_second);
		}
	}

	bool Ledger::IsAffair(std::uint32_t a_first, std::uint32_t a_second) const
	{
		NamedLock lock{ _lock, "ledger" };
		const auto it = _pairs.find(PairKey(a_first, a_second));
		return it != _pairs.end() && it->second.affair;
	}

	void Ledger::SeedFromVanilla(std::uint32_t a_first, std::uint32_t a_second, std::int32_t a_rank, bool a_blood,
		bool a_partner)
	{
		if (a_first == 0 || a_second == 0 || a_first == a_second) {
			return;
		}
		const auto now = GameHours();
		NamedLock lock{ _lock, "ledger" };
		auto& pair = _pairs[PairKey(a_first, a_second)];
		// The FLAGS follow the engine every time: people marry after they meet, and
		// a store frozen at the first interaction called that couple strangers
		// forever while Chemistry's live HasPartner disagreed. Only the BOND seed is
		// one-shot (R-2).
		pair.incest = a_blood;
		pair.partner = a_partner;
		if (pair.seeded) {
			return;   // imported once; from then on our own arithmetic runs (R-2)
		}
		pair.seeded = true;
		// The engine's rank on the scale MEASURED in game (R-4) - see VanillaSeed.
		const auto seed = VanillaSeed(a_rank, a_partner);
		if (seed != 0.0f) {
			pair.bond = ComposeSeed(seed, pair.bond);
			pair.lastTouchedAt = now;
			pair.lastReason = BondReason::kVanilla;
		}
		logger::info("relationship: {:08X} + {:08X} seeded from vanilla - rank {}, blood {}, partner {} -> bond {:+.3f}",
			a_first, a_second, a_rank, a_blood, a_partner, pair.bond);
	}

	float Ledger::SeedValue(std::int32_t a_rank, bool a_partner) noexcept
	{
		return VanillaSeed(a_rank, a_partner);
	}

	float Ledger::ComposeSeed(float a_seed, float a_moved) noexcept
	{
		// The seed is the BASE, and earlier movement is applied on top of it with the
		// same distance-left rule every source uses (R-10). Plain addition made the
		// result depend on which call landed first: a spouse (+0.80) and a -0.5 gave
		// -0.10 one way round and +0.30 the other. For a one-signed history the
		// distance-left steps multiply, so this is exact.
		const auto bond = a_moved >= 0.0f ? a_seed + a_moved * (1.0f - a_seed) : a_seed + a_moved * (1.0f + a_seed);
		return std::clamp(bond, -1.0f, 1.0f);
	}

	float Ledger::PreviewBond(std::uint32_t a_first, std::uint32_t a_second, std::int32_t a_rank, bool a_partner) const
	{
		NamedLock lock{ _lock, "ledger" };
		const auto it = _pairs.find(PairKey(a_first, a_second));
		if (it != _pairs.end() && it->second.seeded) {
			return it->second.bond;
		}
		return ComposeSeed(VanillaSeed(a_rank, a_partner), it == _pairs.end() ? 0.0f : it->second.bond);
	}

	bool Ledger::IsSeeded(std::uint32_t a_first, std::uint32_t a_second) const
	{
		NamedLock lock{ _lock, "ledger" };
		const auto it = _pairs.find(PairKey(a_first, a_second));
		return it != _pairs.end() && it->second.seeded;
	}

	bool Ledger::IsIncest(std::uint32_t a_first, std::uint32_t a_second) const
	{
		NamedLock lock{ _lock, "ledger" };
		const auto it = _pairs.find(PairKey(a_first, a_second));
		return it != _pairs.end() && it->second.incest;
	}

	bool Ledger::IsPartner(std::uint32_t a_first, std::uint32_t a_second) const
	{
		NamedLock lock{ _lock, "ledger" };
		const auto it = _pairs.find(PairKey(a_first, a_second));
		return it != _pairs.end() && it->second.partner;
	}

	void Ledger::ForgetActor(std::uint32_t a_formID)
	{
		if (a_formID == 0) {
			return;
		}
		NamedLock lock{ _lock, "ledger" };
		// Remembered for the session: a scene that was running when they died ends
		// AFTER this, and its RecordScene would otherwise write them straight back.
		_dead.insert(a_formID);
		const auto actorRows = _records.erase(a_formID);
		const auto before = _pairs.size();
		std::erase_if(_pairs, [&](const auto& a_entry) {
			return static_cast<std::uint32_t>(a_entry.first >> 32) == a_formID ||
			       static_cast<std::uint32_t>(a_entry.first & 0xFFFFFFFFu) == a_formID;
		});
		if (actorRows || before != _pairs.size()) {
			logger::info("relationship: {:08X} died - {} actor row and {} pair row(s) forgotten", a_formID,
				actorRows, before - _pairs.size());
		}
	}

	void Ledger::NoteAlive(const std::vector<std::uint32_t>& a_loaded)
	{
		NamedLock lock{ _lock, "ledger" };
		if (_dead.empty()) {
			return;
		}
		for (const auto id : a_loaded) {
			_dead.erase(id);
		}
	}

	float Ledger::HoursSincePair(std::uint32_t a_first, std::uint32_t a_second) const
	{
		const auto now = GameHours();
		NamedLock lock{ _lock, "ledger" };

		const auto it = _pairs.find(PairKey(a_first, a_second));
		if (it == _pairs.end() || it->second.lastSceneAt < 0.0f || now < 0.0f) {
			return std::numeric_limits<float>::infinity();
		}
		return (std::max)(0.0f, now - it->second.lastSceneAt);
	}

	std::uint32_t Ledger::PairScenes(std::uint32_t a_first, std::uint32_t a_second) const
	{
		NamedLock lock{ _lock, "ledger" };
		const auto it = _pairs.find(PairKey(a_first, a_second));
		return it == _pairs.end() ? 0u : it->second.scenes;
	}

	float Ledger::HoursSinceRefusal(std::uint32_t a_formID) const
	{
		const auto now = GameHours();
		NamedLock lock{ _lock, "ledger" };

		const auto it = _records.find(a_formID);
		if (it == _records.end() || it->second.lastRefusedAt < 0.0f || now < 0.0f) {
			return std::numeric_limits<float>::infinity();
		}
		return (std::max)(0.0f, now - it->second.lastRefusedAt);
	}

	void Ledger::RecordRefusal(std::uint32_t a_first, std::uint32_t a_second)
	{
		const auto now = GameHours();
		NamedLock lock{ _lock, "ledger" };

		for (const auto formID : { a_first, a_second }) {
			if (formID == 0) {
				continue;
			}
			auto& record = _records[formID];
			record.lastRefusedAt = now;
			++record.refusals;
		}
	}

	void Ledger::SetNeed(std::uint32_t a_formID, float a_need)
	{
		if (!std::isfinite(a_need)) {
			return;
		}
		NamedLock lock{ _lock, "ledger" };
		_records[a_formID].need = a_need;
	}

	ActorRecord Ledger::Get(std::uint32_t a_formID) const
	{
		NamedLock lock{ _lock, "ledger" };
		const auto entry = _records.find(a_formID);
		return entry == _records.end() ? ActorRecord{} : entry->second;
	}

	float Ledger::HoursSinceScene(std::uint32_t a_formID) const
	{
		const auto record = Get(a_formID);
		if (!record.EverHadAScene()) {
			return std::numeric_limits<float>::infinity();
		}

		const auto now = GameHours();
		if (now < 0.0f) {
			return std::numeric_limits<float>::infinity();
		}

		// Game time can go backwards -- a player loading an older save inside the
		// same session is the ordinary way. A negative gap would read as "just
		// now" and hold the actor back for good, so treat it as long ago.
		const auto since = now - record.lastSceneAt;
		return since < 0.0f ? std::numeric_limits<float>::infinity() : since;
	}

	std::size_t Ledger::Size() const
	{
		NamedLock lock{ _lock, "ledger" };
		return _records.size();
	}

	void Ledger::Clear()
	{
		NamedLock lock{ _lock, "ledger" };
		_records.clear();
		_pairs.clear();
		_dead.clear();
	}

	namespace
	{
		// R-6. One sink on the engine's global death event covers every NPC, and
		// costs nothing in the save - a per-actor Papyrus registration would have
		// meant up to 2000 of them, persisted.
		class DeathSink final : public RE::BSTEventSink<RE::TESDeathEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent& a_event,
				RE::BSTEventSource<RE::TESDeathEvent>*) override
			{
				// The event fires as the actor starts dying and again once dead; the
				// second is the one that is final. The player's death ends in a load,
				// which replaces this state anyway - their rows are left alone.
				const auto* dying = a_event.actorDying.get();
				if (a_event.dead && dying && dying != RE::PlayerCharacter::GetSingleton()) {
					Ledger::GetSingleton().ForgetActor(dying->GetFormID());
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Ledger::RegisterDeathSink()
	{
		static DeathSink sink;
		if (auto* source = RE::TESDeathEvent::GetEventSource()) {
			source->RegisterSink(&sink);
			logger::info("relationship: listening for deaths - a dead NPC's rows are forgotten (R-6)");
		} else {
			logger::error("relationship: no death event source - dead NPCs' rows will stay until the cap");
		}
	}

	void Ledger::LoadPairs(
		const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length)
	{
		if (a_version != 1 && a_version != kPairVersion) {
			logger::warn(
				"ledger: the save's pair table is version {} and this build reads 1 and {} - it is left "
				"alone, and no pair has a history",
				a_version, kPairVersion);
			return;
		}
		// v1 is scene history only; its pairs arrive with no relationship and unseeded.
		const auto entrySize = a_version == 1 ? sizeof(PairEntryV1) : sizeof(PairEntry);

		std::uint32_t count = 0;
		if (a_intfc->ReadRecordData(count) != sizeof(count)) {
			logger::error("ledger: the pair table's count could not be read");
			return;
		}
		if (count > kMaxEntries) {
			logger::error("ledger: the pair table claims {} entries - refusing it", count);
			return;
		}
		// The same length check the other records get: count and size must agree
		// before a single entry is trusted.
		if (a_length != sizeof(count) + count * entrySize) {
			logger::error(
				"ledger: the pair table is {} bytes but {} entries need {} - refusing it",
				a_length, count, sizeof(count) + count * entrySize);
			return;
		}

		NamedLock lock{ _lock, "ledger" };
		_pairs.clear();
		std::uint32_t dropped = 0;
		for (std::uint32_t i = 0; i < count; ++i) {
			PairEntry entry{};
			if (a_version == 1) {
				PairEntryV1 old{};
				if (a_intfc->ReadRecordData(old) != sizeof(old)) {
					logger::error("ledger: the pair table ended early at {} of {}", i, count);
					return;
				}
				entry = PairEntry{ old.first, old.second, old.lastSceneAt, old.scenes, 0.0f, old.lastSceneAt, 0u };
			} else if (a_intfc->ReadRecordData(entry) != sizeof(entry)) {
				logger::error("ledger: the pair table ended early at {} of {}", i, count);
				return;
			}
			if (entry.first == 0 || entry.second == 0) {
				continue;
			}

			// BOTH ids through ResolveFormID, like every other record here, and this
			// is not only about uninstalled mods.
			//
			// A form id carries its plugin's load-order index. Move a plugin and the
			// raw number points at a DIFFERENT actor -- so a pair table read without
			// resolving would hand back a history belonging to two people who never
			// met, and the repeat-pairing bonus would fire on the strength of it.
			// Wrong history is worse than none, so an unresolvable pair is dropped
			// rather than guessed at.
			const auto first = a_intfc->ResolveFormID(entry.first);
			const auto second = a_intfc->ResolveFormID(entry.second);
			if (!first || !second) {
				++dropped;
				continue;
			}
			// Named fields, one lookup: a positional initializer silently reorders when
			// a field is added. Non-finite values from a damaged save become the
			// "never" sentinels rather than poisoning a sort or a bond forever.
			const auto finiteOr = [](float a_value, float a_fallback) { return std::isfinite(a_value) ? a_value : a_fallback; };
			const auto reason = (entry.flags >> 8) & 0xFFu;
			auto&      pair = _pairs[PairKey(*first, *second)];
			pair = PairRecord{};
			pair.lastSceneAt = finiteOr(entry.lastSceneAt, -1.0f);
			pair.scenes = entry.scenes;
			pair.bond = std::clamp(finiteOr(entry.bond, 0.0f), -1.0f, 1.0f);
			pair.lastTouchedAt = finiteOr(entry.lastTouchedAt, -1.0f);
			pair.seeded = (entry.flags & 1u) != 0;
			pair.incest = (entry.flags & 2u) != 0;
			pair.partner = (entry.flags & 4u) != 0;
			pair.lastReason = reason <= static_cast<std::uint32_t>(BondReason::kAddon) ? static_cast<BondReason>(reason) : BondReason::kNone;
			pair.affair = (entry.flags & 8u) != 0;
		}
		logger::info(
			"ledger: read {} pair(s) from the save, {} dropped because a plugin is gone",
			_pairs.size(), dropped);
	}

	// ---- the save game ------------------------------------------------------

	void F4SEAPI Ledger::OnSave(const F4SE::SerializationInterface* a_intfc)
	{
		GetSingleton().Save(a_intfc);
	}

	void F4SEAPI Ledger::OnLoad(const F4SE::SerializationInterface* a_intfc)
	{
		GetSingleton().Load(a_intfc);
	}

	void F4SEAPI Ledger::OnRevert(const F4SE::SerializationInterface*)
	{
		// Fires before a load and on a new game. Everything in memory belongs to
		// the character being left behind; keeping any of it would carry one
		// playthrough's history into another.
		GetSingleton().Clear();
		Aftermath::GetSingleton().Clear();
		Expressions::GetSingleton().Reset();
		logger::info("ledger: cleared for a new game or a load");
	}

	void Ledger::Save(const F4SE::SerializationInterface* a_intfc) const
	{
		NamedLock lock{ _lock, "ledger" };

		Prune();

		if (!a_intfc->OpenRecord(kActorRecord, kVersion)) {
			logger::error("ledger: could not open the save record - this save will remember nothing");
			return;
		}

		const auto count = static_cast<std::uint32_t>(_records.size());
		a_intfc->WriteRecordData(count);

		for (const auto& [formID, record] : _records) {
			const Entry entry{
				formID,
				record.lastSceneAt,
				record.lastRefusedAt,
				record.lastPartner,
				record.scenes,
				record.refusals,
				record.need
			};
			a_intfc->WriteRecordData(entry);
		}

		logger::info("ledger: wrote {} actor(s) into the save", count);

		// Second record, its own type and its own version, so the aftermath layout
		// can change without invalidating everyone's history.
		const auto marks = Aftermath::GetSingleton().Marks();
		if (!a_intfc->OpenRecord(kOverlayRecord, kVersion)) {
			logger::error("ledger: could not open the overlay record - aftermath will not survive this save");
			return;
		}

		std::uint32_t written = 0;
		std::vector<OverlayEntry> entries;
		entries.reserve(marks.size());
		for (const auto& mark : marks) {
			OverlayEntry entry{};
			entry.formID = mark.formID;
			entry.expiresAt = mark.expiresAt;
			// Truncation would silently name a set that does not exist, and the
			// removal would then never match. Dropping it is the visible failure.
			if (mark.setID.size() >= sizeof(entry.setID)) {
				logger::error(
					"ledger: overlay set \"{}\" is too long to save ({} of {} characters) - it is "
					"dropped, and that overlay will not be removed on schedule",
					mark.setID, mark.setID.size(), sizeof(entry.setID) - 1);
				continue;
			}
			std::memcpy(entry.setID, mark.setID.c_str(), mark.setID.size() + 1);
			entries.push_back(entry);
			++written;
		}

		a_intfc->WriteRecordData(written);
		for (const auto& entry : entries) {
			a_intfc->WriteRecordData(entry);
		}
		logger::info("ledger: wrote {} standing overlay(s) into the save", written);

		// The pair table. Its own record type, so a save written before this
		// existed simply has none and the table starts empty -- and an older build
		// reading a newer save skips it with the unknown-record warning rather than
		// misreading it as something else.
		if (a_intfc->OpenRecord(kPairRecord, kPairVersion)) {
			const auto pairCount = static_cast<std::uint32_t>(_pairs.size());
			a_intfc->WriteRecordData(pairCount);
			for (const auto& [key, record] : _pairs) {
				const PairEntry entry{
					static_cast<std::uint32_t>(key >> 32),
					static_cast<std::uint32_t>(key & 0xFFFFFFFFu),
					record.lastSceneAt,
					record.scenes,
					record.bond,
					record.lastTouchedAt,
					(record.seeded ? 1u : 0u) | (record.incest ? 2u : 0u) | (record.partner ? 4u : 0u) | (record.affair ? 8u : 0u) |
						(static_cast<std::uint32_t>(record.lastReason) << 8)
				};
				a_intfc->WriteRecordData(entry);
			}
			logger::info("ledger: wrote {} pair(s) into the save", pairCount);
		}

		// Who is wearing a face we put on them. Applied with lock=true, so nothing
		// else will ever take it off; if the game ends here, this list is the only
		// thing that knows to.
		const auto faces = Expressions::GetSingleton().Wearing();
		if (a_intfc->OpenRecord(kFaceRecord, kVersion)) {
			const auto faceCount = static_cast<std::uint32_t>(faces.size());
			a_intfc->WriteRecordData(faceCount);
			for (const auto formID : faces) {
				a_intfc->WriteRecordData(formID);
			}
			if (faceCount > 0) {
				logger::info("ledger: wrote {} actor(s) wearing a Rapport face", faceCount);
			}
		}

		// And who a scene had hold of. AAF stamped them busy, and only a scene
		// ending clears that -- a scene that this save is about to outlive.
		const auto [first, second] = PapyrusLink::GetSingleton().InFlightPair();
		if (a_intfc->OpenRecord(kSceneRecord, kVersion)) {
			a_intfc->WriteRecordData(first);
			a_intfc->WriteRecordData(second);
			if (first != 0 || second != 0) {
				logger::warn(
					"ledger: this save is being written DURING a scene ({:08X}, {:08X}) - "
					"the next load will release them",
					first, second);
			}
		}
	}

	void Ledger::Prune() const
	{
		const auto hours = Config::GetSingleton().pruneHours;
		const auto now = GameHours();
		if (hours <= 0.0f || now < 0.0f) {
			return;
		}

		// Anyone still wearing something keeps their record whatever its age: the
		// overlay outlives the memory of how it got there, and dropping the record
		// would not remove the overlay.
		std::unordered_set<std::uint32_t> wearing;
		for (const auto& mark : Aftermath::GetSingleton().Marks()) {
			wearing.insert(mark.formID);
		}

		const auto before = _records.size();
		std::erase_if(_records, [&](const auto& entry) {
			const auto& [formID, record] = entry;
			if (wearing.contains(formID)) {
				return false;
			}

			// A record holding NOTHING goes whatever its age, and this is the only
			// path here that was unbounded.
			//
			// SetNeed does _records[formID], so it creates a record -- and a record
			// created that way has no scene and no refusal, which made `last`
			// negative and the age test below false. It could never be dropped. An
			// addon that tracked need for every candidate it looked at would mint an
			// immortal record per actor it had ever seen, and pruning was powerless
			// against exactly the case most likely to happen.
			if (!record.EverHadAScene() && record.lastRefusedAt < 0.0f && record.need == 0.0f) {
				return true;
			}

			const auto last = (std::max)(record.lastSceneAt, record.lastRefusedAt);
			return last >= 0.0f && (now - last) > hours;
		});

		if (const auto dropped = before - _records.size(); dropped > 0) {
			logger::info(
				"ledger: dropped {} actor(s) nothing has happened to for {:.0f} game hours",
				dropped, hours);
		}

		// The pair table on the same clock. It grows with the SQUARE of the people
		// involved rather than with their number, so a long playthrough is where it
		// would quietly become the biggest thing in the co-save if nothing aged it
		// out.
		//
		// No wearing exception here: an overlay stands on an ACTOR, and keeping the
		// memory of who they were with does not keep it on them.
		const auto pairsBefore = _pairs.size();
		std::erase_if(_pairs, [&](const auto& entry) {
			const auto& record = entry.second;
			// A row holding NOTHING goes whatever its age - the hole the actor prune
			// above already closed: a stranger seeded at 0 whose scene was refused has
			// no scene and no touch, so the age test below could never fire for it.
			// Kept if it carries a FACT: a blood tie, an affair, or movement from dialogue,
			// a gift or an addon (re-seeding would erase that and break the one-shot seed).
			const bool moved = record.lastReason != BondReason::kNone && record.lastReason != BondReason::kVanilla;
			if (record.scenes == 0 && std::fabs(record.bond) < kBondFloor && !record.affair && !record.incest && !moved) {
				return true;
			}
			// R-6: a relationship is cleaned on death, never on a timer. Age still
			// clears scene HISTORY - but only for pairs with no bond to lose.
			return record.lastSceneAt >= 0.0f && (now - record.lastSceneAt) > hours &&
			       std::fabs(record.bond) < kBondFloor;
		});
		if (const auto dropped = pairsBefore - _pairs.size(); dropped > 0) {
			logger::info(
				"ledger: dropped {} pair(s) that have not met for {:.0f} game hours", dropped, hours);
		}

		// And a ceiling, which is a different guarantee from the clock above.
		//
		// PruneHours can be set to 0, and 0 means never prune. A player who does
		// that, or an addon that writes need for everybody, has no upper bound at
		// all -- so these caps hold whatever the config says, and drop the oldest
		// first. kMaxEntries guards the LOAD against a corrupt count; nothing
		// guarded runtime growth.
		//
		// The numbers are deliberately far above any real save: 4000 pairs is 112 KB (28-byte entries)
		// and 2000 actors is 56 KB, and reaching either means something is wrong
		// rather than that somebody played a long game.
		Cap(_records, kMaxActorRecords, "actor", [](const ActorRecord& a_record) {
			return (std::max)(a_record.lastSceneAt, a_record.lastRefusedAt);
		});
		Cap(_pairs, kMaxPairRecords, "pair", [](const PairRecord& a_record) {
			return (std::max)(a_record.lastSceneAt, a_record.lastTouchedAt);
		});
	}

	// Drops the oldest until the map fits. Templated over the two record types
	// because the only thing that differs is how a record reports its own age.
	template <class Map, class AgeOf>
	void Ledger::Cap(Map& a_map, std::size_t a_limit, std::string_view a_what, AgeOf a_ageOf) const
	{
		if (a_map.size() <= a_limit) {
			return;
		}

		std::vector<std::pair<float, typename Map::key_type>> byAge;
		byAge.reserve(a_map.size());
		for (const auto& [key, record] : a_map) {
			byAge.emplace_back(a_ageOf(record), key);
		}
		// Oldest first. A never-touched record sorts to the very front, which is
		// correct: it is the least worth keeping.
		std::ranges::sort(byAge, {}, &std::pair<float, typename Map::key_type>::first);

		const auto excess = a_map.size() - a_limit;
		for (std::size_t i = 0; i < excess; ++i) {
			a_map.erase(byAge[i].second);
		}
		logger::warn(
			"ledger: the {} table was over its ceiling of {} - dropped the {} oldest. Something is "
			"writing a great many of these, or PruneHours is 0",
			a_what, a_limit, excess);
	}

	void Ledger::Load(const F4SE::SerializationInterface* a_intfc)
	{
		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;

		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type == kOverlayRecord) {
				LoadOverlays(a_intfc, version, length);
				continue;
			}
			if (type == kFaceRecord) {
				LoadFaces(a_intfc, version, length);
				continue;
			}
			if (type == kSceneRecord) {
				LoadScene(a_intfc, version, length);
				continue;
			}
			if (type == kPairRecord) {
				LoadPairs(a_intfc, version, length);
				continue;
			}
			if (type != kActorRecord) {
				logger::warn("ledger: skipping an unknown record in the save");
				continue;
			}
			if (version != kVersion) {
				// Refusing is the honest answer: a record written by a different
				// layout read as this one is worse than no history at all.
				logger::warn(
					"ledger: the save holds version {} and this build writes {} - that history is "
					"left alone, and everyone starts fresh",
					version, kVersion);
				continue;
			}

			std::uint32_t count = 0;
			if (a_intfc->ReadRecordData(count) != sizeof(count)) {
				logger::error("ledger: the save record ended before its count - nothing was read");
				continue;
			}

			// The one check that separates a truncated save from a valid empty
			// one. Without it a short record reads as whatever happened to follow.
			const auto expected = sizeof(std::uint32_t) + static_cast<std::size_t>(count) * sizeof(Entry);
			if (count > kMaxEntries || expected != length) {
				logger::error(
					"ledger: the save says {} actor(s), which needs {} bytes, and the record is {} - "
					"refusing to read it rather than guessing",
					count, expected, length);
				continue;
			}

			NamedLock lock{ _lock, "ledger" };
			std::uint32_t dropped = 0;
			std::uint32_t read = 0;

			for (std::uint32_t i = 0; i < count; ++i) {
				Entry entry{};
				if (a_intfc->ReadRecordData(entry) != sizeof(entry)) {
					logger::error("ledger: the save ran out after {} of {} actor(s)", read, count);
					break;
				}
				++read;

				// A form id in a save is an index into THAT save's load order. If
				// the plugin it came from is gone, so is the actor, and F4SE says
				// so rather than handing back an id that now means someone else.
				const auto resolved = a_intfc->ResolveFormID(entry.formID);
				if (!resolved) {
					++dropped;
					continue;
				}

				ActorRecord record;
				record.lastSceneAt = entry.lastSceneAt;
				record.lastRefusedAt = entry.lastRefusedAt;
				record.scenes = entry.scenes;
				record.refusals = entry.refusals;
				record.need = std::isfinite(entry.need) ? entry.need : 0.0f;

				// The partner is a form id too, and it can rot independently.
				// A partner we cannot resolve becomes nobody, not a wrong somebody.
				if (entry.lastPartner != 0) {
					const auto partner = a_intfc->ResolveFormID(entry.lastPartner);
					record.lastPartner = partner.value_or(0);
				}

				_records[*resolved] = record;
			}

			logger::info(
				"ledger: read {} actor(s) from the save, {} dropped because their plugin is gone",
				_records.size(), dropped);
		}
	}

	void Ledger::LoadFaces(
		const F4SE::SerializationInterface* a_intfc,
		std::uint32_t                       a_version,
		std::uint32_t                       a_length)
	{
		if (a_version != kVersion) {
			logger::warn("ledger: the save holds face version {} - skipped", a_version);
			return;
		}

		std::uint32_t count = 0;
		if (a_intfc->ReadRecordData(count) != sizeof(count)) {
			return;
		}
		const auto expected = sizeof(std::uint32_t) * (static_cast<std::size_t>(count) + 1);
		if (count > kMaxEntries || expected != a_length) {
			logger::error(
				"ledger: the face record says {} actor(s) ({} bytes) and is {} - refusing to read it",
				count, expected, a_length);
			return;
		}

		std::vector<std::uint32_t> wearing;
		wearing.reserve(count);
		for (std::uint32_t i = 0; i < count; ++i) {
			std::uint32_t formID = 0;
			if (a_intfc->ReadRecordData(formID) != sizeof(formID)) {
				break;
			}
			if (const auto resolved = a_intfc->ResolveFormID(formID)) {
				wearing.push_back(*resolved);
			}
		}
		Expressions::GetSingleton().RestoreWearing(std::move(wearing));
	}

	void Ledger::LoadScene(
		const F4SE::SerializationInterface* a_intfc,
		std::uint32_t                       a_version,
		std::uint32_t                       a_length)
	{
		if (a_version != kVersion || a_length != sizeof(std::uint32_t) * 2) {
			logger::warn("ledger: the in-flight record does not look right - skipped");
			return;
		}

		std::uint32_t first = 0;
		std::uint32_t second = 0;
		if (a_intfc->ReadRecordData(first) != sizeof(first) ||
			a_intfc->ReadRecordData(second) != sizeof(second)) {
			return;
		}

		const auto a = first ? a_intfc->ResolveFormID(first).value_or(0u) : 0u;
		const auto b = second ? a_intfc->ResolveFormID(second).value_or(0u) : 0u;
		PapyrusLink::GetSingleton().RestoreInFlightPair(a, b);
	}

	void Ledger::LoadOverlays(
		const F4SE::SerializationInterface* a_intfc,
		std::uint32_t                       a_version,
		std::uint32_t                       a_length)
	{
		if (a_version != kVersion) {
			logger::warn(
				"ledger: the save holds overlay version {} and this build writes {} - those overlays "
				"are left alone, which means they will stay on until something else removes them",
				a_version, kVersion);
			return;
		}

		std::uint32_t count = 0;
		if (a_intfc->ReadRecordData(count) != sizeof(count)) {
			logger::error("ledger: the overlay record ended before its count - nothing was read");
			return;
		}

		const auto expected =
			sizeof(std::uint32_t) + static_cast<std::size_t>(count) * sizeof(OverlayEntry);
		if (count > kMaxEntries || expected != a_length) {
			logger::error(
				"ledger: the save says {} overlay(s), which needs {} bytes, and the record is {} - "
				"refusing to read it rather than guessing",
				count, expected, a_length);
			return;
		}

		std::vector<Aftermath::Mark> marks;
		marks.reserve(count);
		std::uint32_t dropped = 0;

		for (std::uint32_t i = 0; i < count; ++i) {
			OverlayEntry entry{};
			if (a_intfc->ReadRecordData(entry) != sizeof(entry)) {
				logger::error("ledger: the save ran out after {} of {} overlay(s)", marks.size(), count);
				break;
			}

			const auto resolved = a_intfc->ResolveFormID(entry.formID);
			if (!resolved) {
				++dropped;
				continue;
			}

			// A set id written by a corrupt or hostile save could be unterminated.
			entry.setID[sizeof(entry.setID) - 1] = '\0';

			// A save can carry an hour that makes no sense -- written by a build
			// with a different window, or simply corrupt. An overlay whose expiry
			// is NaN never expires, which is the one outcome this feature exists
			// to prevent, so an unreadable hour becomes "now" rather than "never".
			auto expires = entry.expiresAt;
			if (!std::isfinite(expires)) {
				logger::error(
					"ledger: {:08X} has an unreadable expiry for {} - expiring it now instead",
					*resolved, entry.setID);
				expires = 0.0f;
			}

			Aftermath::Mark mark;
			mark.formID = *resolved;
			mark.expiresAt = expires;
			mark.setID = entry.setID;
			mark.asked = false;
			marks.push_back(std::move(mark));
		}

		logger::info(
			"ledger: read {} standing overlay(s) from the save, {} dropped because their plugin is gone",
			marks.size(), dropped);
		Aftermath::GetSingleton().Restore(std::move(marks));
	}
}
