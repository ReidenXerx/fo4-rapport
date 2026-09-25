#pragma once

#include "NamedLock.h"
#include "Orders.h"

namespace RP
{
	// Rapport as the source of truth for the faces it holds (owner, 2026-09-24: "we
	// need grab whole power on ruling things we rule in rapport including expressions
	// bc we need to be SOT").
	//
	// The engine builds a face as clamp(max(override, animation)) (0x6689D0, the
	// merge fo4-anatomy reverse-engineered), so no AAF mfg set can close a jaw the
	// animation opens -- the owner's cowgirl: a blowjob mouth with a close-open
	// flicker on it. Anatomy's cbp.dll ("OCBPC plugin") owns the one hook after that
	// merge and REPLACES the merged value with ours for every morph we own. The
	// protocol, agreed with the anatomy session:
	//   'RFAS' Rapport -> OCBPC  { u32 version=1; u32 formID; u64 owned; float value[54]; }
	//   'RFAC' Rapport -> OCBPC  { u32 version=1; u32 formID; }   (0 = everyone)
	//   'RFAH' OCBPC -> Rapport  { u32 version; u32 features; }   at PostPostLoad
	//   'RFAK' Rapport -> OCBPC  { u32 version=1; u32 enabled; float lipClearance, lipSpeed, shaftScale,
	//          headMin, headMax, reactScale; }  enabled: 0 aim, 1 shape, 2 lip fit, 3 oral reaction,
	//          4 deep face. Its knobs in Rapport's MCM; without it, its own ini values stand.
	//   'RFAG' Rapport -> OCBPC  { u32 version=1; u32 looker; u32 target; u32 durationMs; float lidsOpen;
	//          u32 flags; }  a glance into the partner's eyes; target 0 = stop. Only with feature bit 4.
	//   'RFAD' Rapport -> OCBPC  { u32 version=1; u32 formID; u64 blend; float value[54]; }
	//          the held face at full depth, right after its RFAS; only with feature bit 3.
	//          Their side: value = lerp(held, deep, depth) for every id in blend.
	// No hello, no authority: Rapport's AAF mfg path stays the whole story, as before.
	// Their side keeps the blink as max(ours, merged) and puts the contact mouth on
	// top while something is in the mouth.
	//
	// OWN-ALL: every morph 0-49, a morph the set does not name going out as 0 -- the
	// owner's "whole power". The one exception is the mouth of an actor speaking one
	// of Rapport's lines: its lip sync keeps it for the line.
	//
	// Fed from PapyrusLink::QueueOrder, the one funnel every face and every line of
	// Rapport's passes, so nothing can put a face on without it knowing.
	class FaceAuthority
	{
	public:
		[[nodiscard]] static FaceAuthority& GetSingleton() noexcept;

		// data/F4SE/Plugins/Rapport/faces.json, written by tools/make_mfg.py from the
		// same table as the AAF sets.
		void Load();

		// The hello, from Anatomy's plugin. From then on the faces go there too.
		void OnHello(std::uint32_t a_version, std::uint32_t a_features);

		// Anatomy answered AND faces.json was read. The hello comes at PostPostLoad,
		// before game data is ready and faces.json is read, so either order works.
		[[nodiscard]] bool Available() const noexcept { return _peer.load() && _loaded.load(); }

		// Every order Rapport queues: a face put on, a face cleared, a line spoken.
		void OnOrder(const Order& a_order);

		// On the poll: a speaking window that has closed gives the mouth back to us.
		void Pump();

		// A load. Anatomy drops every authority on a load too, and Rapport clears
		// every face it held, so this only forgets.
		void Reset();

		// Anatomy's knobs, which live in Rapport's MCM ("Bodies & faces"; owner, 2026-09-25):
		// anatomy.json with the player's MCM choices laid over it, sent as 'RFAK'. Called on the
		// hello and when MCM settings change. Sent only once Anatomy has answered.
		void SendKnobs();

	private:
		using Clock = std::chrono::steady_clock;
		static constexpr std::size_t kSlots = 54;   // the engine's array; ids 0-49 are morphs

		struct Held
		{
			std::string       setID;
			Clock::time_point speakingUntil{};
			bool              speaking{ false };
			// The expression pass (2026-09-25): who wears it, read when it is put on (outside our
			// lock), and which drift sibling it shows now (-1 = the set itself).
			std::string       base;      // the set without its style: Rapport_Pleasure_2
			std::string       persona;   // R-8's, "" for the player
			char              sex{ 0 };  // 'f', 'm', 0 unknown
			int               drift{ -1 };
			Clock::time_point nextDrift{};
		};

		struct Send
		{
			std::uint32_t formID{ 0 };
			std::uint64_t owned{ 0 };
			std::array<float, kSlots> values{};
			bool clear{ false };
			// The world it was built in (_generation): a load bumps it, and a Send built for the
			// world just left is dropped at the wire instead of landing after the clear-all.
			std::uint32_t generation{ 0 };
			// The same face at full depth, for Anatomy to blend toward by its own depth
			// signal: sent as 'RFAD' right after the RFAS it belongs to.
			bool                      deep{ false };
			std::uint64_t             deepMask{ 0 };
			std::array<float, kSlots> deepValues{};
		};

