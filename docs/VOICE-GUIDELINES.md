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
`'em` never matches anything.

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

### V-6 — The voice file path, and the byte that would silently break it.

```
Sound/Voice/<Plugin.esp>/<VoiceType>/<FormID & 0x00FFFFFF, 8 hex>_<n>.fuz
```

Established from a **mod** archive, not vanilla — vanilla is always load order
`00` and therefore proves nothing. `LobotomitePack.esm` writes its FormIDs with
the load-order byte as `00`, masked at lookup, so filenames survive any load
order. Casing is inconsistent inside Bethesda's own archives, so lookup is
case-insensitive. The folder name **includes** the extension.

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

---

## Settled numbers

| | |
| --- | --- |
| model | per line, whichever measures better first; EVERY render gated by STT (V-1) |
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
