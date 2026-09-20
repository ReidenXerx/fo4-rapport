#!/usr/bin/env python3
"""
Render every R-9 bark for every chosen voice, straight to Fallout 4 .fuz.

    python scripts/render-barks.py --dry-run
    python scripts/render-barks.py
    python scripts/render-barks.py --only DLC04GangDiscipleFemale01
    python scripts/render-barks.py --force

Reads voice/lines.json, voice/voices.json and voice/v3-safety.json. A voice type
with "chosen": null is SKIPPED - picking a voice is done by ear (V-9) and this
script will not guess one.

MODEL IS CHOSEN PER LINE (V-1). A line measured 100% verbatim on eleven_v3 gets
v3 and an audio tag, which is the only way to get emotional delivery at all.
Every other line gets eleven_multilingual_v2, where verbatim is guaranteed and
the line keeps its wording - the model does not get a vote on the script (V-1b).

EVERY v3 RENDER IS TRANSCRIBED BACK AND COMPARED before it is allowed to become
a .fuz. These lines ship with subtitles, so a render saying something other than
the authored text puts the screen and the audio into disagreement. A drifting
take is re-rolled on a new seed, and after `--tries` failures the line falls back
to v2 rather than shipping the wrong words.

Key: ELEVENLABS_API_KEY, or the first line of ~/.elevenlabs/api-key. Never printed.
"""
import argparse, json, os, pathlib, re, struct, subprocess, sys, time, urllib.error
import urllib.request, uuid, zlib
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parent.parent
TTS = "https://api.elevenlabs.io/v1/text-to-speech"
STT = "https://api.elevenlabs.io/v1/speech-to-text"
SAFE = "eleven_multilingual_v2"   # verbatim guaranteed
EXPR = "eleven_v3"                # expressive, gated per line by V-1

# Delivery per scenario. Only DOCUMENTED audio tags: an undocumented one was
# measured causing paraphrase, and an uninterpreted tag risks being read aloud.
# Lower stability = broader emotional range, which is where the tremble lives.
SCENARIO = {
    "quickie": {"tag": "[whispers] ", "speed": 1.10, "stability": 0.30},
    "athome":  {"tag": "[whispers] ", "speed": 0.95, "stability": 0.40},
    "tender":  {"tag": "[whispers] ", "speed": 0.92, "stability": 0.35},
}
BASE = {"similarity_boost": 0.75, "style": 0.3, "use_speaker_boost": True}


def api_key() -> str:
    k = os.environ.get("ELEVENLABS_API_KEY", "").strip()
    if k:
        return k
    f = pathlib.Path.home() / ".elevenlabs" / "api-key"
    if not f.exists():
        sys.exit("no ELEVENLABS_API_KEY and no ~/.elevenlabs/api-key")
    return f.read_text(encoding="utf-8").splitlines()[0].strip()


def norm(s: str) -> str:
    """Compare meaning-bearing words only: tags, case and punctuation are noise."""
    s = re.sub(r"\[[^\]]*\]", " ", s).lower()
    return " ".join(re.sub(r"[^a-z' ]", " ", s).split())


def _post(url: str, data: bytes, headers: dict, tries: int = 4) -> bytes:
    for n in range(tries):
        try:
            with urllib.request.urlopen(
                    urllib.request.Request(url, data=data, headers=headers), timeout=180) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            # 429 and 5xx are worth waiting out; any other 4xx is our bug, and
            # retrying it only burns time and credits.
            if e.code != 429 and e.code < 500:
                raise RuntimeError(f"HTTP {e.code}: {e.read()[:200].decode(errors='replace')}")
            if n == tries - 1:
                raise
            time.sleep(2 ** n)
        except urllib.error.URLError:
            if n == tries - 1:
                raise
            time.sleep(2 ** n)
    raise RuntimeError("unreachable")


def tts(key, voice_id, text, model, seed, settings) -> bytes:
    body = json.dumps({"text": text, "model_id": model, "seed": seed,
                       "apply_text_normalization": "off",      # V-12
                       "voice_settings": settings}).encode()
    return _post(f"{TTS}/{voice_id}?output_format=pcm_44100", body,
                 {"xi-api-key": key, "Content-Type": "application/json"})


def wav(pcm: bytes) -> bytes:
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 1, 1, 44100, 88200, 2, 16) +
            b"data" + struct.pack("<I", len(pcm)) + pcm)


