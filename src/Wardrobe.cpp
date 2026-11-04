#include "Wardrobe.h"

#include "DoorHold.h"
#include "Orders.h"
#include "PapyrusLink.h"
#include "Scenarios.h"

namespace RP
{
	Wardrobe& Wardrobe::GetSingleton() noexcept
	{
		static Wardrobe singleton;
		return singleton;
	}

	void Wardrobe::NoteOutfit(std::uint32_t a_actor, const std::vector<std::int32_t>& a_slots,
		const std::vector<std::int32_t>& a_items)
	{
		if (a_actor == 0 || a_slots.size() != a_items.size()) {
			return;
		}
		NamedLock lock{ _lock, "wardrobe" };
		if (!_outfits.contains(a_actor) && _outfits.size() >= kMaxActors) {
			logger::warn("wardrobe: {} outfits held already - {:08X}'s is not kept", kMaxActors, a_actor);
			return;
		}
		auto& outfit = _outfits[a_actor];
		for (std::size_t i = 0; i < a_slots.size(); ++i) {
			const auto slot = a_slots[i];
			const auto item = static_cast<std::uint32_t>(a_items[i]);
			if (item == 0 || slot < 0 || slot > 30) {
				continue;
			}
			auto it = std::ranges::find(outfit.pieces, item, &Piece::item);
			if (it == outfit.pieces.end()) {
				if (outfit.pieces.size() >= kMaxPieces) {
					continue;
				}
				outfit.pieces.push_back(Piece{ item, 0 });
				it = std::prev(outfit.pieces.end());
			}
			it->slots |= 1u << slot;
		}
		// A new scene: look again from the start, the first look a whole interval away.
		outfit.nextLook = Clock::now() + kLookEvery;
		outfit.looks = 0;
		outfit.freeOnce = false;
		if (outfit.pieces.empty()) {
			_outfits.erase(a_actor);
			return;
		}
		logger::info("wardrobe: {:08X} goes in wearing {} piece(s) Rapport will see put back", a_actor,
			outfit.pieces.size());
	}

	void Wardrobe::SceneEnded(const std::vector<std::uint32_t>& a_actors)
	{
		static std::mt19937 random{ std::random_device{}() };
		const auto           now = Clock::now();
		for (const auto id : a_actors) {
			if (id == 0 || id == 0x14) {
				continue;
			}
			// Read before our lock: the persona is Barks', behind its own.
			const bool shy = Scenarios::GetSingleton().IsShy(id);
			NamedLock  lock{ _lock, "wardrobe" };
			const auto found = _outfits.find(id);
			if (found == _outfits.end()) {
				continue;
			}
			auto&      outfit = found->second;
			const auto wait = shy ? std::chrono::duration_cast<Clock::duration>(kShyLook)
			                      : std::chrono::duration_cast<Clock::duration>(std::chrono::seconds{
									std::uniform_int_distribution<std::int64_t>{ kEarliestLook.count(), kLatestLook.count() }(random) });
			outfit.nextLook = now + wait;
			outfit.freeOnce = false;
			outfit.heldIdle = 0;
			logger::info("wardrobe: {:08X}'s scene ended - dressed {} if AAF has not", id,
				shy ? "at once (shy)" : std::format("within {}s", std::chrono::duration_cast<std::chrono::seconds>(wait).count()));
		}
	}

