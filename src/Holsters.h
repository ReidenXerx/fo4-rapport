#pragma once

namespace RP
{
	// Visible Favorites' holstered weapons, hidden for an AAF scene (owner, 2026-09-30: "all is
	// having sex with their pistol attached").
	//
	// Visible Favorites - F4SE (Nexus 108583) draws a favourite weapon on a body as a CLONED NODE
	// on a bone -- VisFavNpc_<id>_s<slot> on an NPC, VisFavSlot_<slot> on the player (the DLL's own
	// strings) -- not as an armour piece. AAF undresses by unequipping biped slots, so it never
	// reaches one; and the mod's INI has no NPC "hide when naked", only bNPCHideSleeping. So for as
	// long as someone is in an AAF scene -- Rapport's or anybody's, a mechanic on every scene --
	// every node of theirs named "VisFav..." is app-culled, again on every poll (the mod may rebuild
	// a display when AAF's undressing changes their equipment), and exactly the nodes culled HERE
	// are shown again when the scene ends. A node the mod had hidden itself is never touched.
	//
	// Nothing happens without VisibleFavorites.dll loaded. The 3D is touched only on the game's
	// main thread -- the bridge's natives, which are frame-synced; a call from anywhere else only
	// marks, and the next poll does the work.
	class Holsters
	{
	public:
		[[nodiscard]] static Holsters& GetSingleton() noexcept;

		void SceneStarted(const std::vector<std::uint32_t>& a_actors);
		void SceneEnded(const std::vector<std::uint32_t>& a_actors);

		// From the bridge's pump, every poll: hide again, and show whoever's scene has ended.
		void Pump();

		// A load: the 3D these names were on belongs to the world being left.
		void Reset();

	private:
		struct Held
		{
			std::uint32_t                         actor{ 0 };
			std::vector<std::string>              culled;   // node names hidden here, to show again
			std::chrono::steady_clock::time_point since{};
			bool                                  ending{ false };
			bool                                  logged{ false };
		};

		// A scene end that never came: MaxSceneSeconds is 600, so this is past any real scene.
		static constexpr auto kLongest = std::chrono::minutes{ 15 };

		[[nodiscard]] bool Present();
		[[nodiscard]] bool OnMainThread() const;
		void               Hide(Held& a_held);
		void               Show(Held& a_held);

		std::mutex        _lock;
		std::vector<Held> _held;
		int               _present{ -1 };
		std::atomic<std::thread::id> _mainThread{};
	};
}
