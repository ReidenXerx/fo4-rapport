#include "Candidates.h"

namespace RP
{
	Candidates& Candidates::GetSingleton() noexcept
	{
		static Candidates singleton;
		return singleton;
	}

	void Candidates::Publish(const std::vector<ScoredPair>& a_ranked)
	{
		std::vector<Offer> built;
		built.reserve(a_ranked.size());
		for (const auto& pair : a_ranked) {
			if (!pair.first || !pair.second) {
				continue;
			}
			built.push_back(Offer{
				pair.first->GetFormID(),
				pair.second->GetFormID(),
				pair.score,
				pair.signals });
		}

		NamedLock lock{ _lock, "candidates" };
		_offers = std::move(built);
		_generation.fetch_add(1);
	}

	void Candidates::Clear()
	{
		NamedLock lock{ _lock, "candidates" };
		_offers.clear();
		_generation.fetch_add(1);
	}

	std::optional<Candidates::Offer> Candidates::Find(std::uint32_t a_first, std::uint32_t a_second) const
	{
		NamedLock lock{ _lock, "candidates" };
		for (const auto& offer : _offers) {
			if ((offer.first == a_first && offer.second == a_second) || (offer.first == a_second && offer.second == a_first)) {
				return offer;
			}
		}
		return std::nullopt;
	}

	std::size_t Candidates::Count() const
	{
		NamedLock lock{ _lock, "candidates" };
		return _offers.size();
	}

	std::optional<Candidates::Offer> Candidates::At(std::size_t a_index) const
	{
		NamedLock lock{ _lock, "candidates" };
		if (a_index >= _offers.size()) {
			return std::nullopt;
		}
		return _offers[a_index];
	}

	void Candidates::StandDown(std::string_view a_who)
	{
		{
			NamedLock lock{ _lock, "candidates" };
			if (_stoodDown) {
				return;
			}
			_stoodDown = true;
		}

		// Outside the lock, and said once rather than every poll: an addon calling
		// this on a timer would otherwise fill the log with it.
		logger::info(
			"candidates: \"{}\" has taken over the decision - Rapport's stand-in will not start "
			"scenes any more. It still scores and publishes every pass",
			a_who.empty() ? "an addon" : a_who);
	}

	bool Candidates::StoodDown() const
	{
		NamedLock lock{ _lock, "candidates" };
		return _stoodDown;
	}
}