	void Wardrobe::OnChecked(std::uint32_t a_actor, std::uint32_t a_item, Checked a_result)
	{
		// AAF's busy flag with no scene of theirs anywhere: evidence for a stale flag, read before
		// our lock (the scene count takes the door's).
		const bool idle = a_result == Checked::kHeld && !DoorHold::InAnyScene(a_actor);
		bool       release = false;
		{
		NamedLock  lock{ _lock, "wardrobe" };
		const auto found = _outfits.find(a_actor);
		if (found == _outfits.end()) {
			return;
		}
		auto& outfit = found->second;
		switch (a_result) {
		case Checked::kWorn:
		case Checked::kGone:
		case Checked::kEquipped:
			std::erase_if(outfit.pieces, [&](const Piece& a_piece) { return a_piece.item == a_item; });
			if (a_result == Checked::kEquipped) {
				logger::info("wardrobe: {:08X} put back on {:08X} - a scene had left it off", a_actor, a_item);
			}
			break;
		case Checked::kFreeOnce:
			outfit.freeOnce = true;
			outfit.nextLook = (std::min)(outfit.nextLook, Clock::now() + kSecondLook);
			break;
		case Checked::kHeld:
			// A scene, or AAF's busy flag left on a dead one. PapyrusLink::NoteActorBusy judges a
			// stale flag only when no scene runs ANYWHERE -- in a town where scenes keep starting,
			// never, and the actor walked the streets naked (Nexus 11-03). Here the evidence is the
			// actor's own: AAF's busy flag, and no scene AAF has been handed has them in it, look
			// after look. Then the flag goes through AAF's own unlock (Bridge ReleaseActor).
			outfit.freeOnce = false;
			if (!idle) {
				outfit.heldIdle = 0;
			} else if (outfit.heldIdleLook != outfit.looks) {
				outfit.heldIdleLook = outfit.looks;
				if (++outfit.heldIdle >= kStaleHeldLooks) {
					outfit.heldIdle = 0;
					outfit.nextLook = Clock::now() + kSecondLook;
					release = true;
				}
			}
			break;
		case Checked::kNotLoaded:
			break;
		}
		if (outfit.pieces.empty()) {
			_outfits.erase(found);
		}
		}
		if (release) {
			logger::info("wardrobe: {:08X} still carries AAF's busy flag and no scene has them - released through AAF's unlock, then dressed", a_actor);
			PapyrusLink::GetSingleton().QueueOrder(Order{ Order::Kind::kRelease, a_actor, {} });
		}
	}

	void Wardrobe::Pump()
	{
		const auto now = Clock::now();
		std::vector<Order> orders;
		{
			NamedLock lock{ _lock, "wardrobe" };
			if (_outfits.empty() || now < _nextPump) {
				return;
			}
			_nextPump = now + std::chrono::seconds{ 2 };
			for (auto it = _outfits.begin(); it != _outfits.end();) {
				auto& outfit = it->second;
				if (now < outfit.nextLook) {
					++it;
					continue;
				}
				// Only a loaded actor is worth a stack, and only those looks count: somebody left
				// naked in a place the player walked away from is still owed their clothes when
				// the player comes back (kMaxActors bounds how many wait).
				const auto* actor = RE::TESForm::GetFormByID<RE::Actor>(it->first);
				if (actor && actor->Get3D()) {
					if (++outfit.looks > kMaxLooks) {
						logger::info("wardrobe: {:08X} - {} look(s) and still unresolved, given up", it->first,
							kMaxLooks);
						it = _outfits.erase(it);
						continue;
					}
					for (const auto& piece : outfit.pieces) {
						// The item rides in `voice` (the bit pattern Game.GetForm takes, high load
						// orders included); the slots as decimal text -- 31 bits, so Papyrus's
						// signed Int reads it whole.
						Order order{ Order::Kind::kRedress, it->first, {} };
						order.voice = piece.item;
						order.extra = std::to_string(piece.slots & 0x7FFFFFFFu);
						order.setID = outfit.freeOnce ? "second" : "first";
						orders.push_back(std::move(order));
					}
				}
				outfit.nextLook = now + kLookEvery;
				++it;
			}
		}
		// Queued OUTSIDE our lock: QueueOrder takes the order queue's.
		auto& link = PapyrusLink::GetSingleton();
		for (auto& order : orders) {
			link.QueueOrder(std::move(order));
		}
	}

	void Wardrobe::Forget()
	{
		NamedLock lock{ _lock, "wardrobe" };
		_outfits.clear();
	}

	std::vector<std::pair<std::uint32_t, std::vector<Wardrobe::Piece>>> Wardrobe::Saved() const
	{
		NamedLock lock{ _lock, "wardrobe" };
		std::vector<std::pair<std::uint32_t, std::vector<Piece>>> out;
		out.reserve(_outfits.size());
		for (const auto& [actor, outfit] : _outfits) {
			out.emplace_back(actor, outfit.pieces);
		}
		return out;
	}

	void Wardrobe::Restore(std::vector<std::pair<std::uint32_t, std::vector<Piece>>> a_outfits)
	{
		NamedLock lock{ _lock, "wardrobe" };
		_outfits.clear();
		// The first look after a load waits a whole interval: AAF has not announced itself yet,
		// and the bridge refuses everything until it has.
		const auto first = Clock::now() + kLookEvery;
		for (auto& [actor, pieces] : a_outfits) {
			if (actor == 0 || pieces.empty() || _outfits.size() >= kMaxActors) {
				continue;
			}
			auto& outfit = _outfits[actor];
			outfit.pieces = std::move(pieces);
			outfit.nextLook = first;
		}
		if (!_outfits.empty()) {
			logger::info("wardrobe: {} actor(s) in this save are still owed clothes a scene took - checking them",
				_outfits.size());
		}
	}
}
