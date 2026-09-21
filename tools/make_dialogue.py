"""Rapport's voiced dialogue: one quest, and one Topic + one line per bark.

Imported by make_esp.py, which places the result inside its QUST group. Every
byte shape here is copied from Fallout4.esm rather than from documentation.

THE NESTING, which is the thing that is easy to get silently wrong. In FO4 a
dialogue Topic does NOT sit at the top level of a plugin. It lives inside the
children of the quest that owns it:

    GRUP type 0  "QUST"
      QUST                       <- the dialogue quest
      GRUP type 10 <quest id>    <- that quest's children
        DIAL                     <- one Topic
        GRUP type 7 <topic id>   <- that Topic's children
          INFO                   <- one spoken line

Measured: the greeting Mayor McDonough spoke in testing (DIAL 000048EB) sits in
GRUP type 0 QUST > GRUP type 10 00003648. A top-level scan of Fallout4.esm for a
DIAL group finds nothing at all, which is how this was nearly missed. Topics put
at the top level would be records the engine never looks at.

ONE TOPIC PER LINE, deliberately. Rapport picks the exact line in code - by
persona, scenario, role, audience and speaker gender - and calls Say on that
Topic. A Topic holding many lines would hand the choice back to the engine,
which picks among a Topic's lines by condition and chance; the same greeting
topic produced two different lines in two calls during testing. We want the
choice to stay ours.

NOT LOCALIZED. Fallout4.esm sets TES4 flag 0x80, so its NAM1 holds a four-byte
string-table id. Rapport.esp does not, so NAM1 takes the literal text. Copying
the base game's NAM1 would put four bytes of garbage in every subtitle.

NO CONDITIONS. The commonest spoken, unconditioned line shape in Fallout4.esm
(396 of them) has no CTDA at all. With no voice-type condition, any speaker may
say it and the engine looks the audio up under THEIR voice type - so one line
serves all 32 voice types, and a voice type with no file degrades to silence
plus a subtitle rather than to a wrong voice.
"""
import json
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Its own quest, not the bridge's: the medic stops and restarts the bridge quest
# to get a fresh script instance, and dialogue owned by it would briefly belong
# to a stopped quest every time that happened.
DIALOGUE_QUEST_FORMID = 0x01000803
DIALOGUE_QUEST_EDID = 'RapportDialogueQuest'

# The branch every Topic belongs to. FO4's own Say()-driven topics - the follower
# commands, category 0 CUST exactly like ours - all sit in a Dialogue Branch
# (DLBR FollowersSayTopics) and point at it through BNAM. The first build had no
# branch: its Topics resolved, their quest was running, Say was called, and
# nothing was ever spoken. Diffed against a line the game DOES speak via Say, the
# branch was the structural difference.
BRANCH_FORMID = 0x01000804
BRANCH_EDID = 'RapportSayTopics'

# A Topic's id is its line's id plus this. The line (INFO) takes the registry id
# because the AUDIO is named by it; the Topic needs an id of its own and gets one
# derived from the line's, so it is just as stable without a second registry.
TOPIC_OFFSET = 0x00100000

PLUGIN = 'Rapport.esp'


def _field(sig, data):
    if len(data) > 0xFFFF:
        raise ValueError(f'{sig} too large for a plain field')
    return sig.encode('ascii') + struct.pack('<H', len(data)) + data


def _zstring(text):
    # ASCII: a smart quote or an em dash fails here rather than becoming mojibake
    # in a subtitle. The line lint keeps the bank to ASCII for the same reason.
    return text.encode('ascii') + b'\0'


def _record(sig, form_id, fields_blob, flags=0):
    return (sig.encode('ascii')
            + struct.pack('<III', len(fields_blob), flags, form_id)
            + struct.pack('<IHH', 0, 131, 0)
            + fields_blob)


def _child_group(form_id, group_type, blob):
    """A GRUP whose label is a form id: type 10 (quest children) or 7 (topic)."""
    return (b'GRUP'
            + struct.pack('<I', 24 + len(blob))
            + struct.pack('<I', form_id)
            + struct.pack('<I', group_type)
            + struct.pack('<IHH', 0, 0, 0)
            + blob)


def dialogue_quest():
    # No VMAD: this quest has no script, it only owns Topics. DNAM copied from
    # the same known-good AAF_MainQuest record make_esp.py uses - start game
    # enabled, so its Topics are always available to Say.
    dnam = bytes.fromhex('110064670000000000000000')
    fields = _field('EDID', _zstring(DIALOGUE_QUEST_EDID))
    fields += _field('DNAM', dnam)
    fields += _field('NEXT', b'')
    return _record('QUST', DIALOGUE_QUEST_FORMID, fields)


