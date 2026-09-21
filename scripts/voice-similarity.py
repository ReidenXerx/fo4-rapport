#!/usr/bin/env python3
"""
Steps 2-4 of the voice fallback (V-25): fingerprint every voice type from the
game's own recordings, and map each voice we did not render to the closest one
we did.

    .venv-voice/Scripts/python scripts/voice-similarity.py            # decode, embed, map
    .venv-voice/Scripts/python scripts/voice-similarity.py --map-only # re-map from the cache

Needs the voice environment (.venv-voice: torch + speechbrain) and
voice/fallback/inventory.json from scripts/voice-inventory.py.

WHAT "SIMILAR" MEANS HERE. A speaker-embedding model (SpeechBrain ECAPA-TDNN,
trained to tell speakers apart) turns a recording into a 192-number
fingerprint of the VOICE - timbre, pitch range, accent, rasp - largely
independent of the words. A voice type's fingerprint is the mean over several
of its longest vanilla lines. Similarity is cosine between fingerprints.

LIKE FOR LIKE. Both sides are VANILLA recordings: a unique voice is compared
with the game's own recordings of our 32 types, not with our renders. Our
renders were designed to match those types, and comparing vanilla speech with
whispered intimate delivery would measure delivery, not voice.

HARD RULES, not scores:
  - same sex only, and the sex must be AGREED by every witness (VTYP flag,
    name, the NPC records that use the voice) or the voice stays silent - see
    female() in build_map for the two records that broke each single source.
  - WHO may borrow is decided by RACE, read from the NPC records that use the
    voice (voice/fallback/vt-races.json), never by a similarity threshold. The
    first version used a threshold of 0.35 and it could not work: speaker
    embeddings score how different two PEOPLE are, so Piper (0.29), Cait (0.30)
    and Geneva (0.29) sat in the same band as Mr Handy (0.25). A human voice
    unlike our 32 and a robot are not separable by a number.
  - eligible: HumanRace, GhoulRace, and the synth bodies with human voices
    (SynthGen2RaceValentine - Nick; DLC03_SynthGen2RaceDiMa - DiMA). Robots, creatures and super mutants stay silent.
  - CHILDREN NEVER: a voice type used by ANY child NPC (HumanChildRace,
    GhoulChildRace) is always silent, whatever else uses it and whatever the
    fingerprint says. Rapport's lines must never come out of a child's voice.
  - --floor only catches a broken fingerprint (a clip that decoded to noise).
The owner can veto any pair (V-9): voices.json is data, and an entry set to
null there stays silent.
"""
import argparse
import concurrent.futures as cf
import json
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
WAV = FB / "wav"
CACHE = FB / "embeddings.npz"


def decode_all(inv, per_voice):
    """fuz -> xwm -> wav for the longest clips of every voice type. Cached on disk."""
    jobs, handles = [], {}
    for row in inv:
        for i, clip in enumerate(row["sample"][:per_voice]):
            out = WAV / row["edid"] / f"{i}.wav"
            if not out.exists():
                jobs.append((row["edid"], clip, out))
    print(f"decoding {len(jobs)} clip(s) ({sum(1 for r in inv) * per_voice - len(jobs)} cached)")

    def blob_of(clip):
        a = clip["archive"]
        if a not in handles:
            handles[a] = ba2list.entries(str(DATA / a))
        fh, ents = handles[a]
        if "_index" not in clip:
            pass
        return ba2list.read(fh, dict(ents)[clip["name"]])

    # Reading the archive is serial (one file handle); the encoder runs in parallel.
    staged = []
    for edid, clip, out in jobs:
        blob = blob_of(clip)
        magic, _ver, lip = struct.unpack_from("<4sII", blob, 0)
        if magic != b"FUZE":
            continue
        out.parent.mkdir(parents=True, exist_ok=True)
        xwm = out.with_suffix(".xwm")
        xwm.write_bytes(blob[12 + lip:])
        staged.append((xwm, out))

    def run(pair):
        xwm, out = pair
        subprocess.run([str(XWMA), str(xwm), str(out)], capture_output=True)
        xwm.unlink(missing_ok=True)
        # xwmaencode has exited non-zero with a plausible message before; trust the file.
        return out.exists() and out.stat().st_size > 1000

    with cf.ThreadPoolExecutor(8) as pool:
        ok = sum(pool.map(run, staged))
    print(f"decoded {ok}/{len(staged)}")


