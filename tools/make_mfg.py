"""Generates Data/AAF/Rapport_mfgSetData.xml.

The FO4 facial morph table is 50 entries and every set below is written against
it by NAME, so a reader can see what a face is doing without counting indices.
The table itself is not ours: it is quoted verbatim from the comment block in
CHAKPack_mfgSetData.xml, which is the only place in the whole AAF install that
writes it down. Everything else -- Atomic Lust, DR_pack, _T_ -- ships bare
numbers.

Run it after editing SETS; the XML is generated, not hand-maintained.
"""
import io
import pathlib

MORPHS = [
    "Brow Squeeze", "Jaw Forward", "Jaw Open", "Left Brow Outer Up",
    "Left Cheek Up", "Left Frown", "Left Jaw", "Left Lip Corner In",
    "Left Lip Corner Out", "Left Lower Eye Lid Down", "Left Lower Eye Lid Up",
    "Left Lower Lip Down", "Left Lower Lip Up", "Left Middle Brow Down",
    "Left Middle Brow Up", "Left Nose Up", "Left Outer Brow Down", "Left Smile",
    "Left Upper Eye Lid Down", "Left Upper Eye Lid Up", "Left Upper Lip Down",
    "Left Upper Lip Up", "Lower Lip Funnel", "Lower Lip Roll In",
    "Lower Lip Roll Out", "Pucker", "Right Outer Brow Up", "Right Cheek Up",
    "Right Frown", "Right Jaw", "Right Lip Corner In", "Right Lip Corner Out",
    "Right Lower Eye Lid Down", "Right Lower Eye Lid Up", "Right Lower Lip Down",
    "Right Lower Lip Up", "Right Middle Brow Down", "Right Middle Brow Up",
    "Right Nose Up", "Right Outer Brow Down", "Right Smile",
    "Right Upper Eye Lid Down", "Right Upper Eye Lid Up", "Right Upper Lip Down",
    "Right Upper Lip Up", "Sticky Lips", "Upper Lip Funnel", "Upper Lip Roll In",
    "Upper Lip Roll Out", "Tongue To Roof",
]
ID = {name: i for i, name in enumerate(MORPHS)}


def pair(left, right, value):
    """Most of this table is mirrored. Writing one side and forgetting the other
    is the single easiest way to produce a face with a twitch in it."""
    return [(left, value), (right, value)]


def sym(part, value):
    return pair("Left " + part, "Right " + part, value)


