#!/usr/bin/env python3
"""
Audit every voice in the load order against our roster, and find the SWEET SPOTS:
groups of unique voices that one new archetype could serve at once.

    .venv-voice/Scripts/python scripts/voice-audit.py            # fingerprint new voices, report
    .venv-voice/Scripts/python scripts/voice-audit.py --report   # report from the cache only

Writes voice/fallback/audit.json and docs/voice-audit.md.

THE OWNER'S IDEA. Vanilla and modded characters with unique voices borrow the
nearest of our 32 rendered voices (V-25), but "nearest" can still be far - Ivy's
best match is 0.26. Rendering a voice per character does not scale. Instead:
fingerprint all of them, cluster the ones our roster serves badly, and each dense
cluster is ONE new archetype that would serve all of its members better than
anything we have. Ranked by how many characters and lines that one voice covers.

HOW TO READ THE NUMBERS. Same speaker scores ~0.6-0.8 on this model; two
different people rarely pass 0.5. The vanilla map's median best match is 0.37.
So a cluster is worth a voice when its members sit well below that today and
close to their shared centre - the centre is the voice to design, and its
closeness to each member is roughly what a well-designed archetype can reach.

Children are never grouped, never counted (the vanilla child rule, applied to
the vanilla voices; mod voices cannot be race-checked offline and are flagged
by name as needing a look before anything is rendered for them).
"""
import argparse
import collections
import concurrent.futures as cf
import glob
import json
import os
import pathlib
import re
import struct
import subprocess
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import ba2list  # noqa: E402

DATA = pathlib.Path(r"D:\GOGGames\Fallout 4 GOTY\Data")
XWMA = pathlib.Path(r"D:\GOGGames\Fallout 4 GOTY\Tools\Audio\xwmaencode.exe")
FB = ROOT / "voice/fallback"
WAV = FB / "wav-mods"
CACHE = FB / "embeddings-mods.npz"
OFFICIAL = {"fallout4.esm", "dlcrobot.esm", "dlcworkshop01.esm", "dlccoast.esm", "dlcworkshop02.esm",
            "dlcworkshop03.esm", "dlcnukaworld.esm"}
NOT_HUMAN = re.compile(r"robot|turret|protectron|assaultron|sentry|eyebot|handy|gutsy|synthgen1|"
                       r"supermutant|mutant|behemoth|deathclaw|dog|cr[A-Z]|creature|ghoulferal|feral|"
                       r"mirelurk|radio|announcer|computer|terminal|holotape|child|kid|boy|girl", re.I)


def load_order():
    active = {l.strip().lstrip("*").lower() for l in
              open(os.path.expandvars(r"%LOCALAPPDATA%\Fallout4\plugins.txt"), encoding="utf-8", errors="replace")
              if l.startswith("*")}
    return active | OFFICIAL


def discover(active):
    """voice type (lower) -> {'lines': n, 'plugins': set, 'clips': [(size, source, name, rec)]}."""
    vts = collections.defaultdict(lambda: {"lines": 0, "plugins": set(), "clips": []})
    for a in glob.glob(str(DATA / "*.ba2")):
        fh, ents = ba2list.entries(a)
        if fh is None:
            continue
        for name, rec in ents:
            p = name.replace("/", "\\").lower().split("\\")
            if len(p) >= 5 and p[0] == "sound" and p[1] == "voice" and p[-1].endswith(".fuz") and p[2] in active:
                v = vts[p[3]]
                v["lines"] += 1
                v["plugins"].add(p[2])
                v["clips"].append((rec[2] or rec[1], a, name, rec))
        fh.close()
    loose = DATA / "Sound" / "Voice"
    for plug in os.listdir(loose):
        if plug.lower() not in active:
            continue
        for vt in os.listdir(loose / plug):
            d = loose / plug / vt
            if not d.is_dir():
                continue
            for f in os.listdir(d):
                if f.lower().endswith(".fuz"):
                    v = vts[vt.lower()]
                    v["lines"] += 1
                    v["plugins"].add(plug.lower())
                    v["clips"].append((os.path.getsize(d / f), "loose", str(d / f), None))
    return vts


def read_clip(source, name, rec):
    if source == "loose":
        return pathlib.Path(name).read_bytes()
    fh, _ = ba2list.entries(source)
    try:
        return ba2list.read(fh, rec)
    finally:
        fh.close()


