# Voice guidelines — Fallout 4 voiceover

**Authoritative for every agent doing voice work on these mods.** Numbered `V-#` so
they can be cited the way `NS-#` and `GP-#` are. Each rule is measured, and the
evidence is stated — where a rule has no evidence line it is a preference, and it
says so.

Measured 2026-09-21 on ElevenLabs Pro against Fallout 4 GOTY (GOG).

---

## The model and the chain

### V-1 — NEITHER model is verbatim-safe. Gate every render.

**Corrected twice, 2026-09-21.** The first version said "never `eleven_v3`" from
n=3 on one line. The second allowed v3 per line but called
`eleven_multilingual_v2` "verbatim guaranteed" — *from the same n=3 on the same
line*. That second claim shipped: the renderer transcribed every v3 take and
passed v2 straight to disk, and a spot-check found **4 of 8 v2 files had drifted**.

Measured per line across the whole bank, n=8:

| model | verbatim |
| --- | --- |
| `eleven_v3` | **28 / 36** |
| `eleven_multilingual_v2` | **16 / 36** |

The model that was trusted is the **less** reliable one, and the two fail on
**different lines** — `"Door's shut. Nobody needs us for a while."` is 100% on v2
and 33% on v3.

**Classifying lines by those rates was sorting noise.** At n=8 a line whose true
rate is ~90% lands on 88% or 100% at random; three lines condemned in one pass
measured 100% in the next. A rate is a hint about which model to TRY FIRST. It is
not a property of the line and it must never decide correctness.

**Correctness comes from a runtime gate on every single render:** transcribe it,
compare it, re-roll the seed on drift, try the other model, and if neither will
say it — **fail, and write nothing**. A line no model will say cannot ship under
a subtitle claiming it did. Unverified audio never reaches disk.

### V-1b — Rewrite a line only when NO model will say it.

Of six drift-prone lines, only **two** genuinely needed rewriting. Three simply
needed the other model and kept their words. The rule is narrower than it first
looked: the model does not get a vote on the script, but a line that nothing can
render cannot ship, and that is the one case where the script yields.

### V-17 — A failed render leaves the PREVIOUS file in place.

The re-render reported "5 failed" while every expected file existed. They were
the **old, ungated** files from the previous run — present, plausible, wrong. A
file listing proves nothing about a re-render.

Check **modification times against the batch**, not existence: five files sat at
16-20 minutes against a 4-minute median. Delete a failed target so it is
genuinely missing rather than silently stale, or the next resumable run will skip
it forever.

### V-18 — STT is not deterministic, so the comparator must tolerate elision.

The same audio transcribes differently between passes. One file passed the gate
at render time and came back `"wanna"` for `"want to"` on a later check — the
same words, elided in delivery, and a correct subtitle either way.

`norm()` therefore maps pronunciation variants (`wanna`/`want to`,
`'em`/`them`, `alright`/`all right`) before comparing. Real drift survives it:
`"gotta"` becoming `"need to"` is a different word and still fails.

Watch the regex: a leading apostrophe has no word boundary before it, so a naive
`\b'em\b` never matches anything.

### V-2 — Audio tags follow V-1: v3 lines only.

`[whispers]`, `[sighs]`, `[laughs]` and the rest are a **v3-only** feature, so
they are available exactly where V-1 allows v3 and nowhere else. Never put
bracket tags in v2 text — they are not interpreted there, and an
uninterpreted tag risks being read aloud.

Delivery comes from the **voice design description instead**. Every voice in this
project carries "Speaks low and close, half-whispered, as if avoiding being
overheard" in its own brief, so the half-whisper is baked into the voice rather
than requested per line.

### V-3 — Render at `pcm_44100`. Never MP3.

Pro-tier only, and one of the few things Pro genuinely unlocks. The chain is
**PCM → WAV → xWMA → FUZ** with no MP3 anywhere, so the game's encoder is the
only lossy stage.

Keep it in proportion: **vanilla FO4 voice is 32 kbps xWMA**, so the game's own
format is the dominant bottleneck. PCM avoids a *cascaded* lossy encode, which is
real but smaller than the source bitrate suggests. Do not oversell it.

### V-4 — `pcm_44100` comes back HEADERLESS, and the MCP names it `.mp3`.

