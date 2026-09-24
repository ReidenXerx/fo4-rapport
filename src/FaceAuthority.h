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

	private:
		using Clock = std::chrono::steady_clock;
		static constexpr std::size_t kSlots = 54;   // the engine's array; ids 0-49 are morphs

		struct Held
		{
			std::string       setID;
			Clock::time_point speakingUntil{};
			bool              speaking{ false };
		};

		struct Send
		{
			std::uint32_t formID{ 0 };
			std::uint64_t owned{ 0 };
			std::array<float, kSlots> values{};
			bool clear{ false };
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
		[[nodiscard]] std::uint64_t MaskFor(const Held& a_held) const noexcept;
		// Puts the set's deep face on a_send, when it has one and Anatomy blends. Under our lock.
		void DeepOf(const std::string& a_setID, Send& a_send) const;

		static void Dispatch(const Send& a_send);

		mutable std::timed_mutex _lock;
		std::atomic_bool         _peer{ false };
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
		std::unordered_set<std::string>                            _unknown;   // said once each
		std::unordered_map<std::uint32_t, Held>                    _held;
	};
}
