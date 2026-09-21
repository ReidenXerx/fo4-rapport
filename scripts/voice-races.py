#!/usr/bin/env python3
"""
Step 1b of the voice fallback (V-25): for every voice type, the RACES and SEXES
of the NPC records that use it - which is what decides who may borrow a voice.

    python scripts/voice-races.py        # writes voice/fallback/vt-races.json

Reads NPC_, RACE and VTYP from the official masters. Two traps, both hit:
  - a templated NPC often has no RNAM of its own; its race is its template's,
    so the chain is followed (Longfellow read as raceless until it was);
  - record ids are master-RELATIVE and must be made global per file. The first
    version did that in a closure defined inside the per-master loop and called
    after it - late binding handed every call the LAST master's list, and every
    DLC record resolved to the wrong id. The defaults on glob() bind it now.
"""
import struct, zlib, collections, pathlib, sys
DATA = pathlib.Path(r"D:\GOGGames\Fallout 4 GOTY\Data")
MASTERS = ["Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"]
def fields(b):
    i = 0
    while i + 6 <= len(b):
        s = b[i:i+4].decode("latin1"); n = struct.unpack_from("<H", b, i+4)[0]; yield s, b[i+6:i+6+n]; i += 6 + n
def walk(d, s, e, want):
    i = s
    while i < e:
        size = struct.unpack_from("<I", d, i+4)[0]
        if d[i:i+4] == b"GRUP":
            yield from walk(d, i+24, i+size, want); i += size; continue
        if d[i:i+4] in want:
            fl, fid = struct.unpack_from("<II", d, i+8); body = d[i+24:i+24+size]
            if fl & 0x40000: body = zlib.decompress(body[4:])
            yield d[i:i+4].decode(), fid, dict(fields(body))
        i += 24 + size
# global ids: load order index = position in MASTERS among present files
present = [m for m in MASTERS if (DATA / m).exists()]
race_edid, vt_edid, vt_races = {}, {}, collections.defaultdict(collections.Counter)
npcs = []
for li, m in enumerate(present):
    d = (DATA / m).read_bytes()
    masters = [f[1].rstrip(b"\0").decode("latin1") for f in fields(d[24:24+struct.unpack_from("<I", d, 4)[0]]) if f[0] == "MAST"]
    # Defaults bind NOW. A plain closure binds late, and every call made after this
    # loop used the LAST master's list - DLC records resolved to the wrong ids.
    def glob(fid, masters=masters, m=m):  # map a record-local id to a load-order-global one
        hi = fid >> 24
        owner = masters[hi] if hi < len(masters) else m
        return (present.index(owner) << 24) | (fid & 0xFFFFFF) if owner in present else None
    i = 24 + struct.unpack_from("<I", d, 4)[0]
    for sig, fid, f in walk(d, i, len(d), {b"RACE", b"VTYP", b"NPC_"}):
        g = glob(fid); ed = f.get("EDID", b"").rstrip(b"\0").decode("latin1")
        if sig == "RACE": race_edid[g] = ed
        elif sig == "VTYP": vt_edid[g] = ed
        else: npcs.append((g, f.get("VTCK"), f.get("RNAM"), f.get("TPLT"), glob,
                           bool(struct.unpack_from("<I", f["ACBS"])[0] & 1) if "ACBS" in f else None))
# A templated NPC often has no RNAM of its own: the race comes from its template.
by_id = {g: (rnam, tplt, glob) for g, _v, rnam, tplt, glob, _s in npcs}
def race_of(g, depth=0):
    rec = by_id.get(g)
    if not rec or depth > 6:
        return None
    rnam, tplt, glob = rec
    if rnam:
        return glob(struct.unpack("<I", rnam)[0])
    return race_of(glob(struct.unpack("<I", tplt)[0]), depth + 1) if tplt else None
# Sex from the NPCs that USE a voice (ACBS flag 0x01): the VTYP's own female
# flag is missing on female voices such as NPCFProctorIngram, and trusting it
# mapped her to a male voice.
vt_sex = collections.defaultdict(collections.Counter)
for g, vtck, _r, _t, glob, female in npcs:
    if vtck:
        vt = glob(struct.unpack("<I", vtck)[0]); r = race_of(g)
        if r is not None:
            vt_races[vt_edid.get(vt, "?")][race_edid.get(r, "?")] += 1
        if female is not None:
            vt_sex[vt_edid.get(vt, "?")]["female" if female else "male"] += 1
print("races seen:", sorted({r for c in vt_races.values() for r in c}))
import json
OUT = pathlib.Path(__file__).resolve().parent.parent / "voice/fallback/vt-races.json"
json.dump({k: {"races": dict(vt_races.get(k, {})), "sex": dict(vt_sex.get(k, {}))}
           for k in set(vt_races) | set(vt_sex)}, open(OUT, "w"), indent=1)
