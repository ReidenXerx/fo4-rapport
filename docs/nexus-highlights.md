# Nexus highlights — what to put on the page, and what to show

The page-facing companion to `docs/FEATURES.md`. Plain words, for players who have never heard of a
speaker embedding. Every row is VERIFIED IN GAME unless it says otherwise; the proof column says where.
**When the page is next written, go through this list top to bottom** — it exists so that nothing
built gets left off it. Add a row the day a feature ships.

Owner rules for the page (memory `rapport-nexus-advertise-voice-fallback`): explain the voice
fallback in detail, in plain words; credit AAF and Commonwealth Moisturizer; keep the "prove it is not
a virus" block (source link, CI build hash, live VirusTotal badge). Page edits go through
`nexus-tools`, and Save/Publish stay the owner's click.

---

## The headline features

### 1. They talk — with subtitles, in their own kind of voice
**Hook.** When two NPCs get together, one opens with a line and the other answers — voiced, subtitled,
and it fits the moment: a quick one behind the counter does not sound like a slow evening at home.
**Show.** One scene, both lines, subtitles visible. Then a second scene with the same pair: different
lines (it never repeats the same opener twice in a row).
**Proof.** FEATURES §7b "Voiced scene barks"; heard in game.

### 2. Four personalities, and every NPC has one
**Hook.** Every NPC is one of four types — the dealmaker, the romantic, the crude one, the shy one —
and they stay that type forever. Same event, four different reactions.
**Show.** Two scenes with different pairs, back to back: a shy "I'm not good at asking for this" next
to a crude one. Bystanders of different types reacting to the same scene.
**Proof.** FEATURES "Four personas"; R-7.

### 3. People notice — and say something
**Hook.** Walk past, and they turn their heads. If they can see it — or hear it through the wall —
they might comment. Alone they mutter; in a crowd they play to the room. Nobody talks over anybody.
**Show.** A scene in a busy place: heads turning toward it, then one or two comments a few seconds
apart. Ideally one bystander who is clearly around a corner (heard, not seen).
**Proof.** FEATURES "Bystanders notice, then react"; Ivy, McDonough, Cathy heard in game.

### 4. Companions and named characters speak too — in the closest voice we have
**Hook.** Piper, Hancock, Nick, Cait — characters with their own actors — would normally be silent.
Rapport measured every voice in the game (629 of them) with a speaker-recognition model, the same
kind of tech that tells voices apart on a phone call, and gives each unique character the most similar
voice we recorded. Only for that one line: talk to them afterwards and they are exactly themselves.
Never a child, never the wrong sex, never a robot pretending to be human.
**Show.** A companion (Piper or Hancock) in a scene, speaking. Then walk up and talk to them normally:
their own voice, untouched.
**Proof.** FEATURES "Unique voices borrow the closest one we have"; Geneva and Ivy heard in game.

### 5. 6,656 voice lines, every one checked
**Hook.** 211 lines across 32 voice types. Text-to-speech gets words wrong now and then, so every
single line was listened back to by machine and compared with its subtitle — anything that did not
match was re-recorded or thrown away. What you hear is what you read.
**Show.** Not a shot — a caption over feature 1.
**Proof.** V-1; the render gate in `scripts/render-barks.py`.

### 6. Crowds sound like crowds
**Hook.** In a crowd, the same NPC projects — same person, louder, playing to the room.
**Show.** A bystander alone (City Hall) vs. a crowd (the Dugout Inn or the Third Rail) - not the
market: children stand around there.
**Proof.** V-21; owner picked every voice by ear.

---

## Under the hood (worth a section, not a headline)

- **AAF's load race** (rare in normal play; confirmed by AAF's author and fixed in AAF 1.7.8, issue #1).
  Rapport notices and recovers it until then. Do NOT claim AAF bugs the author attributed to our own
  bridge (issue #1: §22-24).
- **Faces that match the moment**, and are always taken off afterwards (FEATURES §4).
- **Aftermath that lasts in game hours**, on the right person, from the last act (FEATURES §5).
- **Roles are right.** In a mixed pair the woman is placed in the receiving role even when an
  animation does not say (FEATURES "The female takes AAF's slot 0").
- **Light on the game.** No script on every NPC, no cloak spell, nothing during the post-load rush —
  the thing that measured as the load order's worst Papyrus problem (FEATURES §1).
- **Manages the mods it replaces** instead of fighting them (FEATURES "Takeover").

## Trust

- Source on GitHub, public. Every push builds the DLL on a clean GitHub machine and publishes its
  SHA256; a live VirusTotal badge sits on the page. Say plainly that the CI build and the uploaded
  build are not yet byte-identical (T-3) — do not imply they are.
- Credits: AAF, Commonwealth Moisturizer (author name still to be filled in by the owner).

## For modders (a spoiler block, or a link to FEATURES §8/§8b)

The measured engine findings are the most reusable thing in the repo: AAF going deaf on load,
`ChangePosition` refusing 26/26, a Papyrus stack never returning from an AAF call; and from the voice
work — a generated Topic needs a Dialogue Branch, `Say` works mid-AAF-scene, the audio is resolved at
`Say` time, `HasDetectionLOS` is stealth and root-to-root rays hit the floor.

---

## Demo video — shot list

The scripted demo (`scripts/demo/`) plays these in order; each is reproducible, not luck.

| # | Where | What the viewer sees | Feature |
| --- | --- | --- | --- |
| 1 | City Hall (`city-hall`) | opener + answer, subtitled; Geneva's borrowed voice; McDonough hears it through a wall | 1, 2, 3, 4, 5 |
| 2 | the Dugout Inn (`dugout-inn`) | a bar crowd: heads turn, patrons comment in turn | 3, 6 |
| 3 | the Third Rail (`third-rail`) | Goodneighbor, a crowd, a tender scene | 1, 3, 6 |
| 4 | the Old State House (`state-house`) | Hancock and Fahrenheit, both unique voices, speaking; then talk to Hancock normally | 4 |

Run them with `scripts/demo/demo.py` (see `scripts/demo/README.md`); each shot records its own clip.
