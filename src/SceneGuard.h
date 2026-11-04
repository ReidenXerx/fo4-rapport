#pragma once

// A scene that can no longer be one is stopped, once, from the bridge's poll.
//
// Rapport refused the dead, the fighting and the talking only when a scene STARTED; nothing
// watched a scene once it ran. Nexus 11-04 (WisdomTongue): a stealth sniper killed one of a
// Triggerman pair mid-scene, nobody entered combat, and AAF kept posing the corpse -- the
// ragdoll "vacuumed off screen several times" -- while his buddy carried on with the body.
// And Skinny Malone and Darla were paired off mid-conversation with Nick and the player.
//
// What stops a running scene (owner go, 11-05):
//  - a member dead, dying, unconscious, essential-down or bleeding out -- every AAF scene,
//    other mods' too: AAF posing a corpse is broken for whoever started it;
//  - a member in combat -- scenes Rapport started (its own requests, Chemistry's, Overture's)
//    and the player's: another mod's scene may be meant for a fight's aftermath;
//  - a member pulled into the player's conversation (InConversation) -- every scene but the
//    player's own, whose partner may still stand in the greet scene that asked.
namespace RP::SceneGuard
{
	// The scenes the player is talking in: the player's own current scene, and the scene of
	// everyone talking to the player. Read live from the high process list.
	[[nodiscard]] std::vector<const RE::BGSScene*> ConversationScenes();

	// In the player's conversation: talking to the player, or standing in a scene the player
	// or the player's speaker is in (Skinny Malone's, with Darla and Nick listening).
	[[nodiscard]] bool InConversation(const RE::Actor& a_actor, const std::vector<const RE::BGSScene*>& a_scenes);
	[[nodiscard]] bool InConversation(const RE::Actor& a_actor);

	// Every bridge poll.
	void Pump();

	// A load: no scene of the world being left is still being stopped.
	void Forget();
}
