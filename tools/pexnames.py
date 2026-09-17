import struct, sys
def names(path):
    b = open(path,'rb').read(); o = 16
    def ws(o):
        n = struct.unpack_from('<H', b, o)[0]
        return b[o+2:o+2+n].decode('utf-8','replace'), o+2+n
    for _ in range(3): _s, o = ws(o)
    cnt = struct.unpack_from('<H', b, o)[0]; o += 2
    st = []
    for _ in range(cnt):
        s, o = ws(o); st.append(s)
    if not b[o]: print(f'{path}: no debug info'); return
    o += 1 + 8
    fc = struct.unpack_from('<H', b, o)[0]; o += 2
    out = []
    for _ in range(fc):
        obj, state, fn = [st[struct.unpack_from('<H', b, o+2*k)[0]] for k in range(3)]
        o += 6
        t = b[o]; o += 1
        ic = struct.unpack_from('<H', b, o)[0]; o += 2 + 2*ic
        out.append((obj, state, fn, t))
    print(f'=== {path.split("/")[-1]}  ({fc} functions)')
    for obj, state, fn, t in out:
        if fn.startswith('::'): continue
        print(f'   {fn}{"   [state " + state + "]" if state else ""}')
for p in sys.argv[1:]: names(p)
