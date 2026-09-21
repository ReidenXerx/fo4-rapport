#!/usr/bin/env python3
"""
The TIMBRE gate: every rendered voice must sound like the sex of its voice type,
in BOTH registers (intimate pair lines and projected crowd lines).

    .venv-voice/Scripts/python scripts/voice-sex-check.py        # exit 1 on any miscast voice

WHY IT EXISTS. FemaleEvenToned's crowd voice was a MAN. Nothing caught it: the
render gate checks WORDS (speech-to-text), and the one sex check that existed
measured PITCH - and a man projecting to a crowd sits at 250-350 Hz, a woman's
range. It shipped, and players heard a man under Ivy's and Magnolia's subtitles.

Timbre does not care how loud or high someone speaks. Each voice's fingerprint
(ECAPA, the same model as the fallback map) is compared with the OTHER voices of
the same register, split by sex; a female voice must sit clearly closer to the
women than to the men, and the reverse. Measured on the miscast voice: 0.65 to
the men, 0.26 to the women. Every correct voice: ~0.6 to its own sex, ~0.2 to
the other. The margin below is set well inside that gap.

Run it after any render, and before package-voice.py.
"""
import glob
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
XWMA = pathlib.Path(r"D:\GOGGames\Fallout 4 GOTY\Tools\Audio\xwmaencode.exe")
REGISTERS = {"intimate": "*_quickie_init_0[1-3].fuz", "crowd": "*_observer_crowd_0[1-3].fuz"}
MARGIN = 0.15


def main() -> int:
    import torch
    import torchaudio
    from speechbrain.inference.speaker import EncoderClassifier
    model = EncoderClassifier.from_hparams(source="speechbrain/spkrec-ecapa-voxceleb",
                                           savedir=str(ROOT / "voice/fallback/models/ecapa"),
                                           run_opts={"device": "cuda" if torch.cuda.is_available() else "cpu"})
    work = pathlib.Path(tempfile.mkdtemp())

    def fingerprint(files):
        embs = []
        for f in files:
            b = open(f, "rb").read()
            lip = struct.unpack_from("<I", b, 8)[0]
            (work / "c.xwm").write_bytes(b[12 + lip:])
            (work / "c.wav").unlink(missing_ok=True)
            subprocess.run([str(XWMA), str(work / "c.xwm"), str(work / "c.wav")], capture_output=True)
            a, sr = torchaudio.load(str(work / "c.wav"))
            a16 = torchaudio.functional.resample(a.mean(0), sr, 16000)
            with torch.no_grad():
                embs.append(torch.nn.functional.normalize(model.encode_batch(a16.unsqueeze(0).to(model.device)).squeeze(), dim=-1))
        e = torch.nn.functional.normalize(torch.stack(embs).mean(0), dim=-1)
        return e.cpu().numpy()

    vts = sorted(p.name for p in (ROOT / "voice/out").iterdir() if p.is_dir())
    female = {v: bool(re.search("female", v, re.I)) for v in vts}
    bad = []
    for register, pattern in REGISTERS.items():
        prints = {}
        for v in vts:
            files = sorted(glob.glob(str(ROOT / "voice/out" / v / pattern)))[:6]
            if files:
                prints[v] = fingerprint(files)
        for v, e in prints.items():
            own = [e @ prints[o] for o in prints if o != v and female[o] == female[v]]
            other = [e @ prints[o] for o in prints if o != v and female[o] != female[v]]
            s_own, s_other = float(np.mean(own)), float(np.mean(other))
            ok = s_own > s_other + MARGIN
            if not ok:
                bad.append((register, v, s_own, s_other))
            print(f"{register:8s} {v:28s} {'F' if female[v] else 'M'}  own sex {s_own:+.3f}  other {s_other:+.3f}  "
                  f"{'ok' if ok else 'MISCAST'}")
    if bad:
        print(f"\n{len(bad)} MISCAST voice(s) - do not package:")
        for r, v, a, b in bad:
            print(f"  {v} ({r}): closer to the other sex ({b:+.3f}) than its own ({a:+.3f})")
        return 1
    print(f"\nall {len(vts)} voice types sound like their sex in both registers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
