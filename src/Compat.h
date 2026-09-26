#pragma once

// The calls that differ between the classic OG-only library (alandtse's CommonLibF4) and
// CommonLibF4RD, the one that runs on OG, NG and AE (owner, 2026-09-26). Everything else is
// the same API. Built with RP_RUNTIME_DATABASE, the RD side; without it, the old one, kept so
// the OG-only plugin still builds for comparison.
//
// One rule on the RD side: an engine function CommonLibF4RD does not declare is resolved
// through REL::IDDatabase::resolve, which REPORTS a miss instead of stopping the game the way
// a plain REL::ID does. A miss turns that one feature off, said once in the log.

namespace RP::Compat
{
	// The NPC record's sex: 1 female, 0 male. CommonLibF4RD's GetSex() returns the number;
	// alandtse's returns RE::SEX.
	[[nodiscard]] inline int Sex(RE::TESNPC* a_npc)
	{
		if (!a_npc) {
			return -1;
		}
#ifdef RP_RUNTIME_DATABASE
		return static_cast<int>(a_npc->GetSex());
#else
		return static_cast<int>(a_npc->GetSex() == RE::SEX::kFemale ? 1 : 0);
#endif
	}
	[[nodiscard]] inline bool Female(RE::TESNPC* a_npc) { return Sex(a_npc) == 1; }
	[[nodiscard]] inline bool Male(RE::TESNPC* a_npc) { return Sex(a_npc) == 0; }

	// The name the game shows for this reference: a custom name on it (Rapport's own, O-10;
	// a quest's) first, then its base record's. alandtse's GetDisplayFullName is the engine's
	// own call; CommonLibF4RD does not declare it, so the RD side reads the same two sources
	// through what it does declare. The text lives in the game's string pool, so the pointer
	// stays good as GetDisplayFullName's did. Never null.
	[[nodiscard]] inline const char* DisplayName(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return "";
		}
#ifdef RP_RUNTIME_DATABASE
		auto* base = a_ref->GetObjectReference();
		if (a_ref->extraList && a_ref->extraList->HasType(RE::EXTRA_DATA_TYPE::kTextDisplayData)) {
			if (auto* text = a_ref->extraList->GetByType<RE::ExtraTextDisplayData>()) {
				const char* shown = text->GetDisplayName(base).c_str();
				if (shown && *shown) {
					return shown;
				}
			}
		}
		if (!base) {
			return "";
		}
		const auto name = RE::TESFullName::GetFullName(*base);
		return name.empty() ? "" : name.data();
#else
		const char* name = a_ref->GetDisplayFullName();
		return name ? name : "";
#endif
	}

	// The engine's custom-name call (ExtraDataList::SetOverrideName, OG id 222303). False when
	// this runtime has no known address for it: the name is kept in the co-save and simply not
	// shown until one is known.
	inline bool SetOverrideName(RE::ExtraDataList* a_list, const char* a_name)
	{
		if (!a_list || !a_name) {
			return false;
		}
#ifdef RP_RUNTIME_DATABASE
		// AE id 2190167 (alandtse's NG id, proven 2026-09-26): f4rd-runtime.bin holds it at
		// 1.11.240 0x27DD70, the AE Address Library agrees, and the body matches OG's 0x89440 --
		// look up extra type 0x99, build a 0x48-byte ExtraTextDisplayData if absent, set the name,
		// set bit 0 at +0x10 -- with a lock around the list added in AE.
		static const auto resolved = REL::IDDatabase::get().resolve(REL::ID(222303, 2190167));
		if (!resolved) {
			static std::once_flag said;
			std::call_once(said, [] {
				logger::warn("names: this game version has no known address for the custom-name call ({}) - "
							 "names are kept, not shown",
					REL::id_resolve_status_text(resolved.status));
			});
			return false;
		}
		using func_t = void(RE::ExtraDataList*, const char*);
		reinterpret_cast<func_t*>(REL::Module::get().base() + *resolved.rva)(a_list, a_name);
		return true;
#else
		a_list->SetOverrideName(a_name);
		return true;
#endif
	}

	// The record flag "disabled" (bit 11), which alandtse's TESForm reads as IsDisabled().
	[[nodiscard]] inline bool Disabled(const RE::TESForm* a_form)
	{
		return a_form && (a_form->GetFormFlags() & (1u << 11)) != 0;
	}

	// A form type for a log line. CommonLibF4RD has no GetFormTypeString; its number reads
	// against ENUM_FORM_ID.
	[[nodiscard]] inline std::string FormTypeText(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return "?";
		}
#ifdef RP_RUNTIME_DATABASE
		return std::format("type {}", static_cast<int>(a_form->GetFormType()));
#else
		return std::string{ RE::TESForm::GetFormTypeString(a_form->GetFormType()) };
#endif
	}

	// ---- the co-save ----------------------------------------------------------------------
	// CommonLibF4RD hands the interface out const, keeps SetUniqueID non-const, and has no
	// one-argument ReadRecordData/WriteRecordData templates.
	inline void SetUniqueID(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_id)
	{
		const_cast<F4SE::SerializationInterface*>(a_intfc)->SetUniqueID(a_id);
	}

	// The next record. CommonLibF4RD's wrapper logs a warning every time this is false,
	// which is how F4SE says the list has ended, on every load (the Silhouette port,
	// 2026-09-26). Its wrapper is a cast onto F4SE's own interface, so call that directly.
	inline bool NextRecordInfo(const F4SE::SerializationInterface* a_intfc, std::uint32_t& a_type,
		std::uint32_t& a_version, std::uint32_t& a_length)
	{
#ifdef RP_RUNTIME_DATABASE
		const auto& raw = reinterpret_cast<const F4SE::detail::F4SESerializationInterface&>(*a_intfc);
		return raw.GetNextRecordInfo(&a_type, &a_version, &a_length);
#else
		return a_intfc->GetNextRecordInfo(a_type, a_version, a_length);
#endif
	}

	template <class T>
	std::uint32_t Read(const F4SE::SerializationInterface* a_intfc, T& a_value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		return a_intfc->ReadRecordData(std::addressof(a_value), static_cast<std::uint32_t>(sizeof(T)));
	}

	template <class T>
	bool Write(const F4SE::SerializationInterface* a_intfc, const T& a_value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		return a_intfc->WriteRecordData(std::addressof(a_value), static_cast<std::uint32_t>(sizeof(T)));
	}
}
