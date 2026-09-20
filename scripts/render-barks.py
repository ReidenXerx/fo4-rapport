#!/usr/bin/env python3
"""
Render every R-9 bark for every CHOSEN voice, straight to Fallout 4 .fuz.

    python scripts/render-barks.py --dry-run     # cost + what it would do
    python scripts/render-barks.py               # render what is missing
    python scripts/render-barks.py --only MaleBoston
    python scripts/render-barks.py --force       # re-render even if present

Reads voice/lines.json and voice/voices.json. A voice type with "chosen": null
is SKIPPED - picking a voice is done by ear, and this script will not guess one.

It calls the REST API rather than the MCP tools: 36 lines x 10 voices is 360
renders, which is a script's job. The MCP tools stay for auditioning one line.

Resumable by default - an existing .fuz is left alone, so a network failure
halfway through costs only what it had not already done. Output is named by
LINE ID, not by FormID: the ESP does not exist yet, and mapping line -> FormID
is a separate step that must not be baked into the audio.

Key: ELEVENLABS_API_KEY, or the first line of ~/.elevenlabs/api-key. Never printed.
"""
import argparse, json, os, pathlib, subprocess, sys, time, urllib.error, urllib.request
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parent.parent
API = "https://api.elevenlabs.io/v1/text-to-speech"
MODEL = "eleven_multilingual_v2"
SETTINGS = {"stability": 0.4, "similarity_boost": 0.75, "style": 0.3, "use_speaker_boost": True}


def api_key() -> str:
    k = os.environ.get("ELEVENLABS_API_KEY", "").strip()
    if k:
        return k
    f = pathlib.Path.home() / ".elevenlabs" / "api-key"
    if not f.exists():
        sys.exit("no ELEVENLABS_API_KEY and no ~/.elevenlabs/api-key")
    return f.read_text(encoding="utf-8").splitlines()[0].strip()


def tts(key: str, voice_id: str, text: str, tries: int = 4) -> bytes:
    body = json.dumps({"text": text, "model_id": MODEL, "voice_settings": SETTINGS}).encode()
    req = urllib.request.Request(
        f"{API}/{voice_id}?output_format=pcm_44100", data=body,
        headers={"xi-api-key": key, "Content-Type": "application/json"})
    for n in range(tries):
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            # 429 is rate limiting and 5xx is theirs; both are worth waiting out.
            # A 4xx that is not 429 is our bug and retrying just burns time.
            if e.code != 429 and e.code < 500:
                raise SystemExit(f"HTTP {e.code} for {voice_id}: {e.read()[:300].decode(errors='replace')}")
            if n == tries - 1:
                raise
            time.sleep(2 ** n)
        except urllib.error.URLError:
            if n == tries - 1:
                raise
            time.sleep(2 ** n)
    raise RuntimeError("unreachable")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--only", default=None, help="one voice type")
    ap.add_argument("--jobs", type=int, default=4)
    a = ap.parse_args()

    lines = json.loads((ROOT / "voice/lines.json").read_text(encoding="utf-8"))["lines"]
    types = json.loads((ROOT / "voice/voices.json").read_text(encoding="utf-8"))["types"]

    chosen = {vt: d["chosen"] for vt, d in types.items() if d.get("chosen")}
    skipped = [vt for vt, d in types.items() if not d.get("chosen")]
    if a.only:
        if a.only not in chosen:
            sys.exit(f"{a.only} has no chosen voice (or is not a known type). "
                     f"Chosen: {', '.join(sorted(chosen)) or 'none'}")
        chosen = {a.only: chosen[a.only]}

    jobs = []
    for vt, vid in sorted(chosen.items()):
        for ln in lines:
            fuz = ROOT / "voice/out" / vt / f"{ln['id']}.fuz"
            if fuz.exists() and not a.force:
                continue
            jobs.append((vt, vid, ln, fuz))

    chars = sum(len(j[2]["text"]) for j in jobs)
    print(f"voices chosen : {len(chosen)}  ({', '.join(sorted(chosen)) or 'none'})")
    if skipped:
        print(f"NOT auditioned: {len(skipped)}  ({', '.join(sorted(skipped))}) - skipped")
    print(f"lines         : {len(lines)}")
    print(f"to render     : {len(jobs)}  ({chars:,} characters -> ~{chars:,} credits)")
    if a.dry_run or not jobs:
        return 0

    key = api_key()
    done, failed = [], []

    def one(job):
        vt, vid, ln, fuz = job
        pcm = ROOT / "voice/pcm" / vt / f"{ln['id']}.pcm"
        pcm.parent.mkdir(parents=True, exist_ok=True)
        fuz.parent.mkdir(parents=True, exist_ok=True)
        try:
            pcm.write_bytes(tts(key, vid, ln["text"]))
            r = subprocess.run([sys.executable, str(ROOT / "scripts/pcm-to-fuz.py"),
                                str(pcm), str(fuz)], capture_output=True, text=True)
            # Check the artifact, not the exit code - xwmaencode has been seen
            # to report success while writing nothing.
            if not fuz.exists() or fuz.stat().st_size == 0:
                failed.append((vt, ln["id"], (r.stderr or r.stdout).strip()[:200])); return
            done.append((vt, ln["id"]))
        except Exception as e:
            failed.append((vt, ln["id"], str(e)[:200]))

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for n, _ in enumerate(ex.map(one, jobs), 1):
            if n % 20 == 0 or n == len(jobs):
                print(f"  {n}/{len(jobs)}", flush=True)

    print(f"\nrendered {len(done)}, failed {len(failed)}")
    for vt, lid, why in failed[:10]:
        print(f"  FAILED {vt}/{lid}: {why}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
