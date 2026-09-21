#!/usr/bin/env python3
"""
Render every R-9 bark for every chosen voice, straight to Fallout 4 .fuz.

    python scripts/render-barks.py --dry-run
    python scripts/render-barks.py
    python scripts/render-barks.py --only DLC04GangDiscipleFemale01
    python scripts/render-barks.py --force

Reads voice/lines.json, voice/voices.json and both safety files. A voice type
with "chosen": null is SKIPPED - picking a voice is done by ear (V-9) and this
script will not guess one.

NEITHER MODEL IS VERBATIM-SAFE. Measured per line on this bank, eleven_v3 gets
28/36 and eleven_multilingual_v2 gets 16/36 - and they fail on DIFFERENT lines.
An earlier version of this script trusted v2 blind on the strength of three
samples of one line, and shipped drifted audio under subtitles that disagreed
with it.

So EVERY render is transcribed back and compared before it may become a .fuz.
The safety rates choose only which model to TRY FIRST; correctness comes from
the gate. If the preferred model cannot produce a verbatim take in `--tries`
seeds, the other model is tried, and if neither can, the line FAILS rather than
writing unverified audio - a line no model will say cannot ship under a subtitle
claiming it did.

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

# Observers are not talking to a partner, so they are not half-whispering to one.
# Alone: muttering to themselves, which is close to the pair setting. Crowd: they
# are talking PAST the pair to other people, sometimes across a settlement, so the
# whisper tag comes off entirely and the style goes up to push projection.
#
# CAVEAT worth knowing: every voice in this project was DESIGNED with "speaks low
# and close, half-whispered" in its brief, so intimacy is baked into the timbre
# and no per-render setting fully removes it. Dropping the tag gets most of the
# way; if crowd lines still sound too confiding, the fix is a second voice per
# type briefed to project, not a knob here.
AUDIENCE = {
    "alone": {"tag": "[whispers] ", "speed": 0.95, "stability": 0.35, "style": 0.3},
    "crowd": {"tag": "",            "speed": 1.00, "stability": 0.45, "style": 0.5},
}
BASE = {"similarity_boost": 0.75, "use_speaker_boost": True}


def api_key() -> str:
    k = os.environ.get("ELEVENLABS_API_KEY", "").strip()
    if k:
        return k
    f = pathlib.Path.home() / ".elevenlabs" / "api-key"
    if not f.exists():
        sys.exit("no ELEVENLABS_API_KEY and no ~/.elevenlabs/api-key")
    return f.read_text(encoding="utf-8").splitlines()[0].strip()


# Pronunciation variants of the SAME words. A voice saying "wanna" for "want to"
# is eliding, not substituting, and the subtitle is still correct - flagging it
# burns a re-roll on a good take. Real drift ("gotta" -> "need to") is different
# words and still fails, because "need to" is not in this map.
ELISION = [("wanna", "want to"), ("gonna", "going to"), ("gotta", "got to"),
           ("gimme", "give me"), ("lemme", "let me"), ("kinda", "kind of"),
           ("outta", "out of"), ("alright", "all right"), ("'em", "them"),
           ("'cause", "because"), ("cause", "because")]


def norm(s: str) -> str:
    """Compare meaning-bearing words only: tags, case, punctuation and elision
    are noise. STT is also not deterministic - the same audio transcribes
    slightly differently between passes, so the comparison has to tolerate the
    ways a word can be *said* while still catching a different word."""
    s = re.sub(r"\[[^\]]*\]", " ", s).lower()
    s = " ".join(re.sub(r"[^a-z' ]", " ", s).split())
    for short, full in ELISION:
        # A leading apostrophe has no word boundary before it -  needs a word
        # character on the left, and "'" is not one, so "'em" never matched.
        left = "" if short.startswith("'") else r"\b"
        s = re.sub(left + re.escape(short) + r"\b", full, s)
    return " ".join(s.split())


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
    # These rates decide which model to TRY FIRST. They do not decide
    # correctness - the runtime gate below does. Measured at n=8, a line whose
    # true rate is ~90% lands on 88% or 100% at random, so classifying lines by
    # these numbers was sorting noise.
    s3 = json.loads((ROOT / "voice/v3-safety.json").read_text(encoding="utf-8"))["rates"]
    s2 = json.loads((ROOT / "voice/v2-safety.json").read_text(encoding="utf-8"))["rates"]

    chosen = {vt: d["chosen"] for vt, d in types.items() if d.get("chosen")}
    if a.only:
        if a.only not in chosen:
            sys.exit(f"{a.only} has no chosen voice. Chosen: {', '.join(sorted(chosen)) or 'none'}")
        chosen = {a.only: chosen[a.only]}

    def voice_for(vt, ln):
        """Crowd observer lines use the PROJECTING voice; everything else is
        intimate. A heckle shouted across a settlement and a line murmured into
        somebody's ear are not the same performance, and the difference lives in
        the voice design, not in a per-render setting."""
        if ln.get("kind") == "observer" and ln.get("audience") == "crowd":
            loud = types[vt].get("chosen_loud")
            if loud:
                return loud, True
        return chosen[vt], False

    jobs = []
    for vt in sorted(chosen):
        for ln in lines:
            fuz = ROOT / "voice/out" / vt / f"{ln['id']}.fuz"
            if fuz.exists() and not a.force:
                continue
            vid, loud = voice_for(vt, ln)
            jobs.append((vt, vid, ln, fuz))

    expr = sum(1 for j in jobs if s3.get(j[2]["id"], 0) >= s2.get(j[2]["id"], 0))
    loud_n = sum(1 for j in jobs if voice_for(j[0], j[2])[1])
    chars = sum(len(j[2]["text"]) for j in jobs)
    print(f"voices chosen : {len(chosen)} of {len(types)}"
          f"   ({sum(1 for d in types.values() if d.get('chosen_loud'))} have a loud variant)")
    print(f"projecting    : {loud_n} crowd lines on the loud voice")
    print(f"to render     : {len(jobs)}   ({expr} trying {EXPR} first, "
          f"{len(jobs)-expr} trying {SAFE} first)")
    print(f"characters    : {chars:,}  (every render is transcribed and re-rolled on drift)")
    if a.dry_run or not jobs:
        return 0

    key = api_key()
    done, fellback, failed = [], [], []

    def one(job):
        vt, vid, ln, fuz = job
        # Pair barks are keyed by scenario; observer reactions by audience size.
        sc = (AUDIENCE[ln["audience"]] if ln.get("kind") == "observer"
              else SCENARIO[ln["scenario"]])
        pcm = ROOT / "voice/pcm" / vt / f"{ln['id']}.pcm"
        pcm.parent.mkdir(parents=True, exist_ok=True)
        fuz.parent.mkdir(parents=True, exist_ok=True)
        # Seed from the line id: stable across runs and distinct per line, so
        # re-rendering one line cannot change the others (V-11).
        base = zlib.crc32(ln["id"].encode()) & 0x7FFFFFF
        # Try the model measured better for THIS line first, then the other.
        # Neither model is verbatim-safe in general: v2 measured 16/36 and v3
        # 28/36 on this bank, and they fail on different lines.
        order = ([EXPR, SAFE] if s3.get(ln["id"], 0) >= s2.get(ln["id"], 0)
                 else [SAFE, EXPR])
        audio = used = None
        try:
            for model in order:
                tag = sc["tag"] if model == EXPR else ""
                stab = sc["stability"] if model == EXPR else 0.4
                for t in range(a.tries):
                    cand = tts(key, vid, tag + ln["text"], model, base + t * 977,
                               {**BASE, "stability": stab, "speed": sc["speed"],
                                "style": sc.get("style", 0.3)})
                    # EVERY render is verified, not just the expressive ones.
                    # The previous version trusted v2 blind and shipped drifted
                    # audio under a subtitle that disagreed with it.
                    if norm(stt(key, cand)) == norm(ln["text"]):
                        audio, used = cand, model
                        break
                if audio:
                    break
            if audio is None:
                # Refuse rather than write unverified audio. A line no model
                # will say cannot ship under a subtitle claiming otherwise.
                failed.append((vt, ln["id"], "no verbatim take in "
                               f"{a.tries * len(order)} attempts across both models"))
                return
            if used != order[0]:
                fellback.append((vt, ln["id"], used))
            pcm.write_bytes(audio)
            # --pcm: we asked the API for pcm_44100, so the format is not in
            # doubt and a sniff can only produce a false rejection.
            r = subprocess.run([sys.executable, str(ROOT / "scripts/pcm-to-fuz.py"),
                                str(pcm), str(fuz), "--pcm"], capture_output=True, text=True)
            # Check the artifact, not the exit code - xwmaencode has been seen
            # reporting success while writing nothing.
            if not fuz.exists() or fuz.stat().st_size == 0:
                failed.append((vt, ln["id"], (r.stderr or r.stdout).strip()[:160]))
                return
            done.append((vt, ln["id"], used))
        except Exception as e:
            failed.append((vt, ln["id"], str(e)[:160]))

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for n, _ in enumerate(ex.map(one, jobs), 1):
            if n % 50 == 0 or n == len(jobs):
                print(f"  {n}/{len(jobs)}  ({time.time()-t0:.0f}s)", flush=True)

    import collections as _c
    bym = _c.Counter(m for _, _, m in done)

    # Record which model actually produced each file. The renderer knew this and
    # threw it away, so the bark browser could only show which model was TRIED
    # first - and 16 files in one batch needed their second model.
    man_path = ROOT / "voice/render-manifest.json"
    man = {}
    if man_path.exists():
        man = json.loads(man_path.read_text(encoding="utf-8")).get("rendered", {})
    for vt, lid, model in done:
        man.setdefault(vt, {})[lid] = model
    man_path.write_text(json.dumps(
        {"_": "Which model actually produced each .fuz, after the STT gate and any "
              "fallback. Written by render-barks.py; absent entries were rendered "
              "before this was recorded.", "rendered": man}, indent=2), encoding="utf-8")
    print(f"  manifest : {sum(len(v) for v in man.values())} files recorded")
    print(f"\nrendered {len(done)}, failed {len(failed)}")
    print("  by model : " + ", ".join(f"{m} {n}" for m, n in bym.most_common()))
    print(f"  needed the SECOND model after {a.tries} drifting takes: {len(fellback)}")
    for vt, lid, m in fellback[:10]:
        print(f"      {vt}/{lid} -> {m}")
    if failed:
        print("  FAILED (nothing written - unverified audio is never shipped):")
        for vt, lid, why in failed[:10]:
            print(f"      {vt}/{lid}: {why}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
