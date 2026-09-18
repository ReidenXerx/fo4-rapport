"""Rebuild the Papyrus base sources and flags file from the game's own archives.

The Creation Kit ships Data/Scripts/Source/Base and Institute_Papyrus_Flags.flg. When you only
have CreationKit.exe, neither exists, and nothing Papyrus compiles. Everything needed is still on
disk inside the vanilla BA2s; this reconstructs both.

    python tools/papyrus_setup.py <champollion.exe> <output dir> [--game "D:/GOGGames/Fallout 4 GOTY"]

Afterwards, compile against <output dir>/Source/Base. See docs/papyrus-toolchain.md, in particular
the part about decompiled sources losing default argument values.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib

SEP = chr(92)

VANILLA_ARCHIVES = (
    'Fallout4 - Misc.ba2',
    'DLCRobot - Main.ba2',
    'DLCworkshop01 - Main.ba2',
    'DLCCoast - Main.ba2',
    'DLCworkshop02 - Main.ba2',
    'DLCworkshop03 - Main.ba2',
    'DLCNukaWorld - Main.ba2',
)

# Any three of these agreeing is enough; they are read, never assumed.
FLAG_SOURCES = ('form.pex', 'quest.pex', 'scriptobject.pex')


def open_ba2(path):
    f = open(path, 'rb')
    hdr = f.read(24)
    if len(hdr) < 24 or hdr[:4] != b'BTDX':
        f.close()
        return None, None
    _v, atype = struct.unpack_from('<I4s', hdr, 4)
    count, name_off = struct.unpack_from('<IQ', hdr, 12)
    if atype != b'GNRL':
        f.close()
        return None, None
    recs = []
    data = f.read(count * 36)
    for i in range(count):
        _nh, _ext, _dh, _fl, off, psz, usz, _al = struct.unpack_from('<IIIIQIII', data, i * 36)
        recs.append((off, psz, usz))
    f.seek(name_off)
    nt = f.read()
    names, o = [], 0
    for _ in range(count):
        n = struct.unpack_from('<H', nt, o)[0]
        o += 2
        names.append(nt[o:o + n].decode('utf-8', 'replace'))
        o += n
    return f, list(zip(names, recs))


def extract(f, rec):
    off, psz, usz = rec
    f.seek(off)
    raw = f.read(psz if psz else usz)
    return zlib.decompress(raw) if psz else raw


def user_flags(blob):
    """Read a compiled script's own user-flag table: names and their bit positions."""
    b, o = blob, 16

    def ws(o):
        n = struct.unpack_from('<H', b, o)[0]
        return b[o + 2:o + 2 + n].decode('utf-8', 'replace'), o + 2 + n

    for _ in range(3):
        _s, o = ws(o)
    cnt = struct.unpack_from('<H', b, o)[0]
    o += 2
    strings = []
    for _ in range(cnt):
        s, o = ws(o)
        strings.append(s)

    if b[o]:
        o += 1 + 8
        fc = struct.unpack_from('<H', b, o)[0]
        o += 2
        for _ in range(fc):
            o += 7
            ic = struct.unpack_from('<H', b, o)[0]
            o += 2 + 2 * ic
        groups = struct.unpack_from('<H', b, o)[0]
        o += 2
        for _ in range(groups):
            o += 8
            nc = struct.unpack_from('<H', b, o)[0]
            o += 2 + 2 * nc
        structs = struct.unpack_from('<H', b, o)[0]
        o += 2
        for _ in range(structs):
            o += 4
            nc = struct.unpack_from('<H', b, o)[0]
            o += 2 + 2 * nc
    else:
        o += 1

    ufc = struct.unpack_from('<H', b, o)[0]
    o += 2
    out = {}
    for _ in range(ufc):
        idx = struct.unpack_from('<H', b, o)[0]
        o += 2
        out[strings[idx]] = b[o]
        o += 1
    return out


