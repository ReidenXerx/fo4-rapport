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
}
