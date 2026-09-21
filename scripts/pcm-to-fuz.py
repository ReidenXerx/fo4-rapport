#!/usr/bin/env python3
"""
Turn an ElevenLabs pcm_44100 render into a Fallout 4 .fuz voice file.

    python scripts/pcm-to-fuz.py <raw.pcm> <out.fuz> [--lip <file.lip>]

ElevenLabs' pcm_44100 output is HEADERLESS 16-bit mono little-endian samples,
and the MCP server saves it with a .mp3 extension regardless - the extension is
a lie, the bytes are PCM. Everything here works from the bytes.

The chain is PCM -> WAV -> xWMA -> FUZ with no MP3 anywhere, which is the whole
reason for rendering at pcm_44100: an MP3 source would mean a lossy encode
feeding a second lossy encode, and these lines are quiet, breathy and close-mic,
which is exactly where that compounds audibly.

FUZ layout (little-endian):
    0..3    'FUZE'
    4..7    version (1)
    8..11   lip data size in bytes (0 when there is no lip sync)
    12..    lip data, then the xWMA RIFF
"""
import argparse, pathlib, struct, subprocess, sys, tempfile

RATE, CHANNELS, BITS = 44100, 1, 16
XWMAENCODE = pathlib.Path(r"D:\GOGGames\Fallout 4 GOTY\Tools\Audio\xwmaencode.exe")


def looks_like_mp3_frame(b: bytes) -> bool:
    """True only for a PLAUSIBLE MPEG audio frame header.

    The sync bits alone are not enough. 16-bit PCM whose first sample is -1 -
    ordinary near-silence at the start of a spoken line - is the bytes FF FF,
    which passes a bare sync test and gets a perfectly good render rejected.
    Measured: 1 of 36 barks in the first batch, and it will recur on any line
    that opens quietly. So check the fields a real frame must have.
    """
    if len(b) < 4 or b[0] != 0xFF or (b[1] & 0xE0) != 0xE0:
        return False
    version, layer = (b[1] >> 3) & 3, (b[1] >> 1) & 3
    bitrate, samplerate = (b[2] >> 4) & 0xF, (b[2] >> 2) & 3
    return version != 1 and layer != 0 and bitrate not in (0, 15) and samplerate != 3


def wav_header(n_bytes: int) -> bytes:
    align = CHANNELS * BITS // 8
    return (b"RIFF" + struct.pack("<I", 36 + n_bytes) + b"WAVEfmt "
            + struct.pack("<IHHIIHH", 16, 1, CHANNELS, RATE, RATE * align, align, BITS)
            + b"data" + struct.pack("<I", n_bytes))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("pcm"); ap.add_argument("out")
    ap.add_argument("--lip", default=None)
    ap.add_argument("--keep-wav", action="store_true")
    ap.add_argument("--pcm", dest="pcm_declared", action="store_true",
                    help="caller guarantees raw PCM; skip format sniffing")
    a = ap.parse_args()

    pcm = pathlib.Path(a.pcm).read_bytes()
    # Sniffing is for hand-use. A caller that REQUESTED pcm_44100 already knows
    # the format, and guessing it again can only be wrong: the heuristic needs a
    # plausible MPEG frame header, and roughly 1 render in 2,000 of genuine PCM
    # happens to start with four bytes that form one. Measured: 2 good renders
    # rejected out of 4,464.
    if not a.pcm_declared:
        if pcm[:3] == b"ID3" or looks_like_mp3_frame(pcm):
            sys.exit("refusing: that file is MP3, not PCM - re-render with "
                     "output_format=pcm_44100 (or pass --pcm if you are certain)")
    if pcm[:4] == b"RIFF":
        sys.exit("refusing: that file already has a RIFF header - pass the raw PCM")
    if len(pcm) % 2:
        sys.exit("refusing: odd byte count - not 16-bit samples")
    secs = len(pcm) / 2 / RATE

    if not XWMAENCODE.exists():
        sys.exit(f"refusing: xwmaencode not found at {XWMAENCODE}")

    out = pathlib.Path(a.out); out.parent.mkdir(parents=True, exist_ok=True)
    # mkdtemp, not mkstemp: mkstemp hands back an OPEN handle, and xwmaencode
    # then dies with ERROR_SHARING_VIOLATION because Windows will not let it
    # write a file we are still holding.
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="fuz-"))
    wav = out.with_suffix(".wav") if a.keep_wav else tmp / "in.wav"
    wav.write_bytes(wav_header(len(pcm)) + pcm)

    xwm = tmp / "out.xwm"
    r = subprocess.run([str(XWMAENCODE), str(wav), str(xwm)],
                       capture_output=True, text=True)
    # xwmaencode has been seen to exit 0 while producing nothing, so check the
    # artifact rather than the exit code - a zero that wrote no file is not a pass.
    if not xwm.exists() or xwm.stat().st_size == 0:
        sys.exit(f"xwmaencode produced no output (exit {r.returncode})\n{r.stdout}{r.stderr}")
    payload = xwm.read_bytes()
    if payload[:4] != b"RIFF":
        sys.exit("xwmaencode output is not a RIFF - refusing to pack it")

    lip = pathlib.Path(a.lip).read_bytes() if a.lip else b""
    out.write_bytes(b"FUZE" + struct.pack("<II", 1, len(lip)) + lip + payload)

    if not a.keep_wav:
        wav.unlink(missing_ok=True)
    xwm.unlink(missing_ok=True)
    try:
        tmp.rmdir()
    except OSError:
        pass
    print(f"{out.name}: {secs:.2f}s  pcm {len(pcm):,} -> xwma {len(payload):,} "
          f"-> fuz {out.stat().st_size:,} bytes  (lip {len(lip)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