def basename(name):
    for sep in (SEP, '/'):
        name = name.rsplit(sep, 1)[-1]
    return name.lower()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('champollion')
    parser.add_argument('output')
    parser.add_argument('--game', default=os.path.join('D:' + os.sep, 'GOGGames', 'Fallout 4 GOTY'))
    args = parser.parse_args()

    data_dir = os.path.join(args.game, 'Data')
    pex_dir = os.path.join(args.output, 'pex')
    base_dir = os.path.join(args.output, 'Source', 'Base')
    quarantine = os.path.join(args.output, 'broken')
    for d in (pex_dir, base_dir):
        os.makedirs(d, exist_ok=True)

    # 1. extract every vanilla compiled script -------------------------------
    flags = {}
    total = 0
    for archive in VANILLA_ARCHIVES:
        path = os.path.join(data_dir, archive)
        if not os.path.exists(path):
            print('  missing (skipped): {}'.format(archive))
            continue
        f, entries = open_ba2(path)
        if f is None:
            continue
        written = 0
        for name, rec in entries:
            if not name.lower().endswith('.pex'):
                continue
            blob = extract(f, rec)
            if basename(name) in FLAG_SOURCES:
                try:
                    found = user_flags(blob)
                    for flag, bit in found.items():
                        if flag in flags and flags[flag] != bit:
                            raise SystemExit(
                                'flag {} disagrees between scripts: {} vs {}'.format(
                                    flag, flags[flag], bit))
                    flags.update(found)
                except SystemExit:
                    raise
                except Exception as exc:
                    print('  could not read flags from {}: {}'.format(name, exc))
            parts = name.replace('/', SEP).split(SEP)
            if parts and parts[0].lower() == 'scripts':
                parts = parts[1:]
            dest = os.path.join(pex_dir, *parts)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, 'wb') as fh:
                fh.write(blob)
            written += 1
        f.close()
        total += written
        print('  {}: {} scripts'.format(archive, written))
    print('extracted {} compiled scripts'.format(total))

    if not flags:
        raise SystemExit('no user-flag table found - cannot write the flags file')

    # 2. write the flags file, read out of the bytecode above -----------------
    flg_path = os.path.join(base_dir, 'Institute_Papyrus_Flags.flg')
    with open(flg_path, 'w', encoding='ascii', newline='\n') as fh:
        for flag, bit in sorted(flags.items(), key=lambda kv: kv[1]):
            fh.write('Flag {} {}\n'.format(flag, bit))
    print('wrote {} ({} flags)'.format(flg_path, len(flags)))

    # 3. decompile -----------------------------------------------------------
    print('decompiling...')
    result = subprocess.run(
        [args.champollion, pex_dir, '-p', base_dir, '-r', '-s', '-t'],
        capture_output=True, text=True)
    # A non-zero exit means some scripts failed, not that the run was useless.
    print('  champollion exit {}'.format(result.returncode))

    # 4. quarantine the empties, which abort the compiler on import scan ------
    pattern = re.compile(r'^\s*scriptname\s+\S+', re.IGNORECASE | re.MULTILINE)
    broken, ok = [], 0
    for root, _dirs, files in os.walk(base_dir):
        if os.path.abspath(root).startswith(os.path.abspath(quarantine)):
            continue
        for name in files:
            if not name.lower().endswith('.psc'):
                continue
            path = os.path.join(root, name)
            try:
                text = open(path, 'r', encoding='utf-8', errors='replace').read()
            except OSError:
                broken.append(path)
                continue
            if pattern.search(text):
                ok += 1
            else:
                broken.append(path)

    for path in broken:
        rel = os.path.relpath(path, base_dir)
        dest = os.path.join(quarantine, rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.move(path, dest)

    print('{} usable sources, {} unusable moved to {}'.format(ok, len(broken), quarantine))
    print()
    print('compile against: {}'.format(base_dir))
    print('remember: decompiled sources have NO default argument values - pass every argument.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