# Each set is (id, note, [(morph name, intensity 0-100), ...]).
# Intensities are deliberately short of 100 except at the peak: a face pinned to
# maximum on every morph reads as a rictus rather than as pleasure.
SETS = [
    ("Rapport_Clear",
     "Everything back to zero and unlocked. Applied when a scene ends and when a "
     "save is loaded that was made during one, so nobody is left wearing a face "
     "Rapport put on them.",
     [(name, 0) for name in MORPHS],
     False),

    ("Rapport_Anticipation",
     "Before anything happens: lips just parted, brows lifted, eyes a little heavy.",
     [("Jaw Open", 20)] + sym("Middle Brow Up", 40) + sym("Lip Corner Out", 25)
     + sym("Upper Eye Lid Down", 20),
     True),

    ("Rapport_Pleasure_1",
     "Early. A soft smile and half-closed eyes; nothing exaggerated.",
     [("Jaw Open", 30)] + sym("Smile", 50) + sym("Lower Eye Lid Down", 40)
     + sym("Upper Eye Lid Down", 45) + sym("Middle Brow Up", 50),
     True),

    ("Rapport_Pleasure_2",
     "Building. Mouth further open, cheeks lifting, eyes nearly shut.",
     [("Jaw Open", 60), ("Lower Lip Roll Out", 40)] + sym("Middle Brow Up", 80)
     + sym("Cheek Up", 60) + sym("Upper Eye Lid Down", 70)
     + sym("Lower Eye Lid Down", 60) + sym("Lip Corner Out", 50),
     True),

    ("Rapport_Pleasure_3",
     "Near the peak. Brows high and drawn in, eyes shut, mouth open.",
     [("Jaw Open", 85), ("Brow Squeeze", 40)] + sym("Cheek Up", 90)
     + sym("Middle Brow Up", 100) + sym("Outer Brow Down", 50)
     + sym("Upper Eye Lid Down", 90) + sym("Lower Eye Lid Down", 80),
     True),

    ("Rapport_Climax",
     "The moment. Jaw wide, teeth showing, eyes screwed shut.",
     [("Jaw Open", 100), ("Brow Squeeze", 60), ("Tongue To Roof", 60)]
     + sym("Cheek Up", 100) + sym("Middle Brow Up", 100)
     + sym("Upper Eye Lid Down", 100) + sym("Lower Eye Lid Down", 85)
     + sym("Lower Lip Down", 60),
     True),

    ("Rapport_Oral",
     "Mouth working around something: funnelled lips, jaw forward and open. "
     "Built on the same morphs Atomic Lust's own Blowjob set uses.",
     [("Jaw Open", 100), ("Jaw Forward", 60), ("Lower Lip Funnel", 60),
      ("Upper Lip Funnel", 60), ("Pucker", 40), ("Upper Lip Roll Out", 70),
      ("Lower Lip Roll Out", 50)]
     + sym("Upper Eye Lid Down", 50) + sym("Lower Eye Lid Down", 40),
     True),

    ("Rapport_Kiss",
     "Lips pursed, eyes closed. Used for the foreplay tags, which otherwise get "
     "an expression that belongs to a different act entirely.",
     [("Pucker", 70), ("Jaw Open", 15)] + sym("Upper Eye Lid Down", 80)
     + sym("Smile", 20),
     True),

    ("Rapport_Dazed",
     "Afterwards, briefly. Eyes heavy, a half smile, jaw slack.",
     [("Jaw Open", 25)] + sym("Upper Eye Lid Down", 45) + sym("Smile", 35)
     + sym("Middle Brow Up", 30),
     True),
]

HEADER = '''<mfgSetData xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="common.xsd">
<defaults loadPriority="10"/>

<!--
\tGENERATED BY tools/make_mfg.py -- edit that, not this.

\tRapport's own facial expressions.

\tThe reason these exist: across the entire AAF install there are ten mfgSet
\treferences in total, and the pack that was playing when the owner first watched
\ta Rapport scene has exactly one (Sleep_EyesClosed). The faces are stony because
\tthe animations never ask for anything else, not because anything is broken.

\tUnlike the overlays, this layer costs nothing and depends on nobody: a morph id
\tis an index into the engine's own facial morph table, so there is no texture to
\tship and no mod to credit. Rapport can drive faces on a completely vanilla
\tinstall.

\tThe morph table, 0-49, is quoted from the comment block in
\tCHAKPack_mfgSetData.xml -- the only place in the AAF install that writes it
\tdown. Every other pack ships bare numbers.

\tlock="true" holds a morph against whatever else would move it. Rapport_Clear is
\tthe one set that does not lock, because its whole job is to let go.
-->

'''


def main():
    out = io.StringIO()
    out.write(HEADER)

    for setID, note, settings, lock in SETS:
        out.write("<!-- %s -->\n" % note)
        out.write('<mfgSet id="%s">\n' % setID)
        for name, value in settings:
            out.write('\t<setting morphID="%d" intensity="%d"%s/>  <!-- %s -->\n'
                      % (ID[name], value, ' lock="true"' if lock else '', name))
        out.write('</mfgSet>\n\n')

    out.write('</mfgSetData>\n')

    root = pathlib.Path(__file__).resolve().parent.parent
    path = root / "data" / "AAF" / "Rapport_mfgSetData.xml"
    with io.open(path, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(out.getvalue())

    print("%s: %d set(s)" % (path.name, len(SETS)))
    for setID, _, settings, _ in SETS:
        print("   %-24s %d morph(s)" % (setID, len(settings)))


main()
