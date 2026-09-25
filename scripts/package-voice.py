#!/usr/bin/env python3
"""
Place every rendered bark where the engine will look for it.

    python scripts/package-voice.py <staging dir>      # e.g. D:\\Vortex\\fallout4\\mods\\Rapport-dev
    python scripts/package-voice.py <staging> --check  # report only, write nothing

Renders are named by LINE ID (voice/out/<VoiceType>/<line>.fuz) because the ESP
did not exist when they were made, and a FormID baked into an audio file name is
a FormID that can never change. Here, and only here, a line id becomes the path
the engine actually reads:

    Sound/Voice/Rapport.esp/<VoiceType>/<FormID & 0x00FFFFFF, 8 hex>_1.fuz

The load-order byte is masked - established from a mod archive, not vanilla, since
vanilla is always load order 00 and proves nothing (V-6). The folder is the plugin
file name INCLUDING its extension.

It removes stale files from its own folder (a line that was deleted, or renamed),
because a leftover .fuz named for a retired FormID is audio for a line that no
longer exists - harmless today, and the first thing to go wrong if that id were
ever reissued. The registry never reissues, but the folder should not depend on it.

LIP SYNC (owner, 2026-09-24: "we should have lipsync everywhere"; supersedes V-5).
What ships is build/voice-lip/<VoiceType>/<line>.fuz, made by
scripts/lip-barks.py: the same audio as voice/out plus a lip block. It is used only
while its stamp still names the current render and text. A line without a
current lip copy ships its bare render, so it still plays, and is COUNTED. The
count fails the run, as an unrendered line does, so a release cannot go out
silent-mouthed without saying so.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import make_dialogue  # noqa: E402

LIPPED = ROOT / "build" / "voice-lip"


def lipped_copy(src: pathlib.Path, vt: str, lid: str, text: str) -> pathlib.Path | None:
    """The lip-synced copy of this render, if lip-barks.py made one from THIS audio
    and THIS text. Same stamp as lip-barks.py writes."""
    dst = LIPPED / vt / f"{lid}.fuz"
    meta = dst.with_suffix(".json")
    if not dst.is_file() or not meta.is_file():
        return None
    try:
        stamp = json.loads(meta.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if stamp != {"src_sha1": hashlib.sha1(src.read_bytes()).hexdigest(), "text": text}:
        return None
    return dst


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("staging")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()

    staging = pathlib.Path(a.staging)
    entries = make_dialogue.load()                       # (line, info_id, topic_id)
    by_id = {ln["id"]: info for ln, info, _ in entries}
    text_of = {ln["id"]: ln["text"] for ln, _info, _ in entries}
    voice_root = staging / "Sound" / "Voice" / make_dialogue.PLUGIN

    wanted, missing_src, no_lip = {}, [], []
    types = json.loads((ROOT / "voice" / "voices.json").read_text(encoding="utf-8"))["types"]
    player_voices = {vt for vt, d in types.items() if d.get("player")}
    for vt_dir in sorted((ROOT / "voice/out").iterdir()):
        # voice/out is its own git repository now: .git (and any dot-folder) is not a
        # voice type, and walking it reported 205 "unrendered" lines that hid real ones.
        if not vt_dir.is_dir() or vt_dir.name.startswith("."):
            continue
        if not vt_dir.is_dir():
            continue
        # The PLAYER's voices (O-45) hold the player's own lines for an addon (Overture ships
        # them itself). The player never says a scene bark (R-11), so none of these is Rapport's
        # to ship, and every bark "missing" there is missing by design - as render-barks.py has it.
        if vt_dir.name in player_voices:
            continue
        for lid, info in by_id.items():
            src = vt_dir / f"{lid}.fuz"
            dst = staging / make_dialogue.voice_path(vt_dir.name, info)
            if src.exists():
                lipped = lipped_copy(src, vt_dir.name, lid, text_of[lid])
                if lipped is None:
                    no_lip.append(f"{vt_dir.name}/{lid}")
                wanted[dst] = lipped or src
            elif not lid.endswith(("_m", "_f")):
                # A gendered line is rendered for one gender only, so its absence
                # for the other is correct. Anything else missing is a gap.
                missing_src.append(f"{vt_dir.name}/{lid}")

    existing = set(voice_root.rglob("*.fuz")) if voice_root.exists() else set()
    stale = sorted(existing - set(wanted))
    # CONTENT, not size. A re-render can land on the same byte count as the file it
    # replaces, and a size check would then skip it - leaving the OLD audio deployed
    # while everything reports success. That was about to happen to the recast
    # FemaleEvenToned crowd lines (2026-09-21). Size is the cheap first filter;
    # equal sizes are settled by hashing.
    import hashlib
    def same(a, b):
        if a.stat().st_size != b.stat().st_size:
            return False
        return hashlib.md5(a.read_bytes()).digest() == hashlib.md5(b.read_bytes()).digest()
    todo = [d for d, s in wanted.items() if not d.exists() or not same(d, s)]

    print(f"voice types   : {len({d.parent.name for d in wanted})}")
    print(f"files wanted  : {len(wanted):,}")
    print(f"to copy       : {len(todo):,}")
    print(f"stale to drop : {len(stale):,}")
    if missing_src:
        print(f"NOT RENDERED  : {len(missing_src)} (these lines will be silent with a subtitle)")
        for m in missing_src[:5]:
            print(f"    {m}")
    if no_lip:
        print(f"NO LIP SYNC   : {len(no_lip)} (shipped bare - run scripts/lip-barks.py)")
        for m in no_lip[:5]:
            print(f"    {m}")
    if a.check:
        return 1 if (missing_src or no_lip) else 0

    for dst in todo:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(wanted[dst], dst)
    for s in stale:
        s.unlink()

    # Verify the result rather than the intent.
    have = set(voice_root.rglob("*.fuz"))
    wrong = [d for d in wanted if d not in have or not same(d, wanted[d])]
    print(f"\nin place      : {len(have):,}   wrong or missing: {len(wrong)}")
    # Unrendered lines fail the WRITE mode too: the release gate relies on this exit
    # code, and a line with no audio is exactly what it exists to stop. So does a
    # line with no lip sync, since 2026-09-24.
    return 1 if (wrong or missing_src or no_lip) else 0


if __name__ == "__main__":
    raise SystemExit(main())
