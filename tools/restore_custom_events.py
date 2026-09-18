"""Put back the CustomEvent declarations Champollion drops.

A script that sends a custom event must declare it, or anything registering for that
event fails to compile with "X cannot generate Y events". Champollion does not recover
those declarations, but the names are still in the decompiled source: every send looks
like SendCustomEvent("<scriptname with : replaced by _>_<EventName>", args).

    python tools/restore_custom_events.py <decompiled .psc> [more.psc ...]

Idempotent: a script that already declares an event is left alone.
"""

import io
import re
import sys

SEND = re.compile(r'SendCustomEvent\(\s*"([^"]+)"', re.IGNORECASE)
SCRIPTNAME = re.compile(r'^\s*scriptname\s+(\S+)', re.IGNORECASE | re.MULTILINE)


def restore(path):
    with io.open(path, encoding='utf-8', errors='replace') as fh:
        text = fh.read()

    match = SCRIPTNAME.search(text)
    if not match:
        print('  {}: no Scriptname, skipped'.format(path))
        return 0

    script = match.group(1)
    # The colon stays: AAF sends 'aaf:aaf_api_OnSceneEnd', not 'aaf_aaf_api_...'.
    prefix = script.lower() + '_'

    events = []
    for raw in SEND.findall(text):
        low = raw.lower()
        if not low.startswith(prefix):
            continue
        name = raw[len(prefix):]
        if name and name not in events:
            events.append(name)

    if not events:
        print('  {}: sends no custom events of its own'.format(script))
        return 0

    missing = [
        e for e in events
        if not re.search(r'^\s*customevent\s+' + re.escape(e) + r'\s*$', text, re.IGNORECASE | re.MULTILINE)
    ]
    if not missing:
        print('  {}: all {} declarations already present'.format(script, len(events)))
        return 0

    block = '\n;-- Custom events (restored: Champollion does not recover these) --\n'
    block += ''.join('CustomEvent {}\n'.format(e) for e in missing)

    # After the last struct if there is one, otherwise after the Scriptname line.
    anchor = text.lower().rfind('\nendstruct')
    if anchor != -1:
        cut = text.index('\n', anchor + 1) + 1
    else:
        cut = text.index('\n', match.end()) + 1

    with io.open(path, 'w', encoding='utf-8') as fh:
        fh.write(text[:cut] + block + text[cut:])

    print('  {}: restored {} -> {}'.format(script, len(missing), ', '.join(missing)))
    return len(missing)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    total = 0
    for path in sys.argv[1:]:
        total += restore(path)
    print('{} declarations restored'.format(total))
    return 0


if __name__ == '__main__':
    sys.exit(main())