def embed_all(inv, per_voice, full=False):
    """Fingerprint every voice type not already in the cache (all of them with full=True).

    Incremental by default: a voice's fingerprint depends only on its own vanilla
    recordings, so adding content never changes an existing one - only new voice
    types need the model. The first run is ~4 minutes; a rerun is seconds.
    """
    names, vecs, counts = [], [], []
    if CACHE.exists() and not full:
        z = np.load(CACHE)
        names, vecs, counts = list(z["names"]), list(z["vecs"]), list(z["counts"])
    known = set(names)
    todo = [row for row in inv if row["edid"] not in known]
    if not todo:
        print(f"fingerprints: all {len(names)} cached, none new")
        return

    import torch
    import torchaudio
    from speechbrain.inference.speaker import EncoderClassifier

    dev = "cuda" if torch.cuda.is_available() else "cpu"
    model = EncoderClassifier.from_hparams(source="speechbrain/spkrec-ecapa-voxceleb",
                                           savedir=str(FB / "models/ecapa"), run_opts={"device": dev})
    for row in todo:
        clips = []
        for i in range(per_voice):
            p = WAV / row["edid"] / f"{i}.wav"
            if not p.exists():
                continue
            wav, rate = torchaudio.load(str(p))
            wav = torchaudio.functional.resample(wav.mean(0, keepdim=True), rate, 16000)
            clips.append(wav[:, : 16000 * 10])        # 10 s is plenty for a voice
        if not clips:
            continue
        with torch.no_grad():
            e = torch.cat([model.encode_batch(c.to(dev)).squeeze(1) for c in clips])
        e = torch.nn.functional.normalize(e, dim=-1).mean(0)
        names.append(row["edid"])
        vecs.append(torch.nn.functional.normalize(e, dim=-1).cpu().numpy())
        counts.append(len(clips))
    np.savez(CACHE, names=np.array(names), vecs=np.stack(vecs), counts=np.array(counts))
    print(f"fingerprinted {len(todo)} new voice type(s) on {dev} ({len(names)} in the cache)")


# Nick and DiMA: synth bodies with human voices.
SPEAK = {"HumanRace", "GhoulRace", "SynthGen2RaceValentine", "DLC03_SynthGen2RaceDiMa"}
CHILD = {"HumanChildRace", "GhoulChildRace"}


def eligibility(races):
    """races: {race: NPC count}. Children by ANY use; speaking by MAJORITY use."""
    known = {r: c for r, c in races.items() if r != "?"}
    if set(known) & CHILD:
        return "child voice - never"
    if not known:
        return "no NPC record names its race"
    # Majority, not any: 2 of the 89 NPCs using RobotMrHandy are HumanRace
    # records, and 'any human' handed Codsworth's voice a human one to borrow.
    speaking = sum(c for r, c in known.items() if r in SPEAK)
    if speaking * 2 > sum(known.values()):
        return None
    return "not a human or ghoul voice"


