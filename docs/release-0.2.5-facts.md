# Rapport 0.2.5 — fact sheet for the pages

For Publisher-bud's interview (owner rule: every line is "feature: the hard thing, solved"). Each row has
the hard thing, what nobody else does, the measured number, the player benefit and the decision id.
Numbers are quoted from `docs/FEATURES.md` (F:line), `docs/aaf-under-the-hood.md` (AUH:line),
`docs/relationship-and-personas.md` (RP:line) or the code at the named commit.

**Evidence tags.**
- **Verified** means in game.
- **Built** means it is coded and staged but has not been seen in game yet.

The owner's rule is no caveat on the page, but the tag tells you what you can quote as a measurement.

## New in 0.2.5

| Feature | The hard thing, solved | Nobody else | Measured | Player benefit | Id / tag |
| --- | --- | --- | --- | --- | --- |
| **Scene moans** | Text-to-speech reads vowels as syllables, so it sounds like someone *saying* "ah". Solved with one feeling tag up front and one breath group. Length follows stroke tempo measured by Anatomy's physics. | Moans keyed by persona × sex, timed by measured stroke tempo, and silent when the mouth is full | 443 takes rendered, 419 play (24 gulps held back); 10 kinds; 600 / 1200 ms tempo bands; ±5% pitch, ±2 dB per playback; true peak ≤ −1 dBTP | Every partner sounds like themselves, and in rhythm | R-29 · Built |
| **Climax on untagged trees** | 27 of 61 climax trees name a branch "Orgasm" and tag nothing (F:179-180), so a tag-only climax never fires on them. Solved by reading the trees' branches. | — | 66 positions play only in a Climax or Orgasm branch on the test install (count varies by packs) | The climax moan lands on far more scenes | R-29 · Built |
| **Nobody left naked** | AAF re-dresses only in its own scene-end teardown, so an AAF restart, a scene that never ended or a mid-scene save strands actors naked. Solved by Rapport's own snapshot, kept in the save. It equips without force, because AAF's force-equip is what locked gear. | Covers every AAF scene, any mod's | 31 slots; a check every 30 s; +10 s so AAF's redress goes first; 0 force-equips | Clothes always come back | Wardrobe 6b55bfb · Built |
| **Change who they are** | Persona is read by four mods and a cached voice. Every reader was checked live with each mod's bud before shipping. | — | 2 hotkeys, 2 cycles back to "their own", this save only | Make anyone straight, bi or gay, romantic or vulgar, in game | R-27 + R-7 · Built |
| **Same-sex pairs prefer their own** | AAF casts a man into the gender-neutral slot of an F/M position. Solved by checking Rapport's index of every playable position before excluding. | The fallback stays: never an empty scene | Packs tag f_m / m_m / f_f: 1593 / 517 / 270 | m/m and f/f pairs get their own animations when they exist | owner 10-01 · Built |
| **Holsters** | Visible Favorites draws weapons on the skeleton, not as clothing, so AAF's undress never reaches them. Naming a start equipment set instead left actors fully dressed (AUH:316-318). Solved by hiding and restoring the displays. | — | — | No pistol on the hip during sex | 0.2.5 · Built |
| **Rapport alone starts nothing** | A leftover stand-in started scenes when Chemistry was absent, and a player saw it. Removed. | — | — | Only you, or a mod you installed, starts a scene | 060ab15 |
| **Every feature on by default** | — | — | The 4 Narrator moments and the sex sounds are on by default | It works out of the box | owner policy |

## The core (existing, the page's "under the hood")