Raw 16-bit mono little-endian samples, no container. The MCP server appends a
hardcoded `.mp3` extension regardless of the format requested. **The extension is
a lie; trust the bytes.** `scripts/pcm-to-fuz.py` works from the bytes and
refuses anything that is genuinely MP3 or already RIFF.

---

## The Fallout 4 side

### V-5 — `lip: 0`, always. This is correct, not a shortcut.

`Config.h` sets `blockAnimationFaces{true}`, freezing the face for the length of a
scene, because the engine's facial idle writes the same morphs to blink, breathe
and **talk**. Lip data would be a third writer fighting a block Rapport installs
itself. `FaceFXWrapper` is not a dependency we need.

**Scope - read this before inheriting V-5 anywhere else (2026-09-23).** The block is
per ACTOR: `Bridge.psc` calls `AddMFGBlock` only on an actor whose face Rapport has
just set (`aiKind == 3`), which is a scene participant. So V-5 covers the **pair
barks**, and nothing else:

- **Observer barks** are spoken by actors who are NOT in the scene, so no block is
  on their faces. With `lip: 0` they talk with a still mouth. OPEN - not measured
  in game, and whether it shows at observer distance is the owner's call.
- **Overture's dialogue** is ordinary conversation before any scene, never inside
  the block. It needs lip data: `fo4-overture/scripts/make-lip.py` runs the game's
  own `Tools/LipGen/LipGenerator.exe` (44.1 kHz mono wav, no resampling), and
  `pcm-to-fuz.py --lip` packs it into the `.fuz`.

(Flagged by nexus-modding: V-5's justification is scene-scoped, so it does not
transfer by default. The per-actor reading is from `Bridge.psc`.)

### V-6 — The voice file path, and the byte that would silently break it.

```
Sound/Voice/<Plugin.esp>/<VoiceType>/<FormID & 0x00FFFFFF, 8 hex>_<n>.fuz
```

Established from a **mod** archive, not vanilla — vanilla is always load order
`00` and therefore proves nothing. `LobotomitePack.esm` writes its FormIDs with
the load-order byte as `00`, masked at lookup, so filenames survive any load
order. Casing is inconsistent inside Bethesda's own archives, so lookup is
case-insensitive. The folder name **includes** the extension.

**ESL-flagged plugins use the same rule, unchanged** (measured on disk 2026-09-23 by
nexus-modding, three independent authors):

| plugin (ESL) | INFO in the plugin | voice file |
|---|---|---|
| `WhoIsTheGeneral.esp` | `0200000D` | `0000000D_1.fuz` |
| `Sacrifice Grounds.esp` | `08000803` | `00000803_1.fuz` |
| `KARMA.esm` | `0100009E` | `0000009E_1.fuz` |

The ESL slot (the `xxx` of the runtime `FExxxyyy`) never appears in the name. In all
three the middle bits are zero, so `& 0xFFFFFF` and `& 0xFFF` cannot be told apart
there - keep `& 0xFFFFFF`, which holds for full plugins too, and keep an ESL's object
ids in `0x800`-`0xFFF`. Seen on disk, not yet watched resolving in game.

### V-7 — Match vanilla structurally; 48 kbps is deliberate.

Every shipped `.fuz` must be `FUZE` v1 / `RIFF` / tag `0x0161` / mono / 44100 /
chunks `fmt`, `dpds`, `data`. The `dpds` seek table is required — `xwmaencode`
emits it for free. We encode at 48 kbps against vanilla's 32: higher, with no
reason to go lower.

### V-8 — An unrendered voice type degrades to silence **plus a subtitle**.

That is acceptable, and it is why coverage can grow one voice type at a time.
Never map an actor to a voice that is not theirs in order to fill a gap.

---

## Process

### V-9 — Picking a voice is done BY EAR, by the owner. Agents cannot hear.

An agent may design, organise and render. It may **not** decide which preview
sounds right, and must never auto-pick one to unblock itself.
`scripts/render-barks.py` enforces this: `"chosen": null` is skipped, never
guessed. If you catch yourself reasoning about which voice "sounds" better from a
filename or a byte size, stop — that is not evidence.

### V-10 — Verify a batch by transcribing it, not by listening for it.

