#pragma once

namespace RP::DialogueVoice
{
	// An addon's DIALOGUE lines in a borrowed voice (owner, 2026-09-25: the Mayor of Diamond City
	// answered Overture with subtitles only -- "use our vocal embeddings").
	//
	// Overture's lines are recorded in a few voice types. Anyone else -- a named NPC with a unique
	// voice, a Gunner -- had no file, and the engine showed the subtitle alone. Rapport's barks solve
	// the same problem with SetOverrideVoiceType around one Say (V-25), which a conversation cannot
	// use: an override held through the dialogue menu would send the speaker's OWN vanilla lines to
	// the wrong folder too.
	//
	// So the engine is asked instead, at the one place a response's voice file is named
	// (1.10.163: the response set-up at 0xCA1BF0 calls the path builder 0x6135D0 once, at 0xCA1CCB):
	//   Data\Sound\Voice\<the INFO's plugin>\<voice type>\<INFO>_<n>.wav
	// When the plugin is one voices.json lists under "dialogue" AND no file exists there for the
	// speaker's voice type, the path is built again for the voice the fingerprints chose
	// (scripts/voice-dialogue-map.py) -- if THAT file exists. Every other line of every other plugin
	// is untouched, and a line whose own file exists is untouched.
	//
	// Loose files only: a plugin whose voices are packed in an archive finds no file either way and
	// keeps the engine's answer (a subtitle), never a wrong one.
	//
	// The call site is checked byte for byte before it is patched; anything unexpected leaves the
	// game alone and says so in Rapport.log.
	void Install();
}
