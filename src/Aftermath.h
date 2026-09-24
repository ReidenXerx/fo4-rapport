#pragma once

#include "NamedLock.h"
#include "Orders.h"

namespace RP
{
	// What a scene leaves behind, and the removing of it.
	//
	// AAF can already apply an overlay set on a timer. What it cannot do is
	// outlive the session: its duration is an in-session countdown, so a save, a
	// reload or the game closing mid-count strands the overlay on the actor
	// forever. Rapport's sets therefore carry NO duration -- AAF applies and never
	// removes -- and this owns the other half: when it goes, in game hours, in the
	// save.
	//
	// The decision of WHAT to apply is a lookup, not a policy: AAF's animation
	// tags say what an animation was, and aftermath.json maps those tags to sets.
	// An addon that wants different rules replaces the file.
	class Aftermath
	{
	public:
		// Where the cum comes from. ONE backend now: Commonwealth Moisturizer,
		// which equips a BodySlide-conformed mesh and swaps headparts.
		//
		// CumOverlays was the other, painting a flat LooksMenu texture, and it is
		// gone. Supporting two backends was the right instinct -- a framework
		// supports what is installed -- and it did not survive contact: the mesh
		// is the only one of the two that does a FACE at all, it is what every
		// session here has actually run, and the overlay path was never once
		// exercised end to end because "auto" prefers the mesh whenever both are
		// present. A second path that cannot be tested is not support, it is a
		// claim.
		//
		// kOverlay is deliberately NOT reused as a value. Old saves carry marks
		// written by it, and the mark handling still tells them apart by setID
		// prefix so they come off correctly.
		enum class Backend
		{
			kNone,
			kMoisturizer
		};

		[[nodiscard]] Backend Which() const noexcept { return _backend; }
		[[nodiscard]] static std::string_view Name(Backend a_backend) noexcept;
		// One overlay set standing on one actor until a given game hour.
		struct Mark
		{
			std::uint32_t formID{ 0 };
			float         expiresAt{ 0.0f };  // game hours
			std::string   setID;

			// Set once we have asked AAF for it in THIS session. Not saved: the
			// plugin starts fresh every launch and LooksMenu may or may not have
			// kept the overlay, so every session asks again. Asking twice is free
			// -- AAF will not apply the same overlay to the same actor twice.
			bool asked{ false };
		};

		[[nodiscard]] static Aftermath& GetSingleton() noexcept;

		void Load();
		[[nodiscard]] bool Enabled() const noexcept { return _enabled; }

		// How many times the mesh backend is asked to apply, in one place. Its
		// picker skips slots that are already used, so this stacks distinct decals
		// rather than re-rolling one.
		[[nodiscard]] std::int32_t Layers() const noexcept { return _layers; }

		// Tags seen during a scene, joined by commas, as AAF reported them. Called
		// once per animation; a scene plays several, so they accumulate.
		void NoteTags(std::string_view a_tags);

		// An actor's sex, reported by the bridge when it starts a request, because
		// only Papyrus holds a real Actor to ask. 0 male, 1 female, anything else
		// unknown.
		void NoteSex(std::uint32_t a_formID, std::int32_t a_sex);

		// 0 male, 1 female, -1 not reported. Read by the tree chooser, because an
		// animation carries who it is FOR and picking a gay position for a mixed
		// pair is the kind of wrong that AAF simply refuses and a player simply
		// sees.
		[[nodiscard]] std::int32_t SexOf(std::uint32_t a_formID) const;

		// AAF's composition tag for a pair -- "f_m", "m_m", "f_f" -- or empty when
		// either sex is unknown. Females come first by AAF's own convention; there
		// is no "m_f" tag anywhere in the installed packs.
		[[nodiscard]] std::string CompositionOf(std::uint32_t a_first, std::uint32_t a_second) const;

		// The order AAF actually placed them in, from OnAnimationStart. Slot 0 is
		// the receiving role in 559 of 562 two-actor animations that name both
		// genders, which is the only thing that separates a same-sex pair.
		void NoteSlots(std::uint32_t a_slot0, std::uint32_t a_slot1);

		// The scene ended. Works out what it left on these two and queues the
		// orders. Clears the accumulated tags either way.
		void OnSceneEnded(std::uint32_t a_first, std::uint32_t a_second);

		// Does this animation's tag list name an act that leaves something behind?
		[[nodiscard]] bool NamesAnAftermath(std::string_view a_tags) const;

		// A scene Rapport did not start ended (R-22): the AAF menu's, another mod's.
		// Its tags travel here whole, because the accumulator above belongs to our
		// own scene and the two can run at once. a_actors are in the order AAF's actor
		// array lists them (documented as the slot order on OnAnimationStart only; not
		// yet measured on the other events), with their sexes beside them -- the plugin
		// opened the array itself, so the sexes are certain here. a_dressable, beside
		// them too: false for a race Rapport does not dress. Who the act landed on is
		// decided with everybody; only the dressable are given anything.
		void OnForeignSceneEnded(const std::vector<std::uint32_t>& a_actors, const std::vector<std::int32_t>& a_sexes,
			const std::vector<bool>& a_dressable, std::string_view a_allTags, std::string_view a_lastActTags,
			std::string_view a_where);

