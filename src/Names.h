#pragma once

#include "NamedLock.h"

namespace RP
{
	// Names for the nameless (owner, 2026-09-23: "generate persistent names for NPC
	// if they didn't have names before in moment player approach them").
	//
	// A generic NPC -- a Drifter, a Settler, a Diamond City Resident -- is given a
	// first name and a surname the first time an addon INTRODUCES them (Overture
	// does, on the player's first approach). Generic means the name they go by is
	// a LABEL, and, if their base is flagged Unique, that they inherit it from a
	// template rather than carrying it themselves.
	//
	// A label is a name on five or more NPC records, at least one of them not
	// Unique. Measured 2026-09-23 on this load order (10,962 records): at three
	// records the list already held Preston Garvey, Magnolia, Curie, Shaun and
	// Nora; at five and up, with the game itself using the name generically at
	// least once, it holds Raider, Settler, Drifter, Resident, Guard, Worker --
	// and the few personal names left there (a mod's "Will") carry a name of
	// their own, which the template test protects. The Unique flag alone was
	// wrong both ways: TrainBar.esp's thirteen Third Rail patrons are flagged
	// Unique and all called "Drifter". A count works in every language; a list of
	// label words would not survive a localized game. Rapport-labels.txt, next to
	// Rapport.log, lists every label with its counts.
	//
	// Never someone who has ever been the player's companion.
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
		// has one (it may replace any list), then the MCM [Names] section. The
		// first call also counts every NPC record's name (the labels). Main thread.
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
		// Why not, or "" if they are nameless. Main thread or VM thread.
		[[nodiscard]] std::string WhyNotNameless(RE::Actor* a_actor) const;
		// Every NPC record's name, counted once. Main thread, at data ready.
		void CountLabels();

		mutable std::timed_mutex          _lock;
		bool                              _enabled{ true };
		std::vector<std::string>          _female;
		std::vector<std::string>          _male;
		std::vector<std::string>          _surnames;
		std::unordered_set<std::uint32_t> _introduced;
		// Names three or more NPC records share: a label, not a name.
		std::unordered_set<std::string>   _labels;
		bool                              _counted{ false };
		// _introduced.size(), readable without the lock: the object-loaded event
		// fires for every object in every cell, and nearly always nobody is named.
		std::atomic<std::size_t>          _count{ 0 };
	};
}
