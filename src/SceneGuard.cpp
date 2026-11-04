#include "SceneGuard.h"

#include "Config.h"
#include "ForeignScenes.h"
#include "Orders.h"
#include "PapyrusLink.h"

namespace RP::SceneGuard
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		using Group = std::vector<std::uint32_t>;

		// A scene asked to stop is not asked again for this long: AAF's StopScene takes
		// 0.7-12 s to reach OnSceneEnd (Fo4-mcp 10-27), and a second stop mid-teardown helps nobody.
		// Past it, a scene still running is asked once more.
		constexpr auto kAskAgain = std::chrono::seconds{ 60 };
		// A record whose end Rapport missed stays "running" for good: asked this often, then left
		// alone (microscope 11-05: a dead member re-asked and re-logged every minute, forever).
		constexpr std::uint32_t kMaxAsks = 3;

		struct Stopped
		{
			Group             members;   // sorted
			Clock::time_point at{};
			std::uint32_t     asks{ 1 };
		};

		std::mutex           g_lock;
		std::vector<Stopped> g_stopped;

		[[nodiscard]] bool Contains(const Group& a_group, std::uint32_t a_id)
		{
			return a_id != 0 && std::ranges::find(a_group, a_id) != a_group.end();
		}

		[[nodiscard]] RE::Actor* ActorOf(std::uint32_t a_id)
		{
			auto* form = RE::TESForm::GetFormByID(a_id);
			return form ? form->As<RE::Actor>() : nullptr;
		}

		// Dead, or on the way there: nobody AAF should still be posing, in anybody's scene.
		[[nodiscard]] bool Dead(const RE::Actor& a_actor)
		{
			using State = RE::ACTOR_LIFE_STATE;
			const auto state = static_cast<State>(a_actor.lifeState);
			return a_actor.IsDead(true) || state == State::kDying || state == State::kDead;
		}

		// Knocked out, essential-down or bleeding out. Only for Rapport's own scenes and the
		// player's: another mod may mean exactly that -- a defeat, a captive, a sleeper
		// (microscope 11-05).
		[[nodiscard]] std::string_view Down(const RE::Actor& a_actor)
		{
			using State = RE::ACTOR_LIFE_STATE;
			switch (static_cast<State>(a_actor.lifeState)) {
			case State::kUnconscious:
			case State::kEssentialDown:
				return "is down";
			case State::kBleedout:
				return "is bleeding out";
			default:
				return {};
			}
		}
	}

	std::vector<const RE::BGSScene*> ConversationScenes()
	{
		std::vector<const RE::BGSScene*> scenes;
		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return scenes;
		}
		if (const auto* own = player->GetCurrentScene()) {
			scenes.push_back(own);
		}
		const auto* lists = RE::ProcessLists::GetSingleton();
		if (!lists) {
			return scenes;
		}
		for (const auto& handle : lists->highActorHandles) {
			const auto actor = handle.get();
			if (!actor || !actor->talkingToPlayer) {
				continue;
			}
			if (const auto* scene = actor->GetCurrentScene(); scene && std::ranges::find(scenes, scene) == scenes.end()) {
				scenes.push_back(scene);
			}
		}
		return scenes;
	}

	bool InConversation(const RE::Actor& a_actor, const std::vector<const RE::BGSScene*>& a_scenes)
	{
		if (a_actor.talkingToPlayer) {
			return true;
		}
		const auto* scene = a_actor.GetCurrentScene();
		return scene && std::ranges::find(a_scenes, scene) != a_scenes.end();
	}

	bool InConversation(const RE::Actor& a_actor)
	{
		return InConversation(a_actor, ConversationScenes());
	}

	void Pump()
	{
		if (!Config::GetSingleton().guardScenes) {
			return;
		}
		auto scenes = ForeignScenes::GetSingleton().RunningScenes();
		// Our own scene is not among them: ForeignScenes evicts its pair at OwnSceneStarted, so
		// it is the running pair or nothing. Fo4-mcp 11-04: a corpse in our own scene, no stop.
		const auto own = PapyrusLink::GetSingleton().RunningPair();
		if ((own.first != 0 || own.second != 0) &&
			std::ranges::none_of(scenes, [&](const Group& a_group) { return Contains(a_group, own.first) || Contains(a_group, own.second); })) {
			scenes.push_back({ own.first, own.second });
		}
		const auto now = Clock::now();
		for (auto& members : scenes) {
			std::erase(members, 0u);
			std::ranges::sort(members);
		}
		{
			// A scene no longer running is forgotten; one still running keeps its count of asks.
			std::lock_guard lock{ g_lock };
			std::erase_if(g_stopped, [&](const Stopped& a_stop) {
				return std::ranges::none_of(scenes, [&](const Group& a_group) { return a_group == a_stop.members; });
			});
		}
		if (scenes.empty()) {
			return;
		}

		const auto playerID = RE::PlayerCharacter::GetSingleton() ? RE::PlayerCharacter::GetSingleton()->GetFormID() : 0x14u;
		std::vector<const RE::BGSScene*> talking;
		bool                             talkingRead = false;

		std::vector<Order> stops;
		for (const auto& members : scenes) {
			if (members.empty()) {
				continue;
			}
			std::uint32_t asked = 0;
			{
				std::lock_guard lock{ g_lock };
				const auto found = std::ranges::find(g_stopped, members, &Stopped::members);
				if (found != g_stopped.end()) {
					if (found->asks >= kMaxAsks || now - found->at < kAskAgain) {
						continue;
					}
					asked = found->asks;
				}
			}
			const bool withPlayer = Contains(members, playerID);
			const bool ours = withPlayer || Contains(members, own.first) || Contains(members, own.second);

			std::string     why;
			std::uint32_t   who = 0;
			RE::Actor*      living = nullptr;
			RE::Actor*      fallen = nullptr;
			for (const auto id : members) {
				auto* actor = ActorOf(id);
				if (!actor || !actor->Get3D()) {
					continue;   // not here: the door hold and AAF's own distance check have them
				}
				const auto down = Dead(*actor) ? std::string_view{ "is dead" } : ours ? Down(*actor) : std::string_view{};
				if (!down.empty()) {
					if (why.empty()) {
						why = down;
						who = id;
					}
					if (!fallen) {
						fallen = actor;
					}
					continue;
				}
				if (!living || living->GetFormID() == playerID) {
					living = actor;
				}
				if (!why.empty()) {
					continue;
				}
				if (ours && actor->IsInCombat()) {
					why = "is fighting";
					who = id;
				} else if (!withPlayer && id != playerID) {
					if (!talkingRead) {
						talking = ConversationScenes();
						talkingRead = true;
					}
					if (InConversation(*actor, talking)) {
						why = "is in a conversation with the player";
						who = id;
					}
				}
			}
			if (why.empty()) {
				continue;
			}
			// AAF's StopScene takes any actor of the scene: a living one by preference, the body
			// when nobody else is here (the partner unloaded, or a scene of one).
			auto* target = living ? living : fallen;
			if (!target) {
				continue;
			}
			stops.push_back(Order{ Order::Kind::kStopScene, target->GetFormID(), {} });
			logger::info("scene guard: {:08X} {} - asking AAF to stop the scene ({} member(s), {}){}", who, why,
				members.size(), ours ? "Rapport's" : "another mod's",
				asked ? std::format(" - ask {} of {}, the last one went unanswered", asked + 1, kMaxAsks) : std::string{});
			std::lock_guard lock{ g_lock };
			if (const auto found = std::ranges::find(g_stopped, members, &Stopped::members); found != g_stopped.end()) {
				found->at = now;
				++found->asks;
			} else {
				g_stopped.push_back(Stopped{ members, now, 1 });
			}
		}
		if (!stops.empty()) {
			PapyrusLink::GetSingleton().QueueOrders(stops);
		}
	}

	void Forget()
	{
		std::lock_guard lock{ g_lock };
		g_stopped.clear();
	}
}
