#include "Wardrobe.h"

#include "Orders.h"
#include "PapyrusLink.h"

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

	void Wardrobe::OnChecked(std::uint32_t a_actor, std::uint32_t a_item, Checked a_result)
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
			// A scene, or AAF's busy flag left on a dead one. The flag is not ours to judge here:
			// Rapport releases a stale one the next time anything asks for this actor
			// (PapyrusLink::NoteActorBusy), and the look after that finds them free.
			outfit.freeOnce = false;
			break;
		case Checked::kNotLoaded:
			break;
		}
		if (outfit.pieces.empty()) {
			_outfits.erase(found);
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