def branch(first_topic_id):
    # Shape of DLBR FollowersSayTopics (0002AE5A): owning quest, TNAM zero, DNAM
    # 0x01 (top-level), and SNAM naming ONE starting topic. One branch holds many
    # topics - that one holds all twelve follower Say topics - so every Topic here
    # points at this single record.
    fields = _field('EDID', _zstring(BRANCH_EDID))
    fields += _field('QNAM', struct.pack('<I', DIALOGUE_QUEST_FORMID))
    fields += _field('TNAM', struct.pack('<I', 0))
    fields += _field('DNAM', struct.pack('<I', 1))
    fields += _field('SNAM', struct.pack('<I', first_topic_id))
    return _record('DLBR', BRANCH_FORMID, fields)


def topic(topic_id, info_id, edid):
    # Shape of a CUST topic in Fallout4.esm (DIAL 0002B96F), minus the optional
    # branch and keyword: priority 50.0, owning quest, DATA zero, subtype CUST,
    # and the count of lines it holds.
    fields = _field('EDID', _zstring(edid))
    fields += _field('PNAM', struct.pack('<f', 50.0))
    fields += _field('BNAM', struct.pack('<I', BRANCH_FORMID))
    fields += _field('QNAM', struct.pack('<I', DIALOGUE_QUEST_FORMID))
    fields += _field('DATA', struct.pack('<I', 0))
    fields += _field('SNAM', b'CUST')
    fields += _field('TIFC', struct.pack('<I', 1))
    return _record('DIAL', topic_id, fields)


def line(info_id, text):
    # Byte for byte the commonest spoken, unconditioned INFO in Fallout4.esm,
    # except NAM1, which is literal text here and a string id there.
    #
    # TRDA's second field is the RESPONSE NUMBER. It is the "_1" in the audio
    # file's name - <INFO & 0xFFFFFF>_1.fuz - so it must be 1.
    trda = bytes.fromhex('ffffffff' '01000000' '00010000' 'ffffffff' 'ffffffff')
    # ENAM 0x02 and no NAM9: both as in INFO 00083C0B, a line the game actually
    # speaks via Say. The first build copied the commonest UNCONDITIONED spoken
    # line, which is scene dialogue - structurally close, different role. GNAM
    # stays absent: there it links to a SHARED response, and ours have their own.
    fields = _field('ENAM', struct.pack('<I', 2))
    fields += _field('TRDA', trda)
    fields += _field('NAM1', _zstring(text))
    fields += _field('NAM2', b'\0')
    fields += _field('NAM3', b'\0')
    fields += _field('NAM4', b'\0')
    fields += _field('NAM0', b'\0')
    fields += _field('INAM', struct.pack('<I', 1))
    return _record('INFO', info_id, fields)


def load(bank='voice/lines.json', registry='voice/formids.json'):
    """[(line, info_id, topic_id)] for every line in the bank, ids from the registry."""
    lines = json.loads((ROOT / bank).read_text(encoding='utf-8'))['lines']
    reg = json.loads((ROOT / registry).read_text(encoding='utf-8'))['assigned']
    out, missing = [], []
    for ln in lines:
        if ln['id'] not in reg:
            missing.append(ln['id'])
            continue
        info_id = int(reg[ln['id']], 16)
        if info_id >= 0x01000000 + TOPIC_OFFSET:
            raise ValueError(f'{ln["id"]}: line id {info_id:08X} would collide with the '
                             f'topic range above +{TOPIC_OFFSET:X}')
        out.append((ln, info_id, info_id + TOPIC_OFFSET))
    if missing:
        raise SystemExit(f'{len(missing)} line(s) have no FormID - run scripts/formids.py '
                         f'first. Building without them would ship lines with no audio '
                         f'mapping: {missing[:3]}')
    return out


def build(bank='voice/lines.json'):
    """Bytes that go INSIDE make_esp.py's QUST group, and the record count."""
    entries = load(bank)
    children = branch(entries[0][2]) if entries else b''
    for ln, info_id, topic_id in entries:
        children += topic(topic_id, info_id, 'Rapport_' + ln['id'])
        children += _child_group(topic_id, 7, line(info_id, ln['text']))
    blob = dialogue_quest() + _child_group(DIALOGUE_QUEST_FORMID, 10, children)
    # Records only - the convention make_esp.py already ships with: one quest,
    # plus a Topic and a line per entry. Groups are not counted.
    count = 1 + (1 if entries else 0) + 2 * len(entries)   # quest, branch, topics, lines
    top = max((t for _, _, t in entries), default=DIALOGUE_QUEST_FORMID)
    return blob, count, top, entries


def voice_path(voice_type, info_id):
    """Where the engine looks for a line's audio. The load-order byte is masked."""
    return f'Sound/Voice/{PLUGIN}/{voice_type}/{info_id & 0x00FFFFFF:08X}_1.fuz'
