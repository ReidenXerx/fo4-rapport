#include "Placement.h"

#include "TreeIndex.h"

namespace RP::Placement
{
	namespace
	{
		// One survey per scene: a tree steps through positions on the same spot.
		constexpr auto kResurvey = std::chrono::seconds{ 120 };

		// The footprint's vertical band above the actors' feet. Below the knee is floor,
		// rugs and plates; above the chest is a shelf nobody lies in.
		constexpr float kBandLow = 20.0f;
		constexpr float kBandHigh = 110.0f;

		// A piece that dwarfs the footprint and surrounds its centre is a room shell or a
		// static collection's whole box, not something in the way. Counted, not listed.
		constexpr float kEnclosingFactor = 4.0f;

		// An intrusion shallower than this is a brush, not a clip.
		constexpr float kTolerated = 8.0f;
		// ...and so is one that stays in the footprint's outer part. The footprint is a
		// circle, but the bodies are not: a spooning pair lies along one line, and a box
		// 38u into its r 110 stood beside them. The owner watched that scene and a
		// standing 69 with a container 39u in, and both "looked fine" (2026-09-25). So a
		// hit clips only when it reaches the inner 60% of the radius (44u at r 110). The
		// moves that same night were for hits of 46-110u, and they still count.
		constexpr float kOuterShare = 0.4f;
		// A least-bad spot must beat AAF's own by at least this much to be worth the walk.
		constexpr float kWorthMoving = 20.0f;

		// The search for a clear spot: rings every kStep units out to kReach, kAngles points
		// a ring. Far enough to leave a room's clutter, near enough that AAF's walk is short.
		constexpr float        kStep = 30.0f;
		constexpr float        kReach = 330.0f;
		constexpr std::int32_t kAngles = 16;

		// The navmesh keeps clear of walls and furniture by about this much, so the
		// floor ring stands this far inside the footprint: a small room was rejecting
		// every spot as "the footprint leaves the floor" (measured 2026-09-24).
		constexpr float kNavmeshInset = 35.0f;

		// The survey waits for the actors to be in place: at the first animation AAF may
		// still be walking one of them in (a 397u "footprint", measured).
		constexpr auto kSurveyDelay = std::chrono::seconds{ 4 };

		// Floor continuity: how far a footprint's points may differ in height (a rug, a
		// threshold), and one walk sample from the next (a step, not a ledge).
		constexpr float kFlat = 24.0f;
		constexpr float kStepUp = 36.0f;
		// How far above or below the actors' feet the floor under a spot may be found:
		// the same storey, not the one below.
		constexpr float kStorey = 80.0f;

		constexpr float kPi = 3.14159265358979f;

		std::mutex                                                              g_lock;
		std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> g_surveyed;
		// Our last chosen spot, so the first animation's survey can say whether AAF used it.
		struct Chosen
		{
			std::uint64_t pair{ 0 };
			RE::NiPoint3  spot{};
			std::int32_t  request{ 0 };
		};
		Chosen g_chosen;