STT is how an agent checks its own output without ears. Spot-check any batch with
`scribe_v1` and compare against the authored line. It is how the v3 drift was
found before it shipped — and, later, how V-1's own overstatement was caught and
corrected. The same instrument that finds a defect will find your wrong rule
about it, if you point it at the question twice.

STT is an imperfect witness on whispered audio — a single mismatch is a lead, a
reproducible one across seeds is a finding. Say which one you have.

### V-11 — Always pass `seed`. Builds must be reproducible.

A shipped mod is files. Re-rendering one line must not silently change the other
thirty-five. Pass an explicit seed on every request.

### V-12 — `apply_text_normalization: "off"` for barks.

Short spoken lines with no numbers or dates. `auto` can expand things unasked and
costs latency for nothing.

So a number in a bank is **spelled out** ("twelve"), which makes the model say it
reliably. The transcriber writes it back as digits, and the gate has to allow for
that - see V-28. This setting governs what the MODEL says; it has nothing to do
with how the gate compares.

### V-13 — Validate voice type names against the game archives before spending.

And **make the validator prove it loaded data.** A reference set that comes back
empty condemns every name it is asked about, which reads exactly like "all your
names are wrong". Measured: it did exactly that, to 22 names that were all real,
because the path was wrong and the archive glob matched nothing. Assert a
plausible floor on the reference set before trusting any rejection from it.

### V-14 — REST API for batches, MCP tools for auditioning.

36 lines across 32 voice types is over a thousand renders. That is a script's job
(`scripts/render-barks.py` — resumable, skips what already exists). The MCP tools
are for hearing one thing quickly.

### V-15 — What the owner's ear actually wants: tremble.

Owner feedback, 2026-09-21, on `DLC04GangDiscipleFemale01` preview 3:

> "its amazingly voice is rumbling and shaking as human could do and tremble"

That is the target quality for this project, stated plainly, and it is worth more
than any setting in this document because it is the only thing here that came
from listening.

Two things produce it, and both are controllable:

- **The brief.** That voice was briefed as *"quiet, deeply unsettling, soft and
  breathy and wrong"*. Breath and instability in the DESCRIPTION are what give a
  designed voice somewhere to shake from. A brief that asks for "clear",
  "professional" or "well-projected" forecloses it.
- **`stability`.** Lower values widen the emotional range, which is where the
  tremble lives. This project runs 0.30 on `quickie`, 0.35 on `tender`, 0.40 on
  `athome` — tighter where the delivery should be settled, looser where it should
  not be.

When designing a new voice, write the brief for a person who is *affected by
something*, not for a narrator. "Tired but warm", "breathy and wrong", "ragged
breathing in the voice" all produced usable character. Neutral competence does
not.

### V-16 — Preview 3 won every time, and nobody knows why yet.

Across all 32 designed voice types the owner picked **preview 3**, including the
very first one chosen blind before any pattern existed. Voice Design returns
three variants and does not document an ordering.

Treat this as an **observation, not a rule**. It may be that the third variant
carries the most deviation from the neutral read, which would match V-15
exactly — or it may be coincidence across one listener and one session. Do not
auto-select preview 3 on that basis: V-9 still stands, and an agent choosing a
voice from a pattern is still an agent choosing a voice it cannot hear.

It is written down so that if it holds across the next batch, it becomes
evidence instead of a hunch.

### V-19 — The API surface actually used, and the real budgets.

Endpoints, because the MCP tools do not scale past auditioning (V-14):

| need | call |
| --- | --- |
| design a voice from a prose brief | `POST /v1/text-to-voice/design` -> 3 previews, each with `audio_base_64` + `generated_voice_id` |
| keep one of those previews | `POST /v1/text-to-voice` with `generated_voice_id`, `voice_name`, `voice_description` |
| render | `POST /v1/text-to-speech/{id}?output_format=pcm_44100` |
| verify a render | `POST /v1/speech-to-text`, multipart, `model_id=scribe_v1` |
| budgets and usage | `GET /v1/user` -> `subscription` |

**Professional Voice Cloning is capped at ONE voice on Pro** (`professional_voice_limit: 1`),
needs 30+ minutes of clean training audio, and takes hours. It is the wrong tool for a
project that wants thirty distinct voices. **Voice Design is the route**, and it draws on
the 160 `voice_limit` slots.