def build_map(inv, floor):
    races = json.loads((FB / "vt-races.json").read_text(encoding="utf-8"))
    z = np.load(CACHE)
    by = {r["edid"]: r for r in inv}
    # The cache can hold a voice the current inventory no longer has (content
    # removed); it is simply not mapped, rather than a KeyError.
    keep = [i for i, n in enumerate(z["names"]) if str(n) in by]
    names, vecs = [str(z["names"][i]) for i in keep], z["vecs"][keep]
    idx = {n: i for i, n in enumerate(names)}
    ours = [n for n in names if by[n]["rendered"]]
    def female(n):
        """True/False when every witness agrees, None when they disagree or none speaks.

        Sex is a hard rule, and no single source of it can be trusted:
          - the VTYP female flag is MISSING on real female voices (NPCFProctorIngram),
            so it only ever votes female; its absence proves nothing;
          - one NPC record can be wrong - HolotapeActorFortStrongSoldier, a dummy
            that plays a holotape, is flagged female and voices a male soldier;
          - the name is a convention (NPCF/NPCM, Female/Male), not a rule.
        So: collect the votes that exist, require them to agree, and otherwise the
        voice stays silent. A missing line is recoverable; a wrong-sex voice is not.
        """
        votes = set()
        if by[n]["female"]:
            votes.add(True)
        name = n.lower()
        if re.search(r"(^|_)(dlc\d+)?npcf|female", name):
            votes.add(True)
        elif re.search(r"(^|_)(dlc\d+)?npcm|(^|[^e])male", name):
            votes.add(False)
        sex = races.get(n, {}).get("sex", {})
        if sex.get("female", 0) != sex.get("male", 0):
            votes.add(sex.get("female", 0) > sex.get("male", 0))
        return votes.pop() if len(votes) == 1 else None

    out, best_scores = {}, []
    for n in names:
        row = by[n]
        if row["rendered"]:
            continue
        sex = female(n)
        same = [o for o in ours if female(o) == sex] if sex is not None else ours
        sims = sorted(((float(vecs[idx[n]] @ vecs[idx[o]]), o) for o in same), reverse=True)
        top = sims[:3]
        best_scores.append((top[0][0], n))
        why = eligibility(races.get(n, {}).get("races", {}))
        if why is None and sex is None:
            why = "sex ambiguous - witnesses disagree or none speaks"
        if why is None and top[0][0] < floor:
            why = f"fingerprint unreliable (best {top[0][0]:.2f} < floor {floor})"
        out[n] = {
            "master": row["master"], "id": row["id"], "female": sex,
            "borrow": top[0][1] if why is None else None,
            "silent": why,
            "races": races.get(n, {}).get("races", {}),
            "sex": races.get(n, {}).get("sex", {}),
            "candidates": [{"voice": o, "score": round(s, 3)} for s, o in top],
        }
    best_scores.sort()
    return out, best_scores


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-voice", type=int, default=6)
    ap.add_argument("--floor", type=float, default=0.12)
    ap.add_argument("--map-only", action="store_true")
    ap.add_argument("--full", action="store_true", help="re-fingerprint every voice, ignoring the cache")
    a = ap.parse_args()

    inv = json.loads((FB / "inventory.json").read_text(encoding="utf-8"))
    if not a.map_only:
        decode_all(inv, a.per_voice)
        embed_all(inv, a.per_voice, a.full)

    mapping, scores = build_map(inv, a.floor)
    (FB / "map.json").write_text(json.dumps(mapping, indent=1), encoding="utf-8")

    s = np.array([x for x, _ in scores])
    print(f"\nbest same-sex score over {len(s)} voices: min {s.min():.2f}  p10 {np.percentile(s,10):.2f}  "
          f"median {np.median(s):.2f}  p90 {np.percentile(s,90):.2f}  max {s.max():.2f}")
    from collections import Counter
    print(f"{sum(1 for v in mapping.values() if v['borrow'])} borrow; silent by reason:",
          dict(Counter(v['silent'].split(' (')[0] for v in mapping.values() if v['silent'])))
    print("\nlowest-scoring voices that DO borrow (check these by ear first):")
    for x, n in [p for p in scores if mapping[p[1]]["borrow"]][:10]:
        print(f"  {x:.2f}  {n:36s} -> {mapping[n]['borrow']}")
    print("\nnamed characters:")
    for n in ("NPCFPiper", "NPCFCait", "NPCFCurie", "NPCFGeneva", "NPCMNickValentine", "NPCMPaladinDanse",
              "NPCMPrestonGarvey", "NPCMHancock", "NPCMDeacon", "NPCMMacCready", "NPCMStrong",
              "DLC04NPCMGage", "DLC03MaleOldLongfellow", "NPCMMayorMcDonough", "RobotMrHandy"):
        if n in mapping:
            c = mapping[n]["candidates"]
            print(f"  {n:26s} -> {str(mapping[n]['borrow']):22s} "
                  + "  ".join(f"{x['voice']} {x['score']}" for x in c))


if __name__ == "__main__":
    main()
