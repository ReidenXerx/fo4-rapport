#include "Pairing.h"

namespace
{
	[[nodiscard]] float Distance(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs) noexcept
	{
		const auto dx = a_lhs.x - a_rhs.x;
		const auto dy = a_lhs.y - a_rhs.y;
		const auto dz = a_lhs.z - a_rhs.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	[[nodiscard]] bool ShareAFaction(const RE::Actor& a_lhs, const RE::Actor& a_rhs)
	{
		const auto left = a_lhs.GetNPC();
		const auto right = a_rhs.GetNPC();
		if (!left || !right) {
			return false;
		}

		// Base faction lists only. Runtime faction changes live in extra data and are
		// not consulted: this is a weighting signal, not a gate, so being approximately
		// right is enough and being cheap matters more.
		for (const auto& mine : left->factions) {
			if (!mine.faction) {
				continue;
			}
			for (const auto& theirs : right->factions) {
				if (mine.faction == theirs.faction) {
					return true;
				}
			}
		}
		return false;
	}

	[[nodiscard]] bool IsNight()
	{
		const auto calendar = RE::Calendar::GetSingleton();
		if (!calendar || !calendar->gameHour) {
			return false;
		}
		const auto hour = calendar->gameHour->GetValue();
		return hour < 6.0f || hour >= 20.0f;
	}
}

namespace AF
{
	void PairWeights::LoadFrom(const nlohmann::json& a_json)
	{
		const auto read = [&a_json](const char* a_key, float& a_target) {
			if (const auto it = a_json.find(a_key); it != a_json.end() && it->is_number()) {
				a_target = it->get<float>();
			}
		};

		read("maxPairDistance", maxPairDistance);
		read("observerRadius", observerRadius);
		read("proximityFalloff", proximityFalloff);
		read("observerFalloff", observerFalloff);
		read("minimumScore", minimumScore);

		if (const auto it = a_json.find("observerTolerance"); it != a_json.end() && it->is_number()) {
			observerTolerance = it->get<std::uint32_t>();
		}
		read("proximity", proximity);
		read("sharedFaction", sharedFaction);
		read("interior", interior);
		read("night", night);
		read("perObserver", perObserver);
		read("playerNear", playerNear);
	}

	std::vector<ScoredPair> RankPairs(
		const std::vector<RE::Actor*>&   a_candidates,
		const std::vector<RE::NiPoint3>& a_observerPositions,
		const PairWeights&               a_weights,
		std::size_t                      a_keep)
	{
		std::vector<ScoredPair> ranked;
		if (a_candidates.size() < 2) {
			return ranked;
		}

		const auto  player = RE::PlayerCharacter::GetSingleton();
		const auto  playerPos = player ? player->GetPosition() : RE::NiPoint3{};
		const bool  night = IsNight();
		const auto  observerRadiusSq = a_weights.observerRadius * a_weights.observerRadius;

		for (std::size_t i = 0; i < a_candidates.size(); ++i) {
			for (std::size_t j = i + 1; j < a_candidates.size(); ++j) {
				auto* const first = a_candidates[i];
				auto* const second = a_candidates[j];
				if (!first || !second) {
					continue;
				}

				const auto here = first->GetPosition();
				const auto there = second->GetPosition();
				const auto distance = Distance(here, there);
				if (distance > a_weights.maxPairDistance) {
					continue;
				}

				// Two people who would shoot each other are not a pair, at any score.
				if (first->GetHostileToActor(second) || second->GetHostileToActor(first)) {
					continue;
				}

				const RE::NiPoint3 midpoint{
					(here.x + there.x) * 0.5f,
					(here.y + there.y) * 0.5f,
					(here.z + there.z) * 0.5f
				};

				PairSignals signals;
				signals.distance = distance;
				signals.sharedFaction = ShareAFaction(*first, *second);
				signals.night = night;

				if (const auto cell = first->GetParentCell()) {
					signals.interior = cell->IsInterior();
				}

				for (const auto& position : a_observerPositions) {
					const auto dx = position.x - midpoint.x;
					const auto dy = position.y - midpoint.y;
					const auto dz = position.z - midpoint.z;
					if ((dx * dx + dy * dy + dz * dz) <= observerRadiusSq) {
						++signals.observers;
					}
				}
				// The pair stand at their own midpoint, so they count themselves.
				signals.observers -= (std::min)(signals.observers, 2u);

				if (player) {
					signals.playerNear = Distance(playerPos, midpoint) <= a_weights.observerRadius;
				}

				// Squared by default: 200 units apart is worth far more than twice
				// what 400 is, which is what "they were standing together" means.
				const auto nearness = 1.0f - (distance / a_weights.maxPairDistance);
				const auto proximity = std::pow(nearness, a_weights.proximityFalloff);

				float score = a_weights.proximity * proximity;
				if (signals.sharedFaction) {
					score += a_weights.sharedFaction;
				}
				if (signals.interior) {
					score += a_weights.interior;
				}
				if (signals.night) {
					score += a_weights.night;
				}
				// The first few onlookers are free. Past that it climbs steeply, so a
				// crowd is prohibitive without ever being a hard veto.
				if (signals.observers > a_weights.observerTolerance) {
					const auto excess = static_cast<float>(signals.observers - a_weights.observerTolerance);
					score -= a_weights.perObserver * std::pow(excess, a_weights.observerFalloff);
				}
				if (signals.playerNear) {
					score -= a_weights.playerNear;
				}

				ranked.push_back(ScoredPair{ first, second, score, signals });
			}
		}

		std::ranges::sort(ranked, [](const ScoredPair& a_lhs, const ScoredPair& a_rhs) {
			return a_lhs.score > a_rhs.score;
		});

		if (ranked.size() > a_keep) {
			ranked.resize(a_keep);
		}
		return ranked;
	}
}
