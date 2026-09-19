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

# THE MOUTH IS NEVER LOCKED.
#
# lock="true" holds a morph against whatever else would move it, and the engine
# moves the whole mouth region constantly -- lip sync, breathing, an animation's
# own facial data. Two writers on the same morph every frame is a face that
# twitches several times a second.
#
# This started as a Jaw-Open-only rule, and that was too narrow: the flicker was
# reported again, and this time NOT during climax. Climax leans on Jaw Open,
# which was already unlocked; Rapport_Oral leans on Jaw Forward, both lip
# funnels, the lip rolls and Pucker -- every one of which was still locked. Same
# fault, different morph, which is what "unlock the one that bit us" earns you.
#
# Brows, eyelids and cheeks stay locked: they are what gives an expression its
# shape, and nothing here drives them per frame the way the mouth is driven.
# If a brow ever flickers, it joins this set rather than getting its own rule.
# THE JAW IS NOT SET AT ALL in Rapport_Climax or Rapport_Oral, and that is a
# SEPARATE fix from the unlocking below.
#
# Unlocking was the wrong lever twice. `lock` is AAF's own concept -- whether
# another AAF MOD may override our morph -- and it is no defence at all against
# the animation's own facial track. That fight exists at every intensity; it is
# only VISIBLE when the gap is large, and the amplitude of the twitch is simply
# |our value - the animation's|.
#
# The evidence is exact. Jaw Open reached 100 in precisely two sets, Climax and
# Oral, and those are precisely the two faces the owner reported a slamming chin
# on -- once each, on separate sessions, the second time after unlocking had
# supposedly fixed it. Pleasure_3 sits at 85 and has never been reported; nothing
# else exceeds 60.
#
# So the jaw is left out of both. Most sex animations animate the mouth
# themselves, so an open mouth is expected to survive -- just the animator's
# rather than ours -- and the brows, nose and eyes carry the expression, which is
# the part of this table the owner singled out as working.
#
# UNKNOWN, and the game decides it: whether ApplyMFGSet REPLACES the morph
# override or MERGES into it. If it merges, omitting Jaw Open leaves whatever the
# previous set wrote -- Climax would inherit Pleasure_3's 85 rather than nothing.
# Still better than 100, but not the same thing. Watch for a chin that is stuck
# part-open rather than twitching.
#
# Pleasure_3's 85 is the next suspect if this is not enough.
MOUTH = frozenset(
    ["Jaw Forward", "Jaw Open", "Left Jaw", "Right Jaw",
     "Lower Lip Funnel", "Upper Lip Funnel",
     "Lower Lip Roll In", "Lower Lip Roll Out",
     "Upper Lip Roll In", "Upper Lip Roll Out",
     "Pucker", "Sticky Lips", "Tongue To Roof"]
    + ["%s %s" % (side, part)
       for side in ("Left", "Right")
       for part in ("Lip Corner In", "Lip Corner Out",
                    "Lower Lip Down", "Lower Lip Up",
                    "Upper Lip Down", "Upper Lip Up",
                    "Smile", "Frown")])
assert MOUTH <= set(MORPHS), sorted(MOUTH - set(MORPHS))


def pair(left, right, value):
    """Most of this table is mirrored. Writing one side and forgetting the other
    is the single easiest way to produce a face with a twitch in it."""
    return [(left, value), (right, value)]


def sym(part, value):
    return pair("Left " + part, "Right " + part, value)


def scale(settings, factor):
    """Same face, dialled down. Used to make a style fit the moment: somebody
    gritting during anticipation is not gritting the way they do at the peak."""
    return [(name, int(round(value * factor))) for name, value in settings]


def overlay(base, extra):
    """extra WINS where it names a morph base also sets, so a style can contradict
    the base rather than only add to it -- which is the whole point of a style that
    furrows a brow the base was lifting."""
    out = dict(base)
    out.update(dict(extra))
    return list(out.items())


# ---- the three ways a person reacts ----------------------------------------
#
# Everyone gets the same set of expressions; these decide HOW that person wears
# them. An actor keeps their style for the whole playthrough because it is derived
# from their form id, so this reads as character rather than as the game shuffling
# faces at them.
#
# Style 1 is the original face, unchanged. The other two exist because half the
# morph table was going unused -- nose wrinkle, bared upper lip, the lower-lid
# squint that makes an expression read as felt rather than posed, the bitten lip --
# and because two NPCs wearing an identical climax at the same moment is the thing
# that gives autonomy away as a mod.
#
# `level` is how far into it the expression is, 0-100, and every style scales
# itself by it. Without that, a tense Kiss looks like a tense Climax.
# THE EYES ARE WHERE THE BUDGET WENT, now that the jaw is the animation's.
#
# An audit of the generated table found 15 of the 50 morphs never set above zero,
# and the loudest gap by far: `Upper Eye Lid Down` appears in 24 of 24 style-sets
# and `Upper Eye Lid Up` in NONE. Every face this mod could make narrowed or shut
# the eyes; not one ever opened them, so surprise, shock and being overwhelmed
# were unreachable -- which is most of what makes a face vibrant.
#
# Each style now has its own way of coming, and an actor keeps theirs for the
# whole playthrough because it is derived from their form id. The mapping follows
# what each style already IS rather than an arbitrary split: style 2 is the one
# that grits, and gritting with wide eyes is a contradiction.
#
# ONLY ABOVE level 70, so this reaches Oral, Pleasure_3 and Climax and leaves
# anticipation and kissing alone. A gasp during a kiss is a timer, not a reaction.
#
# Deliberately NO new mouth morphs. The jaw was just handed to the animation
# because we lose that fight; adding frowns and lip rolls would walk straight back
# into it. Eyes, brows, nose and cheeks are uncontested.
EYES_FROM = 70