**The budgets that actually bind are not credits.** 610,000 characters/month is enormous
against this work — the whole 32-voice bank, every experiment and every re-render came to a
small fraction of one month. The limits worth watching:

- `voice_limit: 160` — designed voices you may hold.
- `max_voice_add_edits: 290` — voice create/edit operations, and `voice_add_edit_counter`
  does not reset with the character allowance. Deleting and re-creating voices spends it.
- **The owner's listening time.** 32 voice types at 3 previews is 96 clips. That, not money,
  is what makes a batch expensive. Never cut content scope to save credits; say so if asked.

### V-20 — Only DOCUMENTED audio tags, and what we deliberately do not use.

`[urgent]` is not in ElevenLabs' tag list. Passing it made v3 paraphrase the line every
time it was measured (0/8). An undocumented tag is read as a *direction* rather than
consumed, and the model rewrites around it. Stick to the published list -
`[whispers]`, `[sighs]`, `[laughs]`, `[exhales]`, `[sarcastic]`, `[curious]`, `[excited]`,
`[crying]`, `[mischievously]` - and only on v3 lines (V-2).

Capabilities available on this account and **not** used here, so nobody re-discovers them
as if they were new:

- **`speech_to_speech`** - converts a performance into a target voice, preserving delivery.
  The strongest emotional control available, and it needs a recorded performance per line.
  Worth revisiting if tag-driven emotion is not enough.
- **`text_to_sound_effects`** - generates SFX. Out of scope: Rapport ships no sound effects.
- **the shared Voice Library** - pre-made community voices. Not used because a FO4 voice
  type needs to match Bethesda's, which briefs do better than browsing.
- **`eleven_v3_conversational`, Flash and Turbo** - lower latency, which is worthless for an
  offline batch that is rendered once and shipped as files.

### V-21 — Delivery lives in the BRIEF. To change register, make a sibling voice.

The reusable result of this project, and the one worth reaching for first next
time.

**The problem.** Every voice here was designed with *"Speaks low and close,
half-whispered, as if avoiding being overheard"* in its brief. Correct for two
people in a corner. Then the observer lines arrived and one of them was
*"Louder, would you! Some of us are standing at the back!"* — a heckle thrown
across a settlement, in a voice that had been built to confide.

**What does NOT fix it.** Dropping the `[whispers]` tag, raising `style`,
raising `stability`, raising `speed`. Those shape a performance; they do not
replace a timbre. The intimacy was designed IN, and it stays.

**What does.** Make a **second voice from the same character brief with only the
delivery clause swapped.**

    character clause   (IDENTICAL, never touched)
      "A neutral middle-aged American man, around forty, even and level and
       completely unremarkable - the plain voice of an ordinary working settler..."

    delivery clause    (the ONLY thing that changes)
      intimate:  "Speaks low and close, half-whispered, as if avoiding being
                  overheard."
      projecting:"Speaks up so the whole room hears it, pitched to carry across a
                  crowd, with the easy volume of somebody who does not mind at all
                  being overheard."

Voice Design reads the whole prose brief as one identity, so holding the
character half fixed and varying only the delivery half yields a **sibling** -
recognisably the same person in a different register - rather than a stranger.
That is what makes it safe to swap between them mid-scene.

**The recipe, in order:**

1. **Find the line that breaks hardest** and make it the test text. Not a typical
   line - the one the current voice is worst at. If that one works, the rest do.
2. **Pilot three or four types, never all of them.** Designing four costs about
   550 credits and turns an unanswerable question into an A/B. Designing
   thirty-two before anybody has listened is a spend on an assumption.
3. **Hold everything else constant** in the comparison: same text, same
   `voice_settings`, same seed. Only the brief varies.
4. **Let the owner pick by ear** (V-9). An agent cannot hear a register.
5. **Scale only after the pilot lands**, then wire the renderer to choose a voice
   **per line**, not per voice type.

**Costs, so the shape is known in advance.** Designing is cheap - roughly 137
credits per voice, so 32 siblings is about 4,400. Creating them from previews
costs a voice slot and a voice add/edit each (160 and 290 respectively; this
project is at 64 of each after two sets). **Re-rendering is the real spend** -
1,024 crowd lines came to about 51,000 credits. Design freely; render once.

**Two honest limits on how this one was validated.**

