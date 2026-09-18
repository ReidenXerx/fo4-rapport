"""List the QUST records in an ESP/ESM, with their editor ids and start-game-enabled flag.

Usage: espquests.py <plugin.esp> [RECORDTYPE]
"""
import struct
import sys
import zlib


def records(data, want, out, depth=0):
    o = 0
    n = len(data)
    while o + 24 <= n:
        sig = data[o:o + 4]
        size = struct.unpack_from('<I', data, o + 4)[0]
        if sig == b'GRUP':
            # size includes the 24-byte header
            records(data[o + 24:o + size], want, out, depth + 1)
            o += size
            continue

        flags = struct.unpack_from('<I', data, o + 8)[0]
        formID = struct.unpack_from('<I', data, o + 12)[0]
        body = data[o + 24:o + 24 + size]
        if flags & 0x00040000:  # compressed
            try:
                body = zlib.decompress(body[4:])
            except Exception:
                body = b''
        if sig == want:
            out.append((formID, body))
        o += 24 + size


def subrecords(body):
    o = 0
    n = len(body)
    while o + 6 <= n:
        sig = body[o:o + 4]
        size = struct.unpack_from('<H', body, o + 4)[0]
        yield sig, body[o + 6:o + 6 + size]
        o += 6 + size


def main():
    path = sys.argv[1]
    want = (sys.argv[2] if len(sys.argv) > 2 else 'QUST').encode('ascii')
    with open(path, 'rb') as fh:
        data = fh.read()

    out = []
    records(data, want, out)
    print('%s: %d %s record(s)' % (path, len(out), want.decode()))
    for formID, body in out:
        edid = ''
        dnam = None
        full = ''
        for sig, payload in subrecords(body):
            if sig == b'EDID':
                edid = payload.rstrip(b'\0').decode('latin-1')
            elif sig == b'FULL':
                full = payload.rstrip(b'\0').decode('latin-1')
            elif sig == b'DNAM' and len(payload) >= 2:
                dnam = struct.unpack_from('<H', payload, 0)[0]
        note = ''
        if dnam is not None:
            note = ' flags=0x%04X%s' % (dnam, ' START-GAME-ENABLED' if dnam & 0x0001 else '')
        print('  %08X  %-40s %s%s' % (formID, edid, full, note))


main()
