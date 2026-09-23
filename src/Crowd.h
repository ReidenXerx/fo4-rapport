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
		void Publish(const std::vector<RE::NiPoint3>& a_positions, const std::vector<std::uint32_t>& a_ids,
			float a_radius)
		{
			NamedLock lock{ _lock, "crowd" };
			_positions = a_positions;
			_ids = a_ids;
			_radius = a_radius;
			_published = true;
		}

		// A load: the world these positions were measured in is gone. Back to
		// "never published", so Near says -1 (unknown) until the first scan of the
		// new world instead of counting people in the cell the player just left.
		void Reset()
		{
			NamedLock lock{ _lock, "crowd" };
			_positions.clear();
			_ids.clear();
			_published = false;
		}

		// Any thread. -1 when no scan has published yet, which is NOT the same as
		// "nobody is watching" and must not be rounded to zero by the caller.
		//
		// `a_ignore` is the actor being asked about, who is in the list themselves.
		// Left out BY ID: a position match against a snapshot a scan old let anyone
		// who had moved since count as their own audience.
		[[nodiscard]] std::int32_t Near(const RE::NiPoint3& a_at, std::uint32_t a_ignore) const
		{
			NamedLock lock{ _lock, "crowd" };
			if (!_published) {
				return -1;
			}
			const auto radiusSq = _radius * _radius;
			std::int32_t seen = 0;
			for (std::size_t i = 0; i < _positions.size(); ++i) {
				if (i < _ids.size() && _ids[i] == a_ignore) {
					continue;
				}
				const auto& p = _positions[i];
				const auto  dx = p.x - a_at.x;
				const auto  dy = p.y - a_at.y;
				const auto  dz = p.z - a_at.z;
				if ((dx * dx + dy * dy + dz * dz) <= radiusSq) {
					++seen;
				}
			}
			return seen;
		}

	private:
		Crowd() = default;

		mutable std::timed_mutex  _lock;
		std::vector<RE::NiPoint3> _positions;
		std::vector<std::uint32_t> _ids;
		float                     _radius{ 0.0f };
		bool                      _published{ false };
	};
}
