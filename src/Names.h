#pragma once

#include "NamedLock.h"

namespace RP
{
	// Names for the nameless (owner, 2026-09-23: "generate persistent names for NPC
	// if they didn't have names before in moment player approach them").
	//
	// A generic NPC -- a Drifter, a Settler, a Diamond City Resident: any actor whose
	// base is not flagged Unique -- is given a first name and a surname the first
	// time an addon INTRODUCES them (Overture does, on the player's first approach).
	//
	// The name is DERIVED, the persona's way (R-7): the same person gets the same
	// name forever, on every machine, and the name itself costs the save nothing.
	// What the save keeps is only WHO has been introduced, so a stranger stays
	// "Drifter" until they have told the player their name.
	//
	// Applied with the engine's own custom-name call (ExtraDataList::SetOverrideName,
	// what a renamed weapon uses), on the main thread. Put back after every load for
	// everyone in memory, and again whenever one of them loads, in case the game does
	// not keep a custom name on an actor. Never over somebody else's custom name.
	//
	// A death forgets them. A dead stranger's form id can be handed to somebody new
	// once the body is cleaned up, and a stranger must never walk in already named.
	// The cost is that a body may show as "Settler" again after a load.
	class Names
	{
	public:
		[[nodiscard]] static Names& GetSingleton() noexcept;

		// Built-in lists, then Data/F4SE/Plugins/Rapport/names.json if the player
		// has one (it may replace any list), then the MCM [Names] section.
		void Load();

		// Introduce this actor. The new name if they were nameless and are named NOW;
		// empty if they have a real name, are the player, were introduced already, or
		// names are switched off. Safe from a Papyrus thread: the rename is queued
		// onto the main thread.
		[[nodiscard]] std::string Introduce(RE::Actor* a_actor);

		// The name this actor would be given. Derived, so it can be asked any time.
		[[nodiscard]] std::string NameFor(std::uint32_t a_formID, bool a_female) const;

		// The co-save's half: who has been introduced.
		[[nodiscard]] std::vector<std::uint32_t> Introduced() const;
		void Restore(std::vector<std::uint32_t> a_introduced);
		void Clear();

		// After a load, on the main thread: every introduced actor in memory gets
		// their name back.
		void Reapply();

		// An introduced actor's 3D loaded (main thread): their name goes back on if the
		// game did not keep it.
		void OnLoaded(std::uint32_t a_formID);

		// They died: forgotten, so the id cannot carry the name to somebody new.
		void Forget(std::uint32_t a_formID);

		// The engine's object-loaded event, once, at data ready.
		static void RegisterLoadSink();

	private:
		// Main thread only. Skips an actor who already wears a custom name: ours kept
		// by the game, or somebody else's. True if it named them.
		static bool Apply(RE::Actor* a_actor, const std::string& a_name);
		[[nodiscard]] static bool Nameless(RE::Actor* a_actor);

		mutable std::timed_mutex          _lock;
		bool                              _enabled{ true };
		std::vector<std::string>          _female;
		std::vector<std::string>          _male;
		std::vector<std::string>          _surnames;
		std::unordered_set<std::uint32_t> _introduced;
		// _introduced.size(), readable without the lock: the object-loaded event
		// fires for every object in every cell, and nearly always nobody is named.
		std::atomic<std::size_t>          _count{ 0 };
	};
}