		[[nodiscard]] std::string Lower(std::string_view a_text)
		{
			std::string out{ a_text };
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		[[nodiscard]] std::uint64_t PairKey(std::vector<std::uint32_t> a_ids)
		{
			std::ranges::sort(a_ids);
			std::uint64_t key = 1469598103934665603ull;
			for (const auto id : a_ids) {
				key = (key ^ id) * 1099511628211ull;
			}
			return key;
		}

		// How far the animation spreads, by its kind. ASSUMED numbers, the thing stage 1
		// exists to calibrate: lying and all-fours positions reach far, standing ones little.
		// Unknown (AAF has not picked yet) takes the widest: the spot must fit whatever comes.
		[[nodiscard]] float RadiusFor(std::string_view a_position, std::string_view a_tags)
		{
			if (a_position.empty() && a_tags.empty()) {
				return 110.0f;
			}
			const auto text = Lower(a_position) + "," + Lower(a_tags);
			static constexpr std::array kWide{ "missionary", "doggy", "prone", "spoon", "cowgirl", "69", "lying",
				"laying", "matingpress", "cunnilingus", "bed", "floor" };
			static constexpr std::array kNarrow{ "standing", "kneeling", "carry", "wall", "chair", "stool" };
			if (std::ranges::any_of(kWide, [&](const char* k) { return text.find(k) != std::string::npos; })) {
				return 110.0f;
			}
			if (std::ranges::any_of(kNarrow, [&](const char* k) { return text.find(k) != std::string::npos; })) {
				return 70.0f;
			}
			return 90.0f;
		}

		[[nodiscard]] bool Solid(RE::ENUM_FORM_ID a_type)
		{
			using T = RE::ENUM_FORM_ID;
			switch (a_type) {
			case T::kSTAT:
			case T::kSCOL:
			case T::kMSTT:
			case T::kFURN:
			case T::kCONT:
			case T::kACTI:
			case T::kDOOR:
			case T::kTERM:
			case T::kFLOR:
				return true;
			default:
				return false;
			}
		}

		// Things with bounds that nobody can walk into: the editor markers (XMarker,
		// XMarkerHeading -- our own spot marker among them -- COCMarkerHeading) and
		// AAF's own scene helpers (AAF.esm). Measured: both were reported "AT the spot".
		[[nodiscard]] bool Marker(RE::TESBoundObject* a_base)
		{
			const auto* file = a_base->GetFile(0);
			if (!file) {
				return false;
			}
			const auto name = file->GetFilename();
			if (name == "AAF.esm"sv) {
				return true;
			}
			if (name == "Fallout4.esm"sv) {
				const auto local = a_base->GetFormID() & 0x00FFFFFF;
				return local == 0x34 || local == 0x3B || local == 0x32;
			}
			return false;
		}

		struct Hit
		{
			float       depth{ 0.0f };
			std::string what;
		};

		struct Intrusions
		{
			std::vector<Hit> hits;
			std::uint32_t    enclosing{ 0 };
			std::uint32_t    looked{ 0 };
			float            tolerated{ kTolerated };   // set by Scan from the footprint

			[[nodiscard]] std::size_t Blocking() const
			{
				return static_cast<std::size_t>(
					std::ranges::count_if(hits, [&](const Hit& h) { return h.depth > tolerated; }));
			}
			// The deepest intrusion, 0 when nothing reaches in: how bad a spot is.
			[[nodiscard]] float Worst() const { return hits.empty() ? 0.0f : hits.front().depth; }
			[[nodiscard]] std::string List(std::size_t a_max = 8) const
			{
				// What Blocking() counts, and only that: a list longer than its count read as a
				// miscount (request 3, 2026-09-25: "2 in the way" over three names, one 1u in).
				std::string out;
				std::size_t shown = 0, brushes = 0;
				for (const auto& hit : hits) {
					if (hit.depth <= tolerated) {
						++brushes;
					} else if (shown < a_max) {
						out += (shown++ ? "; " : "") + hit.what;
					}
				}
				if (brushes) {
					out += std::format("{}{} brushing it", out.empty() ? "" : "; ", brushes);
				}
				return out;
			}
		};

		// Every solid object in these cells whose bounds box reaches into the footprint.
		[[nodiscard]] Intrusions Scan(
			const std::vector<RE::TESObjectCELL*>& a_cells, const RE::NiPoint3& a_centre, float a_feet, float a_radius,
			bool a_describe)
		{
			Intrusions out;
			out.tolerated = (std::max)(kTolerated, a_radius * kOuterShare);
			const float low = a_feet + kBandLow;
			const float high = a_feet + kBandHigh;
			for (auto* cell : a_cells) {
				RE::BSAutoLock<RE::BSSpinLock> lock{ cell->spinLock };
				for (const auto& held : cell->references) {
					auto* ref = held.get();
					if (!ref || ref->Is(RE::ENUM_FORM_ID::kACHR) || ref->IsDisabled() || ref->IsDeleted() || !ref->Get3D()) {
						continue;
					}
					auto* base = ref->GetObjectReference();
					if (!base || !Solid(base->GetFormType()) || Marker(base)) {
						continue;
					}
					const auto& bd = base->boundData;
					const float scale = ref->refScale ? static_cast<float>(ref->refScale) / 100.0f : 1.0f;
					const float minX = bd.boundMin.x * scale, maxX = bd.boundMax.x * scale;
					const float minY = bd.boundMin.y * scale, maxY = bd.boundMax.y * scale;
					const float minZ = bd.boundMin.z * scale, maxZ = bd.boundMax.z * scale;
					if (maxX - minX < 4.0f && maxY - minY < 4.0f) {
						continue;   // a marker or a point: nothing to walk into
					}
					++out.looked;
					const auto pos = ref->GetPosition();
					if (pos.z + maxZ < low || pos.z + minZ > high) {
						continue;
					}
					// Horizontal, exact for a Z rotation: the centre into the box's own frame.
					const float dx = a_centre.x - pos.x;
					const float dy = a_centre.y - pos.y;
					const float c = std::cos(-ref->data.angle.z);
					const float s = std::sin(-ref->data.angle.z);
					const float lx = dx * c - dy * s;
					const float ly = dx * s + dy * c;
					const float gap = std::hypot(lx - std::clamp(lx, minX, maxX), ly - std::clamp(ly, minY, maxY));
					if (gap >= a_radius) {
						continue;
					}
					const bool inside = gap == 0.0f;
					if (inside && (maxX - minX) > a_radius * kEnclosingFactor && (maxY - minY) > a_radius * kEnclosingFactor) {
						++out.enclosing;
						continue;
					}
					Hit hit{ a_radius - gap, {} };
					if (a_describe) {
						const auto* file = base->GetFile(0);
						hit.what = std::format("{} {:08X} ({}) ref {:08X}, {:.0f}u in{}",
							RE::TESForm::GetFormTypeString(base->GetFormType()), base->GetFormID(),
							file ? file->GetFilename() : "?", ref->GetFormID(), a_radius - gap, inside ? ", AT the spot" : "");
					}
					out.hits.push_back(std::move(hit));
				}
			}
			std::ranges::sort(out.hits, std::greater{}, &Hit::depth);
			return out;
		}

		// ---- the navmesh ---------------------------------------------------------
		// CommonLibF4 only forward-declares TESObjectCELL::navMeshes (a NavMeshArray). Read as
		// the BSTArray<NiPointer<NavMesh>> it is in the engine's sister titles -- data at 0x00,
		// size at 0x10 -- and trusted only entry by entry, by the game's own NavMesh vtable.
		// The raw reads sit behind a structured-exception guard: a wrong guess skips the
		// navmesh (and with it the move) instead of crashing the game.
		struct Tri
		{
			RE::NiPoint3 a, b, c;
		};

		bool ReadNavmeshPointers(const void* a_array, std::uintptr_t a_vtable, void** a_out, std::uint32_t a_max,
			std::uint32_t* a_count) noexcept
		{
			__try {
				const auto* bytes = static_cast<const std::uint8_t*>(a_array);
				const auto  data = *reinterpret_cast<void* const*>(bytes + 0x00);
				const auto  size = *reinterpret_cast<const std::uint32_t*>(bytes + 0x10);
				if (!data || size == 0 || size > a_max) {
					*a_count = 0;
					return size == 0;
				}
				std::uint32_t n = 0;
				for (std::uint32_t i = 0; i < size; ++i) {
					void* mesh = static_cast<void* const*>(data)[i];
					if (mesh && *static_cast<std::uintptr_t*>(mesh) == a_vtable) {
						a_out[n++] = mesh;
					}
				}
				*a_count = n;
				return true;
			} __except (1) {
				*a_count = 0;
				return false;
			}
		}

		// The triangles of every navmesh in these cells. Empty with a reason when unreadable.
		[[nodiscard]] std::vector<Tri> Navmesh(const std::vector<RE::TESObjectCELL*>& a_cells, std::string& a_why)
		{
			std::vector<Tri> tris;
			static REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::NavMesh[0] };
			for (auto* cell : a_cells) {
				if (!cell->navMeshes) {
					continue;
				}
				void*         meshes[64]{};
				std::uint32_t count = 0;
				if (!ReadNavmeshPointers(cell->navMeshes, vtable.address(), meshes, 64, &count)) {
					a_why = "the cell's navmesh list did not read as expected";
					return {};
				}
				for (std::uint32_t i = 0; i < count; ++i) {
					auto* mesh = static_cast<RE::NavMesh*>(meshes[i]);
					const auto& verts = mesh->vertices;
					for (const auto& t : mesh->triangles) {
						if (t.vertices[0] >= verts.size() || t.vertices[1] >= verts.size() || t.vertices[2] >= verts.size()) {
							continue;
						}
						tris.push_back(Tri{ verts[t.vertices[0]].location, verts[t.vertices[1]].location,
							verts[t.vertices[2]].location });
					}
				}
			}
			if (tris.empty() && a_why.empty()) {
				a_why = "no navmesh in the actors' cell";
			}
			return tris;
		}

