import struct, os, sys, zlib, io, collections

WANT = ['ChangeRelationshipStat','GetAttraction','ChangeStatMinimum','ChangeStatMaximum',
        'ChangeStat','GetActorData','SetActorLocked','StartScene','StopScene','QuickScene',
        'StartSceneByPosition','SetRelationshipRank','GetRelationshipRank']

def ba2_entries(path):
    """Yield (name, offset, packedSize, unpackedSize) for a GNRL BA2. None if not readable."""
    f = open(path, 'rb')
    hdr = f.read(24)
    if len(hdr) < 24 or hdr[:4] != b'BTDX':
        f.close(); return None, None
    version, atype = struct.unpack_from('<I4s', hdr, 4)
    count, nameOff = struct.unpack_from('<IQ', hdr, 12)
    if atype != b'GNRL':
        f.close(); return None, None
    recs = []
    data = f.read(count * 36)
    for i in range(count):
        _nh, _ext, _dh, _fl, off, psz, usz, _al = struct.unpack_from('<IIIIQIII', data, i * 36)
        recs.append((off, psz, usz))
    f.seek(nameOff)
    names = []
    nt = f.read()
    o = 0
    for _ in range(count):
        if o + 2 > len(nt): break
        n = struct.unpack_from('<H', nt, o)[0]; o += 2
        names.append(nt[o:o+n].decode('utf-8', 'replace')); o += n
    return f, list(zip(names, recs))

def pex_strings(blob):
    b = blob
    if len(b) < 20 or struct.unpack_from('<I', b, 0)[0] != 0xFA57C0DE: return None
    o = 16
    try:
        for _ in range(3):
            n = struct.unpack_from('<H', b, o)[0]; o += 2 + n
        cnt = struct.unpack_from('<H', b, o)[0]; o += 2
        out = []
        for _ in range(cnt):
            n = struct.unpack_from('<H', b, o)[0]
            out.append(b[o+2:o+2+n].decode('utf-8','replace')); o += 2 + n
        return out
    except Exception:
        return None

root = sys.argv[1]
archives = scripts = badarc = 0
hits = collections.defaultdict(set)
withscripts = []
for dp, _dn, fn in os.walk(root):
    for f in fn:
        if not f.lower().endswith('.ba2'): continue
        p = os.path.join(dp, f)
        try:
            fh, ents = ba2_entries(p)
        except Exception:
            badarc += 1; continue
        if fh is None: badarc += 1; continue
        archives += 1
        pex = [e for e in ents if e[0].lower().endswith('.pex')]
        if pex:
            withscripts.append((len(pex), os.path.relpath(p, root)))
        mod = os.path.relpath(p, root).split(os.sep)[0]
        for name, (off, psz, usz) in pex:
            scripts += 1
            fh.seek(off)
            raw = fh.read(psz if psz else usz)
            if psz:
                try: raw = zlib.decompress(raw)
                except Exception: continue
            st = pex_strings(raw)
            if not st: continue
            found = {w for w in WANT if w in st}
            if found: hits[mod].update(found)
        fh.close()
print(f'BA2 archives read: {archives} (skipped/unreadable {badarc}); .pex inside: {scripts}')
print(f'archives containing scripts: {len(withscripts)}')
for n, p in sorted(withscripts, reverse=True)[:12]:
    print(f'   {n:5d}  {p}')
print('\n-- AAF/relationship API use inside BA2 scripts --')
for mod in sorted(hits):
    print(f'   {mod}: {", ".join(sorted(hits[mod]))}')
