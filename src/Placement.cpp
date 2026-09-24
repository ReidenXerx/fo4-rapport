#include "Placement.h"

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

		std::mutex                                                            g_lock;
		std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> g_surveyed;

		[[nodiscard]] std::string Lower(std::string_view a_text)
		{
			std::string out{ a_text };
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		// How far the animation spreads, by its kind. ASSUMED numbers, the thing stage 1
		// exists to calibrate: lying and all-fours positions reach far, standing ones little.
		[[nodiscard]] float RadiusFor(std::string_view a_position, std::string_view a_tags)
		{
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

		struct Hit
		{
			float       depth{ 0.0f };
			std::string what;
		};
	}

	void Survey(const std::vector<std::uint32_t>& a_actors, std::string_view a_position, std::string_view a_tags,
		bool a_ours)
	{
		// The actors, loaded and here.
		std::vector<RE::Actor*> actors;
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
		std::ranges::sort(ids);
		std::uint64_t key = 1469598103934665603ull;
		for (const auto id : ids) {
			key = (key ^ id) * 1099511628211ull;
		}
		{
			std::scoped_lock lock{ g_lock };
			const auto now = std::chrono::steady_clock::now();
			if (const auto it = g_surveyed.find(key); it != g_surveyed.end() && now - it->second < kResurvey) {
				return;
			}
			g_surveyed[key] = now;
			std::erase_if(g_surveyed, [&](const auto& a_entry) { return now - a_entry.second > kResurvey * 4; });
		}

		// The footprint: centred between them, standing on the lowest feet, wide enough for
		// the animation's kind and for how far apart AAF has put them.
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
		const float low = feet + kBandLow;
		const float high = feet + kBandHigh;

		std::vector<RE::TESObjectCELL*> cells;
		for (auto* actor : actors) {
			if (auto* cell = actor->GetParentCell(); std::ranges::find(cells, cell) == cells.end()) {
				cells.push_back(cell);
			}
		}

		std::vector<Hit> hits;
		std::uint32_t    enclosing = 0;
		std::uint32_t    looked = 0;
		for (auto* cell : cells) {
			RE::BSAutoLock<RE::BSSpinLock> lock{ cell->spinLock };
			for (const auto& held : cell->references) {
				auto* ref = held.get();
				if (!ref || ref->Is(RE::ENUM_FORM_ID::kACHR) || ref->IsDisabled() || ref->IsDeleted() || !ref->Get3D()) {
					continue;
				}
				auto* base = ref->GetObjectReference();
				if (!base || !Solid(base->GetFormType())) {
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
				++looked;
				const auto pos = ref->GetPosition();
				// Vertical: the box must reach into the band.
				if (pos.z + maxZ < low || pos.z + minZ > high) {
					continue;
				}
				// Horizontal, exact for a Z rotation: the centre into the box's own frame.
				const float dx = centre.x - pos.x;
				const float dy = centre.y - pos.y;
				const float c = std::cos(-ref->data.angle.z);
				const float s = std::sin(-ref->data.angle.z);
				const float lx = dx * c - dy * s;
				const float ly = dx * s + dy * c;
				const float nx = std::clamp(lx, minX, maxX);
				const float ny = std::clamp(ly, minY, maxY);
				const float gap = std::hypot(lx - nx, ly - ny);
				if (gap >= radius) {
					continue;
				}
				const bool inside = gap == 0.0f;
				if (inside && (maxX - minX) > radius * kEnclosingFactor && (maxY - minY) > radius * kEnclosingFactor) {
					++enclosing;
					continue;
				}
				const auto* file = base->GetFile(0);
				hits.push_back(Hit{ radius - gap,
					std::format("{} {:08X} ({}) ref {:08X}, {:.0f}u in{}", RE::TESForm::GetFormTypeString(base->GetFormType()),
						base->GetFormID(), file ? file->GetFilename() : "?", ref->GetFormID(), radius - gap,
						inside ? ", AT the spot" : "") });
			}
		}

		std::ranges::sort(hits, std::greater{}, &Hit::depth);
		std::string list;
		for (std::size_t i = 0; i < hits.size() && i < 8; ++i) {
			list += (i ? "; " : "") + hits[i].what;
		}
		logger::info(
			"placement: {} scene '{}' at ({:.0f}, {:.0f}, {:.0f}), footprint r {:.0f}, z +{:.0f}..+{:.0f}: {} in the way{}{} "
			"({} solid object(s) checked in {} cell(s){})",
			a_ours ? "our" : "a foreign", a_position, centre.x, centre.y, feet, radius, kBandLow, kBandHigh, hits.size(),
			list.empty() ? "" : " - ", list, looked, cells.size(),
			enclosing ? std::format(", {} enclosing piece(s) not counted", enclosing) : std::string{});
	}
}
