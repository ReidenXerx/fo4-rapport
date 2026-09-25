#!/usr/bin/env python3
"""
Step 1 of the voice fallback (V-25): which voice types exist, how many recordings
each has, and its sex - so the similarity pass knows what it is matching.

    python scripts/voice-inventory.py            # writes voice/fallback/inventory.json

Voice types come from the VTYP records of the official masters; recordings are
counted from the six voice archives. Sex is the VTYP DNAM flag 0x02, read from
the record, never guessed from the name (NPCFGeneva has 0x02, MaleEvenToned has
0x01 - "allow default dialog" - and no sex bit).
"""
import json
import pathlib
import struct
import sys
import zlib
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import ba2list  # noqa: E402

DATA = pathlib.Path(r"D:\GOGGames\Fallout 4 GOTY\Data")
MASTERS = ["Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
           "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"]
ARCHIVES = ["Fallout4 - Voices.ba2", "DLCRobot - Voices_en.ba2", "DLCCoast - Voices_en.ba2",
            "DLCworkshop03 - Voices_en.ba2", "DLCNukaWorld - Voices_en.ba2"]
OUT = ROOT / "voice/fallback/inventory.json"


def fields(blob):
    i = 0
    while i + 6 <= len(blob):
        sig = blob[i:i + 4].decode("latin1")
        ln = struct.unpack_from("<H", blob, i + 4)[0]
        yield sig, blob[i + 6:i + 6 + ln]
        i += 6 + ln


def voice_types(path):
    """{EDID: (file-relative id, dnam flags)} from one master's VTYP group."""
    data = path.read_bytes()
    i = 24 + struct.unpack_from("<I", data, 4)[0]
    out = {}
    while i < len(data):
        size = struct.unpack_from("<I", data, i + 4)[0]
        if data[i + 8:i + 12] == b"VTYP":
            j, end = i + 24, i + size
            while j < end:
                rsize, flags, fid = struct.unpack_from("<III", data, j + 4)
                body = data[j + 24:j + 24 + rsize]
                if flags & 0x40000:
                    body = zlib.decompress(body[4:])
                f = dict(fields(body))
                edid = f.get("EDID", b"").rstrip(b"\0").decode("latin1")
                out[edid] = (fid & 0x00FFFFFF, f.get("DNAM", b"\0")[0])
                j += 24 + rsize
        i += size
    return out


def main():
    vt = {}
    for m in MASTERS:
        p = DATA / m
        if not p.exists():
            continue
        for edid, (fid, flags) in voice_types(p).items():
            vt.setdefault(edid.lower(), {"edid": edid, "master": m, "id": fid, "flags": flags})

    files = defaultdict(list)
    for a in ARCHIVES:
        fh, ents = ba2list.entries(str(DATA / a))
        if not ents:
            continue
        for name, (_off, psz, usz) in ents:
            parts = name.replace("\\", "/").lower().split("/")
            if len(parts) >= 5 and parts[0] == "sound" and parts[1] == "voice" and parts[-1].endswith(".fuz"):
                files[parts[3]].append({"archive": a, "name": name, "size": usz or psz})
        fh.close()

    # The player's voices (O-45) are not Rapport's own: nobody borrows them, and the player never barks.
    rendered = {p.name.lower() for p in (ROOT / "voice/out").iterdir()
                if p.is_dir() and not p.name.lower().startswith("playervoice")}
    rows = []
    for key, clips in files.items():
        meta = vt.get(key)
        if not meta:
            continue          # a folder with no VTYP in the masters (a mod's, or a typo in data)
        rows.append({
            "edid": meta["edid"], "master": meta["master"], "id": meta["id"],
            "female": bool(meta["flags"] & 0x02), "allowDefault": bool(meta["flags"] & 0x01),
            "rendered": key in rendered, "clips": len(clips),
            "sample": sorted(clips, key=lambda c: -c["size"])[:12],
        })
    rows.sort(key=lambda r: -r["clips"])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(rows, indent=1), encoding="utf-8")

    uniq = [r for r in rows if not r["rendered"]]
    print(f"voice types with recordings: {len(rows)}  (rendered by us: {len(rows) - len(uniq)}, "
          f"need a fallback: {len(uniq)} - {sum(r['female'] for r in uniq)} female)")
    print("missing from the render set:", sorted(rendered - {r['edid'].lower() for r in rows}))
    for r in uniq[:25]:
        print(f"  {r['edid']:34s} {'F' if r['female'] else 'M'} {r['clips']:6d} lines  {r['master']}")


if __name__ == "__main__":
    main()