def eyes(lvl, settings):
    """Style eye-work, or nothing at all below the threshold."""
    return scale(settings, lvl / 100.0) if lvl >= EYES_FROM else []


# ---- the three ways a person reacts ----------------------------------------
#
# Everyone gets the same set of expressions; these decide HOW that person wears
# them. An actor keeps their style for the whole playthrough because it is derived
# from their form id, so this reads as character rather than as the game shuffling
# faces at them.
#
# `level` is how far into it the expression is, 0-100, and every style scales
# itself by it. Without that, a tense Kiss looks like a tense Climax.
STYLES = [
    ("1", "lets go: open, lifted, soft -- and at the peak the eyes go WIDE",
     lambda lvl: overlay(
         # The outer brows lift, which is the "lifted" this style is named for and
         # was using only half of -- Right Outer Brow Up was one of the fifteen
         # morphs never set at all.
         scale(pair("Left Brow Outer Up", "Right Outer Brow Up", 55), lvl / 100.0),
         # Overrides, NOT additions: the base sets shut these eyes, and a lid that
         # is told to go both down and up at once is a lid doing neither.
         eyes(lvl, sym("Upper Eye Lid Down", 0) + sym("Upper Eye Lid Up", 85)
              + sym("Lower Eye Lid Down", 25)))),

    ("2", "grits: brows down and in, nose wrinkled, teeth bared, eyes screwed shut",
     lambda lvl: overlay(
         overlay(
             scale(sym("Middle Brow Down", 70) + sym("Nose Up", 65)
                   + sym("Upper Lip Up", 55) + sym("Lower Eye Lid Up", 60), lvl / 100.0),
             # Brow Squeeze is the one that must NOT be scaled away: it is what makes
             # the difference between furrowed and merely lowered.
             [("Brow Squeeze", int(round(80 * lvl / 100.0)))]),
         # This style already squinted hard; at the peak it closes completely.
         eyes(lvl, sym("Upper Eye Lid Down", 100) + sym("Lower Eye Lid Up", 75)))),

    ("3", "holds it in: lip bitten, the face uneven, one brow up, eyes unfocused",
     lambda lvl: overlay(
         scale(
             [("Lower Lip Roll In", 70), ("Sticky Lips", 45),
              # Deliberately ONE side. A real face is not symmetrical, and every set
              # here is mirrored, so the only place asymmetry can come from is a style.
              ("Left Smile", 45), ("Right Nose Up", 40), ("Left Brow Outer Up", 60),
              ("Right Middle Brow Down", 35)],
             lvl / 100.0),
         # Neither shut nor wide: half-lidded and somewhere else entirely. The lids
         # are deliberately UNEVEN, like the rest of this style.
         eyes(lvl, [("Left Upper Eye Lid Down", 60), ("Right Upper Eye Lid Down", 45),
                    ("Left Upper Eye Lid Up", 0), ("Right Upper Eye Lid Up", 0),
                    ("Left Lower Eye Lid Down", 35), ("Right Lower Eye Lid Down", 30)]))),
]

