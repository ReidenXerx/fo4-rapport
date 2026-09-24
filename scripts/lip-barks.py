#!/usr/bin/env python3
"""
Give every line Rapport ships its lip sync.

The owner, 2026-09-24: "we should have lipsync everywhere / our mod is first class
not a slop". This supersedes V-5 (docs/VOICE-GUIDELINES.md). Every one of the 6,656
.fuz files Rapport had shipped carried a lip block of length 0, so a line played
with nothing for the engine to animate. The anatomy session saw the engine's lip
object run for 4.2 s and 3.1 s on two held barks while the lip layer (+0xF0) never
moved.

    python scripts/lip-barks.py              # build what is missing or stale
    python scripts/lip-barks.py --check      # report only, write nothing
    python scripts/lip-barks.py --jobs 4     # workers, for a trial only (default 1, see below)

For each line package-voice.py ships (tools/make_dialogue.load()) and each voice
type in voice/out:

    voice/out/<VT>/<line>.fuz      the curated render: FUZE, lip 0, xWMA
      -> its xWMA, decoded by the game's own xwmaencode to a 44.1 kHz mono 16-bit wav
      -> the game's own LipGenerator(wav, the line as spoken) -> .lip
      -> build/voice-lip/<VT>/<line>.fuz = FUZE header + that .lip + the SAME xWMA

The audio is never re-encoded. What plays stays byte-identical to the render that
passed the transcription gate; only the lip block is added. (Overture's
stage-voice.py re-encodes from voice/pcm instead, and a pcm older than its render
would then ship old audio.) The lip is timed against the decoded audio, which
includes the codec's own ~46 ms of padding, so it lines up with what is heard.

A stamp beside each output (<line>.json: sha1 of the source .fuz and the text)
makes a rerun skip what is current. A re-rendered line or a reworded one rebuilds.

LipGenerator writes tmp16khz.wav into its OWN folder, so two runs from one folder
would share that file (fo4-overture/scripts/make-lip.py). Each worker therefore
gets a private copy of the folder (the exe and FonixData.cdf, about 8 MB).

ONE AT A TIME by default, and on purpose. LipGenerator is not deterministic: the
same wav and text gave 3,433 / 3,455 / 3,312 / 3,470 bytes over four runs, and two
sequential passes over 60 lines disagreed on the frame count for 33 of them
(measured 2026-09-24). Parallel runs cannot be checked for cross-talk by
comparison, then: a lip from the wrong line would look like ordinary variation.
The private folder removes the one shared file we know of; one at a time removes
the question. 6,656 files take about half an hour. --jobs is there for a trial.
"""
import argparse
import concurrent.futures
import hashlib
import json
import multiprocessing.util
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import make_dialogue  # noqa: E402

OUT = ROOT / "voice" / "out"
WORK = ROOT / "build" / "voice-lip"

GAME_ROOTS = [
    pathlib.Path(r"D:\GOGGames\Fallout 4 GOTY"),
    pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\Fallout 4"),
]

# Set once per worker process by _init.
_LIPGEN: pathlib.Path | None = None
_XWMA: pathlib.Path | None = None
_SCRATCH: pathlib.Path | None = None


def find_tools(lipgen_arg, xwma_arg):
    """Explicit paths win; otherwise the first game install that has both."""
    if lipgen_arg or xwma_arg:
        pairs = [(pathlib.Path(lipgen_arg or ""), pathlib.Path(xwma_arg or ""))]
    else:
        pairs = [(g / "Tools/LipGen/LipGenerator/LipGenerator.exe", g / "Tools/Audio/xwmaencode.exe")
                 for g in GAME_ROOTS]
    for lipgen, xwma in pairs:
        if lipgen.is_file() and xwma.is_file():
            return lipgen, xwma
    sys.exit("LipGenerator.exe / xwmaencode.exe not found - they ship with the game under Tools/.\n"
             "Pass BOTH --lipgen and --xwma if your install is elsewhere.")


def _init(lipgen_src: str, xwma: str):
    """A private LipGenerator folder per worker: it writes tmp16khz.wav beside itself."""
    global _LIPGEN, _XWMA, _SCRATCH
    _SCRATCH = pathlib.Path(tempfile.mkdtemp(prefix="rapport-lip-"))
    # A pool worker leaves through multiprocessing's own exit path, not atexit.
    multiprocessing.util.Finalize(None, shutil.rmtree, args=(str(_SCRATCH), True), exitpriority=10)
    src_dir = pathlib.Path(lipgen_src).parent
    mine = _SCRATCH / "LipGenerator"
    mine.mkdir()
    for name in ("LipGenerator.exe", "FonixData.cdf"):
        shutil.copy2(src_dir / name, mine / name)
    _LIPGEN = mine / "LipGenerator.exe"
    _XWMA = pathlib.Path(xwma)


