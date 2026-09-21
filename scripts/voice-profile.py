#!/usr/bin/env python3
"""
Profile ONE voice type from its own recordings: fingerprint, measured vocal
traits, how close our rendered voices come, and a Voice Design brief built from
the numbers.

    .venv-voice/Scripts/python scripts/voice-profile.py CompanionIvy.esm _NPC_IVY
    .venv-voice/Scripts/python scripts/voice-profile.py CompanionIvy.esm _NPC_IVY --clips 16

Writes voice/fallback/profiles/<VoiceType>.json. Reads the voice from every
archive in Data whose files live under Sound/Voice/<plugin>/<VoiceType>/, and
from the loose folder of the same name.

WHAT IT MEASURES, and what it cannot. Pitch (median and spread - how melodic),
brightness (spectral centroid - bright or dark), and pace (voiced share of the
clip - how much space between words) are numbers. Warmth, attitude and accent
are not, and the brief says which parts came from a measurement and which are
left for the owner's ear. An agent cannot hear (V-9): this proposes, the owner
picks.

NOT A CLONE. The brief describes a voice in words for Voice Design; nothing here
sends a mod's recordings anywhere. Copying a real voice actor's voice without
their consent is not something this pipeline does.
"""
import argparse
import glob
import json
import pathlib
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
OUT = FB / "profiles"


def clips_of(plugin, vt, n):
    """The n longest recordings of this voice, as (name, fuz bytes)."""
    prefix = f"sound\\voice\\{plugin.lower()}\\{vt.lower()}\\"
    found = []
    for a in glob.glob(str(DATA / "*.ba2")):
        fh, ents = ba2list.entries(a)
        # A texture archive (DX10) comes back as (None, "DX10") - a STRING where
        # the list goes, and iterating it yields characters. No handle, no files.
        if fh is None:
            continue
        for name, rec in ents:
            if name.lower().startswith(prefix) and name.lower().endswith(".fuz"):
                found.append((rec[2] or rec[1], name, a, rec))
        fh.close()
    found.sort(reverse=True)
    out = []
    for _size, name, a, rec in found[:n]:
        fh, _ = ba2list.entries(a)
        out.append((name, ba2list.read(fh, rec)))
        fh.close()
    return out, len(found)


def decode(fuz, work):
    magic, _v, lip = struct.unpack_from("<4sII", fuz, 0)
    if magic != b"FUZE":
        return None
    x, w = work / "clip.xwm", work / "clip.wav"
    x.write_bytes(fuz[12 + lip:])
    w.unlink(missing_ok=True)
    subprocess.run([str(XWMA), str(x), str(w)], capture_output=True)
    return w if w.exists() and w.stat().st_size > 1000 else None


def yin(x, sr, fmin=70, fmax=450, frame=2048, hop=512, thr=0.15):
    """Pitch track by YIN (cumulative-mean-normalised difference, first dip under thr).

    Not torchaudio's detect_pitch_frequency: that one HALVED Ivy's pitch - 123 Hz,
    'male' - where YIN reads her at ~200 Hz, which is what she is. An octave error
    on breathy speech is the classic failure of a simple detector, and here it
    would have designed a man's voice for a woman.
    """
    out = []
    lo, hi = int(sr / fmax), int(sr / fmin)
    for s in range(0, len(x) - frame - hi, hop):
        seg = x[s:s + frame + hi]
        if np.sqrt(np.mean(seg[:frame] ** 2)) < 0.01:
            continue
        d = np.array([np.sum((seg[:frame] - seg[t:t + frame]) ** 2) for t in range(1, hi)])
        cm = d * np.arange(1, hi) / np.maximum(np.cumsum(d), 1e-12)
        idx = np.where(cm[lo:] < thr)[0]
        if len(idx):
            out.append(sr / (lo + idx[0] + 1))
    return np.array(out)


