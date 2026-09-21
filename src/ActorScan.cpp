#include "ActorScan.h"

#include "Config.h"

namespace
{
	// An actor filled into a quest alias is not necessarily busy — settlers sit in
	// settlement aliases permanently. An alias that has INSTANCED PACKAGES is a
	// quest actively directing them, which is the thing we must not interrupt.
	// Returns the quest doing the directing, so the log can name it.
	[[nodiscard]] const RE::TESQuest* QuestDriving(const RE::Actor& a_actor)
	{
		const auto& extra = a_actor.extraList;
		if (!extra) {
			return nullptr;
		}

		const auto aliases = extra->GetByType<RE::ExtraAliasInstanceArray>();
		if (!aliases) {
			return nullptr;
		}

		const auto& config = RP::Config::GetSingleton();
		for (const auto& instance : aliases->aliasArray) {
			if (!instance.instancedPackages || instance.instancedPackages->empty() || !instance.quest) {
				continue;
			}
			// Owner, 2026-09-21: settlement life is not a quest directing someone.
			// Seen in Sanctuary: WorkshopParent held a settler, and Min01 - "When
			// Freedom Calls", long finished - held Sturges, the Longs and Mama Murphy,
			// so no settler there could ever be a candidate. Quest state cannot tell
			// these apart (Min01 has no completed flag at stage 230), so it is a list.
			const char* edid = instance.quest ? instance.quest->GetFormEditorID() : nullptr;
			if (edid && config.IsAmbientQuest(edid)) {
				continue;
			}
			return instance.quest;
		}
		return nullptr;
	}

	// In the part of the world the player is in: the player's own cell when they are
	// indoors, or any exterior cell of the same worldspace when they are out.
	//
	// Get3D() is not enough. Fallout keeps the interior the player just left LOADED
	// but no longer attached, 3D and all. 2026-09-21, standing in Diamond City after
	// leaving the Third Rail: the Drifters there were scored as "indoors, observers
	// 17" beside the Codmans, and distances were taken between two interiors that do
	// not share coordinates. Worse, the Codmans had walked into their house (another
	// interior) when their scene was requested: two seconds in, every order said
	// "not loaded", and AAF ended it at eleven.
	[[nodiscard]] bool InPlayersWorld(RE::Actor& a_actor, RE::PlayerCharacter& a_player)
	{
		auto* cell = a_actor.GetParentCell();
		auto* here = a_player.GetParentCell();
		if (!cell || !here) {
			return false;
		}
		if (cell == here) {
			return true;
		}
		return !cell->IsInterior() && !here->IsInterior() && cell->worldSpace && cell->worldSpace == here->worldSpace;
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
		_questHeld.clear();
		_candidates.clear();
		_observerPositions.clear();
		_loadedIDs.clear();
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
			} else if (!player || !InPlayersWorld(*actor, *player)) {
				// Not a candidate and not a witness: nobody in another cell can see this.
				++_counters.elsewhere;
			} else if (actor->IsChild()) {
				++_counters.child;
			} else if (actor->IsDead(true)) {
				++_counters.dead;
			} else {
				// Past this point the actor is loaded and alive, so they can witness
				// a scene even when they could never take part in one. Privacy is
				// about who can see, not about who is eligible.
				//
				// PEOPLE only (owner, 2026-09-22): brahmin, dogs, turrets and robots are
				// in the process lists too, and a turret-ringed settlement charged every
				// pair a crowd penalty for its machines. Children never reach here - the
				// mod ignores them entirely, as witnesses too.
				if (config.IsRaceAllowed(actor->race)) {
					_observerPositions.push_back(actor->GetPosition());
				}
				// Every loaded actor, whatever their race: overlays are re-applied and
				// stranded orders re-issued from this list, and a custom-race NPC an
				// addon put in a scene must not be left carrying AAF's busy flag.
				_loadedIDs.push_back(actor->GetFormID());

				if (actor->IsInCombat()) {
					++_counters.inCombat;
				} else if (!config.IsRaceAllowed(actor->race)) {
					++_counters.raceNotAllowed;
					++_rejectedRaces[actor->race ? actor->race->GetFormID() : 0u];
				} else if (actor->talkingToPlayer) {
					++_counters.inDialogue;
				} else if (actor->boolFlags.any(RE::Actor::BOOL_FLAGS::kInRandomScene)) {
					// Two NPCs in the middle of an ambient conversation. talkingToPlayer
					// does not cover this and reads 0 straight through one: it is about
					// the PLAYER, and this is two other people talking to each other.
					//
					// Seen in game 2026-09-20 -- the two Goodneighbor Neighborhood Watch
					// actors were paired off partway through their scripted exchange
					// about the dead synth, and the tick line said "dialogue 0" while it
					// happened, because by that filter's definition it was true.
					//
					// kInRandomScene rather than GetCurrentScene(), deliberately. This
					// catches the ambient chatter and leaves authored story scenes to
					// IsQuestDriven below; the broader check would also drop everybody
					// standing in a quest scene, and in Goodneighbor and Diamond City
					// that is most of the street.
					++_counters.inRandomScene;
				} else if (const auto* quest = QuestDriving(*actor)) {
					++_counters.questDriven;
					// Who and which quest: a count cannot tell "the Minutemen hold every
					// settler in Sanctuary" from "one courier is mid-delivery".
					const char* name = actor->GetDisplayFullName();
					const char* edid = quest ? quest->GetFormEditorID() : nullptr;
					_questHeld.push_back(std::format("{} [{}]", name && *name ? name : "?",
						edid && *edid ? std::string{ edid } : std::format("{:08X}", quest ? quest->GetFormID() : 0u)));
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
