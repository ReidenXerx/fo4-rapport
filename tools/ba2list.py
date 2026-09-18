"""List a GNRL BA2's contents, and optionally print one file out of it.

Usage:
    ba2list.py <archive.ba2> [substring]        list matching names
    ba2list.py <archive.ba2> --cat <exactname>  write one file to stdout
"""
import io
import os
import struct
import sys
import zlib


def entries(path):
    f = open(path, 'rb')
    hdr = f.read(24)
    if len(hdr) < 24 or hdr[:4] != b'BTDX':
        f.close()
        return None, None
    version, atype = struct.unpack_from('<I4s', hdr, 4)
    count, nameOff = struct.unpack_from('<IQ', hdr, 12)
    if atype != b'GNRL':
        f.close()
        return None, atype.decode('latin-1')

    recs = []
    data = f.read(count * 36)
    for i in range(count):
        _nh, _ext, _dh, _fl, off, psz, usz, _al = struct.unpack_from('<IIIIQIII', data, i * 36)
        recs.append((off, psz, usz))

    f.seek(nameOff)
    names = []
    for _ in range(count):
        ln = struct.unpack('<H', f.read(2))[0]
        names.append(f.read(ln).decode('latin-1'))

    return f, list(zip(names, recs))


def read(f, rec):
    off, psz, usz = rec
    f.seek(off)
    raw = f.read(psz if psz else usz)
    return zlib.decompress(raw) if psz else raw


def main():
    path = sys.argv[1]
    f, items = entries(path)
    if f is None:
        print('not a GNRL BA2 (%s)' % items)
        return

    if len(sys.argv) > 3 and sys.argv[2] == '--cat':
        want = sys.argv[3].lower().replace('/', '\\')
        for name, rec in items:
            if name.lower().replace('/', '\\') == want:
                sys.stdout.write(read(f, rec).decode('utf-8', 'replace'))
                return
        print('not in the archive: ' + want)
        return

    needle = sys.argv[2].lower() if len(sys.argv) > 2 else ''
    for name, rec in items:
        if needle in name.lower():
            print('%9d  %s' % (rec[2], name))


main()
