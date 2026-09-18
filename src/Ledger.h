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

		[[nodiscard]] std::size_t Size() const;

		void Clear();

	private:
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

		static void F4SEAPI OnSave(const F4SE::SerializationInterface* a_intfc);
		static void F4SEAPI OnLoad(const F4SE::SerializationInterface* a_intfc);
		static void F4SEAPI OnRevert(const F4SE::SerializationInterface* a_intfc);

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
