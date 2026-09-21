#pragma once

namespace RP::Traits
{
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
		auto h = a_formID ^ 0x5BD1E995u;
		h ^= h >> 16;
		h *= 0x85EBCA6Bu;
		h ^= h >> 13;
		h *= 0xC2B2AE35u;
		h ^= h >> 16;
		return static_cast<float>(h >> 8) / static_cast<float>(1u << 24);
	}
}
