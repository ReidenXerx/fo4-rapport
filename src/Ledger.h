#pragma once

#include "NamedLock.h"

namespace RP
{
	// What has happened to each actor, kept in the save game.
	//
	// The framework records FACTS and nothing else: when someone last finished a
	// scene, who with, how many times, when a request involving them was refused.
	// It does not decide what any of that means. "Too soon", "bored of this
	// partner", "wants company" are policy, and policy belongs to the addon --
	// Rapport only guarantees the facts survive a save.
	//
	// The one value the framework carries without understanding is `need`: an
	// addon's own number, stored on its behalf so it does not have to build a
	// second co-save for one float per actor.
	//
	// It lives in the SAVE rather than in a file beside the mod for the obvious
	// reason: a player with three characters has three sets of these facts, and a
	// file next to the plugin would mix them.
	struct ActorRecord
	{
		float         lastSceneAt{ -1.0f };    // game hours; negative means never
		float         lastRefusedAt{ -1.0f };
		std::uint32_t lastPartner{ 0 };
		std::uint32_t scenes{ 0 };
		std::uint32_t refusals{ 0 };
		float         need{ 0.0f };            // an addon's value; we only keep it

		[[nodiscard]] bool EverHadAScene() const noexcept { return lastSceneAt >= 0.0f; }
	};

	class Ledger
	{
	public:
		[[nodiscard]] static Ledger& GetSingleton() noexcept;

		// Called from F4SEPlugin_Load. Without it nothing here persists, and the
		// failure is loud rather than a mod that silently forgets everything on
		// every save.
		static bool Register(const F4SE::SerializationInterface* a_intfc);

		// Game hours since the game began, from the Calendar's own GameDaysPassed.
		// Negative when the calendar is not available -- which is a real state
		// (before a save is loaded) and must not be confused with hour zero.
		[[nodiscard]] static float GameHours();

		// ---- what happened -------------------------------------------------
		void RecordScene(std::uint32_t a_first, std::uint32_t a_second);
		void RecordRefusal(std::uint32_t a_first, std::uint32_t a_second);

		void SetNeed(std::uint32_t a_formID, float a_need);

		// ---- what is known -------------------------------------------------
		[[nodiscard]] ActorRecord Get(std::uint32_t a_formID) const;

		// Game hours since this actor's last scene. Infinity when they have never
		// had one, so "longest since" sorts correctly and a caller comparing
		// against a cooldown does not need a special case.
		[[nodiscard]] float HoursSinceScene(std::uint32_t a_formID) const;

		// And since a request involving them was refused. Infinity when none ever
		// was, for the same reason: a caller comparing against a backoff needs no
		// special case for "never".
		[[nodiscard]] float HoursSinceRefusal(std::uint32_t a_formID) const;

		// ---- and the same two facts about a PAIR ----------------------------
		//
		// Separate from the per-actor records because lastPartner is only the MOST
		// RECENT partner: as soon as either of them is with somebody else, "have
		// these two ever" becomes unanswerable from the actor records alone. An
		// addon that wants couples to emerge needs the history to survive that.
		//
		// Order-independent. (A,B) and (B,A) are one record.
		[[nodiscard]] float HoursSincePair(std::uint32_t a_first, std::uint32_t a_second) const;
		[[nodiscard]] std::uint32_t PairScenes(std::uint32_t a_first, std::uint32_t a_second) const;

