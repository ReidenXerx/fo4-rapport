#include "Ledger.h"

#include "Aftermath.h"

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
	constexpr std::uint32_t kVersion = 1;

	// A corrupt length could otherwise ask for an enormous allocation before the
	// arithmetic below catches it. The real world is a few hundred.
	constexpr std::uint32_t kMaxEntries = 100000;
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
		const auto now = GameHours();
		std::scoped_lock lock{ _lock };

		auto& first = _records[a_first];
		first.lastSceneAt = now;
		first.lastPartner = a_second;
		++first.scenes;

		auto& second = _records[a_second];
		second.lastSceneAt = now;
		second.lastPartner = a_first;
		++second.scenes;

		logger::info(
			"ledger: {:08X} and {:08X} have now had {} and {} scene(s), at hour {:.1f}",
			a_first, a_second, first.scenes, second.scenes, now);
	}

	void Ledger::RecordRefusal(std::uint32_t a_first, std::uint32_t a_second)
	{
		const auto now = GameHours();
		std::scoped_lock lock{ _lock };

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
		std::scoped_lock lock{ _lock };
		_records[a_formID].need = a_need;
	}

	ActorRecord Ledger::Get(std::uint32_t a_formID) const
	{
		std::scoped_lock lock{ _lock };
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
		std::scoped_lock lock{ _lock };
		return _records.size();
	}

	void Ledger::Clear()
	{
		std::scoped_lock lock{ _lock };
		_records.clear();
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
		logger::info("ledger: cleared for a new game or a load");
	}

	void Ledger::Save(const F4SE::SerializationInterface* a_intfc) const
	{
		std::scoped_lock lock{ _lock };

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

			std::scoped_lock lock{ _lock };
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
				record.need = entry.need;

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

			Aftermath::Mark mark;
			mark.formID = *resolved;
			mark.expiresAt = entry.expiresAt;
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
