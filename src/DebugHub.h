#pragma once

namespace RP
{
	// One switch for diagnostics across the whole stack.
	//
	// Debugging this mod means debugging three at once, each with its own idea of
	// where a setting lives: AAF takes them at runtime through its API, anything
	// with an MCM page takes them by name, and the engine's Papyrus logging is an
	// ini the game reads once at startup. Without somewhere to hold that, turning
	// diagnostics on is fiddly and turning them off is forgotten -- which is how a
	// stray troubleshooting_level put six modal pop-ups in front of the owner.
	//
	// So the value here is not switching things on. It is switching them back.
	class DebugHub
	{
	public:
		enum class Target
		{
			kAAF,
			kMCM
		};

		struct Entry
		{
			Target      target{ Target::kAAF };
			std::string mod;      // MCM only
			std::string key;
			std::string type;     // bool | int | float | string
			std::string value;    // always carried as text; Papyrus converts
		};

		[[nodiscard]] static DebugHub& GetSingleton() noexcept;

		// Reads debug.json and applies everything this side can apply. The rest is
		// handed to the bridge, which is the only half that can talk to AAF and MCM.
		void Load();

		[[nodiscard]] const std::vector<Entry>& Entries() const noexcept { return _entries; }
		[[nodiscard]] const std::string& ProfileName() const noexcept { return _profile; }

		[[nodiscard]] static std::filesystem::path ConfigPath();

	private:
		// Writes bEnableLogging / bEnableTrace into Fallout4Custom.ini, backing the
		// file up once. The engine reads these at startup, so a change here lands on
		// the NEXT launch and says so rather than pretending otherwise.
		void ApplyEngineLogging(bool a_enabled);

		std::string        _profile{ "off" };
		std::vector<Entry> _entries;
	};
}
