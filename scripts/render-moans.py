#!/usr/bin/env python3
"""
Render the scene-moan bank (R-29): 8 characters (sex x persona) x 6 kinds, several takes each.

    python scripts/render-moans.py              # render what is missing or stale
    python scripts/render-moans.py --dry-run    # list what would render, spend nothing
    python scripts/render-moans.py --only female_romantic

THE RECIPE (owner, 2026-10-01; voice/moan-voices.json "recipe" and "settled", R-29):
  - eleven_v3, stability 0.2, style 0.6, pcm_44100, a fixed seed per take (V-11);
  - ONE direction at the front -- the persona's settled one -- then ONE continuous breath group
    with every vowel wrapped in breath (hhhaaahhh), commas and ellipses only. A tag between
    syllables starts a new utterance with a hard attack: the flat, plainly spoken "A";
  - the voice is the sex's (Rapport_FemaleEvenToned / Rapport_MaleEvenToned): R-29 keys the
    bank by sex and persona, never by voice type.

NO TRANSCRIPTION GATE. V-1's gate checks words, and a moan has none: speech-to-text cannot verify
it (dogmeat-mod's measured scar: a transcript is blind to performance). The owner's ear is the only
verifier, and he approved the calibration set; this script renders the same recipe.

Every take is edge-trimmed (silence below -45 dB) and written as 16-bit mono 44.1 kHz WAV, the
format two installed moan packs ship loose and play (AAF_DR_creature_pack, rxl_bp70_animations).

Output: voice/out/_moans/<sex>_<persona>/<kind>_<nn>.wav  (the voice repository, fo4-rapport-voice)
Manifest: voice/moans.json -- per take its text, seed, settings and trimmed length, and the hash of
the inputs it was rendered from, so a changed text or seed re-renders and nothing else does.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "voice/out/_moans"
MANIFEST = ROOT / "voice/moans.json"
TTS = "https://api.elevenlabs.io/v1/text-to-speech"
MODEL = "eleven_v3"
SETTINGS = {"stability": 0.2, "similarity_boost": 0.75, "style": 0.6, "use_speaker_boost": True}

VOICES = {"female": "jEN8cFKDy6r0XgCD3czG", "male": "EIls1au8ZummeVlcSZqp"}
# The settled directions, split so a kind can swap the noun: "[tender, trembling breathless moaning]".
PERSONAS = {
    "romantic": ("tender, trembling", "moaning"),
    "reticent": ("shy, stifled", "whimpering"),
    "vulgar": ("desperate, greedy", "moaning"),
    "mercantile": ("composed, quietly pleased", "moaning"),
}
# kind -> (noun override or None, [texts, one per take]). The texts are breath groups: no bare vowel,
# no full stop, no tag inside.
KINDS = {
    "breath": (None, ["hhhhh, hhhaaahhh...", "hhhh... mmmhhh, hhhhh...",
                      "hhhaaahhh... hhhhh...", "mmmhhh... hhhhh, hhhaaahh..."]),
    "short": (None, ["hhhaaahhh...", "mmmhhhaaahhh...", "hhhaaaahh, hhh...",
                     "hhhoooh...", "mmmhh, hhhaaahh...", "hhhaaahhh, mmmh..."]),
    "medium": (None, ["hhhaaahhh, mmmhhhaaahhh...", "hhhaaahhh, hhhaaaahhh...", "mmmhhh, hhhaaahhhh...",
                      "hhhoooh, hhhaaahhh...", "mmmhhhaaahhh, hhhaaahh...", "hhhaaahhh... hhhooohhh..."]),
    "long": (None, ["hhhaaahhh, hhhaaahhhh... mmmhhhaaahhh, hhhaaaaahhhh...",
                    "mmmhhhaaahhh, hhhaaahhh... hhhaaahhhh, mmmhhh, hhhaaaahhh...",
                    "hhhaaahhh, hhhooohhh... mmmhhhaaahhh, hhhaaahhhh, hhhaaaahhh...",
                    "mmmhhh, hhhaaahhh... hhhaaaahhh, hhhooohhh... mmmhhhaaahhh..."]),
    "impact": ("gasping", ["hhhAAAHhh!", "hhAAH, hhhh...", "hhhAAAH!", "hhhOOOHhh!"]),
    "climax": ("climaxing", ["hhhaaahhh, hhhaaahhhh, hhhaaaAAAHHHH, AAAAHHHHHH... hhhaaahhh, hhhaaahhh... mmmhhhhh...",
                             "hhhaaahhh, hhhaaaahhh, hhhAAAAAHHHH... AAAHHH, AAAAHHHHH... hhhaaahhh... hhhhh...",
                             "hhhaaahhh, hhhaaahhh, hhhaaahhh, hhhOOOHHHH, AAAAHHHHH... hhhaaahhh, mmmhhh..."]),
}
# Round 3 (owner, 2026-10-01: "i liked your painful pleasure"): PAINFUL PLEASURE for anal, rough and
# BDSM animations -- the persona's settled direction plus a pained edge, and a strained "nnngh" in
# the breath -- and DEEP GAGS, the receiver's throat on a deep blowjob stroke. Kind -> (direction
# suffix, [texts]); the suffix follows the persona's own "<adjectives> breathless <noun>".
PAIN_EDGE = {"vulgar": ", gasping through rough pain"}
PAIN_EDGE_DEFAULT = ", wincing with sweet pain"
PAIN_KINDS = {
    "pain_short": ["nnnhhhaaahhh...", "hhhaaahhh, nnngh...", "nnngh, hhhaaahh...",
                   "nnnhhh, hhhaaahh...", "hhhaaahh, nnnhhh...", "nnngh... hhhooohh..."],
    "pain_medium": ["nnnhhhaaahhh, hhhaaahhh... nnngh, hhhaaahhhh...", "hhhaaahhh, nnngh, hhhaaaahhh...",
                    "nnngh... hhhaaahhh, nnnhhhaaahhh...",
                    "nnnhhh, hhhaaahhh... hhhaaaahhh, nnngh...", "hhhaaahhh, nnnhhhaaahhh... nnngh...",
                    "nnngh, hhhooohhh... hhhaaahhh..."],
    "pain_long": ["nnnhhhaaahhh, hhhaaahhh... nnngh, hhhaaahhhh, hhhaaaahhh... nnnhhh, hhhaaaahhhh...",
                  "hhhaaahhh, nnngh... hhhaaahhhh, nnnhhhaaahhh... nnngh, hhhaaaaahhhh...",
                  "nnngh, hhhaaahhh... nnnhhhaaahhh, hhhaaahhhh... nnngh, hhhaaahhh, hhhaaaahhh...",
                  "hhhaaahhh, nnnhhh... hhhaaaahhh, nnngh, hhhooohhh... nnnhhhaaahhh..."],
    "pain_impact": ["nnnhhAAAHhh!", "hhAAH, nnngh...", "nnngh, hhhAAAH!", "nnnhhOOOHhh!"],
}
# The GAG on a deep stroke is a real GULP, not a voice: the owner (2026-10-01) rejected a spoken "G",
# found that no-consonant swallows lost the gulp entirely, and picked three ElevenLabs sound effects
# by ear ("i like 3 5 6 NEW gulps"). A swallow is the throat's own sound, so every character gets
# the same three, copied (edge-trimmed, 16-bit mono WAV) rather than rendered.
GAG_SFX = ["voice/audition-moans/gulp/sfx_gulp-3-water.mp3",
           "voice/audition-moans/gulp/sfx_gulp-5-pill.mp3",
           "voice/audition-moans/gulp/sfx_gulp-6-throat.mp3"]


def api_key() -> str:
    k = os.environ.get("ELEVENLABS_API_KEY", "").strip()
    if k:
        return k
    p = pathlib.Path.home() / ".elevenlabs/api-key"
    if p.exists():
        return p.read_text(encoding="utf-8").splitlines()[0].strip()
    sys.exit("no ELEVENLABS_API_KEY and no ~/.elevenlabs/api-key")


def tts(key: str, voice: str, text: str, seed: int) -> bytes:
    body = json.dumps({"text": text, "model_id": MODEL, "seed": seed, "voice_settings": SETTINGS}).encode()
    req = urllib.request.Request(f"{TTS}/{voice}?output_format=pcm_44100", data=body,
                                 headers={"xi-api-key": key, "Content-Type": "application/json"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code in (429, 500, 502, 503) and attempt < 3:
                time.sleep(4 * (attempt + 1))
                continue
            raise SystemExit(f"TTS failed: HTTP {e.code} {e.read()[:200]!r}")
    raise SystemExit("TTS failed after retries")


def wav(pcm: bytes) -> bytes:
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 1, 1, 44100, 88200, 2, 16) +
            b"data" + struct.pack("<I", len(pcm)) + pcm)


def trim(src: pathlib.Path, dst: pathlib.Path) -> float:
    """Edge silence off (-45 dB), the calibration's exact filter; returns the trimmed length."""
    filt = ("silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.05,areverse,"
            "silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.12,areverse")
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(src), "-af", filt,
                    "-ac", "1", "-ar", "44100", "-c:a", "pcm_s16le", str(dst)], check=True)
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(dst)],
                         check=True, capture_output=True, text=True).stdout.strip()
    return round(float(out), 3)