		// ---- the relationship store (R-1): Rapport is mechanism, consumers are policy.
		//
		// BOND, -1 (enemies) .. +1 (as close as two people get), per pair, the player a
		// pair member like anyone (R-11). Every change goes through AddBond with a
		// reason, so a later mod can ask WHY two people are close (R-10).
		//
		// AddBond moves the value toward +1 (or -1) by that fraction of the distance
		// still left. That is the diminishing return R-10 asks for, for every source at
		// once: the first scene means more than the fortieth, and the value can never
		// leave its range. The CURVE that turns bond into behaviour is the consumer's
		// (Chemistry's) - Rapport only keeps the number.
		enum class BondReason : std::uint8_t
		{
			kNone = 0,
			kScene = 1,      // a completed scene together
			kVanilla = 2,    // imported from the engine's own relationship records (R-2)
			kDialogue = 3,   // Overture
			kGift = 4,
			kAddon = 5       // any other mod, through the Papyrus API
		};
		[[nodiscard]] float Bond(std::uint32_t a_first, std::uint32_t a_second) const;
		[[nodiscard]] BondReason LastBondReason(std::uint32_t a_first, std::uint32_t a_second) const;
		float AddBond(std::uint32_t a_first, std::uint32_t a_second, float a_amount, BondReason a_reason);

		// Import the engine's relationship ONCE per pair, the first time they interact
		// (R-2, R-5). Later calls are no-ops: from then on our own arithmetic runs.
		//
		// a_blood and a_partner come from the engine's ASSOCIATION types, not from
		// HasFamilyRelationship: measured, that one is true for a married couple
		// (the Codmans) exactly as for brothers (Vadim and Yefim), R-4.
		void SeedFromVanilla(std::uint32_t a_first, std::uint32_t a_second, std::int32_t a_rank, bool a_blood,
			bool a_partner);
		[[nodiscard]] bool IsSeeded(std::uint32_t a_first, std::uint32_t a_second) const;
		// Chemistry, when it asks for a pair with a member partnered elsewhere.
		void NoteAffair(std::uint32_t a_first, std::uint32_t a_second);
		[[nodiscard]] bool IsAffair(std::uint32_t a_first, std::uint32_t a_second) const;
		// What SeedFromVanilla would store, without storing it (a consumer ranking a
		// pair that has not interacted yet, R-5).
		[[nodiscard]] static float SeedValue(std::int32_t a_rank, bool a_partner) noexcept;
		// BLOOD relatives (siblings, parent/child, grandparents, aunts/uncles, cousins).
		// A FLAG, never a refusal (owner, R-14): nothing in Rapport or Chemistry blocks
		// on it; a later attitude layer reads it as a 'bad thing' others react to.
		[[nodiscard]] bool IsIncest(std::uint32_t a_first, std::uint32_t a_second) const;
		// Spouse or courting, per the engine.
		[[nodiscard]] bool IsPartner(std::uint32_t a_first, std::uint32_t a_second) const;

		// R-6: a dead NPC's rows are waste against a hard ceiling. Called by the
		// engine's death event, never by a timer.
		void ForgetActor(std::uint32_t a_formID);

		// One sink on the engine's global death event, registered at data ready.
		static void RegisterDeathSink();

		[[nodiscard]] std::size_t Size() const;

		void Clear();

	private:
		void RecordSceneLocked(std::uint32_t a_first, std::uint32_t a_second, float& a_before, float& a_after,
			std::uint32_t& a_together);
		// Exactly what goes in the save, one per actor. Every field is four bytes
		// so the struct has no padding and its size is the record's arithmetic --
		// which is what makes the length check on load meaningful.
		struct Entry
		{
			std::uint32_t formID;
			float         lastSceneAt;
			float         lastRefusedAt;
			std::uint32_t lastPartner;
			std::uint32_t scenes;
			std::uint32_t refusals;
			float         need;
		};
		static_assert(sizeof(Entry) == 28, "the on-disk entry has grown padding");

		// One overlay standing on one actor. The set id is a fixed field rather
		// than a length-prefixed string on purpose: every entry being the same
		// size is what lets the load check the record's length against its count,
		// which is the only thing that separates a truncated save from a short one.
		struct OverlayEntry
		{
			std::uint32_t formID;
			float         expiresAt;
			char          setID[32];
		};
		static_assert(sizeof(OverlayEntry) == 40, "the on-disk overlay entry has grown padding");