def traits(wav):
    import torch
    import torchaudio
    audio, sr = torchaudio.load(str(wav))
    audio = audio.mean(0)
    mono16 = torchaudio.functional.resample(audio, sr, 16000).numpy()[: 16000 * 8]
    pitch = torch.tensor(yin(mono16, 16000))
    frame = 1024
    frames = audio[: len(audio) // frame * frame].reshape(-1, frame)
    energy = frames.pow(2).mean(1).sqrt()
    voiced = energy > energy.max() * 0.08
    spec = torch.fft.rfft(frames * torch.hann_window(frame), dim=1).abs()
    freqs = torch.fft.rfftfreq(frame, 1 / sr)
    centroid = (spec * freqs).sum(1) / spec.sum(1).clamp_min(1e-9)
    p = pitch[(pitch > 70) & (pitch < 450)]
    return {
        "f0": float(p.median()) if len(p) else None,
        "f0_spread": float(p.quantile(0.9) - p.quantile(0.1)) if len(p) > 10 else None,
        "centroid": float(centroid[voiced].median()) if voiced.any() else None,
        "voiced_share": float(voiced.float().mean()),
        "seconds": len(audio) / sr,
    }


def fingerprint(wavs):
    import torch
    import torchaudio
    from speechbrain.inference.speaker import EncoderClassifier
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    model = EncoderClassifier.from_hparams(source="speechbrain/spkrec-ecapa-voxceleb",
                                           savedir=str(FB / "models/ecapa"), run_opts={"device": dev})
    embs = []
    for w in wavs:
        a, sr = torchaudio.load(str(w))
        a = torchaudio.functional.resample(a.mean(0, keepdim=True), sr, 16000)[:, : 16000 * 10]
        with torch.no_grad():
            embs.append(torch.nn.functional.normalize(model.encode_batch(a.to(dev)).squeeze(1), dim=-1))
    e = torch.nn.functional.normalize(torch.cat(embs).mean(0), dim=-1)
    return e.cpu().numpy()


def words(t):
    """The measured traits as plain Voice Design language, each tagged with its number."""
    out = []
    f0 = t["f0"]
    if f0:
        if f0 < 120: out.append(f"a deep, low voice (median pitch {f0:.0f} Hz)")
        elif f0 < 165: out.append(f"a low-to-mid voice (median pitch {f0:.0f} Hz)")
        elif f0 < 210: out.append(f"a mid-range voice (median pitch {f0:.0f} Hz)")
        else: out.append(f"a high, light voice (median pitch {f0:.0f} Hz)")
    s = t["f0_spread"]
    if s:
        out.append("very melodic, expressive intonation" if s > 120 else
                   "lively, varied intonation" if s > 70 else "steady, fairly even intonation")
    c = t["centroid"]
    if c:
        out.append("bright and clear" if c > 1900 else "warm and rounded" if c > 1300 else "dark, soft tone")
    v = t["voiced_share"]
    out.append("quick, dense delivery with few pauses" if v > 0.62 else
               "relaxed pace with natural pauses" if v > 0.45 else "slow, unhurried delivery with space between phrases")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plugin")
    ap.add_argument("voice_type")
    ap.add_argument("--clips", type=int, default=12)
    a = ap.parse_args()

    work = OUT / "work"
    work.mkdir(parents=True, exist_ok=True)
    clips, total = clips_of(a.plugin, a.voice_type, a.clips)
    if not clips:
        sys.exit(f"no recordings under Sound/Voice/{a.plugin}/{a.voice_type}/ in any archive")
    wavs, per = [], []
    for i, (name, fuz) in enumerate(clips):
        w = decode(fuz, work)
        if w:
            keep = work / f"{i}.wav"
            keep.unlink(missing_ok=True)
            w.rename(keep)
            wavs.append(keep)
            per.append(traits(keep))
    agg = {k: float(np.median([p[k] for p in per if p[k] is not None])) for k in ("f0", "f0_spread", "centroid", "voiced_share")}
    agg["seconds_measured"] = float(sum(p["seconds"] for p in per))

    vec = fingerprint(wavs)
    cache = np.load(FB / "embeddings.npz")
    inv = {r["edid"]: r for r in json.loads((FB / "inventory.json").read_text(encoding="utf-8"))}
    ours = [(str(n), v) for n, v in zip(cache["names"], cache["vecs"]) if inv.get(str(n), {}).get("rendered")]
    female = agg["f0"] >= 160   # YIN median across clips; adult ranges ~85-155 vs ~165-255 Hz
    ranked = sorted(((float(vec @ v), n) for n, v in ours), reverse=True)
    same = [(s, n) for s, n in ranked if inv[n]["female"] == female or ("female" in n.lower()) == female][:5]

    desc = words(agg)
    brief = (f"A {'woman' if female else 'man'} in her late twenties to thirties, "
             if female else f"A man in his late twenties to thirties, ") + ", ".join(desc) + \
            ". Natural, conversational American English. [Owner by ear: warmth, attitude, accent.]"
    profile = {"plugin": a.plugin, "voice_type": a.voice_type, "recordings": total, "measured_clips": len(wavs),
               "traits": agg, "sex_by_pitch": "female" if female else "male",
               "nearest_ours": [{"voice": n, "score": round(s, 3)} for s, n in same],
               "voice_design_brief": brief, "fingerprint": vec.tolist()}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / f"{a.voice_type}.json").write_text(json.dumps(profile, indent=1), encoding="utf-8")

    print(f"{a.voice_type} ({a.plugin}): {total} recordings, measured the {len(wavs)} longest "
          f"({agg['seconds_measured']:.0f}s of speech)")
    print(f"  pitch   median {agg['f0']:.0f} Hz, spread {agg['f0_spread']:.0f} Hz  -> {'female' if female else 'male'}")
    print(f"  bright  centroid {agg['centroid']:.0f} Hz;  voiced share {agg['voiced_share']:.2f}")
    print("  nearest of ours (same sex):", ", ".join(f"{n} {s:.2f}" for s, n in same))
    print(f"  brief: {brief}")


if __name__ == "__main__":
    main()
