#pragma once

#include "NamedLock.h"
#include "Pairing.h"

namespace RP
{
	// The ranked pairs, published once a tick so an addon can read them.
	//
	// This is the half of the addon door that was missing. RequestScene and CanRun
	// take two actors an addon already has -- but enumeration, filtering and
	// scoring are the framework's job by design, so until now an addon had no way
	// to GET a pair. Rapport's own scheduler reached straight into the ranked
	// vector, which is why the decision it makes is marked a stand-in.
	//
	// The split: Rapport SCORES, the addon SELECTS. PairSignals already says as
	// much in its own comment -- "an addon decides what to do with these, the
	// framework only says what is true of the two actors" -- and minimumScore is
	// documented as what an addon would need, never enforced here. This publishes
	// those measurements instead of keeping them for one branch of one tick.
	class Candidates
	{
	public:
		// FORM IDS, not RE::Actor*. The pointers in a ScoredPair are valid on the
		// tick that produced them; Papyrus reads this later, on its own poll, and an
		// actor can unload in between. A stale form id resolves to None and the
		// addon skips it; a stale pointer is a crash in somebody else's mod.
		struct Offer
		{
			std::uint32_t first{ 0 };
			std::uint32_t second{ 0 };
			float         score{ 0.0f };
			PairSignals   signals{};
		};

		[[nodiscard]] static Candidates& GetSingleton() noexcept;

		// Main thread, once a tick. Replaces the whole list: a candidate that was
		// offered last tick and is gone this tick should stop being offered.
		void Publish(const std::vector<ScoredPair>& a_ranked);

		// Nothing is loaded, or the pass found no viable pair.
		void Clear();

		[[nodiscard]] std::size_t Count() const;
		// Bumped on every Publish. An addon reading the list across several calls
		// reads it before and after, and starts over if it moved.
		[[nodiscard]] std::uint32_t Generation() const noexcept { return _generation.load(); }

		// Empty when the index is out of range, which is the normal way a Papyrus
		// loop finds the end rather than an error.
		[[nodiscard]] std::optional<Offer> At(std::size_t a_index) const;
		// The published offer for these two, either order - what the Narrator
		// explains a request with.
		[[nodiscard]] std::optional<Offer> Find(std::uint32_t a_first, std::uint32_t a_second) const;

		// An addon has taken over the decision, so Rapport's stand-in stops making
		// it. One-way for the session, and deliberately not a config setting: two
		// things starting scenes is the failure this prevents, and it should not
		// depend on an ini being edited to match which mods are installed.
		void StandDown(std::string_view a_who);
		[[nodiscard]] bool StoodDown() const;

	private:
		mutable std::timed_mutex _lock;
		std::vector<Offer>       _offers;
		std::atomic_uint32_t     _generation{ 0 };
		bool                     _stoodDown{ false };
	};
}