		// The floor under (x, y) on the navmesh, the one nearest a_near in height, if any
		// lies within kStorey of it.
		[[nodiscard]] std::optional<float> FloorAt(const std::vector<Tri>& a_tris, float a_x, float a_y, float a_near)
		{
			std::optional<float> best;
			for (const auto& t : a_tris) {
				const float d = (t.b.y - t.c.y) * (t.a.x - t.c.x) + (t.c.x - t.b.x) * (t.a.y - t.c.y);
				if (std::fabs(d) < 1e-3f) {
					continue;
				}
				const float l1 = ((t.b.y - t.c.y) * (a_x - t.c.x) + (t.c.x - t.b.x) * (a_y - t.c.y)) / d;
				const float l2 = ((t.c.y - t.a.y) * (a_x - t.c.x) + (t.a.x - t.c.x) * (a_y - t.c.y)) / d;
				const float l3 = 1.0f - l1 - l2;
				if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f) {
					continue;
				}
				const float z = l1 * t.a.z + l2 * t.b.z + l3 * t.c.z;
				if (std::fabs(z - a_near) > kStorey) {
					continue;
				}
				if (!best || std::fabs(z - a_near) < std::fabs(*best - a_near)) {
					best = z;
				}
			}
			return best;
		}

		// The whole footprint on walkable, level floor: its centre, and rings at the radius and
		// half of it. Returns the centre's floor height, or nothing with the reason.
		[[nodiscard]] std::optional<float> FootprintOnFloor(
			const std::vector<Tri>& a_tris, float a_x, float a_y, float a_near, float a_radius, std::string& a_why)
		{
			const auto centre = FloorAt(a_tris, a_x, a_y, a_near);
			if (!centre) {
				a_why = "off the navmesh";
				return std::nullopt;
			}
			for (const float r : { (std::max)(a_radius - kNavmeshInset, a_radius * 0.6f), a_radius * 0.5f }) {
				for (std::int32_t k = 0; k < 8; ++k) {
					const float a = kPi * 2.0f * static_cast<float>(k) / 8.0f;
					const auto  z = FloorAt(a_tris, a_x + r * std::cos(a), a_y + r * std::sin(a), *centre);
					if (!z) {
						a_why = std::format("its edge leaves the floor at {:.0f}u", r);
						return std::nullopt;
					}
					if (std::fabs(*z - *centre) > kFlat) {
						a_why = std::format("not level ({:.0f}u drop at {:.0f}u)", std::fabs(*z - *centre), r);
						return std::nullopt;
					}
				}
			}
			return centre;
		}

