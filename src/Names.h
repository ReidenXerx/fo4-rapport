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
	// Nora; at five and up it is roles, creatures and robots -- Raider, Settler,
	// Drifter, Resident, Guard, Worker -- with a few exceptions a count cannot see
	// (a mod's non-Unique "Will"). So the census proposes and names.json disposes:
	// its "keep" list is never a label, its "label" list always is. A count works
	// in every language, where a list of label words would not survive a
	// localized game. Rapport-labels.txt, next to Rapport.log, lists every label.
	//
	// Never someone who has ever been the player's companion, never the dead.
	//
	// The name is DERIVED, the persona's way (R-7): the same person gets the same
	// name forever, on every machine, and the name itself costs the save nothing.
	// What the save keeps is WHO has been introduced -- and which base they were,
	// so that a spawned actor whose id the engine hands to somebody new is seen as
	// somebody new.
	//
	// Applied with the engine's own custom-name call (ExtraDataList::SetOverrideName,
	// what a renamed weapon uses), which the game keeps in the save by itself
	// (measured 2026-09-23: "1 kept their name, 0 named again"). Where it does not --
	// a cell that reset -- the name is given back at the next introduction, quietly,
	// if it is still the same person. Never over somebody else's custom name.
	class Names
	{
	public:
		[[nodiscard]] static Names& GetSingleton() noexcept;

		// Built-in lists, then Data/F4SE/Plugins/Rapport/names.json if the player
		// has one (it may replace any list, and add "keep" and "label" lists), then
		// the MCM [Names] section. The first call also counts every NPC record's name
		// (the labels): at data ready, before any world exists.
		void Load();

		// Introduce this actor. The new name if they were nameless and are named NOW;
		// empty if they have a real name, are the player or dead, were introduced
		// already (their name is quietly given back if the game lost it), or names are
		// switched off. The rename goes through F4SE's task queue to the main thread.
		[[nodiscard]] std::string Introduce(RE::Actor* a_actor);

		// The name this actor would be given. Derived, so it can be asked any time.
		[[nodiscard]] std::string NameFor(std::uint32_t a_formID, bool a_female) const;

		// The co-save's half: who has been introduced, and as which base (0 when a
		// save from before bases were kept says only who).
		[[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>> Introduced() const;
		void Restore(std::vector<std::pair<std::uint32_t, std::uint32_t>> a_introduced);
		void Clear();

		// They died: forgotten, so the id cannot carry the name to somebody new.
		void Forget(std::uint32_t a_formID);

	private:
		// Main thread only. Skips an actor who already wears a custom name: ours kept
		// by the game, or somebody else's. True if it named them.
		static bool Apply(RE::Actor* a_actor, const std::string& a_name);
		static void ApplyOnMainThread(std::uint32_t a_formID, std::string a_name);
		// Why not, or "" if they are nameless.
		[[nodiscard]] std::string WhyNotNameless(RE::Actor* a_actor) const;
		// Every NPC record's name, counted once.
		void CountLabels();

		mutable std::timed_mutex                          _lock;
		bool                                              _enabled{ true };
		std::vector<std::string>                          _female;
		std::vector<std::string>                          _male;
		std::vector<std::string>                          _surnames;
		// Form id -> the base they had when introduced (0: not known).
		std::unordered_map<std::uint32_t, std::uint32_t>  _introduced;
		// Names five or more NPC records share, not all Unique: a label, not a name.
		std::unordered_set<std::string>                   _labels;
		// names.json: never a label / always a label, whatever the count says.
		std::unordered_set<std::string>                   _keep;
		std::unordered_set<std::string>                   _forceLabels;
		bool                                              _counted{ false };
	};
}
