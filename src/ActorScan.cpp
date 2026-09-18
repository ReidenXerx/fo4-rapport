#include "ActorScan.h"

#include "Config.h"

namespace
{
	// An actor filled into a quest alias is not necessarily busy — settlers sit in
	// settlement aliases permanently. An alias that has INSTANCED PACKAGES is a
	// quest actively directing them, which is the thing we must not interrupt.
	[[nodiscard]] bool IsQuestDriven(const RE::Actor& a_actor)
	{
		const auto& extra = a_actor.extraList;
		if (!extra) {
			return false;
		}

		const auto aliases = extra->GetByType<RE::ExtraAliasInstanceArray>();
		if (!aliases) {
			return false;
		}

		for (const auto& instance : aliases->aliasArray) {
			if (instance.instancedPackages && !instance.instancedPackages->empty()) {
				return true;
			}
		}
		return false;
	}

	// How many actors to process between clock reads. Reading the clock per actor
	// would cost more than the filters it is meant to bound.
	constexpr std::size_t kClockEvery = 16;

	void Append(std::vector<RE::ActorHandle>& a_out, const RE::BSTArray<RE::ActorHandle>& a_in)
	{
		a_out.reserve(a_out.size() + a_in.size());
		for (const auto& handle : a_in) {
			a_out.push_back(handle);
		}
	}
}

namespace RP
{
	void ActorScan::Begin(float a_radius)
	{
		_handles.clear();
		_rejectedRaces.clear();
		_candidates.clear();
		_observerPositions.clear();
		_cursor = 0;
		_counters = {};
		_slices = 0;
		_radiusSq = a_radius * a_radius;

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		_origin = player->GetPosition();

		const auto lists = RE::ProcessLists::GetSingleton();
		if (!lists) {
			return;
		}

		// High and middle-high are the actors the game is actually simulating.
		// Low and middle-low are too far away to walk anywhere in time.
		Append(_handles, lists->highActorHandles);
		Append(_handles, lists->middleHighActorHandles);
	}

	bool ActorScan::Step(float a_budgetMs)
	{
		++_slices;

		const auto started = std::chrono::steady_clock::now();
		const auto budget = std::chrono::duration<float, std::milli>{ a_budgetMs };
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto& config = Config::GetSingleton();

		std::size_t sinceClock = 0;
		while (_cursor < _handles.size()) {
			const auto handle = _handles[_cursor++];
			const auto actorPtr = handle.get();
			const auto actor = actorPtr.get();
			++_counters.seen;

			if (!actor) {
				++_counters.stale;
			} else if (actor == player) {
				// The player is never an autonomy candidate; they take part by choice.
			} else if (!actor->Get3D()) {
				++_counters.notLoaded;
			} else if (actor->IsChild()) {
				++_counters.child;
			} else if (actor->IsDead(true)) {
				++_counters.dead;
			} else {
				// Past this point the actor is loaded and alive, so they can witness
				// a scene even when they could never take part in one. Privacy is
				// about who can see, not about who is eligible.
				_observerPositions.push_back(actor->GetPosition());

				if (actor->IsInCombat()) {
					++_counters.inCombat;
				} else if (!config.IsRaceAllowed(actor->race)) {
					++_counters.raceNotAllowed;
					++_rejectedRaces[actor->race ? actor->race->GetFormID() : 0u];
				} else if (actor->talkingToPlayer) {
					++_counters.inDialogue;
				} else if (IsQuestDriven(*actor)) {
					++_counters.questDriven;
				} else {
					const auto here = actor->GetPosition();
					const auto dx = here.x - _origin.x;
					const auto dy = here.y - _origin.y;
					const auto dz = here.z - _origin.z;
					if ((dx * dx + dy * dy + dz * dz) > _radiusSq) {
						++_counters.outOfRange;
					} else {
						++_counters.candidates;
						_candidates.push_back(handle);
					}
				}
			}

			if (++sinceClock >= kClockEvery) {
				sinceClock = 0;
				if (std::chrono::steady_clock::now() - started >= budget) {
					return _cursor >= _handles.size();
				}
			}
		}

		return true;
	}
}
