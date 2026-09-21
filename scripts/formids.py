#!/usr/bin/env python3
"""
The FormID registry: every voice line gets an id that NEVER changes.

    python scripts/formids.py            # assign ids to any new lines, report
    python scripts/formids.py --check    # fail if the registry would change

WHY THIS FILE EXISTS - the rule that makes adding lines free.

A dialogue line in the ESP is an INFO record with a FormID, and its voice file is
NAMED by that FormID: Sound/Voice/Rapport.esp/<VoiceType>/<FormID & 0xFFFFFF>_1.fuz.

The natural way to number records is in iteration order. Then inserting one line
in the middle of a bank shifts every line after it to a new id, and:

  - every .fuz after it is now named for the wrong line, so NPCs say the wrong
    thing - silently, with the correct subtitle over the wrong audio;
  - every existing save that referenced those INFOs now points at different ones;
  - nothing errors. It just plays wrong.

So a FormID is a function of the line's ID, never of its POSITION. This registry
is append-only:

  - a line keeps its id forever;
  - a new line takes the next unused id;
  - a DELETED line's id is retired and never handed out again, because a save
    made while it existed may still hold it.

It is committed to the repository and it is the source of truth. Regenerating the
ESP from scratch must reproduce the same ids, which is what --check proves.
"""
import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REGISTRY = ROOT / "voice/formids.json"

# The plugin's own records carry load-order index 0x01 here (Fallout4.esm is its
# one master). make_esp.py already owns 0x01000800-0x01000802 for the quests and
# the message, so voice lines start well clear of them.
FIRST = 0x01001000
LAST = 0x01FFFFFF

BANKS = ["voice/lines.json", "voice/overture-lines.json"]


def load_ids():
    ids = []
    for bank in BANKS:
        p = ROOT / bank
        if p.exists():
            ids += [l["id"] for l in json.loads(p.read_text(encoding="utf-8"))["lines"]]
    dupes = {i for i in ids if ids.count(i) > 1}
    if dupes:
        sys.exit(f"line ids are not unique across banks: {sorted(dupes)[:5]}")
    return ids


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if any line would be assigned a new id")
    a = ap.parse_args()

    reg = {"_": "Append-only. A line keeps its FormID forever; a deleted line's id is "
                "retired, never reused. Edit by running scripts/formids.py, never by hand.",
           "next": f"0x{FIRST:08X}", "assigned": {}, "retired": {}}
    if REGISTRY.exists():
        reg = json.loads(REGISTRY.read_text(encoding="utf-8"))

    assigned = reg["assigned"]
    retired = reg.setdefault("retired", {})
    nxt = int(reg["next"], 16)
    live = load_ids()

    new = [i for i in live if i not in assigned]
    gone = [i for i in assigned if i not in set(live)]

    if a.check:
        if new or gone:
            print(f"registry is STALE: {len(new)} new line(s), {len(gone)} removed")
            for i in new[:5]: print(f"   new     {i}")
            for i in gone[:5]: print(f"   removed {i}")
            return 1
        print(f"registry current: {len(assigned):,} lines, none new, none removed")
        return 0

    for i in gone:
        # Retire rather than free: a save made while this line existed can still
        # hold the id, and handing it to a different line would make that save
        # play the wrong audio.
        retired[i] = assigned.pop(i)

    used = {int(v, 16) for v in assigned.values()} | {int(v, 16) for v in retired.values()}
    for i in new:
        while nxt in used:
            nxt += 1
        if nxt > LAST:
            sys.exit("FormID space exhausted")
        assigned[i] = f"0x{nxt:08X}"
        used.add(nxt)
        nxt += 1

    reg["next"] = f"0x{nxt:08X}"
    REGISTRY.write_text(json.dumps(reg, indent=2, sort_keys=False), encoding="utf-8")
    print(f"assigned {len(assigned):,} | new {len(new):,} | retired {len(gone):,} "
          f"(total ever retired {len(retired):,}) | next {reg['next']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
