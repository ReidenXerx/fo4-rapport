#pragma once

namespace RP
{
	// Rapport configuring the mods it works alongside (amendment A-11).
	//
	// Where Rapport takes over a job another mod is also doing, the two fight, and
	// the user is the one who sees the result without being told why. The honest
	// answer is neither "make them edit an ini" nor "do it silently": detect the
	// other mod's state, act, and say exactly what was changed and for what
	// reason. Nothing is deleted, no file is touched, and turning the feature off
	// puts it back.
	//
	// The plugin resolves the forms -- it is the half that can read the load order
	// cheaply -- and the bridge does the stopping, because a quest is game state
	// and all game state is touched from the VM's own thread.
	class Takeover
	{
	public:
		struct Item
		{
			std::string   plugin;
			std::string   name;      // editor id, for the log
			std::string   reason;
			std::uint32_t formID{ 0 };   // resolved, runtime
			bool          restoreByStarting{ true };
		};

		[[nodiscard]] static Takeover& GetSingleton() noexcept;

		// Reads takeover.json and resolves every quest against the load order.
		// A plugin that is not installed drops out here, quietly and by design.
		void Load();

		[[nodiscard]] const std::vector<Item>& Items() const noexcept { return _items; }

		// True while Rapport owns the feature these takeovers exist for. When it
		// is false the bridge starts the quests again instead of stopping them.
		[[nodiscard]] bool ShouldTakeOver() const noexcept { return _active; }
		void SetActive(bool a_active) noexcept { _active = a_active; }

		[[nodiscard]] static std::filesystem::path ConfigPath();

	private:
		std::vector<Item> _items;
		bool              _active{ true };
	};
}