def stt(key, pcm: bytes) -> str:
    bd = uuid.uuid4().hex
    m = ("--" + bd).encode()
    body = (m + b'\r\nContent-Disposition: form-data; name="model_id"\r\n\r\nscribe_v1\r\n' +
            m + b'\r\nContent-Disposition: form-data; name="file"; filename="a.wav"\r\n'
                b'Content-Type: audio/wav\r\n\r\n' + wav(pcm) + b"\r\n" + m + b"--\r\n")
    out = _post(STT, body, {"xi-api-key": key,
                            "Content-Type": "multipart/form-data; boundary=" + bd})
    return json.loads(out).get("text", "")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--only", default=None, help="one voice type")
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--tries", type=int, default=3, help="v3 re-rolls before falling back to v2")
    a = ap.parse_args()

    lines = json.loads((ROOT / "voice/lines.json").read_text(encoding="utf-8"))["lines"]
    types = json.loads((ROOT / "voice/voices.json").read_text(encoding="utf-8"))["types"]
    safety = json.loads((ROOT / "voice/v3-safety.json").read_text(encoding="utf-8"))["rates"]

    chosen = {vt: d["chosen"] for vt, d in types.items() if d.get("chosen")}
    if a.only:
        if a.only not in chosen:
            sys.exit(f"{a.only} has no chosen voice. Chosen: {', '.join(sorted(chosen)) or 'none'}")
        chosen = {a.only: chosen[a.only]}

    jobs = []
    for vt, vid in sorted(chosen.items()):
        for ln in lines:
            fuz = ROOT / "voice/out" / vt / f"{ln['id']}.fuz"
            if fuz.exists() and not a.force:
                continue
            jobs.append((vt, vid, ln, fuz))

    expr = sum(1 for j in jobs if safety.get(j[2]["id"], 0) == 100)
    chars = sum(len(j[2]["text"]) for j in jobs)
    print(f"voices chosen : {len(chosen)} of {len(types)}")
    print(f"to render     : {len(jobs)}   ({expr} with emotion on {EXPR}, "
          f"{len(jobs)-expr} verbatim-only on {SAFE})")
    print(f"characters    : {chars:,}  (plus re-rolls on drift)")
    if a.dry_run or not jobs:
        return 0

    key = api_key()
    done, fellback, failed = [], [], []

    def one(job):
        vt, vid, ln, fuz = job
        sc = SCENARIO[ln["scenario"]]
        emotional = safety.get(ln["id"], 0) == 100
        pcm = ROOT / "voice/pcm" / vt / f"{ln['id']}.pcm"
        pcm.parent.mkdir(parents=True, exist_ok=True)
        fuz.parent.mkdir(parents=True, exist_ok=True)
        # Seed from the line id: stable across runs and distinct per line, so
        # re-rendering one line cannot change the others (V-11).
        base_seed = zlib.crc32(ln["id"].encode()) & 0x7FFFFFF
        audio = None
        try:
            if emotional:
                for t in range(a.tries):
                    cand = tts(key, vid, sc["tag"] + ln["text"], EXPR, base_seed + t * 977,
                               {**BASE, "stability": sc["stability"], "speed": sc["speed"]})
                    if norm(stt(key, cand)) == norm(ln["text"]):
                        audio = cand
                        break
                if audio is None:
                    fellback.append((vt, ln["id"]))
            if audio is None:
                audio = tts(key, vid, ln["text"], SAFE, base_seed,
                            {**BASE, "stability": 0.4, "speed": sc["speed"]})
            pcm.write_bytes(audio)
            r = subprocess.run([sys.executable, str(ROOT / "scripts/pcm-to-fuz.py"),
                                str(pcm), str(fuz)], capture_output=True, text=True)
            # Check the artifact, not the exit code - xwmaencode has been seen
            # reporting success while writing nothing.
            if not fuz.exists() or fuz.stat().st_size == 0:
                failed.append((vt, ln["id"], (r.stderr or r.stdout).strip()[:160]))
                return
            done.append((vt, ln["id"]))
        except Exception as e:
            failed.append((vt, ln["id"], str(e)[:160]))

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for n, _ in enumerate(ex.map(one, jobs), 1):
            if n % 50 == 0 or n == len(jobs):
                print(f"  {n}/{len(jobs)}  ({time.time()-t0:.0f}s)", flush=True)

    print(f"\nrendered {len(done)}, failed {len(failed)}")
    print(f"fell back to {SAFE} after {a.tries} drifting v3 takes: {len(fellback)}")
    for vt, lid in fellback[:8]:
        print(f"    {vt}/{lid}")
    for vt, lid, why in failed[:8]:
        print(f"  FAILED {vt}/{lid}: {why}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