- The A/B was not perfectly controlled. The current-voice side was rendered with
  the crowd `voice_settings` applied, while the projecting side was a Voice
  Design preview at its own defaults. So it compared *knobs-only* against
  *new brief plus defaults*, not the brief alone. The preference was clear, but
  a cleaner test would render both through the same settings.
- **Twenty-eight of the thirty-two siblings were never auditioned.** The owner
  heard four and chose preview 2; that choice was applied to the rest by pattern.
  Per V-16 a pattern is not evidence. All 96 previews are kept in
  `voice/audition-loud/` so any type can be swapped with one field in
  `voices.json` and a single `--only` re-render.

**Where this goes next.** The dialogue mod will want more registers than two -
angry, flirtatious, formal, frightened. This is the method for all of them: one
character brief, N delivery clauses, N sibling voices, chosen per line. The
character stays the same person throughout, which is the entire point.

### V-22 — Explicit anatomy splits a line by SPEAKER gender, never by partner.

Writing a persona that is genuinely crude rather than merely gruff forces a
structural decision, and it is easy to get expensively wrong.

**The asymmetry:** the speaking voice type tells you the speaker's gender for
free (all 32 here encode it in the name). **The partner is unknown at authoring
time** - Rapport pairs any two eligible actors, so a line cannot assume who is on
the other end.

So split by what the line refers to:

| refers to | handling |
| --- | --- |
| the speaker's own body | **gendered variant**, keyed off the voice type |
| the partner's body | **anatomically neutral**: mouth, hands, tongue, ass, fingers, throat |
| the act itself | neutral - "fuck me", "get your hands on me" work for anybody |

A bank line is therefore either one string or `{"m": ..., "f": ...}`, which emits
two lines plus a `gender` tag; the renderer skips lines belonging to the other
gender (`scripts/render-barks.py`, `gender_of`).

**It is much cheaper than it looks.** Only **3 of 55** vulgar lines needed
splitting. Most explicit writing is about what one person is doing *to* another,
and that is already neutral. Do not pre-split a whole bank by gender - write
neutral by default and gender only the lines that genuinely cannot be.

**Two traps:**

- **`"Female"` contains `"male"`.** Test for female first or every female voice
  is classified male and says the wrong line. Silent, and wrong on 12 of 32.
- **A type matching neither is treated as NEUTRAL, not guessed.** It then renders
  no gendered line at all, which is a gap rather than a wrong body part. Prefer
  the gap: a missing line degrades to silence with a subtitle (V-8), while a wrong
  one is jarring and unfixable in play.

**Overture will hit this far harder.** Dialogue is second-person and continuous,
where a bark is a moment. Budget for a higher split rate there, and settle the
convention before authoring rather than retrofitting it - the retrofit here was
cheap only because the bank was small.

### V-23 — A Say()-able topic needs a DIALOGUE BRANCH. Verified in game.

Our first plugin built topics that resolved, whose quest was running, and on which
`Say` was called — and nothing was ever spoken. The fix, found by diffing against a
line the game **actually** speaks via `Say` (the follower commands):

- **Every topic belongs to a `DLBR` (Dialogue Branch)** and points at it with `BNAM`.
  One branch holds many topics — `FollowersSayTopics` holds twelve. Ours now has one,
  `RapportSayTopics`, holding all of them. **Without it, a category-0 topic is inert.**
- **The line shape that works for `Say` is `ENAM 0x02`, no `NAM9`.** The first build
  copied the commonest *unconditioned* spoken line, which is **scene** dialogue — close
  in shape, wrong in role. Copy a template **from the same role**, not merely the same
  record type.

Ruled out by measurement on the way, so nobody re-tries them: the owning quest was
running (asked it directly); the topic category was right (the game's own `Say` topics
are category 0 `CUST`, 1,243 of them); the actor could speak (McDonough spoke a vanilla
greeting via `Say` in the same session).

**`V-8` is now verified, not assumed:** a line with no audio for the speaker's voice
type still shows its subtitle. McDonough's voice type was never rendered and he displays
our line anyway.

**Subtitles sit at roughly 72–90% of frame height.** Cropping from 80% or lower misses
the speaker name and often the text — several "failures" in this session had to be
re-read with a correct band before they could be trusted.

