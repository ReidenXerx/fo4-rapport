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
		// Where the cum comes from. Two mods do this job and they share nothing:
		// CumOverlays paints a flat texture through LooksMenu, Commonwealth
		// Moisturizer equips a BodySlide-conformed mesh and swaps headparts. The
		// mesh looks far better and is the only one of the two that has a face at
		// all, so it wins when both are present -- but a framework supports what
		// is installed rather than requiring one particular mod.
		enum class Backend
		{
			kNone,
			kOverlay,
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

		// Tags seen during a scene, joined by commas, as AAF reported them. Called
		// once per animation; a scene plays several, so they accumulate.
		void NoteTags(std::string_view a_tags);

		// The scene ended. Works out what it left on these two and queues the
		// orders. Clears the accumulated tags either way.
		void OnSceneEnded(std::uint32_t a_first, std::uint32_t a_second);

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
		bool  _requireClimax{ false };

		// tag, lowercased -> the sets it calls for. A vector rather than a map so
		// the file's order survives into what gets applied.
		std::vector<std::pair<std::string, std::vector<std::string>>> _rules;

		// Set id -> the Moisturizer regions it corresponds to, any of "FOR". One
		// mapping drives both backends: the tags pick sets, and the sets carry
		// their own translation rather than the file listing every tag twice.
		std::vector<std::pair<std::string, std::string>> _regions;

		Backend _backend{ Backend::kNone };

		std::string        _sceneTags;   // accumulated across one scene
		std::vector<Mark>  _marks;
	};
}
