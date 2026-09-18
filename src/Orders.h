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
			kClearExpression = 5
		};

		Kind          kind{ Kind::kApplyOverlay };
		std::uint32_t formID{ 0 };
		std::string   setID;
	};
}
