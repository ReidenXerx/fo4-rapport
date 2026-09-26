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

import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import make_dialogue  # noqa: E402

SCRIPT_NAME = 'Rapport:Bridge'
QUEST_EDID = 'RapportBridgeQuest'

# The medic runs on its OWN quest with its OWN script, and that is the whole
# point of it rather than an organisational preference.
#
# A stuck Papyrus stack blocks every later event ON THAT SCRIPT. The bridge is
# the script that calls AAF, so the bridge is the script that can get stuck --
# and a watchdog living inside it would be stuck with it, which is a watchdog
# that reports nothing exactly when there is something to report. A second
# script cannot be blocked by the first, and a second QUEST means the medic can
# stop and start the bridge's quest to get a fresh script instance without
# resetting itself in the process.
#
# It never calls AAF. That is what keeps it the thing that stays alive.
MEDIC_SCRIPT_NAME = 'Rapport:Medic'
MEDIC_QUEST_EDID = 'RapportMedicQuest'
AUTHOR = 'Rapport'
MASTER = 'Fallout4.esm'

# The first object id a new plugin may use; below 0x800 is reserved.
QUEST_FORMID = 0x01000800
TES4_LIGHT = 0x200   # the TES4 record flag that makes a plugin light (ESL)
MESSAGE_FORMID = 0x01000801
MEDIC_QUEST_FORMID = 0x01000802

# The only thing Rapport ever has to ask the player. AAF's main quest being
# stopped is the one failure this framework must not fix on its own: it may mean
# AAF is on its way out of this save, which nothing in the plugin can see and the
# player can. Every other AAF failure is repaired silently.
#
# ASCII only -- zstring() encodes as ascii and a smart quote is enough to fail it.
MESSAGE_EDID = 'RapportAAFQuestStopped'
MESSAGE_TITLE = 'Rapport'
MESSAGE_BODY = (
    'Advanced Animation Framework is installed, but its main quest is not running, '
    'so no scene can start. Rapport can start it again. '
    'If you are removing AAF from this save, leave it alone.')
MESSAGE_BUTTONS = ('Start AAF', 'Leave it alone')


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


def message_group():
    # Shape copied from three real two-button boxes in Fallout4.esm
    # (0024A36F, 0024A36E, 0024A36C): EDID, DESC, FULL, INAM, DNAM, then one ITXT
    # per button, consecutive and with no conditions between them. DNAM bit 0 is
    # what makes it a message box rather than a corner notification, and without
    # it Show() returns immediately and the buttons are never seen.
    #
    # Those records are localized, so their DESC/FULL/ITXT hold four-byte string
    # ids. Ours is not -- record('TES4', 0, ...) sets no flags, so bit 7 is clear
    # -- and the same subrecords take literal text.
    mesg = field('EDID', zstring(MESSAGE_EDID))
    mesg += field('DESC', zstring(MESSAGE_BODY))
    mesg += field('FULL', zstring(MESSAGE_TITLE))
    mesg += field('INAM', struct.pack('<I', 0))     # no icon
    mesg += field('DNAM', struct.pack('<I', 1))     # 0x01: message box
    for button in MESSAGE_BUTTONS:
        mesg += field('ITXT', zstring(button))

    return group('MESG', record('MESG', MESSAGE_FORMID, mesg))


def quest(script_name, quest_edid, form_id):
    vmad = struct.pack('<hhH', 6, 2, 1)        # version, object format, script count
    vmad += wstring(script_name)
    vmad += struct.pack('<B', 0)               # status: local
    vmad += struct.pack('<H', 0)               # no properties

    # DNAM copied from AAF_MainQuest, a quest that starts itself and runs:
    # flags 0x0011 (start game enabled), priority 100. Copying a known-good
    # record beats inventing twelve bytes of flags.
    dnam = bytes.fromhex('11 00 64 67 00 00 00 00 00 00 00 00'.replace(' ', ''))

    fields = field('EDID', zstring(quest_edid))
    fields += field('VMAD', vmad)
    fields += field('DNAM', dnam)
    fields += field('NEXT', b'')               # alias section marker, empty
    return record('QUST', form_id, fields)


