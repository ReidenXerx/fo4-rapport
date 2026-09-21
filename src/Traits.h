#pragma once

namespace RP::Traits
{
	// The identity a derived trait is keyed on. Fallout4.esm forms keep their raw id
	// (unchanged, so vanilla NPCs keep the persona they already have); a form from
	// any other plugin is keyed on (plugin filename, local id), because the runtime
	// id carries a load-order byte and reordering plugins would otherwise give every
	// modded NPC a new persona and faithfulness mid-playthrough. Spawned (FF) forms
	// have no file identity; their runtime id is all there is.
	[[nodiscard]] inline std::uint32_t StableID(std::uint32_t a_formID)
	{
		const auto index = a_formID >> 24;
		if (index == 0x00 || index == 0xFF) {
			return a_formID;
		}
		const auto* form = RE::TESForm::GetFormByID(a_formID);
		const auto* file = form ? form->GetFile(0) : nullptr;
		if (!file) {
			return a_formID;
		}
		std::uint32_t hash = 2166136261u;   // FNV-1a over the lower-cased filename
		for (const char c : file->GetFilename()) {
			hash ^= static_cast<std::uint8_t>(std::tolower(static_cast<unsigned char>(c)));
			hash *= 16777619u;
		}
		const auto local = file->IsLight() ? (a_formID & 0x00000FFFu) : (a_formID & 0x00FFFFFFu);
		return hash ^ (local * 0x9E3779B1u);
	}

	// Faithfulness, 0 (strays freely) .. 1 (never looks elsewhere). Owner's decision
	// 2026-09-21: "a random persistent value per npc". Persistent the way personas
	// are (R-7) - DERIVED from the form id, so the same NPC is the same forever, on
	// every machine, at zero save cost.
	//
	// A different mixer from PersonaOf's multiplicative hash and Expressions' % 6,
	// deliberately: a trait that correlated with a persona would make "reticent"
	// secretly mean "faithful", which nobody decided.
	[[nodiscard]] inline float Faithfulness(std::uint32_t a_formID) noexcept
	{
		// murmur3's finaliser: every input bit reaches every output bit.
		auto h = StableID(a_formID) ^ 0x5BD1E995u;
		h ^= h >> 16;
		h *= 0x85EBCA6Bu;
		h ^= h >> 13;
		h *= 0xC2B2AE35u;
		h ^= h >> 16;
		return static_cast<float>(h >> 8) / static_cast<float>(1u << 24);
	}
}
