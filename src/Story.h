#pragma once

// Where the player is in the game's own story, for the rules that depend on it.
namespace RP::Story
{
	// MQ101 "War Never Changes", Fallout4.esm 0001ED86: character creation, pre-war
	// Sanctuary, the bomb and Vault 111. Its log entry at stage 1000 completes it, and
	// that is the moment the player steps out of the vault (read from the record,
	// 2026-09-25).
	inline constexpr std::uint32_t kOpeningQuest = 0x0001ED86;
	inline constexpr std::uint16_t kOpeningOver = 1000;

	// Is the game's opening still being played? (Owner poll, 2026-09-25: nothing of
	// ours starts before the player leaves Vault 111. The Nexus report that brought it
	// up: "the frozen corpses in the cryopods come to life and start having sex".)
	//
	// Read from the quest's CURRENT STAGE, not its running flags: stage 0 is "never
	// started", which is what an alternate start that skips the opening leaves behind,
	// and MS Skip Prewar Sanctuary (in the owner's load order) jumps straight to 900 -
	// in the pod - and then plays on to 1000 like the vanilla game. Both come out right.
	[[nodiscard]] inline bool OpeningRunning()
	{
		const auto quest = RE::TESForm::GetFormByID<RE::TESQuest>(kOpeningQuest);
		if (!quest) {
			return false;
		}
		const auto stage = quest->currentStage;
		return stage > 0 && stage < kOpeningOver;
	}

	// ActorState's lifeState, 0 = alive. Anything else - unconscious, restrained,
	// bleeding out, downed - is nobody who could agree to anything.
	[[nodiscard]] inline std::string_view LifeStateName(std::uint32_t a_state)
	{
		switch (a_state) {
		case 0: return "alive";
		case 1: return "dying";
		case 2: return "dead";
		case 3: return "unconscious";
		case 4: return "being reanimated";
		case 5: return "being recycled";
		case 6: return "restrained";
		case 7: return "down (essential)";
		case 8: return "bleeding out";
		default: return "not awake";
		}
	}
}