		// A straight walk from slot 0 to the spot stays on floor, a step at a time.
		[[nodiscard]] bool Reachable(const std::vector<Tri>& a_tris, const RE::NiPoint3& a_from, float a_x, float a_y)
		{
			const float dist = std::hypot(a_x - a_from.x, a_y - a_from.y);
			const auto  steps = (std::max)(1, static_cast<int>(dist / 30.0f));
			float       last = a_from.z;
			for (int i = 1; i <= steps; ++i) {
				const float t = static_cast<float>(i) / static_cast<float>(steps);
				const auto  z = FloorAt(a_tris, a_from.x + (a_x - a_from.x) * t, a_from.y + (a_y - a_from.y) * t, last);
				if (!z || std::fabs(*z - last) > kStepUp) {
					return false;
				}
				last = *z;
			}
			return true;
		}
	}

	namespace
	{
		struct Pending
		{
			std::vector<std::uint32_t>            actors;
			std::string                           position;
			std::string                           tags;
			bool                                  ours{ false };
			std::chrono::steady_clock::time_point due{};
		};
		std::mutex           g_pendingLock;
		std::vector<Pending> g_pending;
	}

	void RunSurvey(const std::vector<std::uint32_t>& a_actors, std::string_view a_position, std::string_view a_tags,
		bool a_ours);

	void Survey(const std::vector<std::uint32_t>& a_actors, std::string_view a_position, std::string_view a_tags,
		bool a_ours)
	{
		// Measured when the actors are in place, not at the first animation.
		std::scoped_lock lock{ g_pendingLock };
		if (g_pending.size() < 16) {
			g_pending.push_back(Pending{ a_actors, std::string{ a_position }, std::string{ a_tags }, a_ours,
				std::chrono::steady_clock::now() + kSurveyDelay });
		}
	}

	void Pump()
	{
		std::vector<Pending> due;
		{
			std::scoped_lock lock{ g_pendingLock };
			const auto now = std::chrono::steady_clock::now();
			for (auto it = g_pending.begin(); it != g_pending.end();) {
				if (it->due <= now) {
					due.push_back(std::move(*it));
					it = g_pending.erase(it);
				} else {
					++it;
				}
			}
		}
		for (const auto& job : due) {
			RunSurvey(job.actors, job.position, job.tags, job.ours);
		}
	}

	void RunSurvey(const std::vector<std::uint32_t>& a_actors, std::string_view a_position, std::string_view a_tags,
		bool a_ours)
	{
		std::vector<RE::Actor*>    actors;
		std::vector<std::uint32_t> ids;
		for (const auto id : a_actors) {
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(id);
			if (actor && actor->Get3D() && actor->GetParentCell()) {
				actors.push_back(actor);
				ids.push_back(id);
			}
		}
		if (actors.empty()) {
			return;
		}
		const auto key = PairKey(ids);
		Chosen     chosen;
		{
			std::scoped_lock lock{ g_lock };
			const auto now = std::chrono::steady_clock::now();
			if (const auto it = g_surveyed.find(key); it != g_surveyed.end() && now - it->second < kResurvey) {
				return;
			}
			g_surveyed[key] = now;
			std::erase_if(g_surveyed, [&](const auto& a_entry) { return now - a_entry.second > kResurvey * 4; });
			chosen = g_chosen;
		}

		RE::NiPoint3 centre{};
		float        feet = actors.front()->GetPosition().z;
		for (auto* actor : actors) {
			const auto p = actor->GetPosition();
			centre.x += p.x;
			centre.y += p.y;
			feet = (std::min)(feet, p.z);
		}
		centre.x /= static_cast<float>(actors.size());
		centre.y /= static_cast<float>(actors.size());
		float spread = 0.0f;
		for (auto* actor : actors) {
			const auto p = actor->GetPosition();
			spread = (std::max)(spread, std::hypot(p.x - centre.x, p.y - centre.y));
		}
		const float radius = (std::max)(RadiusFor(a_position, a_tags), spread + 40.0f);

		std::vector<RE::TESObjectCELL*> cells;
		for (auto* actor : actors) {
			if (auto* cell = actor->GetParentCell(); std::ranges::find(cells, cell) == cells.end()) {
				cells.push_back(cell);
			}
		}
		const auto found = Scan(cells, centre, feet, radius, true);

		// Did AAF use the spot we chose? The scene's centre against it, a few seconds in.
		std::string ours;
		if (a_ours && chosen.pair == key) {
			ours = std::format(" | our spot for request {} was ({:.0f}, {:.0f}): the scene's centre is {:.0f}u from it",
				chosen.request, chosen.spot.x, chosen.spot.y, std::hypot(centre.x - chosen.spot.x, centre.y - chosen.spot.y));
		}
		logger::info(
			"placement: {} scene '{}' at ({:.0f}, {:.0f}, {:.0f}), footprint r {:.0f}, z +{:.0f}..+{:.0f}: {} in the way{}{} "
			"({} solid object(s) checked in {} cell(s){}){}",
			a_ours ? "our" : "a foreign", a_position, centre.x, centre.y, feet, radius, kBandLow, kBandHigh,
			found.Blocking(), found.hits.empty() ? "" : " - ", found.List(), found.looked, cells.size(),
			found.enclosing ? std::format(", {} enclosing piece(s) not counted", found.enclosing) : std::string{}, ours);
	}

	std::vector<float> ChooseSpot(RE::Actor* a_slot0, RE::Actor* a_slot1, std::string_view a_position,
		std::int32_t a_request)
	{
		if (!a_slot0 || !a_slot0->GetParentCell()) {
			return {};
		}
		// A furniture tree plays on its furniture, found by AAF near the pair.
		if (!a_position.empty()) {
			if (const auto* entry = TreeIndex::GetSingleton().Find(a_position); entry && TreeIndex::NeedsFurniture(*entry)) {
				logger::info("placement: request {}: '{}' plays on furniture - AAF places it", a_request, a_position);
				return {};
			}
		}
		const auto  from = a_slot0->GetPosition();
		const float radius = RadiusFor(a_position, "");

		std::vector<RE::TESObjectCELL*> cells{ a_slot0->GetParentCell() };
		if (a_slot1 && a_slot1->GetParentCell() && a_slot1->GetParentCell() != cells.front()) {
			cells.push_back(a_slot1->GetParentCell());
		}

		// AAF's own spot first: if it is clear, nothing moves.
		const auto here = Scan(cells, from, from.z, radius, true);
		if (here.Blocking() == 0) {
			logger::info("placement: request {}: AAF's own spot ({:.0f}, {:.0f}, {:.0f}) is clear for r {:.0f} - left as it is",
				a_request, from.x, from.y, from.z, radius);
			return {};
		}

		std::string why;
		auto        tris = Navmesh(cells, why);
		// Only the triangles the search can touch: a cell's navmesh is thousands, the search
		// a few hundred units, and this runs on the frame the scene starts.
		const float reach = kReach + radius + 60.0f;
		std::erase_if(tris, [&](const Tri& t) {
			const float minX = (std::min)({ t.a.x, t.b.x, t.c.x }), maxX = (std::max)({ t.a.x, t.b.x, t.c.x });
			const float minY = (std::min)({ t.a.y, t.b.y, t.c.y }), maxY = (std::max)({ t.a.y, t.b.y, t.c.y });
			return maxX < from.x - reach || minX > from.x + reach || maxY < from.y - reach || minY > from.y + reach;
		});
		if (tris.empty() && why.empty()) {
			why = "no navmesh near the pair";
		}
		if (tris.empty()) {
			logger::info("placement: request {}: AAF's spot has {} in the way ({}), but {} - left to AAF", a_request,
				here.Blocking(), here.List(3), why);
			return {};
		}

		// Outward in rings; the first clear, reachable spot wins. Each rejection reason is
		// counted, so a failed search says what stood in the way.
		std::map<std::string, std::uint32_t> rejected;
		std::uint32_t                        tried = 0;
		const float                          facing = a_slot0->data.angle.z * 180.0f / kPi;
		const auto remember = [&](const RE::NiPoint3& a_spot) {
			std::scoped_lock           lock{ g_lock };
			std::vector<std::uint32_t> ids{ a_slot0->GetFormID() };
			if (a_slot1) {
				ids.push_back(a_slot1->GetFormID());
			}
			g_chosen = Chosen{ PairKey(ids), a_spot, a_request };
		};
		// The owner's call when no spot is clear (2026-09-25): the least-bad one -- on the
		// floor, walkable, with the shallowest intrusion -- if it clearly beats AAF's own.
		struct LeastBad
		{
			RE::NiPoint3  spot{};
			float         worst{ 0.0f };
			float         distance{ 0.0f };
			std::uint32_t candidate{ 0 };
		};
		std::optional<LeastBad> leastBad;
		for (float d = kStep; d <= kReach; d += kStep) {
			for (std::int32_t k = 0; k < kAngles; ++k) {
				const float a = kPi * 2.0f * static_cast<float>(k) / static_cast<float>(kAngles);
				const float x = from.x + d * std::cos(a);
				const float y = from.y + d * std::sin(a);
				++tried;
				std::string reason;
				const auto  floor = FootprintOnFloor(tris, x, y, from.z, radius, reason);
				if (!floor) {
					++rejected[reason.starts_with("its edge") ? "footprint leaves the floor" : reason.starts_with("not level") ? "not level" : reason];
					continue;
				}
				const RE::NiPoint3 spot{ x, y, *floor };
				const auto         there = Scan(cells, spot, *floor, radius, false);
				if (there.Blocking() > 0) {
					++rejected["an object in the footprint"];
					// Only a spot that would beat the best so far is worth the walk test.
					if ((!leastBad || there.Worst() < leastBad->worst) && Reachable(tris, from, x, y)) {
						leastBad = LeastBad{ spot, there.Worst(), d, tried };
					}
					continue;
				}
				if (!Reachable(tris, from, x, y)) {
					++rejected["no straight walk to it"];
					continue;
				}
				logger::info(
					"placement: request {}: AAF's spot ({:.0f}, {:.0f}, {:.0f}) has {} in the way ({}) - moved {:.0f}u to "
					"({:.0f}, {:.0f}, {:.0f}), clear for r {:.0f}, level, reachable (candidate {} of {}, {} navmesh "
					"triangle(s))",
					a_request, from.x, from.y, from.z, here.Blocking(), here.List(3), d, x, y, *floor, radius, tried,
					static_cast<int>(kReach / kStep) * kAngles, tris.size());
				remember(spot);
				return { x, y, *floor, facing };
			}
		}
		std::string summary;
		for (const auto& [reason, n] : rejected) {
			summary += std::format("{}{} x{}", summary.empty() ? "" : ", ", reason, n);
		}
		if (leastBad && leastBad->worst + kWorthMoving <= here.Worst()) {
			const auto there = Scan(cells, leastBad->spot, leastBad->spot.z, radius, true);
			logger::info(
				"placement: request {}: AAF's spot ({:.0f}, {:.0f}, {:.0f}) has {} in the way ({}; worst {:.0f}u), and no "
				"clear spot within {:.0f}u ({} tried: {}) - moved {:.0f}u to the LEAST-BAD one ({:.0f}, {:.0f}, {:.0f}), "
				"worst {:.0f}u ({}), level, reachable (candidate {})",
				a_request, from.x, from.y, from.z, here.Blocking(), here.List(3), here.Worst(), kReach, tried, summary,
				leastBad->distance, leastBad->spot.x, leastBad->spot.y, leastBad->spot.z, leastBad->worst, there.List(3),
				leastBad->candidate);
			remember(leastBad->spot);
			return { leastBad->spot.x, leastBad->spot.y, leastBad->spot.z, facing };
		}
		logger::info(
			"placement: request {}: AAF's spot has {} in the way ({}; worst {:.0f}u), and no clear spot within {:.0f}u ({} "
			"tried: {}){} - left to AAF",
			a_request, here.Blocking(), here.List(3), here.Worst(), kReach, tried, summary,
			leastBad ? std::format("; the least-bad one, worst {:.0f}u, is not {:.0f}u better", leastBad->worst, kWorthMoving)
					 : std::string{});
		return {};
	}
}