**Never hardcode a runtime FormID.** Rapport.esp's `01` became `0xE2` on this machine,
from `plugins.txt`. That index is the player's load order, not ours. In the shipped
code resolve topics by **file-relative id** (`Game.GetFormFromFile`), never by a number
that happened to be right here.

---

## Settled numbers

| | |
| --- | --- |
| model | per line, whichever measures better first; EVERY render gated by STT (V-1) |
| voice | per line: intimate (preview 3) for pairs and lone observers, projecting (preview 2) for crowd lines (V-21) |
| output format | `pcm_44100` |
| voice settings | `similarity_boost 0.75`, `style 0.3`, `use_speaker_boost true` |
| stability / speed | per scenario: quickie 0.30 / 1.10 &middot; tender 0.35 / 0.92 &middot; athome 0.40 / 0.95 |
| audio tag | `[whispers]` on v3-safe lines only |
| xWMA bitrate | 48 kbps (vanilla: 32) |
| lip data | 0 bytes |
| delivery | TopicInfo records, for subtitles (owner poll 2026-09-21) |

**Cost is not the constraint.** Ten voice designs plus 36 renders cost 2,488 of
610,000 monthly credits — 0.4%. Rendering all 32 voice types in full is roughly
36,000, under 6%. The real budgets are the **290 voice add/edits** and the
owner's time auditioning. Never cut content scope to save credits; say so if
asked to.

## Commands

```bash
python scripts/render-barks.py --dry-run          # cost + what it would do
python scripts/render-barks.py                    # render what is missing
python scripts/render-barks.py --only MaleBoston  # one voice type
python scripts/pcm-to-fuz.py in.pcm out.fuz       # one file, by hand
```

## Gotchas that cost time

- **`xwmaencode` exits non-zero while printing a plausible message**, and fails
  with `ERROR_SHARING_VIOLATION` against a file still held open. `mkstemp` hands
  back an OPEN handle — use `mkdtemp`. **Check the artifact, never the exit code.**
- **A bare MP3 sync-word test false-positives on PCM.** 16-bit PCM whose first
  sample is `-1` is the bytes `FF FF`, and a quiet opening is ordinary in TTS — it
  hit 1 render in 36. Require a plausible MPEG frame header instead.
- **`/tmp` means two different directories on this machine.** git-bash rewrites it
  in command arguments; Python resolves it internally as `C:\tmp`. Pass absolute
  paths between the two. This bit twice in one session.
- **`Archive2.exe` opens a GUI and hangs** when run headless. Read BA2s directly —
  `BTDX`/`GNRL` is a 24-byte header, 36-byte records and a name table.
- **The MCP server can only read files under `ELEVENLABS_MCP_BASE_PATH`.** Anything
  to be transcribed must sit inside it.
- **Generated HTML is never trusted until its JavaScript parses.** A page whose
  script throws renders its header and nothing else, which looks almost fine. An
  escaped apostrophe collapsed on the way into the file, `Bethesda's` closed a JS
  string early, and the bark browser shipped blank. Extract the script and run
  `node --check` before claiming a generated page works.
- **Escapes collapse on the way into a file more often than seems possible.** A
  backslash-b became a literal backspace byte inside a regex; `
` in a patch
  source became a real newline and silently matched nothing. Write patches with
  raw strings or `chr()`, and verify the bytes with `cat -A` when a replacement
  "succeeds" but changes no behaviour.


### V-24 - Test speech with a say strip, and time it from the bridge (2026-09-21)

Use `fo4-mcp/tools/host/saystrip.py` (documented in fo4-mcp's
`docs/dev-command-channel.md`): many attempts, same line, alternating speakers,
one image of subtitle bands. Single screenshots misled twice in one session.

Two findings it produced, both of which reversed a confident reading:

- **Named NPCs with UNIQUE voice types are silent, not broken.** Only 30 of 531
  voice types in Fallout4.esm are generic; Geneva (`NPCFGeneva`) and McDonough
  (`NPCMMayorMcDonough`) are unique and not among our 32 renders. They show our
  subtitle and play nothing - V-8, working as designed. Rendering them is a
  credits decision per character, not a fix.
- **"Allow Default Dialog" is NOT what gates our lines.** Both of those voice
  types have that flag OFF and both speak our unconditioned lines. Checked in the
  ESM and in the game, so nobody re-derives it from memory.

