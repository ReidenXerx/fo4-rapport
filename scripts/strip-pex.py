"""Rewrite the build-machine fields of compiled Papyrus scripts, in place.

    python scripts/strip-pex.py <folder> <label>

The compiler writes three strings into every .pex header: the source file's full
path, the Windows user name and the computer name. A release is a stranger's
download, and those three name this machine (dev-vs-release check, 2026-09-26).
The game never reads them: a Papyrus stack trace prints "<unknown file>" for a
script without debug info, which is every script in the logs here, ours included.

Each becomes neutral: the source keeps its name without the folder it was
compiled from ("Approach.psc"), and user and machine become <label> and "release".
Nothing after the header moves: the rest of the file is compared byte for byte
before it is written, and a file that is not a Fallout 4 .pex is refused.

Header, Fallout 4 (little-endian): u32 magic 0xFA57C0DE, u8 major, u8 minor,
u16 game id, u64 compile time, then three u16-length strings.
"""
import pathlib
import struct
import sys

MAGIC = 0xFA57C0DE


def read_str(b, i):
    n = struct.unpack_from('<H', b, i)[0]
    return b[i + 2:i + 2 + n], i + 2 + n


def pack_str(s):
    return struct.pack('<H', len(s)) + s


def strip(path, label):
    b = path.read_bytes()
    if len(b) < 16 or struct.unpack_from('<I', b, 0)[0] != MAGIC:
        raise SystemExit(f'{path}: not a Fallout 4 .pex')
    i = 16
    source, i = read_str(b, i)
    _user, i = read_str(b, i)
    _machine, i = read_str(b, i)
    rest = b[i:]
    name = source.replace(b'/', b'\\').rsplit(b'\\', 1)[-1]
    out = b[:16] + pack_str(name) + pack_str(label.encode('ascii')) + pack_str(b'release') + rest
    if out[-len(rest):] != rest or out[:16] != b[:16]:
        raise SystemExit(f'{path}: the body moved - refusing to write')
    path.write_bytes(out)
    return len(b) - len(out)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    root, label = pathlib.Path(sys.argv[1]), sys.argv[2]
    files = sorted(root.rglob('*.pex'))
    if not files:
        raise SystemExit(f'no .pex under {root}')
    for f in files:
        strip(f, label)
    leaks = [f for f in files if any(s in f.read_bytes() for s in (b'Users\\', b'DESKTOP-'))]
    if leaks:
        raise SystemExit('still naming this machine: ' + ', '.join(str(f) for f in leaks))
    print(f'  {len(files)} .pex: build-machine path, user and computer name replaced')


if __name__ == '__main__':
    main()