		// Queues a removal for everything whose hour has passed, and a (re-)apply
		// for anything standing on an actor who is here and has not been asked for
		// yet this session. Called on the tick, with the ids the scan just saw.
		//
		// Presence matters because the plugin has no memory across launches: every
		// session has to ask again, and asking about someone three cells away is a
		// call into AAF that cannot be seen to have worked or failed.
		void Tick(const std::vector<std::uint32_t>& a_here);

		// An apply the bridge could not carry out -- the actor turned out not to
		// be loaded after all. The mark is put back into "not asked for yet" so
		// the next tick offers it again. Without this an order is consumed
		// whether or not anything happened, and the mark sits there believing it
		// has been dealt with.
		void Defer(std::uint32_t a_formID);

		// ---- the save --------------------------------------------------------
		[[nodiscard]] std::vector<Mark> Marks() const;
		void Restore(std::vector<Mark> a_marks);
		void Clear();

		// Takes everything off everyone, now. The panic switch and the uninstall
		// path: an overlay Rapport applied must never be something only Rapport
		// can remove.
		void RemoveEverything(std::string_view a_why);

		[[nodiscard]] std::size_t Size() const;

		[[nodiscard]] static std::filesystem::path ConfigPath();

	private:
		// Everything the tags matched, deduplicated and in the order the file
		// lists them, so the same scene always produces the same sets.
		[[nodiscard]] std::vector<std::string> SetsFor(std::string_view a_tags) const;

		// Which of Moisturizer's three places the chosen sets correspond to, as
		// some subset of "FOR". Empty means the sets say nothing it understands.
		[[nodiscard]] std::string RegionsFor(const std::vector<std::string>& a_sets) const;

		void ChooseBackend(const std::string& a_wanted);

		// Says out loud whether the worn meshes can follow an actor's body. They
		// can only do so from a .tri that a BodySlide build produces, and a fresh
		// install has none -- which looks exactly like the mod being broken.
		void CheckMoisturizerMorphs();

		void Apply(std::uint32_t a_formID, const std::string& a_setID, float a_expiresAt);

		mutable std::timed_mutex _lock;

		bool  _enabled{ true };
		float _hours{ 12.0f };
		std::int32_t _layers{ 3 };
		bool  _requireClimax{ false };

		// tag, lowercased -> the sets it calls for. A vector rather than a map so
		// the file's order survives into what gets applied.
		std::vector<std::pair<std::string, std::vector<std::string>>> _rules;

		// Set id -> the Moisturizer regions it corresponds to, any of "FOR". One
		// mapping drives both backends: the tags pick sets, and the sets carry
		// their own translation rather than the file listing every tag twice.
		std::vector<std::pair<std::string, std::string>> _regions;

		Backend _backend{ Backend::kNone };

		// Which of the two a scene's cum belongs to, or 0 when nothing here can
		// tell. Nothing is applied in that case: the wrong person is worse than
		// nobody, because on a clothed NPC the face is the only region that shows.
		// Who a scene's cum belongs to: one of them, both, or nobody.
		//
		// A mixed pair resolves to one -- the tag names the receiving part and sex
		// says who owns it. A same-sex pair resolves to BOTH, deliberately: nothing
		// available can separate them, and applying to neither leaves two people
		// wrong where applying to both leaves one. Empty means the tags named no
		// act at all, which is a kiss and really should leave nothing.
		[[nodiscard]] std::vector<std::uint32_t> ReceiversOf(
			std::string_view a_tags, std::uint32_t a_first, std::uint32_t a_second) const;

		// The same question for one actor or three and more (R-22, a reading for the
		// owner to overrule): alone, anything the tags leave lands on them; in a
		// group, the tags' receiving part decides -- the women if the act names a
		// part, and everybody when it names one anybody has and no woman is there.
		[[nodiscard]] std::vector<std::uint32_t> GroupReceiversOf(
			std::string_view a_tags, const std::vector<std::uint32_t>& a_actors) const;

		// Everything heard this scene, for the log: it is what separates "no
		// animation ever told us anything" from "nothing it told us was an act".
		std::string        _sceneTags;

		// The tags of the last animation that named an ACT, and the only ones that
		// decide anything. A scene finishes where it finishes; it should not leave
		// a mark everywhere it passed through on the way.
		std::string        _lastActTags;
		std::uint32_t      _slot0{ 0 };
		std::uint32_t      _slot1{ 0 };
		std::unordered_map<std::uint32_t, std::int32_t> _sex;
		std::vector<Mark>  _marks;
	};
}