def build(script_name, quest_edid, with_message):
    # ---- the quests ---------------------------------------------------------
    # The main plugin carries two: the bridge, and the medic that watches it.
    # The Moisturizer plugin carries one, because it has nothing to watch.
    quests = quest(script_name, quest_edid, QUEST_FORMID)
    dialogue_records, dialogue_top = 0, 0
    if with_message:
        quests += quest(MEDIC_SCRIPT_NAME, MEDIC_QUEST_EDID, MEDIC_QUEST_FORMID)
        # The voiced barks. Their quest and its Topic tree go INSIDE this QUST
        # group, because FO4 nests a quest's dialogue in the quest's own children
        # rather than at the top level - see make_dialogue.py.
        blob, dialogue_records, dialogue_top, _ = make_dialogue.build()
        quests += blob
    quest_group = group('QUST', quests)

    # Only the main plugin. The Moisturizer one exists so that no script naming a
    # Commonwealth Moisturizer type loads without that mod, and it has no reason
    # to carry a question the bridge asks.
    extra = message_group() if with_message else b''
    next_object = (MEDIC_QUEST_FORMID if with_message else QUEST_FORMID) + 1
    # Past every id actually used, or the Creation Kit would hand out one that
    # collides with a Topic the first time anybody opened this plugin in it.
    next_object = max(next_object, dialogue_top + 1)

    # ---- the header ---------------------------------------------------------
    # Record count: bridge quest, plus the medic quest and the message when this
    # is the main plugin. A wrong count here is the kind of thing that loads
    # fine and then goes wrong somewhere nowhere near it.
    hedr = struct.pack('<fiI', 1.0, (3 if with_message else 1) + dialogue_records, next_object)
    header_fields = field('HEDR', hedr)
    header_fields += field('CNAM', zstring(AUTHOR))
    header_fields += field('MAST', zstring(MASTER))
    header_fields += field('DATA', struct.pack('<Q', 0))

    # The Moisturizer plugin is LIGHT (0x200, ESL): its one quest sits at 0x800, the
    # range a light plugin holds, so it loads in the FE slot and takes no load-order
    # index (asked for on Discord, 2026-09-26). The main plugin cannot be: its dialogue
    # runs far past 0xFFF, and every voice file is named by its INFO's id.
    header = record('TES4', 0, header_fields, flags=0 if with_message else TES4_LIGHT)
    if not with_message and not 0x800 <= (QUEST_FORMID & 0x00FFFFFF) <= 0xFFF:
        raise SystemExit(f'{QUEST_FORMID:08X} is outside 0x800-0xFFF, the only ids a light plugin holds.')
    return header + quest_group + extra


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    script_name = sys.argv[2] if len(sys.argv) > 2 else SCRIPT_NAME
    quest_edid = sys.argv[3] if len(sys.argv) > 3 else QUEST_EDID

    blob = build(script_name, quest_edid, script_name == SCRIPT_NAME)
    with open(sys.argv[1], 'wb') as fh:
        fh.write(blob)
    print('wrote {} ({} bytes)'.format(sys.argv[1], len(blob)))
    print('  quest  {} formID {:08X}'.format(quest_edid, QUEST_FORMID))
    print('  script {}'.format(script_name))
    if script_name == SCRIPT_NAME:
        print('  medic  {} formID {:08X}'.format(MEDIC_QUEST_EDID, MEDIC_QUEST_FORMID))
        print('  script {}'.format(MEDIC_SCRIPT_NAME))
    print('  master {}'.format(MASTER))
    if script_name == SCRIPT_NAME:
        print('  message {} formID {:08X} ({} buttons)'.format(
            MESSAGE_EDID, MESSAGE_FORMID, len(MESSAGE_BUTTONS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
