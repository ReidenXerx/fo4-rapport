#!/usr/bin/env python3
"""
Rebuild the voice fallback (V-25) end to end - the ONE command to run after
adding content:

    python scripts/rebuild-voices.py            # incremental: only new voices are fingerprinted
    python scripts/rebuild-voices.py --full     # re-fingerprint everything (~4 min on the GPU)
    python scripts/rebuild-voices.py --dry-run  # show what would change, write nothing

WHEN TO RUN IT
  - a voice type was RENDERED (new folder under voice/out/): it stops borrowing
    and starts speaking as itself, and other voices may now map to it;
  - the game's voice archives or masters changed (a DLC was added);
  - voice/fallback/overrides.json was edited (the owner's picks by ear).
It does NOT need rerunning when LINES are added: the map is about voices, and a
new line is spoken by whichever voice the map already chose.

THE STEPS, each one's output the next one's input:
    1 voice-inventory.py     voice types, their recordings, what we rendered
    2 voice-races.py         races + sexes of the NPCs using each voice
    3 voice-similarity.py    decode, fingerprint (.venv-voice), measured map
    4 build-voices-table.py  map + owner overrides -> data/.../voices.json
Then it prints WHAT CHANGED against the table it replaced, because a rebuild
that silently moves Piper to a different voice is a change the owner should
hear about, not discover.

The fingerprinting environment (.venv-voice: torch + speechbrain, ~3 GB) is
created on first use. Everything else runs on the ordinary Python.
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENV = ROOT / ".venv-voice"
VPY = VENV / "Scripts" / "python.exe"
TABLE = ROOT / "data/F4SE/Plugins/Rapport/voices.json"


def step(title, cmd):
    print(f"\n== {title}", flush=True)
    r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode != 0:
        sys.exit(f"STOPPED at '{title}' (exit {r.returncode}) - nothing after it ran, "
                 f"and voices.json is unchanged unless this was the last step")


def ensure_venv():
    if VPY.exists():
        return
    print("== creating .venv-voice (first use: torch + speechbrain, a few minutes)")
    subprocess.run([sys.executable, "-m", "venv", str(VENV)], check=True)
    pip = [str(VPY), "-m", "pip", "install", "-q"]
    subprocess.run(pip + ["--upgrade", "pip"], check=True)
    subprocess.run(pip + ["torch", "torchaudio", "--index-url", "https://download.pytorch.org/whl/cu121"], check=True)
    subprocess.run(pip + ["speechbrain", "soundfile", "numpy"], check=True)


def pairs(path):
    if not path.exists():
        return {}
    t = json.loads(path.read_text(encoding="utf-8"))
    own = {o["voice"]: "(own voice)" for o in t.get("own", [])}
    return own | {b["voice"]: (b["as"]["voice"] if b.get("as") else "(silent)") for b in t.get("borrow", [])}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--full", action="store_true", help="re-fingerprint every voice, ignoring the cache")
    ap.add_argument("--dry-run", action="store_true", help="rebuild into a scratch copy and report only")
    a = ap.parse_args()

    before_path = TABLE.with_suffix(".before.json")
    if TABLE.exists():
        shutil.copy2(TABLE, before_path)
    before = pairs(before_path)

    ensure_venv()
    step("1/4 inventory", [sys.executable, "scripts/voice-inventory.py"])
    step("2/4 races and sexes", [sys.executable, "scripts/voice-races.py"])
    step("3/4 fingerprints and map", [str(VPY), "scripts/voice-similarity.py"] + (["--full"] if a.full else []))
    step("4/4 runtime table", [sys.executable, "scripts/build-voices-table.py"])

    after = pairs(TABLE)
    changed = sorted(n for n in set(before) | set(after) if before.get(n) != after.get(n))
    print(f"\n== {len(changed)} voice(s) changed")
    for n in changed[:40]:
        print(f"  {n:40s} {before.get(n, '(new)'):24s} -> {after.get(n, '(gone)')}")
    if len(changed) > 40:
        print(f"  ... and {len(changed) - 40} more")

    if a.dry_run and before_path.exists():
        shutil.copy2(before_path, TABLE)
        print("\ndry run: voices.json restored; nothing written")
    before_path.unlink(missing_ok=True)
    if changed and not a.dry_run:
        print("\nvoices.json changed - rebuild nothing else; deploy it (a NEW file needs Vortex Deploy), "
              "and listen to any named character above before shipping (V-9).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