def decode_voice(vt, info, per_voice):
    out = []
    for i, (_s, source, name, rec) in enumerate(sorted(info["clips"], reverse=True)[:per_voice]):
        w = WAV / vt / f"{i}.wav"
        if w.exists():
            out.append(w)
            continue
        fuz = read_clip(source, name, rec)
        magic, _v, lip = struct.unpack_from("<4sII", fuz, 0)
        if magic != b"FUZE":
            continue
        w.parent.mkdir(parents=True, exist_ok=True)
        x = w.with_suffix(".xwm")
        x.write_bytes(fuz[12 + lip:])
        subprocess.run([str(XWMA), str(x), str(w)], capture_output=True)
        x.unlink(missing_ok=True)
        if w.exists() and w.stat().st_size > 1000:
            out.append(w)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-voice", type=int, default=6)
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--gap", type=float, default=0.30, help="best match below this = served badly")
    ap.add_argument("--join", type=float, default=0.30, help="min closeness to a cluster centre to belong")
    a = ap.parse_args()

    inv = {r["edid"].lower(): r for r in json.loads((FB / "inventory.json").read_text(encoding="utf-8"))}
    vmap = json.loads((FB / "map.json").read_text(encoding="utf-8"))
    vz = np.load(FB / "embeddings.npz")
    vanilla = {str(n).lower(): v for n, v in zip(vz["names"], vz["vecs"])}
    ours = {n: v for n, v in vanilla.items() if inv.get(n, {}).get("rendered")}

    mods = {}
    if not a.report:
        active = load_order()
        vts = discover(active)
        todo = {vt: info for vt, info in vts.items()
                if vt not in inv and not info["plugins"] <= OFFICIAL and info["plugins"] != {"rapport.esp"}}
        print(f"mod-only voice types: {len(todo)}")
        cached = {}
        if CACHE.exists():
            z = np.load(CACHE, allow_pickle=True)
            cached = {str(n): (v, float(p), int(l), str(pl)) for n, v, p, l, pl in
                      zip(z["names"], z["vecs"], z["f0"], z["lines"], z["plugins"])}
        new = {vt: info for vt, info in todo.items() if vt not in cached}
        print(f"fingerprinting {len(new)} new ({len(cached)} cached)")
        with cf.ThreadPoolExecutor(8) as pool:
            wavs = dict(zip(new, pool.map(lambda kv: decode_voice(kv[0], kv[1], a.per_voice), new.items())))

        import torch
        import torchaudio
        from speechbrain.inference.speaker import EncoderClassifier
        import importlib.util
        spec = importlib.util.spec_from_file_location("vp", ROOT / "scripts/voice-profile.py")
        vp = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(vp)
        model = EncoderClassifier.from_hparams(source="speechbrain/spkrec-ecapa-voxceleb",
                                               savedir=str(FB / "models/ecapa"), run_opts={"device": "cuda"})
        for vt, files in wavs.items():
            if not files:
                continue
            embs, pitch = [], []
            for f in files:
                x, sr = torchaudio.load(str(f))
                x16 = torchaudio.functional.resample(x.mean(0), sr, 16000)
                with torch.no_grad():
                    embs.append(torch.nn.functional.normalize(model.encode_batch(x16[: 16000 * 10].unsqueeze(0).cuda()).squeeze(), dim=-1))
                pt = vp.yin(x16.numpy()[: 16000 * 6], 16000)
                if len(pt):
                    pitch.append(float(np.median(pt)))
            e = torch.nn.functional.normalize(torch.stack(embs).mean(0), dim=-1).cpu().numpy()
            cached[vt] = (e, float(np.median(pitch)) if pitch else 0.0, todo[vt]["lines"],
                          ",".join(sorted(todo[vt]["plugins"])))
        names = list(cached)
        np.savez(CACHE, names=np.array(names), vecs=np.stack([cached[n][0] for n in names]),
                 f0=np.array([cached[n][1] for n in names]), lines=np.array([cached[n][2] for n in names]),
                 plugins=np.array([cached[n][3] for n in names]))
        mods = cached
    else:
        z = np.load(CACHE, allow_pickle=True)
        mods = {str(n): (v, float(p), int(l), str(pl)) for n, v, p, l, pl in
                zip(z["names"], z["vecs"], z["f0"], z["lines"], z["plugins"])}

    # The population: vanilla uniques that may speak (the map's own gating) + mod voices.
    pop = []
    for name, row in vmap.items():
        key = name.lower()
        if row.get("silent") or key not in vanilla:
            continue
        pop.append({"voice": name, "source": row["master"], "female": row["female"],
                    "lines": inv.get(key, {}).get("clips", 0), "vec": vanilla[key], "origin": "vanilla"})
    for name, (vec, f0, lines, plugins) in mods.items():
        if f0 <= 0:
            continue
        pop.append({"voice": name, "source": plugins, "female": f0 >= 160, "lines": lines, "vec": vec,
                    "origin": "mod", "f0": f0, "check": bool(NOT_HUMAN.search(name))})

    our_names = list(ours)
    our_vecs = np.stack([ours[n] for n in our_names])
    fem = {n: bool(re.search("female", n, re.I)) for n in our_names}
    for p in pop:
        sims = our_vecs @ p["vec"]
        idx = [i for i, n in enumerate(our_names) if fem[n] == p["female"]]
        best = max(idx, key=lambda i: sims[i])
        p["best"], p["best_voice"] = float(sims[best]), our_names[best]

    gap = [p for p in pop if p["best"] < a.gap]
    clusters = []
    for female in (True, False):
        members = sorted([p for p in gap if p["female"] == female], key=lambda p: -p["lines"])
        left = list(members)
        while left:
            # Seed on the voice with the most lines; grow by closeness to the running centre.
            seed = left.pop(0)
            group = [seed]
            centre = seed["vec"].copy()
            changed = True
            while changed:
                changed = False
                for p in list(left):
                    c = centre / np.linalg.norm(centre)
                    if float(c @ p["vec"]) >= a.join:
                        group.append(p)
                        left.remove(p)
                        centre = centre + p["vec"]
                        changed = True
            c = centre / np.linalg.norm(centre)
            # LEAVE-ONE-OUT. Closeness to a centre that INCLUDES the member is
            # inflated: in a pair, half the centre is the member itself, so both
            # score ~0.7 whatever they sound like. The honest number is closeness
            # to the centre of the OTHERS - what a voice built for the group gives.
            fit = []
            for p in group:
                rest = centre - p["vec"]
                n = np.linalg.norm(rest)
                fit.append(float((rest / n) @ p["vec"]) if len(group) > 1 and n > 1e-9 else 0.0)
            lift = [f - p["best"] for f, p in zip(fit, group)]
            clusters.append({"female": female, "centre": c, "members": group, "fit": fit, "lift": lift,
                             "lines": sum(p["lines"] for p in group)})
    clusters = [c for c in clusters if len(c["members"]) >= 2]
    clusters.sort(key=lambda c: -(c["lines"] * max(0.0, float(np.mean(c["lift"])))))

    report = {"population": len(pop), "gap": len(gap), "gap_threshold": a.gap, "join": a.join, "clusters": [
        {"female": c["female"], "members": len(c["members"]), "lines": c["lines"],
         "mean_fit": round(float(np.mean(c["fit"])), 3), "mean_lift": round(float(np.mean(c["lift"])), 3),
         "voices": [{"voice": p["voice"], "source": p["source"], "lines": p["lines"], "today": round(p["best"], 3),
                     "borrows": p["best_voice"], "fit": round(f, 3), "check": p.get("check", False)}
                    for p, f in sorted(zip(c["members"], c["fit"]), key=lambda t: -t[0]["lines"])],
         "centre": c["centre"].tolist()} for c in clusters]}
    (FB / "audit.json").write_text(json.dumps(report, indent=1), encoding="utf-8")

    md = [f"# Voice audit - where one new voice would serve many\n",
          f"Generated by `scripts/voice-audit.py`. Population: **{len(pop)}** unique voices that may speak "
          f"({sum(p['origin'] == 'vanilla' for p in pop)} vanilla, {sum(p['origin'] == 'mod' for p in pop)} from mods). "
          f"**{len(gap)}** are served badly today (best match to our 32 below {a.gap}). "
          f"Grouped when closer than {a.join} to a shared centre. Ranked by lines covered x lift.\n",
          "`fit` = closeness to the centre of the group's OTHER members (leave-one-out) - roughly what one voice "
          "designed for the group gives each member, and deliberately not inflated by the member itself. "
          "`today` = their best match now. A name flagged **check** may not be a human voice - look before rendering.\n"]
    for i, c in enumerate(report["clusters"][:15], 1):
        md.append(f"\n## {i}. {'female' if c['female'] else 'male'} - {c['members']} voices, {c['lines']:,} lines, "
                  f"fit {c['mean_fit']:.2f}, lift +{c['mean_lift']:.2f}\n")
        md.append("| voice | from | lines | today | fit |\n| --- | --- | --- | --- | --- |")
        for v in c["voices"][:10]:
            md.append(f"| {v['voice']}{' **check**' if v['check'] else ''} | {v['source'][:40]} | {v['lines']:,} | "
                      f"{v['today']:.2f} ({v['borrows']}) | {v['fit']:.2f} |")
    (ROOT / "docs/voice-audit.md").write_text("\n".join(md) + "\n", encoding="utf-8")

    print(f"population {len(pop)}, served badly {len(gap)}, clusters of 2+: {len(clusters)}")
    for i, c in enumerate(report["clusters"][:10], 1):
        top = ", ".join(v["voice"] for v in c["voices"][:4])
        print(f"{i:2d}. {'F' if c['female'] else 'M'} {c['members']:3d} voices {c['lines']:7,} lines  fit {c['mean_fit']:.2f} "
              f"lift +{c['mean_lift']:.2f}  | {top}")


if __name__ == "__main__":
    main()
