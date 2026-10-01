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
    "breath": (None, ["hhhhh, hhhaaahhh...", "hhhh... mmmhhh, hhhhh..."]),
    "short": (None, ["hhhaaahhh...", "mmmhhhaaahhh...", "hhhaaaahh, hhh..."]),
    "medium": (None, ["hhhaaahhh, mmmhhhaaahhh...", "hhhaaahhh, hhhaaaahhh...", "mmmhhh, hhhaaahhhh..."]),
    "long": (None, ["hhhaaahhh, hhhaaahhhh... mmmhhhaaahhh, hhhaaaaahhhh...",
                    "mmmhhhaaahhh, hhhaaahhh... hhhaaahhhh, mmmhhh, hhhaaaahhh..."]),
    "impact": ("gasping", ["hhhAAAHhh!", "hhAAH, hhhh...", "hhhAAAH!"]),
    "climax": ("climaxing", ["hhhaaahhh, hhhaaahhhh, hhhaaaAAAHHHH, AAAAHHHHHH... hhhaaahhh, hhhaaahhh... mmmhhhhh...",
                             "hhhaaahhh, hhhaaaahhh, hhhAAAAAHHHH... AAAHHH, AAAAHHHHH... hhhaaahhh... hhhhh..."]),
}


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


def jobs():
    for sex, voice in VOICES.items():
        for persona, (adjectives, noun) in PERSONAS.items():
            for kind, (kind_noun, texts) in KINDS.items():
                direction = f"[{adjectives} breathless {kind_noun or noun}]"
                for n, body in enumerate(texts, 1):
                    clip = f"{sex}_{persona}/{kind}_{n:02d}"
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
    todo = [j for j in jobs() if (not args.only or j["id"].startswith(args.only))]
    stale = [j for j in todo if takes.get(j["id"], {}).get("inputs") != j["inputs"]
             or not (OUT / f"{j['id']}.wav").exists()]
    print(f"{len(todo)} takes in scope, {len(stale)} to render")
    if args.dry_run or not stale:
        for j in stale[:12]:
            print("  would render", j["id"], "|", j["text"][:70])
        return 0
    key = api_key()
    with tempfile.TemporaryDirectory() as tmp:
        for i, j in enumerate(stale, 1):
            raw = pathlib.Path(tmp) / "raw.wav"
            raw.write_bytes(wav(tts(key, j["voice"], j["text"], j["seed"])))
            dst = OUT / f"{j['id']}.wav"
            dst.parent.mkdir(parents=True, exist_ok=True)
            seconds = trim(raw, dst)
            takes[j["id"]] = {k: j[k] for k in ("sex", "persona", "kind", "take", "voice", "text", "seed", "inputs")}
            takes[j["id"]].update({"seconds": seconds, "sha256": hashlib.sha256(dst.read_bytes()).hexdigest(),
                                   "model": MODEL, "settings": SETTINGS})
            # Written after every take: a failure later keeps everything already paid for.
            MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            print(f"  [{i}/{len(stale)}] {j['id']}  {seconds:.2f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