| Feature | The hard thing, solved | Measured | Player benefit | Id / tag |
| --- | --- | --- | --- | --- |
| **The doorbell** | Calling the Papyrus VM from an F4SE task crashed the game twice in DispatchMethodCallImpl (F:95). Papyrus now polls, C++ answers, and the direction never reverses. | crashed twice before (F:95); poll every 3 s | Rapport cannot crash your game through the script engine | Verified |
| **One timer, CallFunctionNoWait** | A Papyrus stack never returns from an AAF call (every call ends in ui.Invoke, F:110). So one AAF call goes per stack, and the next poll is scheduled first. Note: the old "second timer id" story was corrected: one failure, not two (F:124-129). | At most 1 AAF call per poll (F:117) | Rapport never stalls the scripts other mods depend on | Verified |
| **Refusal detection** | AAF reports a refusal through the same event as a start. Treating it as a start looped about 8 times a second (F:153). Solved by reading the arguments: 11 for a start, 4 for a refusal. A dangling refusal used to hold both actors busy for 13 minutes (AUH:441). | 11 vs 4 args; 8/s loop gone | No phantom scenes, no frozen NPCs | Verified |
| **AAF watchdog** | AAF went deaf after loads (AUH:62). Rapport found the cure: verified 3/3 in game, about 4 s each (F:623). AAF's author confirmed it and fixed it in AAF 1.7.8 (issue #1). "About half of loads" was our dev loop, not normal play (F:869-872). Rapport now only watches. | 3/3, ~4 s | A dead AAF is noticed, and on old AAF repaired | Verified |
| **Tree selection** | 54 of 74 tree positions carry different tags at their two ends, so "doggy" got a facial (F:176). 27 of 61 "endings" are only a branch name (F:179). Rapport reads the whole tree. | 54/74, 27/61 | The scene asked for is the scene that plays, to a real ending | A-25 · Verified |
| **Emergency stop** | A tree ignores duration: a 30 s scene ran 3.5 min with no end event (F:215). The longest tree is 265 s × 1.89 worst ratio ≈ 501 s, so the stop is set at 600 s (F:218). | 600 s cap | No scene runs forever | Verified |
| **Faces** | Packs use 10 mfgSets across 2,240 actions (0.4%, F:248). Each act's face is classified from tags, 98.8%, then from names, 99.2% (F:260). The engine merges faces as clamp(max(override, animation)), so no AAF face can close a jaw (RP:1068). Anatomy's hook replaces the value after that merge. | 0.4%; 98.8% → 99.2% | Faces that fit the act, every scene | R-24 · Verified |
| **Glances** | The engine ignores eyelid overrides (RP:1070). Glances and eye rolls go through Anatomy. | 4-7 s glances, ×2.0 to ×0.6 by stage (RP:1317) | Partners look each other in the eye | R-28 |
| **Unique voices borrow** | 629 voice types fingerprinted. No threshold could work, because Piper and Mr Handy scored 0.29 vs 0.25 (F:712). | 462 borrow, 135 silent, 0 cross sex (F:719) | Named characters speak too | R-9 · Verified (by ear) |
| **Voice bank** | Neither TTS model is verbatim: 28/36 and 16/36, failing on different lines (F:689). Every take is gated by transcription. | 211 lines × 32 voice types = 6,656 files (F:684) | Subtitles always match the audio | V-1 · Verified (by ear) |
| **Real dialogue** | A hand-built Topic stayed silent until it got a Dialogue Branch (F:668). | — | Lines are real dialogue with lip sync, not sound effects | V-23 · Verified |
| **Bystanders react** | Rolling per poll would make anyone who lingers certain to speak. One roll per scene (RP:478). | About 1 in 3; a queued line came 9 s later (F:747) | Onlookers notice, or hear | R-12 · Verified (by ear) |
| **Relationships** | Seeded from the engine's own ranks, measured: spouses +0.80, family +0.45, friends +0.15 (F:755). Blood is a flag, never a refusal (R-14). | bond −1 to +1 | A history between everyone who has touched | R-13/R-14 · Verified |
| **Personas** | `id % 4` would tie persona to face style (0% vulgar on odd faces). A mixed hash gives 24.6–26.0% each (F:678). | 25,000 ids | Four kinds of people, stable forever | R-7 · Verified |
| **The Narrator** | Each addon reports its own share of the score. MCM registration failed for 23 of 28 settings until it was rebuilt (F:774). | Score parts sum exactly (0.97 example) | Know who, why, and the numbers | R-20 · Verified |
| **Aftermath** | AAF's overlay timer is an in-session countdown that strands overlays forever (F:336), so it is persisted in game hours. A reload doubled x3 → x6 before the fix (F:376). Who gets it: slot 0 is the receiving role, 559 of 562 (F:419). | 4 game hours default (0.2.4); 559/562 | The aftermath lands on the right person and wears off | A-13/A-19/A-21 · Verified |
| **Safeguards and ledger** | AAF_ActorBusy survives a dead request, so the NPC is refused by every AAF mod forever (F:533). Rapport releases a stale flag after a 120 s grace. | 13-minute stall gone (F:526) | NPCs never become permanently unusable | A-12/A-15 · ledger Verified; stale-flag release Built |
| **Takeover** | Silently changing another mod's settings looks like breaking it (F:565). Takeover is announced and reversible. | 4 entries, 21 quests (F:555) | Rapport and your other sex mods do not fight | A-11/A-16 · Verified |
| **Scheduler** | Runs native, no Papyrus scanning. | 0.004–0.028 ms a pass for 49–78 actors, about 0.2 µs per actor (F:35) | Zero script lag | A-8 · Verified |

## Notes

- **Not yet seen in game:** scenes with the player, the priority lane and lovers-by-word are Built (F:831-847). Names for the nameless was verified before its rewrite.
- **Stale docs to fix in FEATURES.md:** aftermath default 12 h → 4 h since 0.2.4 (F:333).
