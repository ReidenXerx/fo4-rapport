#include "DoorHold.h"

#include "Config.h"
#include "ForeignScenes.h"
#include "Orders.h"
#include "Compat.h"
#include "PapyrusLink.h"

namespace RP::DoorHold
{
	namespace
	{
		using Activate_t = bool (*)(RE::TESObjectDOOR*, RE::TESObjectREFR*, RE::TESObjectREFR*, RE::TESBoundObject*, std::int32_t);
		Activate_t g_original = nullptr;

		using Clock = std::chrono::steady_clock;
		using Group = std::vector<std::uint32_t>;

		// After the last scene ends, AAF still tears it down on its UI for a moment, and the door
		// waits that out: Rapport.ini DoorSettleSeconds (Config.h). Fo4-mcp 10-28: 2 s held 3/3.
		[[nodiscard]] Clock::duration Settle()
		{
			return std::chrono::duration_cast<Clock::duration>(
				std::chrono::duration<float>{ Config::GetSingleton().doorSettleSeconds });
		}
		// Fo4-mcp 10-27: AAF's StopScene took 0.7-12 s to reach OnSceneEnd, and a door pressed
		// before it crashed 5/5. So the door never opens on a clock while a scene still runs:
		// at kTell the player is told how to leave; only at kCap, a scene whose end Rapport
		// must have missed, does it open, and that scene never holds a door again.
		constexpr auto  kTell = std::chrono::seconds{ 30 };
		constexpr auto  kCap = std::chrono::seconds{ 90 };
		// Our own request asked to stop before AAF began its walk sends no end at all (10-28,
		// trial 3: no OnWalkInit, no OnSceneEnd). Unwalked this long after the press, Rapport
		// gives it up (AbandonInFlight) -- only dropping it from the count re-held the door
		// every 12 s for good (door-hold-5, trial 4).
		constexpr auto  kUnwalked = std::chrono::seconds{ 10 };
		// A walk that never became a scene nor ended (a missed refusal) stops counting.
		constexpr auto  kWalkExpiry = std::chrono::seconds{ 120 };
		constexpr float kWalkAway = 300.0f;
		constexpr auto  kMessage = "Rapport: Wrapping scene up for travel";   // alasdairn's wording, 10-27

		// THE APPROACH (owner go, 11-05; WisdomTongue on Nexus: "you just have to stand there while
		// moans come from somewhere off-screen"). AAF needs 0.7-12 s to end a scene, so the stop is
		// asked for as the player comes up to a load door, facing it, rather than at the press: most
		// of that time passes while they walk the last steps. And no new scene starts here meanwhile.
		// Facing, because a town street is lined with shop doors (Diamond City) and walking past one
		// must not end every scene in the market.
		//
		// APPROACHING, not standing near (microscope 11-05): the player arriving through a door
		// stands right beside its way back, and a small shop has every door within reach. So the
		// approach begins only when the player MOVES toward a door they face; once begun it holds
		// while they stay close and keep facing it.
		constexpr float kNear = 300.0f;
		constexpr float kNearLeave = 450.0f;
		constexpr float kRightAt = 60.0f;     // this close, any heading counts
		constexpr float kFacing = 0.7f;       // cosine: within about 45 degrees of straight ahead
		constexpr float kMoving = 15.0f;      // units closer since the last poll: walking at it
		constexpr float kDoorHeight = 300.0f;
		// Within this of a load door with a scene here, the bridge polls fast (PollSeconds). The
		// interval in force was set by the PREVIOUS poll, so this must cover a 3 s poll at a run.
		constexpr float kWatch = 2000.0f;
		// The doors of the outdoor persistent cell nearer than this are read with the grid cell's.
		constexpr float kDoorRead = 4096.0f;
		constexpr auto  kRescan = std::chrono::seconds{ 5 };     // a cell read with no door yet
		constexpr auto  kAskAgainAt = std::chrono::seconds{ 15 }; // a stop asked and still running

		struct Hold
		{
			bool                active{ false };
			RE::ObjectRefHandle door;
			RE::TESObjectDOOR*  base{ nullptr };
			RE::NiPoint3        at;
			Group               actors;
			Clock::time_point   since{};
			bool                ended{ false };
			Clock::time_point   endedAt{};
			bool                toldStillEnding{ false };
		};

