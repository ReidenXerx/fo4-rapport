#pragma once

// A load door the player uses while an AAF scene runs in their cell is held until that
// scene has ended and AAF has finished with it, then opened for them.
//
// Why: AAF crashes when a cell unloads under a running scene. Its own StopScene ->
// sendEvent -> UI.Invoke lands on HUD images the load has freed (Scaleform
// Image::Update). Measured 10-27 by Fo4-mcp with Rapport OFF: 4 crashes in 4. A stop
// queued at the press cannot beat the door -- every AAF call goes through the bridge's
// poll -- so the door waits for the stop instead.
//
// "A scene" is anything AAF has been handed: from Rapport's own StartScene, or any mod's
// OnWalkInit, to its OnSceneEnd. 10-28 (door-hold-4): counting only from OnSceneInit missed
// walking pairs, and a door taken during the walk crashed 4 of 7.
namespace RP::DoorHold
{
	// Hooks TESObjectDOOR::Activate (vfunc 0x40). Logs and does nothing when the vtable
	// has no address on this runtime.
	void Install();

	// Called on every bridge poll: lets the held door through once the scenes have ended
	// and settled, and lets go if the player walks off or opens the Pip-Boy.
	void Pump();

	// A door is held right now: the bridge polls fast and starts no scene.
	[[nodiscard]] bool Active();

	// AAF's walk began for these actors (any mod's scene; Bridge OnWalkInit).
	void Walking(const std::vector<std::uint32_t>& a_actors);
	// Their scene ended (OnSceneEnd, ours or not).
	void Ended(const std::vector<std::uint32_t>& a_actors);
	// AAF refused a scene at init: a walk that is not yet a scene is not one any more.
	void Refused();
}
