"""Fully parse a QUST VMAD so the trailing fragment data (if any) is visible."""
import struct
import sys


class R:
    def __init__(self, b):
        self.b = b
        self.o = 0

    def u8(self):
        v = self.b[self.o]
        self.o += 1
        return v

    def i16(self):
        v = struct.unpack_from('<h', self.b, self.o)[0]
        self.o += 2
        return v

    def u16(self):
        v = struct.unpack_from('<H', self.b, self.o)[0]
        self.o += 2
        return v

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.o)[0]
        self.o += 4
        return v

    def f32(self):
        v = struct.unpack_from('<f', self.b, self.o)[0]
        self.o += 4
        return v

    def ws(self):
        n = self.u16()
        v = self.b[self.o:self.o + n].decode('latin-1')
        self.o += n
        return v


def prop_value(r, ptype, depth=0):
    if ptype == 1:      # object, objFormat 2
        form = r.u32()
        alias = r.u16()
        r.u16()
        return 'object({:08X} alias {})'.format(form, alias)
    if ptype == 2:
        return 'string("{}")'.format(r.ws())
    if ptype == 3:
        return 'int({})'.format(struct.unpack('<i', struct.pack('<I', r.u32()))[0])
    if ptype == 4:
        return 'float({})'.format(r.f32())
    if ptype == 5:
        return 'bool({})'.format(r.u8())
    if ptype == 6:
        return 'variable'
    if ptype == 7:
        n = r.u32()
        return 'struct[{}]'.format(n)
    if 11 <= ptype <= 15:   # arrays
        n = r.u32()
        inner = ptype - 10
        vals = [prop_value(r, inner, depth + 1) for _ in range(n)]
        return 'array[{}]{}'.format(n, vals[:3])
    raise ValueError('unknown property type {}'.format(ptype))


def parse(vmad):
    r = R(vmad)
    version = r.i16()
    objfmt = r.i16()
    count = r.u16()
    print('  version {} objFormat {} scripts {}'.format(version, objfmt, count))
    for i in range(count):
        name = r.ws()
        status = r.u8()
        props = r.u16()
        print('    script[{}] "{}" status {} properties {}'.format(i, name, status, props))
        for _ in range(props):
            pname = r.ws()
            ptype = r.u8()
            pstatus = r.u8()
            value = prop_value(r, ptype)
            print('       {} : {} (status {}) = {}'.format(pname, ptype, pstatus, value))
    print('  consumed {} of {} bytes'.format(r.o, len(vmad)))
    rest = vmad[r.o:]
    if rest:
        print('  TRAILING {} bytes: {}'.format(len(rest), rest[:64].hex(' ')))
        t = R(rest)
        qver = t.u8()
        fragcount = t.u16()
        fname = t.ws()
        print('    quest fragment header: version {} fragments {} file "{}"'.format(
            qver, fragcount, fname))
    else:
        print('  no trailing data')


path = sys.argv[1]
target = sys.argv[2]
blob = open(path, 'rb').read()


def fields(data):
    o = 0
    while o + 6 <= len(data):
        sig = data[o:o + 4].decode('latin-1')
        size = struct.unpack_from('<H', data, o + 4)[0]
        yield sig, data[o + 6:o + 6 + size]
        o += 6 + size


size = struct.unpack_from('<I', blob, 4)[0]
o = 24 + size
while o + 24 <= len(blob):
    if blob[o:o + 4] != b'GRUP':
        break
    gsize, label, _gtype = struct.unpack_from('<I4sI', blob, o + 4)
    if label == b'QUST':
        p = o + 24
        while p + 24 <= o + gsize:
            rsize, rflags, rform = struct.unpack_from('<III', blob, p + 4)
            data = blob[p + 24:p + 24 + rsize]
            fl = list(fields(data))
            edid = next((d.rstrip(b'\0').decode('latin-1') for s, d in fl if s == 'EDID'), '')
            if target.lower() in edid.lower():
                print('QUST {:08X} "{}"'.format(rform, edid))
                vmad = next((d for s, d in fl if s == 'VMAD'), None)
                if vmad:
                    parse(vmad)
                break
            p += 24 + rsize
    o += gsize