		struct Walk
		{
			Group             actors;
			Clock::time_point at{};
		};

		std::mutex                        g_lock;
		Hold                              g_hold;
		std::unordered_set<std::uint32_t> g_ghosts;   // members of a scene that held a door to kCap

		std::mutex        g_walkLock;   // never taken while g_lock is held, nor the other way round
		std::vector<Walk> g_walks;

		struct Asked
		{
			Group             members;
			Clock::time_point at{};
		};
		struct Approach
		{
			const void*               place{ nullptr };   // the interior, or the worldspace outdoors
			const RE::TESObjectCELL*  cell{ nullptr };
			Clock::time_point         readAt{};
			std::vector<RE::NiPoint3> doors;   // the load doors around the player, read per cell
			std::optional<RE::NiPoint3> last;  // the player's position at the previous poll
			bool                      near{ false };
			std::vector<Asked>        asked;   // scenes asked to stop on this approach
		};
		std::mutex       g_approachLock;   // taken before g_walkLock, never inside g_lock
		Approach         g_approach;
		std::atomic_bool g_near{ false };
		std::atomic_bool g_watching{ false };

		[[nodiscard]] bool IsLoadDoor(RE::TESObjectREFR* a_door)
		{
			return a_door->extraList && a_door->extraList->GetByType(RE::EXTRA_DATA_TYPE::kTeleport);
		}

		// The cell's load doors where they stand, within a_range of a_near.
		void DoorsIn(RE::TESObjectCELL& a_cell, const RE::NiPoint3& a_near, float a_range, std::vector<RE::NiPoint3>& a_out)
		{
			RE::BSAutoLock<RE::BSSpinLock> lock{ a_cell.spinLock };
			for (const auto& held : a_cell.references) {
				auto* ref = held.get();
				if (!ref || RP::Compat::Disabled(ref) || ref->IsDeleted() || !IsLoadDoor(ref)) {
					continue;
				}
				if (ref->data.location.GetDistance(a_near) <= a_range) {
					a_out.push_back(ref->data.location);
				}
			}
		}

		// Outdoors a building's load door is a PERSISTENT reference: it lives in the worldspace's
		// persistent cell, not in the grid cell the player stands in (microscope 11-05). That
		// member's offset is not one Layout.h checks, so the read proves itself: a CELL whose
		// worldspace is this one, or nothing.
		[[nodiscard]] RE::TESObjectCELL* PersistentCell(RE::TESWorldSpace* a_world)
		{
			__try {
				auto* cell = a_world->persistentCell;
				if (cell && cell->GetFormType() == RE::ENUM_FORM_ID::kCELL && !cell->IsInterior() && cell->worldSpace == a_world) {
					return cell;
				}
			} __except (1) {
			}
			return nullptr;
		}

		// Here = what a load door unloads: the player's interior, or the outdoor worldspace they stand in.
		[[nodiscard]] bool SamePlace(const RE::Actor& a_actor, const RE::PlayerCharacter& a_player)
		{
			const auto* cell = a_actor.GetParentCell();
			const auto* here = a_player.GetParentCell();
			if (!cell || !here) {
				return false;
			}
			if (cell == here) {
				return true;
			}
			return !cell->IsInterior() && !here->IsInterior() && cell->worldSpace && cell->worldSpace == here->worldSpace;
		}

		[[nodiscard]] bool Contains(const Group& a_group, std::uint32_t a_id)
		{
			return a_id != 0 && std::ranges::find(a_group, a_id) != a_group.end();
		}

		// Everything AAF has been handed and not finished: other mods' scenes from their walk or
		// init, ours from the moment the bridge asked for it. One list per scene: groups that
		// share anybody are one scene seen twice (a walk and its init, our pair and its walk).
		[[nodiscard]] std::vector<Group> Tracked()
		{
			const auto now = Clock::now();
			auto       raw = ForeignScenes::GetSingleton().RunningScenes();
			{
				std::lock_guard lock{ g_walkLock };
				std::erase_if(g_walks, [&](const Walk& a_walk) { return now - a_walk.at > kWalkExpiry; });
				for (const auto& walk : g_walks) {
					raw.push_back(walk.actors);
				}
			}
			if (const auto [first, second] = PapyrusLink::GetSingleton().InFlightPair(); first != 0 || second != 0) {
				raw.push_back({ first, second });
			}
			std::vector<Group> scenes;
			for (auto& group : raw) {
				std::erase(group, 0u);
				if (group.empty()) {
					continue;
				}
				auto into = std::ranges::find_if(scenes, [&](const Group& a_scene) {
					return std::ranges::any_of(group, [&](std::uint32_t a_id) { return Contains(a_scene, a_id); });
				});
				if (into == scenes.end()) {
					scenes.push_back(std::move(group));
					continue;
				}
				for (const auto id : group) {
					if (!Contains(*into, id)) {
						into->push_back(id);
					}
				}
			}
			return scenes;
		}