### V-25 - A unique voice BORROWS the closest rendered one, for one line only (2026-09-21)

Owner's idea: rather than silence (V-8), a named NPC with a unique voice type
speaks our line in the most similar voice we rendered. Proven in game:

    who.SetOverrideVoiceType(borrowed)
    who.Say(line, None, False, None)
    who.SetOverrideVoiceType(None)

Geneva (`NPCFGeneva`, unrendered) spoke *"Hurry up, and mean every second of
it."* audibly as `FemaleEvenToned`, set-say-clear on ONE Papyrus stack. The audio
is resolved at Say time, so clearing immediately does not cut the line - which
is what makes this safe: the borrowed voice exists for one call and no save can
ever be written while an actor wears it. Zero disk cost, and it covers voice
types added by other mods.

Never cross sex. The pairing itself is measured (speaker embeddings of the
game's own recordings), and the owner can veto any pair by ear (V-9).

**Rebuilding it: one command.** `python scripts/rebuild-voices.py` runs the whole
pipeline (inventory -> races/sexes -> fingerprints + map -> runtime table) and
prints WHAT CHANGED against the table it replaced. Incremental: only new voice
types are fingerprinted. `--dry-run` reports without writing; `--full` redoes
every fingerprint. Run it when a voice type is RENDERED, when voice archives or
masters change, or after editing `voice/fallback/overrides.json` - not when lines
are added (a new line is spoken by whichever voice the map already chose). CI runs
`build-voices-table.py --check` so the committed table cannot drift from the map.

### V-26 - Fingerprints steer CHOICES, not DESIGNS: a synthetic voice tops out near 0.2 (2026-09-21)

`scripts/voice-audit.py` found the owner's sweet spots: 916 unique voices in the load order, 325
served badly, and Ivy's group of 145 (Piper, Cait, Alice Bell...) covering 27,270 lines. Then the
measurement hit a ceiling that is itself the finding:

- **Game-vs-game** (choosing which existing voice to borrow) spans the full range - median best match
  0.37, Gage->MaleRough 0.735. The V-25 map lives here, and it is sound.
- **Synthetic-vs-game** does not. Every ElevenLabs voice - six designed candidates for Ivy, AND our own
  32 renders - scores at most ~0.2 against a real game recording, whatever it sounds like: different
  microphone, codec and processing dominate the fingerprint. What Ivy says today (our FemaleEvenToned
  render) is +0.18 against her; the best designed candidate +0.20.

So the realistic bar for a DESIGNED voice is "measurably closer than the current borrow", not 0.35,
and the owner's ear (V-9) is the real judge. Two traps met on the way, both worth not repeating:

- **The baseline must be what PLAYS.** "Today" first used the vanilla recordings of the borrowed
  voice (0.24) - but the player hears our render of it, a different person. Measure the render.
- **Register is part of the fingerprint.** Whispered previews against normal speech measured the
  whisper. Design and score the identity in normal speech; make the whispered and crowd siblings
  by swapping only the delivery clause (V-21).

Also: `voice-profile.py` uses YIN for pitch - torchaudio's detector HALVED Ivy (123 Hz, "male" vs her
real ~200 Hz); and a cluster member's fit is LEAVE-ONE-OUT, or every pair scores ~0.7 by construction.

### V-27 - Gate every voice on TIMBRE, not pitch: a man shouting sits in a woman's range (2026-09-21)

FemaleEvenToned's crowd voice was a man, shipped, and heard under Ivy's and Magnolia's subtitles.
The brief did say "woman"; Voice Design produced a man anyway, and preview 2 was picked in a batch
of 32 without anyone hearing that one. Nothing could catch it: the render gate checks WORDS (V-1),
and the only sex check measured PITCH - and projecting to a crowd puts a man at 250-350 Hz.

- **`scripts/voice-sex-check.py`** fingerprints every voice type in both registers and requires
  it to sit clearly closer to its own sex than the other (margin 0.15). The miscast voice: 0.26 to
  the women, 0.65 to the men. Proven to FAIL on it (exit 1, names it), and to pass all 32 after the
  recast (FemaleEvenToned crowd now 0.50 women / 0.28 men). Run after any render, before packaging.
