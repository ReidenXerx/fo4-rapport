"""Write a .psc of native declarations straight out of a .pex.

Champollion decompiles bytecode, and a script that is nothing BUT native
declarations has no bytecode -- so it produces nothing at all and the source goes
missing silently. `UI` and `Utility` are both that shape, which is why neither
reached the reconstructed base even though Utility.pex was extracted with the
other 10,270.

Nothing here is guessed. The function table in a .pex carries the return type,
the parameter names and their types, and the global/native flags, so each
declaration is read out of the thing that defines it rather than copied from a
wiki.

    python tools/pex_natives.py <in.pex> [out.psc]

Prints to stdout when no output path is given. Refuses rather than guesses if a
function turns out to have bytecode, because this tool cannot write a body, and
refuses if the parse does not consume the file exactly.
"""

import struct
import sys


class Reader:
    def __init__(self, blob):
        self.b = blob
        self.o = 0

    def u8(self):
        v = self.b[self.o]
        self.o += 1
        return v

    def u16(self):
        v = struct.unpack_from('<H', self.b, self.o)[0]
        self.o += 2
        return v

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.o)[0]
        self.o += 4
        return v

    def u64(self):
        v = struct.unpack_from('<Q', self.b, self.o)[0]
        self.o += 8
        return v

    def wstr(self):
        n = self.u16()
        s = self.b[self.o:self.o + n].decode('utf-8', 'replace')
        self.o += n
        return s


def read_value(r):
    """A default value. Only its width matters here, never the value itself."""
    kind = r.u8()
    if kind == 0:
        return
    if kind in (1, 2):
        r.u16()
    elif kind in (3, 4):
        r.u32()
    elif kind == 5:
        r.u8()
    else:
        raise ValueError('unknown value type {}'.format(kind))


def parse(blob):
    r = Reader(blob)
    if r.u32() != 0xFA57C0DE:
        raise ValueError('not a pex')
    major, minor = r.u8(), r.u8()
    game = r.u16()
    r.u64()
    src = r.wstr()
    r.wstr()
    r.wstr()

    strings = [r.wstr() for _ in range(r.u16())]

    def S(i):
        return strings[i] if i < len(strings) else '<{}>'.format(i)

    if r.u8():
        r.u64()
        for _ in range(r.u16()):
            r.u16()
            r.u16()
            r.u16()
            r.u8()
            for _ in range(r.u16()):
                r.u16()
        for _ in range(r.u16()):
            r.u16()
            r.u16()
            r.u16()
            r.u32()
            for _ in range(r.u16()):
                r.u16()
        for _ in range(r.u16()):
            r.u16()
            r.u16()
            for _ in range(r.u16()):
                r.u16()

    flagbits = {}
    for _ in range(r.u16()):
        name, bit = r.u16(), r.u8()
        flagbits[bit] = S(name)

    def flag_names(mask):
        return [flagbits[b] for b in sorted(flagbits) if mask & (1 << b)]

    def read_function(name):
        ret = S(r.u16())
        doc = S(r.u16())
        uflags = r.u32()
        fflags = r.u8()
        params = [(S(r.u16()), S(r.u16())) for _ in range(r.u16())]
        for _ in range(r.u16()):
            r.u16()
            r.u16()
        ninstr = r.u16()
        return {
            'name': name, 'ret': ret, 'doc': doc,
            'global': bool(fflags & 1), 'native': bool(fflags & 2),
            'flags': flag_names(uflags), 'params': params, 'instructions': ninstr,
        }

    objects = []
    for _ in range(r.u16()):
        oname = S(r.u16())
        r.u32()
        obj = {'name': oname, 'parent': S(r.u16()), 'doc': S(r.u16())}
        # A Fallout 4 addition sitting between the doc string and the user flags,
        # and the one byte that makes or breaks this whole parse. Its position was
        # settled empirically, not assumed: reading it BEFORE the doc string also
        # consumes every one of the 10,271 vanilla scripts without complaint, but
        # it then reports each script's doc string as the script's own name. Read
        # after, the doc string is empty, which is what a script with no doc
        # comment should say.
        r.u8()
        obj['flags'] = flag_names(r.u32())
        r.u16()

        for _ in range(r.u16()):
            r.u16()
            for _ in range(r.u16()):
                r.u16()
                r.u16()
                r.u8()
                r.u32()
                read_value(r)
                r.u16()
                r.u8()

        variables = []
        for _ in range(r.u16()):
            vn, vt = S(r.u16()), S(r.u16())
            r.u32()
            read_value(r)
            r.u8()
            variables.append((vn, vt))
        obj['variables'] = variables

        properties = []
        for _ in range(r.u16()):
            pn, pt = S(r.u16()), S(r.u16())
            r.u16()
            puf = r.u32()
            pf = r.u8()
            if pf & 4:
                r.u16()
                properties.append((pn, pt, flag_names(puf), True))
            else:
                if pf & 1:
                    read_function(pn)
                if pf & 2:
                    read_function(pn)
                properties.append((pn, pt, flag_names(puf), False))
        obj['properties'] = properties

        states = []
        for _ in range(r.u16()):
            sn = S(r.u16())
            fns = [read_function(S(r.u16())) for _ in range(r.u16())]
            states.append((sn, fns))
        obj['states'] = states
        objects.append(obj)

    return {'source': src, 'major': major, 'minor': minor, 'game': game,
            'objects': objects, 'consumed': r.o, 'total': len(blob)}


