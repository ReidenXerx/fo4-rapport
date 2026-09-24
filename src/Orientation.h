#pragma once

namespace RP
{
	// R-27, owner poll 2026-09-25: every NPC has a sexual orientation.
	//   - straight 70%, bi 20%, gay 10% among ordinary NPCs;
	//   - a HARD line: a mismatched pair is never a pair, whatever the bond;
	//   - romanceable companions are PLAYERSEXUAL: open to the player whatever the player's
	//     sex, with a real orientation toward everyone else.
	//
	// DERIVED from the form id like a persona (R-7): the same NPC is the same forever, on
	// every machine, at zero save cost. personas.json pins a named character, on the same
	// entries as persona pins: "orientation": "straight" | "bi" | "gay", "playersexual": true.
	//
	// Gameplay, not mechanics (R-22's principle): this decides who Rapport and its addons
	// PAIR. A scene started from the AAF menu is never refused over it.
	class Orientation
	{
	public:
		enum class Kind : std::uint8_t
		{
			kStraight,
			kBi,
			kGay
		};

		[[nodiscard]] static Orientation& GetSingleton() noexcept;

		// The pins in personas.json. At data ready; again when MCM settings change is not
		// needed -- the file is hand-kept, like the persona pins.
		void Load();

		[[nodiscard]] Kind Of(std::uint32_t a_formID) const;
		[[nodiscard]] static std::string_view Name(Kind a_kind) noexcept;

		// Would a_who agree to sex with a_with, by orientation alone?
		//   - the player: yes, always -- the player has no orientation (R-11), the player's
		//     choices are the player's own;
		//   - a playersexual pin, when a_with is the player: yes;
		//   - a sex nothing can tell (no NPC record): yes -- this rule refuses only on what
		//     it knows, never on a guess.
		[[nodiscard]] bool Attracted(RE::Actor* a_who, RE::Actor* a_with) const;

		// Both ways. A pair is a pair only when each would.
		[[nodiscard]] bool Mutual(RE::Actor* a_first, RE::Actor* a_second) const
		{
			return Attracted(a_first, a_second) && Attracted(a_second, a_first);
		}

		// "Piper is straight and Ivy is a woman" -- for the log and the refusal text.
		[[nodiscard]] std::string WhyNot(RE::Actor* a_first, RE::Actor* a_second) const;

	private:
		[[nodiscard]] std::optional<Kind> PinOf(std::uint32_t a_formID) const;
		[[nodiscard]] bool                Playersexual(std::uint32_t a_formID) const;

		mutable std::mutex                          _lock;
		std::unordered_map<std::uint32_t, Kind>     _pins;
		std::unordered_set<std::uint32_t>           _playersexual;
	};
}
