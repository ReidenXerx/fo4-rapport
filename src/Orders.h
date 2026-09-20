#pragma once

namespace RP
{
	// One instruction for the bridge.
	//
	// The plugin cannot call AAF -- dispatching into the VM from a job thread
	// crashed the game twice -- so everything AAF has to do arrives here and the
	// bridge collects it on the poll it is already making. Overlays and
	// expressions share the queue because they share the constraint, and one
	// drain loop is easier to keep bounded than two.
	struct Order
	{
		enum class Kind
		{
			kApplyOverlay = 1,
			kRemoveOverlay = 2,
			kApplyExpression = 3,

			// Take AAF's busy and locked keywords off an actor. Only ever sent for
			// someone a scene of OURS left flagged -- AAF clears them itself when a
			// scene ends properly, so a flag still there is one nothing will ever
			// clear, and the actor is unusable by every AAF mod until it goes.
			kRelease = 4,

			// Take a face back off. Separate from kApplyExpression because applying
			// the zeroed set may not be enough on its own: every mfgSet in every
			// installed pack sets lock="true" and not one ships lock="false", so
			// nothing on this machine demonstrates what releases a lock. This kind
			// applies the cleared set AND asks AAF to drop its morph blocks, which
			// costs one extra call and removes the guess.
			kClearExpression = 5,

			// Commonwealth Moisturizer. Its cum is worn geometry, not a texture --
			// a BodySlide-conformed mesh on an armour slot plus morphing headparts
			// for the face -- so it is driven through its own modder API rather
			// than through AAF's overlay calls. setID carries the regions: any of
			// F (front), O (oral), R (rear).
			//
			// These two go to a SEPARATE queue, drained by a SEPARATE script in an
			// optional plugin. Nothing in Rapport's own scripts may name a
			// Moisturizer type, or Rapport would carry an unresolvable reference
			// on every install that does not have it.
			kApplyMoisturizer = 6,
			kClearMoisturizer = 7,

			// AAF has stopped answering: run its own EveryTime_Initialization
			// again. Neither of these carries an actor or a set -- they are about
			// the framework, not about anybody in it.
			kReviveAAF = 9,

			// AAF's main quest is not running at all. Starting another mod's quest
			// is a bigger act than re-running its init, and the reason may be that
			// AAF is being removed from this save -- which nothing here can see and
			// the player can. So this one asks.
			kAskStartAAF = 10,

			// The gentle restart did not take. This is AAF's OWN harder reboot --
			// Stop() then Start() on its main quest, which is what its updater does
			// to itself when the version changes. GAME_DATA is carried across by
			// hand, exactly as AAF carries it, because a quest stop resets the
			// script and that string is the identity its stored data is keyed to.
			kRestartAAFQuest = 11,

			// Nothing between 12 and 14 any more. kChangePosition,
			// kChangeToPosition, kRelocate and kResumeElsewhere all existed to move
			// a scene AAF had already started, and AAF does not allow that:
			// ChangePosition was refused 26 times out of 26 with tags, and refused
			// again handed a position id and no filters at all. Relocation -- stop,
			// then start again -- did work and is in the history if a mid-scene
			// change is ever wanted, but nothing triggers it now that a stage asks
			// for nothing.

			// Ask AAF what it would match for these two, with the same tag we are
			// about to request. setID is that tag.
			//
			// This exists because ChangePosition has been refused 26 times out of
			// 26 -- every stage, every scene, on furniture and off it, inside trees
			// and outside -- for tags whose content demonstrably exists: five
			// selectable female+male kissing positions, 273 female+male positions
			// in all. Counting the XML ourselves says one thing and AAF says
			// another, and only AAF's answer decides anything.
			kQueryAnimations = 15,

			// Teleports, and they exist for TESTING rather than for the mod.
			//
			// Papyrus has MoveTo and it handles cells, loaded or not; C++ here has
			// only SetPosition, which moves within a cell and is no use for the one
			// case that matters -- reaching somebody who unloaded. So the doorbell
			// carries it, at the cost of one poll.
			//
			// These are handled BEFORE the Is3DLoaded guard in the drain, and that
			// is deliberate: an unloaded actor is the POINT of them, and MoveTo is
			// not an AAF call, so it cannot wedge the stack the way the guard
			// exists to prevent.
			kMovePlayerTo = 16,   // put the player next to this actor
			kMoveHere = 17,       // put this actor next to the player

			// Point the player's camera. setID carries "pitch,yaw" in degrees,
			// already worked out on the C++ side -- it has the positions and real
			// trigonometry, and Papyrus has SetAngle. Splitting it that way keeps
			// the arithmetic somewhere it can be read.
			kLookAt = 18,

			// Game.PassTime. The aftermath holds for TWELVE GAME HOURS and every
			// cooldown is in game hours, so without this none of that layer can be
			// tested without playing through it in real time.
			kPassTime = 19,

			// EnableAI(false). Holds an NPC exactly where they are so they can be
			// looked at without wandering off mid-observation. setID is "0" to
			// freeze and "1" to thaw. Papyrus-only, hence an order.
			kSetAI = 20,

			// A one-line dump of everything the engine will tell us about an
			// actor, traced into the log. Papyrus-only: almost none of these
			// queries are exposed on the C++ side.
			kState = 21,

			// Debug.SetGodMode. The console is unreachable from Papyrus -- that
			// was searched for and confirmed absent -- but the cheat EFFECTS are
			// ordinary natives on Debug.
			kGodMode = 22
		};

		Kind          kind{ Kind::kApplyOverlay };
		std::uint32_t formID{ 0 };
		std::string   setID;

		// A second string. Only the query uses it now: the tags to exclude.
		std::string   extra;
	};
}