		// A set's face at full depth: the morphs in the blend, and where they go.
		struct Deep
		{
			std::uint64_t             mask{ 0 };
			std::array<float, kSlots> values{};
		};

		// The values of a set, found under our lock. False when faces.json never
		// named it, which is said once.
		[[nodiscard]] bool ValuesOf(const std::string& a_setID, std::array<float, kSlots>& a_out);
		// The held face as it should show NOW: its set, with its drift sibling laid over.
		[[nodiscard]] bool ValuesFor(const Held& a_held, std::array<float, kSlots>& a_out);
		[[nodiscard]] std::uint64_t MaskFor(const Held& a_held) const noexcept;
		// Puts the held face's deep face on a_send, when Anatomy blends: the most specific of
		// persona|sex, persona|*, *|sex for its base set, else the generic one. Under our lock.
		void DeepOf(const Held& a_held, Send& a_send) const;

		static void Dispatch(const Send& a_send);

		// GLANCES (owner, 2026-09-25: "glance in the partner eyes. for ex during blowjob time to time
		// glances on 1-2 seconds maybe in another poses"). Rapport decides who, when and how long, by
		// persona; Anatomy's plugin turns the eyes and opens the lids ('RFAG', hello bit 4). A held
		// actor's partner is the held actor they are MUTUALLY nearest to: every actor in any AAF
		// scene holds a Rapport face (R-22), and two partners are each other's nearest even when
		// another scene plays close by (a bare nearest-neighbour could look into the next scene --
		// review 2026-09-25). Called from Pump WITHOUT our lock: it snapshots under the lock and
		// asks Barks for personas outside it, so no lock of Barks' is ever taken under ours.
		struct Glance
		{
			std::uint32_t looker{ 0 };
			std::uint32_t target{ 0 };
			std::uint32_t durationMs{ 0 };
			float         lidsOpen{ 0.0f };
			std::uint32_t flags{ 0 };   // bit 0: an eye ROLL (target = the looker; hello bit 9)
			// The face for that moment ('RFAX', sent just before the RFAG; hello bit 7).
			bool                      face{ false };
			std::uint64_t             faceMask{ 0 };
			std::array<float, kSlots> faceValues{};
			std::uint32_t             generation{ 0 };   // as Send::generation
		};
		[[nodiscard]] std::vector<Glance> DueGlances(Clock::time_point a_now);
		mutable std::timed_mutex          _glanceLock;   // _nextGlance, _glanceBase, _nextRoll, _eyesBusy,
		                                                  // _longLook, _dice
		static void                       Dispatch(const Glance& a_glance);
		static std::mutex&                WireLock();

		mutable std::timed_mutex _lock;
		std::atomic_bool         _peer{ false };
		std::atomic<std::uint32_t> _generation{ 0 };   // bumped by Reset, under _lock
		std::atomic_bool         _loaded{ false };
		std::uint32_t            _peerVersion{ 0 };
		// The hello's feature bits. Bit 1: while the engine plays a line on a held face,
		// the mouth ids are the line's lip sync on their side, for the line's real length
		// -- so Rapport's own 9 s mouth window is not needed (anatomy f39831b, 2026-09-24).
		// Bit 3: it blends a held face toward its deep face ('RFAD') by the depth of
		// what is in the mouth -- the brows drawing together as it goes deeper.
		std::atomic<std::uint32_t> _peerFeatures{ 0 };
		std::uint32_t            _morphs{ 50 };
		std::uint64_t            _mouth{ 0 };
		std::unordered_map<std::string, std::array<float, kSlots>> _sets;
		std::unordered_map<std::string, Deep>                      _deep;
		// The expression pass, from faces.json (tools/make_mfg.py DEEP_BY, DRIFT, GLANCE).
		std::unordered_map<std::string, std::unordered_map<std::string, Deep>> _deepBy;        // base -> who
		std::unordered_map<std::string, std::vector<Deep>>                     _drift;         // base -> siblings
		std::unordered_map<std::string, std::unordered_map<std::string, Deep>> _glanceFaces;   // oral|face -> persona
		std::mt19937                                                            _driftDice{ std::random_device{}() };
		std::unordered_set<std::string>                            _unknown;   // said once each
		std::unordered_map<std::uint32_t, Held>                    _held;
		std::unordered_map<std::uint32_t, Clock::time_point>       _nextGlance;   // per held actor
		std::unordered_map<std::uint32_t, std::string>             _glanceBase;   // the base each was last seen in
		std::unordered_map<std::uint32_t, Clock::time_point>       _nextRoll;     // eye rolls, per held actor
		std::unordered_map<std::uint32_t, Clock::time_point>       _eyesBusy;     // a glance or roll until then
		std::unordered_set<std::uint32_t>                          _longLook;     // the peak look, after its roll
		std::mt19937                                               _dice{ std::random_device{}() };
	};
}
