#include "Morphs.h"

#include "Orders.h"
#include "PapyrusLink.h"

namespace RP
{
	Morphs& Morphs::GetSingleton() noexcept
	{
		static Morphs singleton;
		return singleton;
	}

	void Morphs::OnSceneEnded(std::uint32_t a_first, std::uint32_t a_second)
	{
		const auto at = std::chrono::steady_clock::now() + kDelay;
		NamedLock lock{ _lock, "morphs" };
		for (const auto actor : { a_first, a_second }) {
			if (actor != 0) {
				_due.push_back(Due{ actor, at });
			}
		}
	}

	void Morphs::OnGameLoaded(const std::vector<std::uint32_t>& a_actors)
	{
		if (a_actors.empty()) {
			return;
		}
		// Straight into the queue: the drain waits out the loading screen by itself,
		// and budgets eight orders a poll, so a big ledger costs polls, not a frame.
		auto& link = PapyrusLink::GetSingleton();
		for (const auto actor : a_actors) {
			link.QueueOrder(Order{ Order::Kind::kClearAAFMorphs, actor, {} });
		}
		logger::info("morphs: {} actor(s) from the ledger queued for an AAF-morph sweep after the load",
			a_actors.size());
	}

	void Morphs::Pump()
	{
		std::vector<std::uint32_t> ready;
		{
			NamedLock lock{ _lock, "morphs" };
			if (_due.empty()) {
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			std::erase_if(_due, [&](const Due& a_due) {
				if (a_due.at > now) {
					return false;
				}
				ready.push_back(a_due.actor);
				return true;
			});
		}
		if (ready.empty()) {
			return;
		}
		// Queued OUTSIDE our lock: QueueOrder takes the order queue's.
		auto& link = PapyrusLink::GetSingleton();
		for (const auto actor : ready) {
			link.QueueOrder(Order{ Order::Kind::kClearAAFMorphs, actor, {} });
		}
		// "queued", not "cleared": the bridge does it, and says so in its own trace.
		logger::info("morphs: the scene is {}s behind us - an AAF-morph clear queued for {} actor(s)",
			kDelay.count(), ready.size());
	}

	void Morphs::Forget()
	{
		NamedLock lock{ _lock, "morphs" };
		_due.clear();
	}
}
