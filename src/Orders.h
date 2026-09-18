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

			// Move a running scene to the next stage of a scenario. setID carries
			// the tags AAF may choose from, extra the ones it must avoid. It picks
			// the animation; the scenario only names the KIND of moment it wants.
			kChangePosition = 8,

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

			// Move the scene to a NAMED position rather than to whatever fits a set
			// of tags. setID is the position id. This is how a tree gets chosen:
			// AAF has no tree API, but a position may declare one, so naming the
			// position is naming the tree -- and it is the only way to reach a
			// climax, every standalone one being hidden by design.
			kChangeToPosition = 12
		};

		Kind          kind{ Kind::kApplyOverlay };
		std::uint32_t formID{ 0 };
		std::string   setID;

		// A second string, used only by kChangePosition, which needs both the tags
		// AAF may choose from and the tags it must avoid.
		std::string   extra;
	};
}
