# Voice guidelines — Fallout 4 voiceover

**Authoritative for every agent doing voice work on these mods.** Numbered `V-#` so
they can be cited the way `NS-#` and `GP-#` are. Each rule is measured, and the
evidence is stated — where a rule has no evidence line it is a preference, and it
says so.

Measured 2026-09-21 on ElevenLabs Pro against Fallout 4 GOTY (GOG).

---

## The model and the chain

### V-1 — v3 is allowed PER LINE, proven by measurement. Never globally.

**Corrected 2026-09-21. The first version of this rule said "never `eleven_v3`"
on the strength of n=3 renders of a single line, and it was wrong about the
magnitude.** Measured properly, v3's verbatim rate is about **76%**, not ~0%.

It is also not random. Drift is a property of the **line**, measured at n=8 to 10:

| line | verbatim |
| --- | --- |
| `Then stop talking.` | 10/10 |
| `I'm not going anywhere.` | 10/10 |
| `Lock it. I don't want to hear anybody.` | 3/10 |
| `Come on - we gotta make this quick.` | **0/10** |

Dissecting the 0/10 line found the trigger, and it is not what it looks like:

| variant | verbatim |
| --- | --- |
| `Come on - we gotta make this quick.` | 0% |
| `Come on, we gotta make this quick.` | 0% |
| `Come on - we **have to** make this quick.` | 0% |
| `Come on. We gotta make this quick.` | 50% |
| **`We gotta make this quick.`** | **100%** |

Not the punctuation, not the colloquialism — **the leading conversational
fragment**. v3 appears to read an opener like "Come on" as *direction* rather
than content and regenerates the clause after it. A colloquial/plain A-B across
8 matched line pairs found no difference at all (77% vs 75%), so "gotta" was
never the problem.

**The rule:** a line may render on v3, with audio tags, **only if it measures
100% over n≥8**. Otherwise it renders on `eleven_multilingual_v2`, where verbatim
is guaranteed. Rates live in `voice/v3-safety.json`; 28 of 36 lines currently
qualify.

These lines ship **with subtitles**, and the subtitle is the authored text — a
drifting render puts the screen and the audio into disagreement. The gate exists
for that, not for taste.

### V-1b — Never rewrite a good line to raise its score.

Of six rewrites attempted on drift-prone lines, two reached 100%, two improved
partway, and two stayed at **0%** — both of them the fragmentary hesitation lines
(`"...Yeah. Yeah, alright."`). Those are nearly contentless, which is exactly why
the model feels free to reinvent them, and exactly what makes them good writing.

A line that will not pass renders on v2 and keeps its words. **The TTS model does
not get a vote on the script.** Only rewrites that measured 100% were applied;
the rest of the bank stands as written.

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

---

## Settled numbers

| | |
| --- | --- |
| model | `eleven_multilingual_v2` by default; `eleven_v3` only on lines measured 100% (V-1) |
| output format | `pcm_44100` |
| voice settings | `stability 0.4`, `similarity_boost 0.75`, `style 0.3`, `use_speaker_boost true` |
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