- **Recasting a sibling:** keep the character clause, add "clearly and unmistakably a woman's
  voice", test on the line that broke, gate the previews on timbre BEFORE the owner listens, and
  prefer the one closest to the intimate sibling (+0.47 here vs the miscast voice's +0.23).
- **Re-render only what changed:** move the bad files aside (`voice/retired/`) and let the renderer
  fill the gaps - `--force` would have redone all 211 lines. 32 lines, 1,676 characters.
- **`package-voice.py` compares CONTENT, not size**: a same-size re-render would otherwise be
  skipped, leaving the old audio deployed while everything reports success.

How it was found is the method worth keeping: the owner heard it; a subtitle named the wrong person;
the file at the path was proven right (MD5), which ruled out packaging; a swap test (another actor,
same line) ruled out the actor; only then did the voice itself come under suspicion.

### V-28 - The gate canonicalises NUMBERS; it must never strip them (2026-09-23)

`norm()` compared words after deleting every character that is not a letter. So
digits were DELETED: the bank says "twelve", the transcriber writes "12", and
`"...there are people watching"` never equals `"...there are twelve people
watching"`. That is a false NEGATIVE: the audio was right, and the gate rejected it
every time. The line failed for good rather than now and then.

Measured on Overture's `ov_vulgar_recoil_01`, "I want your hands on me and there are
twelve people watching.":

- FemaleBoston and FemaleRough failed on every take (6 each), and FemaleBoston failed
  again on a retry at `--tries 8`.
- FemaleRough and two other voices only passed on eleven_multilingual_v2, because
  that model's takes happened to transcribe as "twelve". The more expressive v3 was
  given up for the gate's sake.
- After the fix, all four passed on v3 with no fallback: FemaleBoston in 4 s, the
  other three in 13 s together.

The same class of problem as V-18, so it lives in the same `norm()`. Digits become
words on BOTH sides, before anything is stripped (`numbers_as_words`):

- integers, `1,000`, `3.5` ("three point five"), and ordinals (`21st` -> "twenty
  first");
- `%` and "per cent" -> "percent";
- "a hundred" = "one hundred", and "a hundred and twelve" = "112".

That last fold is blindness by DESIGN. A transcript reading "100" cannot say whether
the voice said "a" or "one", so the gate must not pretend to tell them apart.

**It still catches real drift.** "twelve" vs 13, 120, and "a hundred and twelve" vs
120 all still fail. The test (`twelve` vs `12` and 16 other same-meaning pairs, plus 8
one-word-different pairs) failed 12 of 25 before the fix and 0 after. Neither bank
contains a digit, so no line compares differently except the two "a hundred" lines,
and those fold on both sides.

**What the gate can never see** (nexus-modding, 2026-09-23 - keep this list short and
check these by ear):

- homographs ("record" said either way transcribes the same);
- mispronounced proper nouns;
- stress in an ALL-CAPS run (the transcript lowercases it);
- rhythm, and silences: a `[pause]` is invisible to a transcript.

### Leads from nexus-modding's pass (2026-09-23) - NOT measured on this bank

Recorded so they are not rediscovered, and not adopted until measured here:

- **Direction tags.** They reported that v3 CONSUMES free-form direction such as
  `[shouting, enraged]` without speaking it (the transcript came back clean), and they
  replaced three sibling voices with one voice directed three ways. That conflicts
  with V-20, where the undocumented `[urgent]` caused paraphrase 0/8. The two
  measurements may be on different model builds.
  - Before relying on it, re-measure `[urgent]` and one multi-word direction on this
    bank, gated.
  - V-21's sibling voices stay until then, and the owner judges by ear (V-9).
  - If tags are adopted, the gate still compares the BANK text. `norm()` already drops
    `[...]`, so a tagged render and its subtitle compare correctly.
- **Knobs alone do not direct.** Their owner compared knob-only takes against directed
  ones: "knobs only doesn't work, all the rest is perfect". Stability 0.5 flattened the
  voice.
- **`[pause]` genuinely pauses** (by ear). Short sentences written as beats get run
  together without it.
- **A multi-word ALL-CAPS run lets the model choose the stressed word, and it chose
  wrong.** A single capitalised word is unambiguous.
- **Calibrate before bulk.** Render one line for each distinct risk before the batch:
  swear forms, homographs, the Fallout lexicon, emphasis, pacing. One softened swear
  word would have been wrong across 485 lines at once.