		// One pair, as it goes into the save. Four-byte fields only, same reason as
		// the others: no padding means the record's length is its arithmetic, which
		// is what makes the length check on load meaningful.
		// Version 1, read-only: saves written before the relationship store.
		struct PairEntryV1
		{
			std::uint32_t first;
			std::uint32_t second;
			float         lastSceneAt;
			std::uint32_t scenes;
		};
		static_assert(sizeof(PairEntryV1) == 16, "the on-disk v1 pair entry has grown padding");

		// Version 2: the relationship store. flags: bit 0 seeded from vanilla, bits 8-15
		// the last bond reason.
		struct PairEntry
		{
			std::uint32_t first;
			std::uint32_t second;
			float         lastSceneAt;
			std::uint32_t scenes;
			float         bond;
			float         lastTouchedAt;
			std::uint32_t flags;
		};
		static_assert(sizeof(PairEntry) == 28, "the on-disk pair entry has grown padding");

		struct PairRecord
		{
			float         lastSceneAt{ -1.0f };
			std::uint32_t scenes{ 0 };
			float         bond{ 0.0f };
			// Anything that wrote this record - a scene, dialogue, a gift. The cap evicts
			// by THIS, not by lastSceneAt: a friendship built only in dialogue has no
			// scene, and would otherwise be the first thing thrown away.
			float         lastTouchedAt{ -1.0f };
			bool          seeded{ false };
			// BLOOD relatives, from the engine's association types. A FLAG, NEVER A
			// REFUSAL (owner, R-14): nothing blocks on it. It is there for the attitude
			// layer to come, where others who know treat it as a bad thing - and in the
			// wasteland a common one. Their bond is the engine's rank like anyone's.
			bool          incest{ false };
			bool          partner{ false };   // spouse or courting, per the engine
			BondReason    lastReason{ BondReason::kNone };
			// One of them was partnered to someone ELSE when these two had a scene
			// (owner: cheating is a future "bad thing"). Set, never cleared.
			bool          affair{ false };
		};

		// (lower << 32) | higher, so the two orders are one key.
		[[nodiscard]] static std::uint64_t PairKey(std::uint32_t a_first, std::uint32_t a_second) noexcept
		{
			return a_first < a_second
			         ? (static_cast<std::uint64_t>(a_first) << 32) | a_second
			         : (static_cast<std::uint64_t>(a_second) << 32) | a_first;
		}

		mutable std::unordered_map<std::uint64_t, PairRecord> _pairs;

		static void F4SEAPI OnSave(const F4SE::SerializationInterface* a_intfc);
		static void F4SEAPI OnLoad(const F4SE::SerializationInterface* a_intfc);
		static void F4SEAPI OnRevert(const F4SE::SerializationInterface* a_intfc);

		template <class Map, class AgeOf>
		void Cap(Map& a_map, std::size_t a_limit, std::string_view a_what, AgeOf a_ageOf) const;

		void LoadPairs(
			const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);

		void Save(const F4SE::SerializationInterface* a_intfc) const;
		void Load(const F4SE::SerializationInterface* a_intfc);
		// Drops records that have gone cold. const because it runs from Save, which
		// is the only place that has a reason to care how big this has got.
		void Prune() const;

		static void LoadFaces(
			const F4SE::SerializationInterface* a_intfc,
			std::uint32_t                       a_version,
			std::uint32_t                       a_length);
		static void LoadScene(
			const F4SE::SerializationInterface* a_intfc,
			std::uint32_t                       a_version,
			std::uint32_t                       a_length);
		static void LoadOverlays(
			const F4SE::SerializationInterface* a_intfc,
			std::uint32_t                       a_version,
			std::uint32_t                       a_length);

		mutable std::timed_mutex                                   _lock;
		mutable std::unordered_map<std::uint32_t, ActorRecord>     _records;
	};
}
