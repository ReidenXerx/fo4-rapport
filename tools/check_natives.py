"""Check that every Papyrus native declaration has a C++ binding, and vice versa.

The compiler cannot do this, and that is the point. A function declared `Global
Native` in Core.psc compiles anywhere it is called: the compiler only checks that
the DECLARATION exists. Whether anything ever called BindNativeMethod for it is not
knowable until the call is made in game, where it fails as a Papyrus error in a log
nobody is watching, on whichever poll happened to reach it first.

So this is the check that would otherwise be done by playing.

    python tools/check_natives.py                       # Rapport alone
    python tools/check_natives.py ../fo4-chemistry      # and what an addon calls

The second form is the one worth running after touching the addon API: it lists
every Rapport:Core call in the addon's scripts and says whether each is bound. An
addon compiling cleanly proves only that Core.psc promised the function.

Exit code 1 on any mismatch, so it can gate a build.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# A declaration looks like one of:
#   Function Trace(String asText) Global Native
#   Int Function CandidateCount() Global Native
#   Float Function HoursSincePair(Int aiFirst, Int aiSecond) Global Native
DECLARED = re.compile(
    r'^\s*(?:[A-Za-z:\[\]]+\s+)?Function\s+([A-Za-z_]\w*)\s*\(.*Global\s+Native',
    re.MULTILINE | re.IGNORECASE)

BOUND = re.compile(r'BindNativeMethod\(\s*kCoreScript,\s*"([^"]+)"sv')

CALLED = re.compile(r'Rapport:Core\.([A-Za-z_]\w*)')


def main():
    psc = ROOT / 'papyrus' / 'Rapport' / 'Core.psc'
    cpp = ROOT / 'src' / 'PapyrusLink.cpp'
    for path in (psc, cpp):
        if not path.exists():
            print('missing: {}'.format(path))
            return 1

    declared = set(DECLARED.findall(psc.read_text(encoding='utf-8')))
    bound = set(BOUND.findall(cpp.read_text(encoding='utf-8')))

    print('Core.psc declares {} native(s); PapyrusLink binds {}.'.format(
        len(declared), len(bound)))

    failed = False

    unbound = sorted(declared - bound)
    if unbound:
        failed = True
        print('\nDECLARED BUT NEVER BOUND. These compile and fail at RUNTIME:')
        for name in unbound:
            print('   {}'.format(name))

    # The other direction is not a failure, only waste: a bound native nothing
    # declares cannot be reached from Papyrus at all.
    unreachable = sorted(bound - declared)
    if unreachable:
        print('\nBound but not declared in Core.psc, so unreachable from Papyrus:')
        for name in unreachable:
            print('   {}'.format(name))

    # Optional: an addon's calls.
    for extra in sys.argv[1:]:
        addon = pathlib.Path(extra)
        if not addon.is_absolute():
            addon = (ROOT / extra).resolve()
        scripts = sorted(addon.rglob('*.psc'))
        if not scripts:
            print('\nno .psc under {}'.format(addon))
            continue

        used = set()
        for script in scripts:
            used.update(CALLED.findall(script.read_text(encoding='utf-8')))

        print('\n{} calls {} Rapport:Core function(s):'.format(addon.name, len(used)))
        for name in sorted(used):
            ok = name in bound
            failed = failed or not ok
            print('   {} {}'.format('ok  ' if ok else 'MISSING', name))

    print('\n{}'.format('MISMATCH' if failed else 'consistent'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