def emit(parsed):
    out = []
    for obj in parsed['objects']:
        head = 'ScriptName {}'.format(obj['name'])
        if obj['parent']:
            head += ' extends {}'.format(obj['parent'])
        if obj['flags']:
            head += ' ' + ' '.join(obj['flags'])
        out.append(head)
        if obj['doc']:
            out.append('{' + obj['doc'] + '}')
        out.append('')
        out.append(';-- Reconstructed from {} by tools/pex_natives.py.'.format(
            parsed['source'] or 'the compiled script'))
        out.append(';-- Signatures are READ from the compiled function table, not transcribed.')
        out.append(';-- A pex records no default argument values, so pass every argument.')
        out.append('')

        for name, vtype in obj['variables']:
            out.append('{} {}'.format(vtype, name))
        if obj['variables']:
            out.append('')

        for pname, ptype, pflags, auto in obj['properties']:
            line = '{} Property {}'.format(ptype, pname)
            if auto:
                line += ' Auto'
            if pflags:
                line += ' ' + ' '.join(pflags)
            out.append(line)
        if obj['properties']:
            out.append('')

        for sname, fns in obj['states']:
            if sname:
                out.append('State {}'.format(sname))
            for fn in fns:
                if fn['instructions']:
                    raise SystemExit(
                        'ERROR: {}.{} has {} instructions - this tool writes '
                        'declarations only and cannot recover a body.'.format(
                            obj['name'], fn['name'], fn['instructions']))
                ret = fn['ret']
                sig = '' if ret in ('', 'None', 'none') else ret + ' '
                sig += 'Function {}('.format(fn['name'])
                sig += ', '.join('{} {}'.format(t, n) for n, t in fn['params'])
                sig += ')'
                for word in (['Global'] if fn['global'] else []) + \
                            (['Native'] if fn['native'] else []) + fn['flags']:
                    sig += ' ' + word
                out.append(sig)
                if fn['doc']:
                    out.append('{' + fn['doc'] + '}')
            if sname:
                out.append('EndState')
        out.append('')
    return '\n'.join(out)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    blob = open(sys.argv[1], 'rb').read()
    parsed = parse(blob)
    if parsed['consumed'] != parsed['total']:
        raise SystemExit('ERROR: consumed {} of {} bytes - the parse is wrong, '
                         'refusing to write a source from it'.format(
                             parsed['consumed'], parsed['total']))
    text = emit(parsed)
    if len(sys.argv) > 2:
        with open(sys.argv[2], 'w', encoding='utf-8', newline='\r\n') as fh:
            fh.write(text)
        print('wrote {} ({} object(s), {} of {} bytes consumed)'.format(
            sys.argv[2], len(parsed['objects']), parsed['consumed'], parsed['total']))
    else:
        print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
