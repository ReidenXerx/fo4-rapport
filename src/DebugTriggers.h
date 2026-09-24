#pragma once

namespace RP
{
	// Scenes on demand, from the MCM's Debug page and its hotkeys (R-23, owner poll
	// 2026-09-24). fo4-mcp cannot start AAF scenes, so the owner starts them himself --
	// through the same entry points a real scene uses, not a side door.
	//
	// Every trigger comes as two buttons (the owner's choice):
	//   FORCED skips the soft gates: the score bar, cooldowns, privacy, time of day.
	//   REAL   obeys every gate the stand-in obeys, and says which one refused.
	// Both keep the hard rules: adults only, alive, loaded, in the player's world, not
	// fighting, a race Rapport dresses, and one scene at a time.
	//
	// Each answers with one line for the HUD: what happened, or what refused and why.
	namespace DebugTriggers
	{
		// The actor the player is facing: within a_maxDistance units, and at most
		// a_maxAngle degrees either side of the player's heading, the smallest angle
		// winning. nullptr when nobody is.
		[[nodiscard]] RE::Actor* ActorInFront(float a_maxDistance, float a_maxAngle);

		// A scene between these two, now (the player may be one of them).
		[[nodiscard]] std::string SceneWith(RE::Actor* a_first, RE::Actor* a_second, bool a_force);

		// A scene for this actor with the partner Rapport's own pairing ranks best.
		[[nodiscard]] std::string SceneFor(RE::Actor* a_target, bool a_force);

		// A scene for the best pair of one kind nearby -- "FF", "FM" or "MM" -- so the
		// owner can test each on demand. The actor faced (may be nullptr) is one of
		// the two when their sex fits the kind; otherwise the whole nearby field is
		// ranked and the HUD says the one faced was passed over.
		[[nodiscard]] std::string ScenePair(RE::Actor* a_facing, std::string_view a_pair, bool a_force);
	}
}
