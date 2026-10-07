#pragma once

namespace RP
{
	// The free camera for the player's AAF scenes (owner, 2026-10-07: "a free camera
	// whenever the player is in an AAF scene, and back to the previous view when it ends").
	//
	// Any AAF scene the player is in, Rapport's or anybody's: a mechanic. The engine's own
	// tfc (PlayerCamera::ToggleFreeCameraMode, world running) goes on at the scene's first
	// animation, after the walk and after AAF has settled its own camera; the player flies
	// it with the usual movement and look. At the end it comes off FIRST, then the view the
	// player had before the scene is put back -- once, on the first settled landing, and
	// never again (VATS Bullets 0.9.12: overriding a later switch, the player's own, was hated).
	//
	// tfc is a TOGGLE: every toggle is guarded on the camera actually being (or not being)
	// free, on the game's thread, so a camera something else took back is never toggled
	// INTO free. If the player leaves the free camera themselves, it stays left.
	// Rapport.ini FreeCamera = 0 turns it off.
	class FreeCam
	{
	public:
		[[nodiscard]] static FreeCam& GetSingleton() noexcept;

		// The actors of an AAF scene that just animated / just ended. Nothing happens unless
		// the player is among them.
		// AAF's walk began (Bridge OnWalkInit): the view NOW is the one to come back to --
		// before AAF moves the camera for the scene.
		void Walking(const std::vector<std::uint32_t>& a_actors);
		void Animating(const std::vector<std::uint32_t>& a_actors);
		void SceneEnded(const std::vector<std::uint32_t>& a_actors);

		// Every bridge poll: remembers the player's view while no scene holds them, and
		// notices when the free camera was taken away.
		void Pump();

		// A load or a new game: whatever was in flight is over.
		void Reset();

	private:
		FreeCam() = default;

		std::atomic_bool     _inScene{ false };     // the player is in an AAF scene we follow
		std::atomic_bool     _entered{ false };     // we turned the free camera on, and it is still ours
		std::atomic_bool     _lastFirst{ false };   // the player's view before the scene: first person
		std::atomic_bool     _held{ false };        // a walk began: _lastFirst is frozen until the scene ends
		std::atomic_int64_t  _heldAtMs{ 0 };
		std::atomic_uint32_t _generation{ 0 };      // a scene's tasks never act on the next one
		std::atomic_uint32_t _misses{ 0 };          // polls in a row the free camera was not up
	};
}
