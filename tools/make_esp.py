"""Generate a Rapport plugin: one quest with one script attached, nothing else.

The Creation Kit is the usual way to make this, and it cannot be driven headlessly.
The plugin needed here is small enough to write directly, and its shape is copied
from a real working record rather than from documentation: AAF.esm's AAF_MainQuest
carries a VMAD with four scripts, and parsing it end to end shows that a QUST VMAD
with no fragments has no trailing fragment section -- 2288 of 2288 bytes consumed.
So ours is version 6, object format 2, one script, zero properties.

    python tools/make_esp.py <output.esp> [script name] [quest editor id]

Defaults to the bridge. The second plugin it writes is Rapport_Moisturizer.esp,
which exists so that no script naming a Commonwealth Moisturizer type ever loads
on an install that does not have that mod -- an unresolvable reference inside
Rapport:Bridge would put the whole framework at risk to gain one integration.

Verify what came out with tools/read_esp.py.
"""

import struct
import sys

SCRIPT_NAME = 'Rapport:Bridge'
QUEST_EDID = 'RapportBridgeQuest'
AUTHOR = 'Rapport'
MASTER = 'Fallout4.esm'

# The first object id a new plugin may use; below 0x800 is reserved.
QUEST_FORMID = 0x01000800


def field(sig, data):
    if len(data) > 0xFFFF:
        raise ValueError('{} too large for a plain field'.format(sig))
    return sig.encode('ascii') + struct.pack('<H', len(data)) + data


def zstring(text):
    return text.encode('ascii') + b'\0'


def wstring(text):
    raw = text.encode('ascii')
    return struct.pack('<H', len(raw)) + raw


def record(sig, form_id, fields_blob, flags=0):
    # 24-byte record header: sig, dataSize, flags, formID, VCS1, formVersion, VCS2
    return (sig.encode('ascii')
            + struct.pack('<III', len(fields_blob), flags, form_id)
            + struct.pack('<IHH', 0, 131, 0)
            + fields_blob)


def group(label, records_blob):
    size = 24 + len(records_blob)
    return (b'GRUP'
            + struct.pack('<I', size)
            + label.encode('ascii')
            + struct.pack('<I', 0)          # top-level group
            + struct.pack('<IHH', 0, 0, 0)
            + records_blob)


def build(script_name, quest_edid):
    # ---- the quest ----------------------------------------------------------
    vmad = struct.pack('<hhH', 6, 2, 1)        # version, object format, script count
    vmad += wstring(script_name)
    vmad += struct.pack('<B', 0)               # status: local
    vmad += struct.pack('<H', 0)               # no properties

    # DNAM copied from AAF_MainQuest, a quest that starts itself and runs:
    # flags 0x0011 (start game enabled), priority 100. Copying a known-good
    # record beats inventing twelve bytes of flags.
    dnam = bytes.fromhex('11 00 64 67 00 00 00 00 00 00 00 00'.replace(' ', ''))

    quest = field('EDID', zstring(quest_edid))
    quest += field('VMAD', vmad)
    quest += field('DNAM', dnam)
    quest += field('NEXT', b'')                # alias section marker, empty

    quest_record = record('QUST', QUEST_FORMID, quest)
    quest_group = group('QUST', quest_record)

    # ---- the header ---------------------------------------------------------
    hedr = struct.pack('<fiI', 1.0, 1, QUEST_FORMID + 1)
    header_fields = field('HEDR', hedr)
    header_fields += field('CNAM', zstring(AUTHOR))
    header_fields += field('MAST', zstring(MASTER))
    header_fields += field('DATA', struct.pack('<Q', 0))

    header = record('TES4', 0, header_fields)
    return header + quest_group


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    script_name = sys.argv[2] if len(sys.argv) > 2 else SCRIPT_NAME
    quest_edid = sys.argv[3] if len(sys.argv) > 3 else QUEST_EDID

    blob = build(script_name, quest_edid)
    with open(sys.argv[1], 'wb') as fh:
        fh.write(blob)
    print('wrote {} ({} bytes)'.format(sys.argv[1], len(blob)))
    print('  quest  {} formID {:08X}'.format(quest_edid, QUEST_FORMID))
    print('  script {}'.format(script_name))
    print('  master {}'.format(MASTER))
    return 0


if __name__ == '__main__':
    sys.exit(main())