# Each set is (id, note, [(morph name, intensity 0-100), ...], lock, level).
# Intensities are deliberately short of 100 except at the peak: a face pinned to
# maximum on every morph reads as a rictus rather than as pleasure.
SETS = [
    ("Rapport_Clear",
     "Everything back to zero and unlocked. Applied when a scene ends and when a "
     "save is loaded that was made during one, so nobody is left wearing a face "
     "Rapport put on them.",
     [(name, 0) for name in MORPHS],
     False, 0),

    ("Rapport_Anticipation",
     "Before anything happens: lips just parted, brows lifted, eyes a little heavy.",
     [("Jaw Open", 20)] + sym("Middle Brow Up", 40) + sym("Lip Corner Out", 25)
     + sym("Upper Eye Lid Down", 20),
     True, 25),

    ("Rapport_Pleasure_1",
     "Early. A soft smile and half-closed eyes; nothing exaggerated.",
     [("Jaw Open", 30)] + sym("Smile", 50) + sym("Lower Eye Lid Down", 40)
     + sym("Upper Eye Lid Down", 45) + sym("Middle Brow Up", 50),
     True, 35),

    ("Rapport_Pleasure_2",
     "Building. Mouth further open, cheeks lifting, eyes nearly shut.",
     [("Jaw Open", 60), ("Lower Lip Roll Out", 40)] + sym("Middle Brow Up", 80)
     + sym("Cheek Up", 60) + sym("Upper Eye Lid Down", 70)
     + sym("Lower Eye Lid Down", 60) + sym("Lip Corner Out", 50),
     True, 60),

    ("Rapport_Pleasure_3",
     "Near the peak. Brows high and drawn in, eyes shut, mouth open.",
     [("Jaw Open", 85), ("Brow Squeeze", 40)] + sym("Cheek Up", 90)
     + sym("Middle Brow Up", 100) + sym("Outer Brow Down", 50)
     + sym("Upper Eye Lid Down", 90) + sym("Lower Eye Lid Down", 80),
     True, 85),

    ("Rapport_Climax",
     "The moment. Teeth showing, eyes screwed shut -- and the JAW IS NOT OURS. "
     "See the note on MOUTH below: the animation owns it.",
     [("Brow Squeeze", 60), ("Tongue To Roof", 60)]
     + sym("Cheek Up", 100) + sym("Middle Brow Up", 100)
     + sym("Upper Eye Lid Down", 100) + sym("Lower Eye Lid Down", 85)
     + sym("Lower Lip Down", 60),
     True, 100),

    ("Rapport_Oral",
     "Mouth working around something: funnelled lips. The JAW IS NOT OURS -- it "
     "was the loudest part of this set and the animation does it better.",
     [("Lower Lip Funnel", 60),
      ("Upper Lip Funnel", 60), ("Pucker", 40), ("Upper Lip Roll Out", 70),
      ("Lower Lip Roll Out", 50)]
     + sym("Upper Eye Lid Down", 50) + sym("Lower Eye Lid Down", 40),
     True, 70),

    ("Rapport_Kiss",
     "Lips pursed, eyes closed. Used for the foreplay tags, which otherwise get "
     "an expression that belongs to a different act entirely.",
     [("Pucker", 70), ("Jaw Open", 15)] + sym("Upper Eye Lid Down", 80)
     + sym("Smile", 20),
     True, 30),

    ("Rapport_Dazed",
     "Afterwards, briefly. Eyes heavy, a half smile, jaw slack.",
     [("Jaw Open", 25)] + sym("Upper Eye Lid Down", 45) + sym("Smile", 35)
     + sym("Middle Brow Up", 30),
     True, 30),
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

    emitted = []
    for setID, note, settings, lock, level in SETS:
        # Rapport_Clear has no variants: it is the reset, and there is only one
        # way to put a face back to nothing.
        if level == 0:
            variants = [("", "", [])]
        else:
            variants = [("_" + tag, " -- " + how, layer(level))
                        for tag, how, layer in STYLES]

        for suffix, how, extra in variants:
            name_v = setID + suffix
            emitted.append(name_v)
            out.write("<!-- %s -->\n" % (note + how))
            out.write('<mfgSet id="%s">\n' % name_v)
            for name, value in overlay(settings, extra):
                # THE MOUTH IS NEVER LOCKED, whatever the set asks for. See MOUTH.
                #
                # lock="true" holds a morph against whatever else would move it,
                # and the jaw is the one thing something else moves constantly:
                # the animation's own facial data, idle breathing, anything
                # driving a mouth frame by frame. A locked Jaw Open and a
                # per-frame animation both writing every frame is a chin that
                # opens and closes several times a second.
                #
                # Reported in game during CLIMAX, which is where it is worst: Jaw
                # Open was locked in every set and climbs 15, 20, 25, 30, 60, 85
                # to 100 at Climax and Oral. At 15 the argument is invisible; at
                # 100 it is a fully open jaw against a closed one.
                holds = lock and name not in MOUTH
                out.write('\t<setting morphID="%d" intensity="%d"%s/>  <!-- %s -->\n'
                          % (ID[name], value,
                             ' lock="true"' if holds else '', name))
            out.write('</mfgSet>\n\n')

    out.write('</mfgSetData>\n')

    root = pathlib.Path(__file__).resolve().parent.parent
    path = root / "data" / "AAF" / "Rapport_mfgSetData.xml"
    with io.open(path, 'w', encoding="utf-8", newline="\n") as fh:
        fh.write(out.getvalue())

    # The plugin builds these names by appending a style number, so a mismatch
    # here is a face that silently never appears. Printed so it can be checked.
    print("%s: %d set(s) = %d base(s) x %d style(s)"
          % (path.name, len(emitted), len(SETS) - 1, len(STYLES)))
    for name in emitted:
        print("   %s" % name)


main()
