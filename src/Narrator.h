#pragma once

#include "Candidates.h"
#include "NamedLock.h"

namespace RP
{
	// The Narrator (roadmap item 11, owner poll 2026-09-21): a line on the HUD saying
	// who is about to have a scene and WHY, then a second line with the numbers that
	// decided it. It sits in Rapport because every mod built on Rapport - Chemistry,
	// Overture, Player Proposals - starts its scenes through here, so one module
	// narrates all of them.
	//
	// Four moments, each its own MCM checkbox: a scene starts (on by default), why a
	// likely pair did NOT happen, a relationship crossing a threshold, and a
	// bystander reacting. Everything loaded is covered, not only what the player
	// could see - the owner's choice.
	//
	// It only WRITES TEXT. The HUD notification is a kNarrate order the bridge turns
	// into Debug.Notification (rule 1: the plugin never calls into the VM), and the
	// last entries are kept for the history the MCM button shows.
	class Narrator
	{
	public:
		[[nodiscard]] static Narrator& GetSingleton() noexcept;

		// narrator.json, with the player's MCM [Narrator] section laid over it.
		void Load();

		// An addon's own share of a pair's score, reported just before it asks for
		// the scene ("bond", +0.45). Printed after Rapport's own parts. Kept for five
		// minutes, and used by the next request for the same two.
		void AddBonus(std::uint32_t a_first, std::uint32_t a_second, std::string_view a_label, float a_value);

		// A request was accepted: keep the offer it was decided from.
		void OnRequestAccepted(std::uint32_t a_first, std::uint32_t a_second);

		// The scene STARTED, whoever asked for it. Composes and shows the line.
		void OnSceneRequested(std::uint32_t a_first, std::uint32_t a_second, std::string_view a_scenario);

		// An addon passed on a likely pair and says why (a clause, no names:
		// "too many people are watching"). Rate-limited here, per pair and overall.
		void OnNearMiss(std::uint32_t a_first, std::uint32_t a_second, std::string_view a_why, float a_score, float a_bar);

		// The ledger, after it changed a bond. Speaks only on a threshold crossed or a
		// first scene together.
		// a_fromScene: RecordScene, the only source that may say "a first time".
		void OnBondChanged(std::uint32_t a_first, std::uint32_t a_second, float a_before, float a_after,
			std::uint32_t a_scenes, bool a_fromScene);

		// A bystander rolled a comment on the scene in progress.
		void OnBystander(std::uint32_t a_watcher, bool a_heardOnly);

		// An addon's OWN moment, in its own words (owner, 2026-09-23: Overture needs a
		// narrator "as we have for chemistry" -- to let players know, without killing
		// the mood, what is happening and how when they act). "{first}" and "{second}"
		// in the text become the two names. The numbers line shows only with the
		// numbers switch on. Its own switch: "addon moments".
		void OnAddonLine(std::uint32_t a_first, std::uint32_t a_second, std::string_view a_headline,
			std::string_view a_numbers);

		// Newest last, one entry per line - what the MCM button shows.
		[[nodiscard]] std::string History() const;

	private:
		struct Bonus
		{
			std::string label;
			float       value{ 0.0f };
		};
		struct Pending
		{
			std::vector<Bonus>                    bonuses;
			// The published offer AS IT WAS when the scene was requested. The line is
			// spoken at the start, after AAF's walk, and by then Rapport has usually
			// republished - a later Find() lost the crowd, the time and the parts.
			std::optional<Candidates::Offer>      offer;
			std::chrono::steady_clock::time_point at{};
		};

		// Queues the HUD line(s) and records the entry. Caller does NOT hold _lock.
		void Emit(const std::string& a_headline, const std::string& a_numbers);

		mutable std::timed_mutex _lock;
		bool                     _enabled{ true };
		bool                     _sceneStarts{ true };
		bool                     _nearMisses{ false };
		bool                     _relationshipTurns{ false };
		bool                     _bystanders{ false };
		bool                     _addonLines{ true };
		bool                     _numbers{ true };
		float                    _nearMissCooldown{ 300.0f };   // real seconds, across all pairs
		float                    _pairMissCooldown{ 1800.0f };  // real seconds, the same two again
		std::size_t              _historySize{ 12 };

		std::unordered_map<std::uint64_t, Pending>                               _pending;
		std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> _missedAt;
		std::optional<std::chrono::steady_clock::time_point>                     _lastMiss;
		std::deque<std::string>                                                   _history;
		std::pair<std::uint32_t, std::uint32_t>                                   _lastScene{ 0, 0 };
	};
}