		// Our own request, in flight, that AAF has neither walked nor begun.
		[[nodiscard]] bool OwnUnwalked()
		{
			const auto [first, second] = PapyrusLink::GetSingleton().InFlightPair();
			if (first == 0 && second == 0) {
				return false;
			}
			if (const auto running = PapyrusLink::GetSingleton().RunningPair(); running.first != 0 || running.second != 0) {
				return false;
			}
			std::lock_guard lock{ g_walkLock };
			return !std::ranges::any_of(g_walks, [&](const Walk& a_walk) {
				return Contains(a_walk.actors, first) || Contains(a_walk.actors, second);
			});
		}

		// Every scene with someone in the player's place, as its members. The player's own
		// scenes are left out: they are not standing at a door.
		[[nodiscard]] std::vector<Group> ScenesHere(const RE::PlayerCharacter& a_player, const std::unordered_set<std::uint32_t>& a_ghosts)
		{
			std::vector<Group> here;
			const auto         playerID = a_player.GetFormID();
			for (auto& members : Tracked()) {
				if (Contains(members, playerID)) {
					continue;
				}
				if (std::ranges::all_of(members, [&](std::uint32_t a_id) { return a_ghosts.contains(a_id); })) {
					continue;
				}
				const bool near = std::ranges::any_of(members, [&](std::uint32_t a_id) {
					const auto* form = RE::TESForm::GetFormByID(a_id);
					const auto* actor = form ? form->As<RE::Actor>() : nullptr;
					return actor && actor->Get3D() && SamePlace(*actor, a_player);
				});
				if (near) {
					here.push_back(std::move(members));
				}
			}
			return here;
		}

		// One StopScene per scene: AAF's StopScene takes any one of its actors.
		void AskToStop(const std::vector<Group>& a_scenes)
		{
			std::vector<Order> stops;
			for (const auto& members : a_scenes) {
				stops.push_back(Order{ Order::Kind::kStopScene, members.front(), {} });
			}
			PapyrusLink::GetSingleton().QueueOrders(stops);
		}

