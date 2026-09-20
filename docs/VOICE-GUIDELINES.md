# Voice guidelines — Fallout 4 voiceover

**Authoritative for every agent doing voice work on these mods.** Numbered `V-#` so
they can be cited the way `NS-#` and `GP-#` are. Each rule is measured, and the
evidence is stated — where a rule has no evidence line it is a preference, and it
says so.

Measured 2026-09-21 on ElevenLabs Pro against Fallout 4 GOTY (GOG).

---

## The model and the chain

### V-1 — Ship `eleven_multilingual_v2`. Never `eleven_v3`.

**Eleven v3 does not reliably say the words you send it.** Three renders per case,
each transcribed back with `scribe_v1` and compared against the input:

| case | verbatim |
| --- | --- |
| `eleven_multilingual_v2`, plain text | **3 / 3** |
| `eleven_v3`, plain text | **0 / 3** |
| `eleven_v3` + `[whispers]` | 1 / 3 |
| `eleven_v3` + `[whispers]` + CAPS/ellipses | 1 / 3 |
| `eleven_v3` + `[urgent]` | 0 / 3 |

Every v3 failure substituted words — "we **gotta** make this quick" came back as
"we **need to** make this quick", reproducibly, across seeds, and never once on
v2 with the same transcriber. Some of the noisier transcripts are ASR error on
whispered audio; that substitution is not.

These lines ship **with subtitles**, and the subtitle is the authored text. A
model that paraphrases produces a screen that disagrees with the audio. v3's
expressiveness does not buy that back.

### V-2 — Audio tags are therefore OUT.

`[whispers]`, `[sighs]`, `[laughs]` and the rest are a **v3-only** feature, and
V-1 rules out v3. Do not put bracket tags in v2 text — they are not interpreted,
and an uninterpreted tag risks being read aloud.

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
`scribe_v1` and compare against the authored line. It is the only reason V-1 was
caught rather than shipped.

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
| model | `eleven_multilingual_v2` |
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