def kinds_for(persona, adjectives, noun):
    """(kind, direction, texts) for one character: the base kinds, then round 3's."""
    for kind, (kind_noun, texts) in KINDS.items():
        yield kind, f"[{adjectives} breathless {kind_noun or noun}]", texts
    edge = PAIN_EDGE.get(persona, PAIN_EDGE_DEFAULT)
    for kind, texts in PAIN_KINDS.items():
        yield kind, f"[{adjectives} breathless {noun}{edge}]", texts
    for sfx in GAG_SFX:
        yield "gag", "sfx", [sfx]


# Loudness per kind (owner, 2026-10-01, from his first test: "barely listenable", drastically louder).
# Integrated LUFS, true peak capped at -1 dBTP so nothing clips. A breath stays softer than a moan
# and the climax the loudest, so the scene keeps its shape. Measured before: moans about -13,
# breath -19, gulps -26 (the real culprit there), impacts and climaxes -9.
LOUDNESS = {"breath": -15.0, "short": -11.0, "medium": -11.0, "long": -11.0, "impact": -10.0,
            "climax": -9.0, "pain_short": -11.0, "pain_medium": -11.0, "pain_long": -11.0,
            "pain_impact": -10.0, "gag": -12.0}
TRUE_PEAK = -1.0


def normalize(path: pathlib.Path, kind: str) -> float:
    """Two-pass EBU R128 loudnorm to the kind's target; returns the target it now meets."""
    target = LOUDNESS[kind]
    probe = subprocess.run(["ffmpeg", "-hide_banner", "-i", str(path), "-af",
                            f"loudnorm=I={target}:TP={TRUE_PEAK}:LRA=11:print_format=json", "-f", "null", "-"],
                           check=True, capture_output=True, text=True).stderr
    m = json.loads(probe[probe.rindex("{"):probe.rindex("}") + 1])
    if kind == "gag":
        # A gulp is one sub-second transient: linear gain hits the peak cap first (measured -19.4 LUFS
        # against -12), and loudnorm's dynamic mode needs ~3 s of audio, so it falls back to linear.
        # So: the gain it is short by, and a peak limiter at -1 dB catching the click.
        gain = target - float(m["input_i"])
        filt = f"volume={gain:.2f}dB,alimiter=limit={10 ** (TRUE_PEAK / 20):.3f}:attack=1:release=40:level=false"
        tmp = path.with_suffix(".norm.wav")
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(path), "-af", filt,
                        "-ac", "1", "-ar", "44100", "-c:a", "pcm_s16le", str(tmp)], check=True)
        tmp.replace(path)
        return target
    filt = (f"loudnorm=I={target}:TP={TRUE_PEAK}:LRA=11:measured_I={m['input_i']}:measured_TP={m['input_tp']}:"
            f"measured_LRA={m['input_lra']}:measured_thresh={m['input_thresh']}:offset={m['target_offset']}:"
            # A gulp is one transient: linear gain hits the peak cap first (measured -19.4 LUFS against
            # a -12 target), so it alone is levelled DYNAMICALLY, which compresses the click to lift the body.
            f"linear={'false' if kind == 'gag' else 'true'}")
    tmp = path.with_suffix(".norm.wav")
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(path), "-af", filt,
                    "-ac", "1", "-ar", "44100", "-c:a", "pcm_s16le", str(tmp)], check=True)
    tmp.replace(path)
    return target