		void ApproachStep(const RE::PlayerCharacter& a_player, const std::unordered_set<std::uint32_t>& a_ghosts)
		{
			if (!Config::GetSingleton().holdDoors) {
				g_near = false;
				g_watching = false;
				return;
			}
			const auto      now = Clock::now();
			auto*           cell = a_player.GetParentCell();
			const auto&     at = a_player.data.location;
			std::lock_guard lock{ g_approachLock };

			const void* place = !cell ? nullptr : cell->IsInterior() ? static_cast<const void*>(cell) : static_cast<const void*>(cell->worldSpace);
			if (place != g_approach.place) {
				if (g_approach.near) {
					logger::info("door approach: the player changed place - new scenes may start again");
				}
				g_approach = {};
				g_approach.place = place;
			}
			if (cell != g_approach.cell || (g_approach.doors.empty() && now - g_approach.readAt >= kRescan)) {
				g_approach.cell = cell;
				g_approach.readAt = now;
				g_approach.doors.clear();
				if (cell) {
					DoorsIn(*cell, at, kDoorRead, g_approach.doors);
					if (!cell->IsInterior() && cell->worldSpace) {
						if (auto* persistent = PersistentCell(cell->worldSpace)) {
							DoorsIn(*persistent, at, kDoorRead, g_approach.doors);
						}
					}
				}
			}

			const float yaw = a_player.data.angle.z;
			const float fx = std::sin(yaw);
			const float fy = std::cos(yaw);
			const auto  last = g_approach.last;
			g_approach.last = at;
			float best = (std::numeric_limits<float>::max)();   // the nearest door, any heading
			float approached = -1.0f;                           // the nearest door being walked at
			bool  stillFacing = false;                          // a faced door within kNearLeave
			for (const auto& door : g_approach.doors) {
				if (std::abs(door.z - at.z) > kDoorHeight) {
					continue;   // another floor
				}
				const float dx = door.x - at.x;
				const float dy = door.y - at.y;
				const float dist = std::hypot(dx, dy);
				best = (std::min)(best, dist);
				const bool faced = dist <= kRightAt || (dx * fx + dy * fy) / dist >= kFacing;
				if (!faced) {
					continue;
				}
				if (dist <= kNearLeave) {
					stillFacing = true;
				}
				const bool closing = last && std::hypot(door.x - last->x, door.y - last->y) - dist >= kMoving;
				if (dist <= kNear && closing && (approached < 0.0f || dist < approached)) {
					approached = dist;
				}
			}

			std::vector<Group> here;
			if (best <= kWatch) {
				here = ScenesHere(a_player, a_ghosts);
			}
			// Scenes over are forgotten, so the same actors' next scene is asked again.
			std::erase_if(g_approach.asked, [&](const Asked& a_asked) {
				return std::ranges::none_of(here, [&](const Group& a_group) {
					return std::ranges::any_of(a_group, [&](std::uint32_t a_id) { return Contains(a_asked.members, a_id); });
				});
			});

			if (!g_approach.near && approached >= 0.0f) {
				logger::info("door approach: the player walks at a load door {:.0f}u away - {} scene(s) here asked to stop, and none starts here meanwhile",
					approached, here.size());
				g_approach.near = true;
			} else if (g_approach.near && !stillFacing) {
				logger::info("door approach: the player turned or walked away from the door - new scenes may start here again");
				g_approach.near = false;
				g_approach.asked.clear();
			}

			if (g_approach.near) {
				// Our own request AAF has not walked yet is not asked: a stop that early sends no end
				// at all (10-28), and the bridge releases it before StartScene instead (DoorHeld).
				// Once it walks, it is a scene here like any other and the next poll asks it.
				const auto [first, second] = PapyrusLink::GetSingleton().InFlightPair();
				const bool unwalked = OwnUnwalked();
				std::vector<Group> fresh;
				for (const auto& members : here) {
					if (unwalked && (Contains(members, first) || Contains(members, second))) {
						continue;
					}
					auto asked = std::ranges::find_if(g_approach.asked, [&](const Asked& a_asked) {
						return std::ranges::any_of(members, [&](std::uint32_t a_id) { return Contains(a_asked.members, a_id); });
					});
					if (asked == g_approach.asked.end()) {
						fresh.push_back(members);
						g_approach.asked.push_back(Asked{ members, now });
					} else if (now - asked->at >= kAskAgainAt) {
						// Still running 15 s on: the stop may have been lost. Asked once more.
						fresh.push_back(members);
						asked->at = now;
					}
				}
				if (!fresh.empty()) {
					AskToStop(fresh);
				}
			}
			g_near = g_approach.near;
			g_watching = best <= kWatch && (g_approach.near || !here.empty());
		}

		bool Thunk(RE::TESObjectDOOR* a_this, RE::TESObjectREFR* a_door, RE::TESObjectREFR* a_actor,
			RE::TESBoundObject* a_object, std::int32_t a_count)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!Config::GetSingleton().holdDoors || !player || a_actor != player || !a_door || !IsLoadDoor(a_door)) {
				return g_original(a_this, a_door, a_actor, a_object, a_count);
			}

			std::unordered_set<std::uint32_t> ghosts;
			{
				std::lock_guard lock{ g_lock };
				if (g_hold.active) {
					RE::SendHUDMessage::ShowHUDMessage(kMessage, "", true, false);
					return false;
				}
				ghosts = g_ghosts;
			}

			const auto scenes = ScenesHere(*player, ghosts);
			if (scenes.empty()) {
				return g_original(a_this, a_door, a_actor, a_object, a_count);
			}

