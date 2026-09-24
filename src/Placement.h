#pragma once

namespace RP::Placement
{
	// "Scenes starting INSIDE a table / clutter" (owner, 2026-09-24): STAGE 1, DETECTION ONLY.
	//
	// AAF plays a ground position where the first actor stands, and knows nothing of what
	// stands around that spot; an animation that spreads a metre or two sideways -- lying,
	// doggy -- then puts the bodies through a table. Stage 2 will choose a clear spot and hand
	// AAF a marker through SceneSettings.locationObject. Before that moves anything, this says
	// what the geometry test SEES, so the owner can hold it against the scenes he watches clip.
	//
	// Once per scene, at its first animation: every loaded reference in the actors' cells whose
	// base object is solid (static, static collection, movable static, furniture, container,
	// activator, door, terminal, flora) and whose bounds box, placed by the reference's own
	// position, Z rotation and scale, reaches into a cylinder around the actors -- radius by
	// the position's kind, from knee to chest height so the floor under them never counts.
	// Written to Rapport.log; nothing in the world is touched.
	void Survey(const std::vector<std::uint32_t>& a_actors, std::string_view a_position, std::string_view a_tags,
		bool a_ours);

	// Runs the surveys that have waited long enough (4 s after the first animation, so
	// the actors stand where the scene plays). Called on Rapport's poll.
	void Pump();

	// STAGE 2 (owner, same day): where OUR scene should play. Asked by the bridge just before
	// StartScene, with the actors in AAF's slot order (slot 0 is where AAF would put it).
	//
	// Returns {x, y, z, facing in degrees} of a clear spot to hand AAF as locationObject, or
	// EMPTY to leave AAF's own spot alone -- which is the answer when:
	//   - the chosen position needs furniture (AAF places it on the furniture itself);
	//   - AAF's own spot is already clear;
	//   - the cell's navmesh cannot be read (no guessing about floors and walls);
	//   - nothing clear is found within reach (the any-pose rule: never fail a scene over this).
	// A spot is CLEAR when a ring of points around it, at the footprint's radius and half of it,
	// all stand on the cell's navmesh within a step of one another -- walkable floor, which the
	// baked navmesh already routes around statics and furniture -- AND no solid object's bounds
	// box reaches into the footprint (settlement builds and moved clutter, which the baked
	// navmesh does not know). It must be REACHABLE: a straight walk from slot 0 stays on the
	// navmesh. Every decision is one Rapport.log line.
	[[nodiscard]] std::vector<float> ChooseSpot(RE::Actor* a_slot0, RE::Actor* a_slot1, std::string_view a_position,
		std::int32_t a_request);
}
