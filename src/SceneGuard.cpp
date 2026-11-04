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

		struct Stopped
		{
			Group             members;   // sorted
			Clock::time_point at{};
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

		// Dead, or on the way there, or down: nobody AAF should still be posing.
		[[nodiscard]] std::string_view Down(const RE::Actor& a_actor)
		{
			using State = RE::ACTOR_LIFE_STATE;
			if (a_actor.IsDead(true)) {
				return "is dead";
			}
			switch (static_cast<State>(a_actor.lifeState)) {
			case State::kDying:
			case State::kDead:
				return "is dead";
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
		const auto now = Clock::now();
		{
			std::lock_guard lock{ g_lock };
			std::erase_if(g_stopped, [&](const Stopped& a_stop) { return now - a_stop.at > kAskAgain; });
		}
		if (scenes.empty()) {
			return;
		}

		const auto own = PapyrusLink::GetSingleton().RunningPair();
		const auto playerID = RE::PlayerCharacter::GetSingleton() ? RE::PlayerCharacter::GetSingleton()->GetFormID() : 0x14u;
		std::vector<const RE::BGSScene*> talking;
		bool                             talkingRead = false;

		std::vector<Order> stops;
		for (auto& members : scenes) {
			std::erase(members, 0u);
			if (members.empty()) {
				continue;
			}
			std::ranges::sort(members);
			{
				std::lock_guard lock{ g_lock };
				if (std::ranges::any_of(g_stopped, [&](const Stopped& a_stop) { return a_stop.members == members; })) {
					continue;
				}
			}
			const bool withPlayer = Contains(members, playerID);
			const bool ours = withPlayer || Contains(members, own.first) || Contains(members, own.second);

			std::string     why;
			std::uint32_t   who = 0;
			RE::Actor*      living = nullptr;
			for (const auto id : members) {
				auto* actor = ActorOf(id);
				if (!actor || !actor->Get3D()) {
					continue;   // not here: the door hold and AAF's own distance check have them
				}
				if (const auto down = Down(*actor); !down.empty()) {
					if (why.empty()) {
						why = down;
						who = id;
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
			if (why.empty() || !living) {
				continue;
			}
			// AAF's StopScene takes any actor of the scene: a living one, never the corpse.
			stops.push_back(Order{ Order::Kind::kStopScene, living->GetFormID(), {} });
			logger::info("scene guard: {:08X} {} - asking AAF to stop the scene ({} member(s), {})", who, why,
				members.size(), ours ? "Rapport's" : "another mod's");
			std::lock_guard lock{ g_lock };
			g_stopped.push_back(Stopped{ members, now });
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
