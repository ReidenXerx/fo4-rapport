#pragma once

namespace RP
{
	// What the framework MEASURES about a pair. No judgement here: an addon decides
	// what to do with these, the framework only says what is true of the two actors.
	struct PairSignals
	{
		float         distance{ 0.0f };
		bool          sharedFaction{ false };
		bool          interior{ false };
		bool          night{ false };
		std::uint32_t observers{ 0 };      // uninvolved living actors who could see the spot
		bool          playerNear{ false };
	};

	// Weights come from scoring.json so policy lives in config, not in this code.
	struct PairWeights
	{
		float maxPairDistance{ 1536.0f };
		float observerRadius{ 900.0f };

		// Both curves, not lines. Proximity falls off fast so that being near each
		// other beats having a faction in common, and the crowd penalty is flat until
		// a few bystanders have been forgiven: in this world nobody is shy, but
		// nobody performs for ten people either.
		float proximityFalloff{ 2.0f };
		std::uint32_t observerTolerance{ 2 };
		float observerFalloff{ 1.5f };

		float proximity{ 1.50f };
		float sharedFaction{ 0.60f };
		float interior{ 0.30f };
		float night{ 0.25f };
		float perObserver{ 0.20f };   // subtracted, past the tolerance
		float playerNear{ 0.50f };    // subtracted

		// What an addon would need before acting. The framework never enforces it;
		// it only reports whether the best pair cleared it.
		float minimumScore{ 0.90f };

		void LoadFrom(const nlohmann::json& a_json);
	};

	// Rapport's score, part by part. RankPairs sums exactly these, and the Narrator
	// prints them, so the numbers a player reads are the numbers that decided.
	struct ScoreParts
	{
		float proximity{ 0.0f };
		float faction{ 0.0f };
		float interior{ 0.0f };
		float night{ 0.0f };
		float crowd{ 0.0f };    // <= 0
		float player{ 0.0f };   // <= 0

		[[nodiscard]] float Total() const noexcept { return proximity + faction + interior + night + crowd + player; }
	};

	[[nodiscard]] ScoreParts Breakdown(const PairSignals& a_signals, const PairWeights& a_weights);

	struct ScoredPair
	{
		RE::Actor*  first{ nullptr };
		RE::Actor*  second{ nullptr };
		float       score{ 0.0f };
		PairSignals signals{};
	};

	// Ranks every non-hostile pair within range. Pure measurement, no side effects:
	// nothing here touches an actor, starts anything, or writes state.
	[[nodiscard]] std::vector<ScoredPair> RankPairs(
		const std::vector<RE::Actor*>&   a_candidates,
		const std::vector<RE::NiPoint3>& a_observerPositions,
		const PairWeights&               a_weights,
		std::size_t                      a_keep);
}