			Hold hold;
			hold.active = true;
			hold.door = a_door->GetHandle();
			hold.base = a_this;
			hold.at = a_door->data.location;
			hold.since = Clock::now();
			for (const auto& members : scenes) {
				hold.actors.insert(hold.actors.end(), members.begin(), members.end());
			}
			{
				std::lock_guard lock{ g_lock };
				g_hold = std::move(hold);
			}
			AskToStop(scenes);
			RE::SendHUDMessage::ShowHUDMessage(kMessage, "", false, false);
			logger::info("door hold: {:08X} held - {} AAF scene(s) here are asked to stop before the player leaves",
				a_door->GetFormID(), scenes.size());
			return false;
		}
	}

	void Install()
	{
#ifdef RP_RUNTIME_DATABASE
		const auto resolved = REL::IDDatabase::get().resolve(RE::VTABLE::TESObjectDOOR[0]);
		if (!resolved) {
			logger::warn("door hold: the door type has no address on this game version ({}) - doors are not held",
				REL::id_resolve_status_text(resolved.status));
			return;
		}
		REL::Relocation<std::uintptr_t> vtable{ REL::Offset(*resolved.rva) };
#else
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::TESObjectDOOR[0] };
#endif
		g_original = reinterpret_cast<Activate_t>(vtable.write_vfunc(0x40, &Thunk));
		logger::info("door hold: installed");
	}

	bool Active()
	{
		std::lock_guard lock{ g_lock };
		return g_hold.active;
	}

	bool Near()
	{
		return g_near.load();
	}

	void Forget()
	{
		std::lock_guard lock{ g_approachLock };
		g_approach = {};
		g_near = false;
		g_watching = false;
	}

	bool Watching()
	{
		return g_watching.load() || g_near.load() || Active();
	}

	bool InAnyScene(std::uint32_t a_actor)
	{
		return std::ranges::any_of(Tracked(), [&](const Group& a_group) { return Contains(a_group, a_actor); });
	}

	void Walking(const std::vector<std::uint32_t>& a_actors)
	{
		Group group;
		for (const auto id : a_actors) {
			if (id != 0) {
				group.push_back(id);
			}
		}
		if (group.empty()) {
			return;
		}
		std::lock_guard lock{ g_walkLock };
		// One actor, one AAF scene: an older walk naming any of them is over.
		std::erase_if(g_walks, [&](const Walk& a_walk) {
			return std::ranges::any_of(group, [&](std::uint32_t a_id) { return Contains(a_walk.actors, a_id); });
		});
		g_walks.push_back(Walk{ std::move(group), Clock::now() });
	}

	void Ended(const std::vector<std::uint32_t>& a_actors)
	{
		std::lock_guard lock{ g_walkLock };
		std::erase_if(g_walks, [&](const Walk& a_walk) {
			return std::ranges::any_of(a_actors, [&](std::uint32_t a_id) { return Contains(a_walk.actors, a_id); });
		});
	}

	void Refused()
	{
		// The refusal names no actors. A walk whose scene AAF has begun is not the one refused.
		const auto running = ForeignScenes::GetSingleton().RunningScenes();
		const auto own = PapyrusLink::GetSingleton().RunningPair();
		std::lock_guard lock{ g_walkLock };
		std::erase_if(g_walks, [&](const Walk& a_walk) {
			const bool begun = Contains(a_walk.actors, own.first) || Contains(a_walk.actors, own.second) ||
			                   std::ranges::any_of(running, [&](const Group& a_group) {
				                   return std::ranges::any_of(a_group, [&](std::uint32_t a_id) { return Contains(a_walk.actors, a_id); });
			                   });
			return !begun;
		});
	}

	void Pump()
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* ui = RE::UI::GetSingleton();

		Hold                              snapshot;
		std::unordered_set<std::uint32_t> ghosts;
		bool                              held = false;
		{
			std::lock_guard lock{ g_lock };
			held = g_hold.active;
			ghosts = g_ghosts;
		}
		if (!held) {
			if (player) {
				ApproachStep(*player, ghosts);
			}
			return;
		}
		{
			std::lock_guard lock{ g_lock };
			if (!g_hold.active) {
				return;
			}
			if (!player || player->data.location.GetDistance(g_hold.at) > kWalkAway ||
				(ui && ui->GetMenuOpen("PipboyMenu"))) {
				logger::info("door hold: let go - the player walked off or opened the Pip-Boy");
				g_hold = {};
				return;
			}
			snapshot = g_hold;
			ghosts = g_ghosts;
		}

		const auto now = Clock::now();
		if (const auto [first, second] = PapyrusLink::GetSingleton().InFlightPair();
			now - snapshot.since >= kUnwalked && (Contains(snapshot.actors, first) || Contains(snapshot.actors, second)) && OwnUnwalked()) {
			if (PapyrusLink::GetSingleton().AbandonInFlight("the player is leaving through a door and AAF never began this scene")) {
				Ended({ first, second });
			}
		}

		// Asked fresh every poll, not only of the scenes counted at the press: a scene AAF began
		// meanwhile (10-28 trial 4: one 0.4 s before the door opened) holds the door as well.
		const auto here = ScenesHere(*player, ghosts);
		std::vector<Group> fresh;
		for (const auto& members : here) {
			if (!std::ranges::any_of(members, [&](std::uint32_t a_id) { return Contains(snapshot.actors, a_id); })) {
				fresh.push_back(members);
			}
		}

		bool open = false;
		{
			std::lock_guard lock{ g_lock };
			if (!g_hold.active || g_hold.since != snapshot.since) {
				return;   // let go or replaced while we looked
			}
			for (const auto& members : fresh) {
				g_hold.actors.insert(g_hold.actors.end(), members.begin(), members.end());
			}
			if (!here.empty()) {
				if (g_hold.ended) {
					logger::info("door hold: another scene began here - the door waits for it too");
				}
				g_hold.ended = false;
			} else if (!g_hold.ended) {
				g_hold.ended = true;
				g_hold.endedAt = now;
				logger::info("door hold: the scene(s) here ended - the door opens in {:.1f}s", Config::GetSingleton().doorSettleSeconds);
			}

			if (g_hold.ended) {
				open = now - g_hold.endedAt >= Settle();
			} else {
				const auto waited = now - g_hold.since;
				if (waited >= kCap) {
					g_ghosts.insert(g_hold.actors.begin(), g_hold.actors.end());
					logger::warn("door hold: no scene end after {}s - Rapport must have missed it; the door opens and these actors hold no door again this session", kCap.count());
					open = true;
				} else if (waited >= kTell && !g_hold.toldStillEnding) {
					g_hold.toldStillEnding = true;
					logger::warn("door hold: no scene end after {}s - the door keeps waiting", kTell.count());
					if (auto* tasks = F4SE::GetTaskInterface()) {
						tasks->AddTask([] { RE::SendHUDMessage::ShowHUDMessage("Rapport: scene still ending - move away to cancel", "", false, false); });
					}
				}
			}
			if (open) {
				snapshot = g_hold;
				g_hold = {};
			}
		}
		if (!fresh.empty()) {
			AskToStop(fresh);
		}
		if (!open) {
			return;
		}

		auto* tasks = F4SE::GetTaskInterface();
		if (!tasks) {
			return;
		}
		tasks->AddTask([door = snapshot.door, base = snapshot.base, since = snapshot.since, told = snapshot.toldStillEnding] {
			const auto ref = door.get();
			auto*      player = RE::PlayerCharacter::GetSingleton();
			if (!ref || !player || !g_original) {
				return;
			}
			// The last look, on the thread that opens it: a scene begun since the poll keeps it shut.
			std::unordered_set<std::uint32_t> ghosts;
			{
				std::lock_guard lock{ g_lock };
				if (g_hold.active) {
					return;
				}
				ghosts = g_ghosts;
			}
			if (const auto late = ScenesHere(*player, ghosts); !late.empty()) {
				// The same hold, its clock kept: the 90 s cap counts from the press.
				Hold again;
				again.active = true;
				again.door = door;
				again.base = base;
				again.at = ref->data.location;
				again.since = since;
				again.toldStillEnding = told;
				for (const auto& members : late) {
					again.actors.insert(again.actors.end(), members.begin(), members.end());
				}
				{
					std::lock_guard lock{ g_lock };
					g_hold = std::move(again);
				}
				AskToStop(late);
				logger::info("door hold: a scene began here as the door was opening - held again");
				return;
			}
			logger::info("door hold: opening {:08X} for the player", ref->GetFormID());
			g_original(base, ref.get(), player, nullptr, 1);
		});
	}
}
