#pragma once

#include "NamedLock.h"

namespace RP
{
	// "How many people could see this spot" — published by the scan, readable
	// from anywhere.
	//
	// Rapport already computes this for every pairing score, but the working copy
	// lives in ActorScan, which is a member of Scheduler and MAIN THREAD ONLY.
	// A Papyrus native runs on the VM thread, so a native reading that vector
	// directly is a data race — the same class of mistake as the plugin calling
	// into the VM, just pointing the other way.
	//
	// So the scan publishes a snapshot here under a lock, and anyone else reads
	// it under the same lock. The numbers a caller gets are therefore the numbers
	// RAPPORT ITSELF ACTED ON a moment ago, rather than a second opinion computed
	// from a different population that would drift from the pairing score.
	//
	// The population is ActorScan's: loaded, alive, PEOPLE only — never brahmin,
	// dogs, turrets or robots, and never children, who the mod ignores entirely
	// as witnesses too.
	class Crowd
	{
	public:
		static Crowd& GetSingleton()
		{
			static Crowd instance;
			return instance;
		}

		// Main thread, once per scan pass.
		void Publish(const std::vector<RE::NiPoint3>& a_positions, float a_radius)
		{
			NamedLock lock{ _lock, "crowd" };
			_positions = a_positions;
			_radius = a_radius;
			_published = true;
		}

		// Any thread. -1 when no scan has published yet, which is NOT the same as
		// "nobody is watching" and must not be rounded to zero by the caller.
		//
		// `a_ignore` are points to leave out — the actor being asked about, who is
		// in the list themselves, and the player, who is the one doing the asking.
		[[nodiscard]] std::int32_t Near(const RE::NiPoint3& a_at,
			std::span<const RE::NiPoint3> a_ignore) const
		{
			NamedLock lock{ _lock, "crowd" };
			if (!_published) {
				return -1;
			}
			const auto radiusSq = _radius * _radius;
			std::int32_t seen = 0;
			for (const auto& p : _positions) {
				const auto dx = p.x - a_at.x;
				const auto dy = p.y - a_at.y;
				const auto dz = p.z - a_at.z;
				if ((dx * dx + dy * dy + dz * dz) > radiusSq) {
					continue;
				}
				const bool skip = std::ranges::any_of(a_ignore, [&](const RE::NiPoint3& q) {
					const auto ex = p.x - q.x;
					const auto ey = p.y - q.y;
					const auto ez = p.z - q.z;
					return (ex * ex + ey * ey + ez * ez) < 1.0f;
				});
				if (!skip) {
					++seen;
				}
			}
			return seen;
		}

	private:
		Crowd() = default;

		mutable std::timed_mutex  _lock;
		std::vector<RE::NiPoint3> _positions;
		float                     _radius{ 0.0f };
		bool                      _published{ false };
	};
}