def stamp_of(src_bytes: bytes, text: str) -> dict:
    return {"src_sha1": hashlib.sha1(src_bytes).hexdigest(), "text": text}


def is_current(dst: pathlib.Path, stamp: dict) -> bool:
    meta = dst.with_suffix(".json")
    if not dst.is_file() or not meta.is_file():
        return False
    try:
        return json.loads(meta.read_text(encoding="utf-8")) == stamp
    except (OSError, ValueError):
        return False


def build_one(vt: str, lid: str, text: str) -> tuple[str, str, str]:
    """Returns (vt, lid, "" on success or the reason it failed)."""
    src = OUT / vt / f"{lid}.fuz"
    dst = WORK / vt / f"{lid}.fuz"
    data = src.read_bytes()
    if data[:4] != b"FUZE" or len(data) < 12:
        return vt, lid, "source is not a FUZE file"
    _, lip_len = struct.unpack_from("<II", data, 4)
    xwm = data[12 + lip_len:]
    if xwm[:4] != b"RIFF" or xwm[8:12] != b"XWMA":
        return vt, lid, "source audio is not xWMA"

    work = _SCRATCH / "job"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir()
    (work / "a.xwm").write_bytes(xwm)
    r = subprocess.run([str(_XWMA), str(work / "a.xwm"), str(work / "a.wav")], capture_output=True, text=True)
    wav_path = work / "a.wav"
    # The artifact, not the exit code: xwmaencode has exited 0 having written nothing.
    if not wav_path.is_file():
        return vt, lid, f"xwmaencode wrote no wav: {(r.stderr or r.stdout).strip()[:160]}"
    with wave.open(str(wav_path)) as w:
        shape = (w.getframerate(), w.getnchannels(), w.getsampwidth())
    if shape != (44100, 1, 2):
        return vt, lid, f"decoded audio is {shape}, LipGenerator wants 44100 Hz mono 16-bit"

    # The text drives the phoneme alignment: the line as SPOKEN, which for these
    # renders is the bank text itself (the transcription gate matched them to it).
    r = subprocess.run([str(_LIPGEN), str(wav_path), text, "-Language:USEnglish"],
                       capture_output=True, text=True, cwd=str(_LIPGEN.parent))
    lip_path = work / "a.lip"
    if not lip_path.is_file() or lip_path.stat().st_size == 0:
        return vt, lid, f"LipGenerator wrote no .lip: {(r.stderr or r.stdout).strip()[:160]}"
    lip = lip_path.read_bytes()

    out = b"FUZE" + struct.pack("<II", 1, len(lip)) + lip + xwm
    dst.parent.mkdir(parents=True, exist_ok=True)
    part = dst.with_suffix(".part")
    part.write_bytes(out)
    os.replace(part, dst)
    dst.with_suffix(".json").write_text(json.dumps(stamp_of(data, text)), encoding="utf-8")
    return vt, lid, ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--lipgen", default=None)
    ap.add_argument("--xwma", default=None)
    ap.add_argument("--only", default=None, help="one voice type, for a trial")
    a = ap.parse_args()

    text_of = {ln["id"]: ln["text"] for ln, _info, _topic in make_dialogue.load()}
    jobs, current = [], 0
    for vt_dir in sorted(OUT.iterdir()):
        if not vt_dir.is_dir() or vt_dir.name.startswith(".") or (a.only and vt_dir.name != a.only):
            continue
        for lid, text in text_of.items():
            src = vt_dir / f"{lid}.fuz"
            if not src.is_file():
                continue
            if is_current(WORK / vt_dir.name / f"{lid}.fuz", stamp_of(src.read_bytes(), text)):
                current += 1
            else:
                jobs.append((vt_dir.name, lid, text))

    print(f"lines          : {len(text_of)}")
    print(f"current        : {current:,}")
    print(f"to build       : {len(jobs):,}")
    if a.check or not jobs:
        return 1 if (a.check and jobs) else 0

    lipgen, xwma = find_tools(a.lipgen, a.xwma)
    failed = []
    done = 0
    with concurrent.futures.ProcessPoolExecutor(
            max_workers=a.jobs, initializer=_init, initargs=(str(lipgen), str(xwma))) as pool:
        for vt, lid, why in pool.map(build_one, *zip(*jobs), chunksize=8):
            done += 1
            if why:
                failed.append(f"{vt}/{lid}: {why}")
            if done % 500 == 0:
                print(f"  {done:,}/{len(jobs):,}", flush=True)

    print(f"built          : {len(jobs) - len(failed):,}")
    print(f"FAILED         : {len(failed)}")
    for f in failed[:20]:
        print(f"    {f}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
