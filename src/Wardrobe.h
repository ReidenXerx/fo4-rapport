#pragma once

#include "NamedLock.h"

namespace RP
{
	// What an actor was wearing before AAF undressed them, kept until they are dressed again.
	//
	// AAF snapshots a loadout at scene start and restores it in its OWN scene-end teardown.
	// That snapshot lives inside AAF: a scene that never ends properly -- Rapport reviving a
	// deaf AAF, a scene retired with no end event, a panic clear, a save written mid-scene and
	// loaded later -- leaves the actor naked for good, behaving normally otherwise (owner,
	// 2026-10-01). So Rapport keeps its own copy, in the co-save, and puts back only what is
	// MISSING: never over anything the actor wears now, never an item they no longer carry,
	// never while AAF holds them.
	//
	// The read and the equip are Papyrus's (Bridge.psc NoteOutfit / Redress): F4SE's
	// GetWornItem knows every runtime's biped layout, where a C++ read would assume one (Layout.h).
	// The plugin keeps the lists, decides when to look, and hears back what was found.
	class Wardrobe
	{
	public:
		[[nodiscard]] static Wardrobe& GetSingleton() noexcept;

		// One actor's worn armour, slot by slot (F4SE slot index 0-30 = biped slots 30-60), read by
		// the bridge just before a scene can undress them. MERGED into what is already held for
		// them, never replacing it: an actor a broken scene left naked has less on at the next
		// scene's start, and that must not erase the outfit still owed to them.
		void NoteOutfit(std::uint32_t a_actor, const std::vector<std::int32_t>& a_slots,
			const std::vector<std::int32_t>& a_items);

		// The bridge's answer for one piece (Bridge.psc Redress).
		enum class Checked : std::int32_t
		{
			kWorn = 0,       // on them already (AAF dressed them, or we just did): done
			kHeld = 1,       // AAF holds them: a scene, or a stale flag -- look again later
			kNotLoaded = 2,  // nobody to dress right now: later
			kGone = 3,       // not carried any more, or something else of theirs fills the slot: done
			kFreeOnce = 4,   // free for the first time: come back, so AAF's own redress goes first
			kEquipped = 5    // put back on: done
		};
		void OnChecked(std::uint32_t a_actor, std::uint32_t a_item, Checked a_result);

		// From the bridge's pump, every poll: queue the looks that have come due.
		void Pump();

		// A load: the arriving save's outfits come from the co-save (Restore), so the world
		// being left is forgotten first.
		void Forget();

		// The co-save (Ledger's WARD record).
		struct Piece
		{
			std::uint32_t item{ 0 };
			std::uint32_t slots{ 0 };   // bit n = F4SE slot index n (n < 31)
		};
		[[nodiscard]] std::vector<std::pair<std::uint32_t, std::vector<Piece>>> Saved() const;
		void Restore(std::vector<std::pair<std::uint32_t, std::vector<Piece>>> a_outfits);

	private:
		using Clock = std::chrono::steady_clock;

		struct Outfit
		{
			std::vector<Piece> pieces;
			Clock::time_point  nextLook{};
			std::uint32_t      looks{ 0 };    // looks queued, to give up on an outfit nobody resolves
			bool               freeOnce{ false };
		};

		// A scene lasts minutes; a look while AAF holds them costs one cheap stack per piece.
		static constexpr auto kLookEvery = std::chrono::seconds{ 30 };
		// After the first sighting free of AAF: long enough for AAF's own redress to land first.
		static constexpr auto kSecondLook = std::chrono::seconds{ 10 };
		// Looks at a LOADED actor before an outfit nobody resolves is dropped (~1 h held by AAF).
		static constexpr std::uint32_t kMaxLooks = 120;
		static constexpr std::size_t   kMaxActors = 256;
		static constexpr std::size_t   kMaxPieces = 32;

		mutable std::timed_mutex                     _lock;
		std::unordered_map<std::uint32_t, Outfit>    _outfits;
		Clock::time_point                            _nextPump{};
	};
}
