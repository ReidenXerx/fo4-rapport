#pragma once

// Runtime Database finds the game's FUNCTIONS on OG, NG and AE; it does not make a class's
// LAYOUT the same (its own docs, FEATURES.md). Rapport reads a few members directly -- an
// actor's race, life state and extra data, the process lists' handles, Main's freeze flag, the
// UI's menu counters -- so before any of it is trusted, each is read once and checked against
// what it must be. A runtime where one differs turns Rapport off for the session, said in one
// line naming the member: never half on.
//
// The OG-only build (RAPPORT_RUNTIME_DATABASE OFF) refuses every other runtime at load, so
// there Ok() is always true and Check() does nothing.

namespace RP::Layout
{
	// On the first load or new game of a session, before anything reads an actor. Main thread.
	void Check();

	// True once the members checked out, or on the OG-only build. False before the first
	// check has run, and for the rest of the session after one failed.
	[[nodiscard]] bool Ok() noexcept;
}
