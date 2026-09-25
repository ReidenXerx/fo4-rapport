#pragma once

#include "NamedLock.h"

namespace RP
{
	// Which voice an actor speaks RAPPORT'S lines in (V-25). A framework service:
	// barks are its first client, and observers, Overture and Chemistry ask the
	// same question the same way instead of each re-deriving it.
	//
	// Our lines are rendered for a fixed set of generic voice types. An actor
	// with one of those speaks in their own voice. An actor with anything else --
	// a companion, a named NPC, a voice another mod added -- would be silent with
	// a subtitle (V-8), so instead they BORROW the closest voice we rendered, for
	// the one line, and have their own back the moment it is said:
	//
	//     SetOverrideVoiceType(borrowed) -> Say -> SetOverrideVoiceType(None)
	//
	// on ONE Papyrus stack. The engine resolves the audio at Say time, so the
	// borrowed voice exists for a single call and no save can ever catch an actor
	// wearing it. Proven in game on Geneva, including that her own voice returns.
	//
	// "Closest" is MEASURED, not guessed: scripts/voice-similarity.py compares
	// speaker-embedding fingerprints of the game's own recordings, same sex only,
	// and leaves a voice silent when nothing we have is close enough. The result
	// is data -- voices.json -- and the owner can veto any pair there.
	class Voices
	{
	public:
		[[nodiscard]] static Voices& GetSingleton() noexcept;

		// At kGameDataReady: the voice types are forms, and their runtime ids
		// depend on this player's load order.
		void Load();

		// The runtime form id of the voice type this speaker should BORROW for a
		// Rapport line, or 0 to speak in their own (or to stay silent, when the
		// map says nothing we have is close enough).
		[[nodiscard]] std::uint32_t BorrowFor(std::uint32_t a_speaker) const;

		// Whether this actor would be HEARD saying a Rapport line: their own voice
		// is rendered, or they have one to borrow. A caller choosing WHO speaks
		// (observers pick among bystanders) asks this first, so it never spends a
		// line on someone who would only produce a subtitle.
		[[nodiscard]] bool CanSpeak(std::uint32_t a_speaker) const;

		// THE one path a Rapport line takes to the game. Queues a kSayTopic order:
		// a_topic is the Topic's FILE-RELATIVE id in Rapport.esp, a_target who it is
		// said to (0 for no one), and the borrowed voice is decided here, never by
		// the caller. Barks, observers, and every later speaker come through it.
		void Speak(std::uint32_t a_speaker, std::uint32_t a_target, std::uint32_t a_topic) const;

		// DIALOGUE (DialogueVoice.h; owner, 2026-09-25): an addon's conversation lines, which the
		// engine voices by the speaker's own voice type. voices.json "dialogue" lists the plugins it
		// applies to and, per voice type, the nearest voice THOSE plugins ship
		// (scripts/voice-dialogue-map.py). 0: nothing to borrow.
		[[nodiscard]] bool          IsDialoguePlugin(std::string_view a_plugin) const;
		[[nodiscard]] std::uint32_t DialogueBorrow(std::uint32_t a_voiceType) const;

	private:
		[[nodiscard]] static std::uint32_t Resolve(const nlohmann::json& a_ref, std::string_view a_what);

		mutable std::timed_mutex                         _lock;
		bool                                             _enabled{ false };
		std::unordered_set<std::uint32_t>                _own;      // rendered: speak as themselves
		std::unordered_map<std::uint32_t, std::uint32_t> _borrow;   // unique -> rendered, 0 = silent
		std::unordered_set<std::uint32_t>                _speakingRaces;   // who may take the default
		std::uint32_t                                    _unmappedFemale{ 0 };
		std::uint32_t                                    _unmappedMale{ 0 };
		std::vector<std::string>                         _dialoguePlugins;   // lower-case
		std::unordered_map<std::uint32_t, std::uint32_t> _dialogue;          // voice type -> voice type
	};
}