def jobs():
    takes_seen = {}
    for sex, voice in VOICES.items():
        for persona, (adjectives, noun) in PERSONAS.items():
            for kind, direction, texts in kinds_for(persona, adjectives, noun):
                for body in texts:
                    n = takes_seen[(sex, persona, kind)] = takes_seen.get((sex, persona, kind), 0) + 1
                    clip = f"{sex}_{persona}/{kind}_{n:02d}"
                    if direction == "sfx":
                        # A copied sound effect: its inputs are the source file's bytes.
                        src = ROOT / body
                        inputs = hashlib.sha256(b"sfx" + src.read_bytes()).hexdigest()
                        yield {"id": clip, "sex": sex, "persona": persona, "kind": kind, "take": n,
                               "voice": None, "text": f"sfx:{body}", "seed": 0, "inputs": inputs, "sfx": src}
                        continue
                    text = f"{direction} {body}"
                    seed = int(hashlib.sha256(clip.encode()).hexdigest()[:8], 16) % 4294967295
                    inputs = hashlib.sha256(json.dumps([voice, text, seed, MODEL, SETTINGS], sort_keys=True).encode()).hexdigest()
                    yield {"id": clip, "sex": sex, "persona": persona, "kind": kind, "take": n,
                           "voice": voice, "text": text, "seed": seed, "inputs": inputs}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8")) if MANIFEST.exists() else {"takes": {}}
    takes = manifest.setdefault("takes", {})
    # Loudness first, on every take already rendered whose recorded target differs from LOUDNESS:
    # a changed target re-levels the files without re-rendering a single one.
    relevel = [(tid, t) for tid, t in takes.items()
               if t.get("lufs") != LOUDNESS.get(t["kind"]) and (OUT / f"{tid}.wav").exists()]
    if relevel and not args.dry_run:
        print(f"re-levelling {len(relevel)} take(s) to their kind's loudness")
        for i, (tid, t) in enumerate(relevel, 1):
            dst = OUT / f"{tid}.wav"
            t["lufs"] = normalize(dst, t["kind"])
            t["sha256"] = hashlib.sha256(dst.read_bytes()).hexdigest()
            if i % 50 == 0:
                print(f"  {i}/{len(relevel)}")
        MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    todo = [j for j in jobs() if (not args.only or j["id"].startswith(args.only))]
    stale = [j for j in todo if takes.get(j["id"], {}).get("inputs") != j["inputs"]
             or not (OUT / f"{j['id']}.wav").exists()]
    print(f"{len(todo)} takes in scope, {len(stale)} to render")
    if args.dry_run or not stale:
        for j in stale[:12]:
            print("  would render", j["id"], "|", j["text"][:70])
        return 0
    key = api_key() if any("sfx" not in j for j in stale) else None
    with tempfile.TemporaryDirectory() as tmp:
        for i, j in enumerate(stale, 1):
            dst = OUT / f"{j['id']}.wav"
            dst.parent.mkdir(parents=True, exist_ok=True)
            if "sfx" in j:
                seconds = trim(j["sfx"], dst)   # the approved effect itself, trimmed like every take
            else:
                raw = pathlib.Path(tmp) / "raw.wav"
                raw.write_bytes(wav(tts(key, j["voice"], j["text"], j["seed"])))
                seconds = trim(raw, dst)
            lufs = normalize(dst, j["kind"])
            takes[j["id"]] = {k: j[k] for k in ("sex", "persona", "kind", "take", "voice", "text", "seed", "inputs")}
            takes[j["id"]].update({"seconds": seconds, "lufs": lufs, "sha256": hashlib.sha256(dst.read_bytes()).hexdigest(),
                                   "model": None if "sfx" in j else MODEL,
                                   "settings": None if "sfx" in j else SETTINGS})
            # Written after every take: a failure later keeps everything already paid for.
            MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            print(f"  [{i}/{len(stale)}] {j['id']}  {seconds:.2f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
