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
import json
import pathlib
import re

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
    # MEASURED 2026-09-24 (anatomy's probe=1, 14 lines: 6 on faces Rapport held, 8
    # vanilla): the MFG ids a line's lip sync actually moves, minus 3/26, 4/27 and
    # 14/37 -- the brows and cheeks, which moved only because anatomy's A-26 reaction
    # and scene faces write them mid-line. The first guess also handed over the frowns,
    # the sideways jaw and Lip Corner Out, which no line ever moved: dropped.
    ["Jaw Forward", "Jaw Open",
     "Lower Lip Funnel", "Upper Lip Funnel",
     "Lower Lip Roll In", "Lower Lip Roll Out",
     "Upper Lip Roll In", "Upper Lip Roll Out",
     "Pucker", "Sticky Lips", "Tongue To Roof"]
    + ["%s %s" % (side, part)
       for side in ("Left", "Right")
       for part in ("Lip Corner In",
                    "Lower Lip Down", "Lower Lip Up",
                    "Upper Lip Down", "Upper Lip Up",
                    "Smile")])
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

    # ---- the vulgar three ---------------------------------------------------
    #
    # Added because the style axis turned out to be the thing that gives an NPC a
    # face of their own: it is derived from their form id, so it never changes
    # for them, and the same person comes the same way every time. Three ways was
    # not enough population for that to read as character rather than coincidence.
    #
    # Nothing here goes above 80. The jaw taught that lesson: the visible twitch
    # is |our value - whatever else is writing the morph|, and 100 is where it
    # stops being an expression and becomes a fight. These also spend the morphs
    # the earlier audit found completely unused -- Frown, Lip Corner In, Lower Lip
    # Up, Upper Lip Roll In -- which is most of what was left on the table.
    #
    # NO JAW in any of them, for the same reason Climax and Oral have none.

    ("4", "gone: vacant, tongue out, the lights are off",
     lambda lvl: overlay(
         scale([("Tongue To Roof", 70), ("Left Lower Lip Down", 55),
                ("Right Lower Lip Down", 55), ("Left Frown", 30), ("Right Frown", 30),
                ("Left Middle Brow Up", 70), ("Right Middle Brow Up", 70)],
               lvl / 100.0),
         # Lids hauled wide while the rest of the face gives up. FO4 has no
         # eyeball-roll morph, so "rolled back" has to be read from the lid.
         eyes(lvl, sym("Upper Eye Lid Down", 0) + sym("Upper Eye Lid Up", 95)
              + sym("Lower Eye Lid Up", 30)))),

    ("5", "sneers: lip curled off the teeth, contemptuous, enjoying it meanly",
     lambda lvl: overlay(
         scale([("Left Upper Lip Up", 75), ("Right Upper Lip Up", 45),
                ("Left Nose Up", 65), ("Right Nose Up", 35),
                ("Left Lip Corner In", 50), ("Right Lip Corner In", 50),
                ("Left Lower Lip Up", 40), ("Right Lower Lip Up", 40),
                ("Left Brow Outer Up", 70), ("Right Middle Brow Down", 45)],
               lvl / 100.0),
         # Narrowed, not shut. A sneer that closes its eyes is just a wince.
         eyes(lvl, sym("Upper Eye Lid Down", 55) + sym("Lower Eye Lid Up", 70)))),

    ("6", "grins: wide, shameless, teeth out and thoroughly pleased",
     lambda lvl: overlay(
         scale([("Left Smile", 80), ("Right Smile", 65),
                ("Left Lip Corner Out", 60), ("Right Lip Corner Out", 60),
                ("Left Upper Lip Up", 40), ("Right Upper Lip Up", 40),
                ("Left Cheek Up", 70), ("Right Cheek Up", 70),
                ("Left Middle Brow Up", 60), ("Right Middle Brow Up", 60)],
               lvl / 100.0),
         # Cheeks this high push the lower lid up on their own; the squeeze is
         # what separates a real grin from bared teeth.
         eyes(lvl, sym("Upper Eye Lid Down", 65) + sym("Lower Eye Lid Up", 75)))),
]

# FACE AUTHORITY (owner, 2026-09-24: "we need grab whole power on ruling things we
# rule in rapport including expressions bc we need to be SOT").
#
# Everything above about losing the jaw was true of the AAF path: the engine merges a
# face as max(override, animation) (0x6689D0, fo4-anatomy's field notes), so an mfg
# set can open what the animation leaves closed and never close what it opens. The
# owner saw exactly that in cowgirl: Pleasure_3's Jaw Open 85 -- the "next suspect"
# named above -- wide open like a blowjob, and a close-open flicker on top.
#
# With Anatomy's cbp.dll installed, Rapport's values REPLACE the merged ones after
# that merge, for every morph (FaceAuthority, via faces.json below). So the jaw is
# ours again, and set deliberately low: lips parted when building, open on the moan
# at the peak. The blink stays the engine's, and Anatomy's contact mouth still opens
# the jaw around whatever is in it. Without cbp.dll this XML is the whole story,
# and the history above still applies to it.
#
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
     [("Jaw Open", 15)] + sym("Middle Brow Up", 40) + sym("Lip Corner Out", 25)
     + sym("Upper Eye Lid Down", 20),
     True, 25),

    ("Rapport_Pleasure_1",
     "Early. A soft smile and half-closed eyes; nothing exaggerated.",
     [("Jaw Open", 15)] + sym("Smile", 50) + sym("Lower Eye Lid Down", 40)
     + sym("Upper Eye Lid Down", 45) + sym("Middle Brow Up", 50),
     True, 35),

    ("Rapport_Pleasure_2",
     "Building. Mouth further open, cheeks lifting, eyes nearly shut.",
     [("Jaw Open", 25), ("Lower Lip Roll Out", 40)] + sym("Middle Brow Up", 80)
     + sym("Cheek Up", 60) + sym("Upper Eye Lid Down", 70)
     + sym("Lower Eye Lid Down", 60) + sym("Lip Corner Out", 50),
     True, 60),

    ("Rapport_Pleasure_3",
     "Near the peak. Brows high and drawn in, eyes shut, mouth open.",
     [("Jaw Open", 35), ("Brow Squeeze", 40)] + sym("Cheek Up", 90)
     + sym("Middle Brow Up", 100) + sym("Outer Brow Down", 50)
     + sym("Upper Eye Lid Down", 90) + sym("Lower Eye Lid Down", 80),
     True, 85),

    ("Rapport_Climax",
     "The moment. Teeth showing, eyes screwed shut, the mouth open on it -- ours "
     "now: see FACE AUTHORITY.",
     [("Jaw Open", 45), ("Brow Squeeze", 60), ("Tongue To Roof", 60)]
     + sym("Cheek Up", 100) + sym("Middle Brow Up", 100)
     + sym("Upper Eye Lid Down", 100) + sym("Lower Eye Lid Down", 85)
     + sym("Lower Lip Down", 60),
     True, 100),

    ("Rapport_Oral",
     "Mouth working around something, eyes up at him: heavy lids, the inner brows "
     "raised and drawn together -- pleading (owner poll, 2026-09-24: 'dont see our "
     "special expressions on blowjob'). Anatomy's contact mouth opens the jaw to fit. "
     "The eyes and brows are PROTECTED from the styles: see PROTECTED.",
     [("Jaw Open", 35), ("Lower Lip Funnel", 60),
      ("Upper Lip Funnel", 60), ("Pucker", 40), ("Upper Lip Roll Out", 70),
      ("Lower Lip Roll Out", 50), ("Brow Squeeze", 35)]
     + sym("Upper Eye Lid Down", 55) + sym("Lower Eye Lid Down", 30)
     + sym("Middle Brow Up", 75) + sym("Outer Brow Down", 20) + sym("Cheek Up", 25),
     True, 70),

    ("Rapport_Kiss",
     "Lips pursed, eyes closed. Used for the foreplay tags, which otherwise get "
     "an expression that belongs to a different act entirely.",
     [("Pucker", 70), ("Jaw Open", 15)] + sym("Upper Eye Lid Down", 80)
     + sym("Smile", 20),
     True, 30),

    ("Rapport_Dazed",
     "Afterwards, briefly. Eyes heavy, a half smile, jaw slack.",
     [("Jaw Open", 20)] + sym("Upper Eye Lid Down", 45) + sym("Smile", 35)
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


def check_styles_match_the_plugin():
    """The plugin builds a set name by appending a style number; this file emits
    the sets. If the two disagree the plugin asks AAF for an id that does not
    exist -- and AAF does not complain about an unknown mfgSet, so the face just
    silently never changes.

    That is not hypothetical. Three of the eight faces shipped that way for the
    whole of their existence because VariantFor could not tell a style suffix
    from a base name ending in a digit. Nothing caught it but a human reading two
    adjacent log lines. So the counts are checked here, where it costs nothing.
    """
    root = pathlib.Path(__file__).resolve().parent.parent
    header = (root / "src" / "Expressions.h").read_text(encoding="utf-8")
    found = re.search(r"kStyles\s*=\s*(\d+)", header)
    if not found:
        raise SystemExit("could not find kStyles in src/Expressions.h")
    declared = int(found.group(1))
    if declared != len(STYLES):
        raise SystemExit(
            "MISMATCH: src/Expressions.h says kStyles = %d but this file defines "
            "%d style(s). The plugin would ask AAF for set names that do not "
            "exist and the faces would silently never apply." % (declared, len(STYLES)))
    print("kStyles = %d in both the plugin and this file" % declared)


# A set whose eyes and brows ARE the act: the styles (a person's own way of
# reacting) may not override them there. Rapport_Oral lost its heavy-lidded look to
# style 1's wide eyes on Ivy -- the owner saw no oral face at all (2026-09-24).
PROTECTED = {"Rapport_Oral"}

# The same face at FULL DEPTH (owner, 2026-09-24: "when penis go deep in throat broves
# sliding closer like хмурится"). Rapport authors both ends; Anatomy's cbp.dll blends
# the brows, lids, nose and cheeks from the held face toward this one by its own depth
# signal, frame by frame ('RFAD', hello feature bit 3). Every morph named here is in
# the blend, zeros included: a zero says "at full depth this goes down to nothing" --
# the pleading lift of the inner brows gives way to the squeeze.
DEEP = {
    "Rapport_Oral":
        [("Brow Squeeze", 85)]
        + sym("Middle Brow Down", 55) + sym("Middle Brow Up", 0)
        + [("Left Brow Outer Up", 0), ("Right Outer Brow Up", 0)]
        + sym("Outer Brow Down", 30)
        + sym("Upper Eye Lid Down", 75) + sym("Upper Eye Lid Up", 0)
        + sym("Lower Eye Lid Up", 45) + sym("Lower Eye Lid Down", 0)
        + sym("Nose Up", 30) + sym("Cheek Up", 45),
}

# Deep in her vagina or anus (owner, 2026-09-25, through the anatomy session: "a reaction
# like the oral deep face"). The same blend by the same depth signal; the pleasure sets
# are what a penetrated actor holds, so their deep face is the PLEASURE frown rather than
# the oral wince: inner brows up AND drawn together, eyes squeezed nearly shut, nose and
# cheeks up. Each stage reaches it by its own intensity -- stage 1 is a softer version,
# so a slow start does not peak on the first deep stroke. Climax is at its peak already.
def pleasure_deep(factor):
    face = ([("Brow Squeeze", 60)]
            + sym("Middle Brow Up", 70) + sym("Middle Brow Down", 0)
            + [("Left Brow Outer Up", 0), ("Right Outer Brow Up", 0)]
            + sym("Outer Brow Down", 25)
            + sym("Upper Eye Lid Down", 80) + sym("Upper Eye Lid Up", 0)
            + sym("Lower Eye Lid Up", 40) + sym("Lower Eye Lid Down", 0)
            + sym("Nose Up", 35) + sym("Cheek Up", 50))
    return scale(face, factor)


DEEP.update({
    "Rapport_Pleasure_1": pleasure_deep(0.70),
    "Rapport_Pleasure_2": pleasure_deep(0.85),
    "Rapport_Pleasure_3": pleasure_deep(1.00),
})
# ---- THE EXPRESSION PASS (owner, 2026-09-25: "make more variety of expressions overall ...
# use these our innovations fully"). Three tables that only Anatomy's plugin can wear; the AAF
# path never sees them. A persona is R-8's; a sex is the actor's own ('f' / 'm').
#
# DEEP_BY: the face at full depth, per person, laid over the generic DEEP. Keys are
# "persona|sex", with "*" for any; the most specific that exists wins (persona|sex,
# persona|*, *|sex), then the generic DEEP. The per-stage factor of pleasure_deep applies.
def deep_face(squeeze, mid_up, mid_down, outer_down, lid_down, lid_up_low, nose, cheek):
    return ([("Brow Squeeze", squeeze)]
            + sym("Middle Brow Up", mid_up) + sym("Middle Brow Down", mid_down)
            + [("Left Brow Outer Up", 0), ("Right Outer Brow Up", 0)]
            + sym("Outer Brow Down", outer_down)
            + sym("Upper Eye Lid Down", lid_down) + sym("Upper Eye Lid Up", 0)
            + sym("Lower Eye Lid Up", lid_up_low) + sym("Lower Eye Lid Down", 0)
            + sym("Nose Up", nose) + sym("Cheek Up", cheek))


PLEASURE_DEEP_BY = {
    # She melts: the inner brows float up, the eyes close softly.
    "romantic|*": deep_face(30, 90, 0, 15, 85, 25, 15, 55),
    # Hungry: brows pulled down and in, eyes NARROWED on him, not shut; a snarl in the nose.
    "vulgar|*": deep_face(70, 0, 40, 30, 45, 60, 55, 60),
    # Winces and hides it: squeezed shut, the brows up in spite of herself.
    "reticent|*": deep_face(80, 60, 0, 20, 100, 50, 40, 40),
    # Composed until it cracks: less of everything, the lids going.
    "mercantile|*": deep_face(45, 50, 0, 20, 65, 30, 20, 40),
    # A man grimaces rather than frowns: brows down hard, eyes squinting, nose up.
    "*|m": deep_face(75, 0, 60, 40, 70, 55, 50, 50),
    # ... and the vulgar man keeps watching.
    "vulgar|m": deep_face(70, 0, 55, 35, 40, 60, 55, 55),
    "reticent|m": deep_face(80, 0, 50, 35, 95, 55, 45, 45),
}
ORAL_DEEP_BY = {
    # EVERY persona keeps the frown at depth -- brows drawn together and down -- which the
    # owner called "very immersive" (A-29, 2026-09-25). The first cut raised the brows for
    # these two instead, and the owner saw "deep brows reactions disappeared during blowjob".
    # The persona lives in the eyes and cheeks, not in the frown.
    # Taking it eagerly: the frown, with the eyes still half open on him.
    "vulgar|*": deep_face(85, 0, 55, 30, 50, 40, 35, 55),
    # The pleading gives way to the frown, eyes shut, a little of the lift left in the inner brows.
    "romantic|*": deep_face(85, 15, 50, 25, 85, 45, 25, 40),
    # The wince (the generic DEEP) is the reticent's; the mercantile keeps it too.
}
DEEP_BY = {
    "Rapport_Oral": ORAL_DEEP_BY,
    "Rapport_Pleasure_1": {k: scale(v, 0.70) for k, v in PLEASURE_DEEP_BY.items()},
    "Rapport_Pleasure_2": {k: scale(v, 0.85) for k, v in PLEASURE_DEEP_BY.items()},
    "Rapport_Pleasure_3": PLEASURE_DEEP_BY,
}

# DRIFT: so no face holds for minutes. Every 15-30 s a held face of these sets moves to one
# of its siblings (or back to itself), each laid OVER the held face (its style included),
# by the set's own level. Anatomy eases the change (hello bit 8); without that it never
# drifts. The mouth ones only ever reach a free mouth: while something is in it the
# contact mouth wins, and a speaking actor's mouth is the line's.
def drift_siblings(level):
    k = level / 100.0
    return {
        # eyes shut in bliss, the brows floating
        "bliss": scale(sym("Upper Eye Lid Down", 95) + sym("Middle Brow Up", 70) + sym("Cheek Up", 40), k),
        # the lip caught in the teeth
        "bite": scale([("Lower Lip Roll In", 65), ("Sticky Lips", 35), ("Brow Squeeze", 35)]
                      + sym("Upper Eye Lid Down", 60), k),
        # a gasp: the mouth opens, the lids fly up, the brows too
        "gasp": [("Jaw Open", int(round(20 + 30 * k)))] + scale(sym("Upper Eye Lid Down", 0)
                 + sym("Upper Eye Lid Up", 45) + sym("Middle Brow Up", 85), k),
        # open-eyed, looking at nothing, taking it in
        "open": scale(sym("Upper Eye Lid Down", 15) + sym("Lower Eye Lid Down", 25)
                      + sym("Middle Brow Up", 45) + sym("Lip Corner Out", 30), k),
    }


DRIFT = {"Rapport_Anticipation": drift_siblings(25), "Rapport_Pleasure_1": drift_siblings(35),
         "Rapport_Pleasure_2": drift_siblings(60), "Rapport_Pleasure_3": drift_siblings(85)}

# GLANCE: the face during a glance into the partner's eyes ('RFAX', hello bit 7), per context
# and persona. Eased in and out by Anatomy over the glance; its lids layer opens the eyes.
GLANCE = {
    "oral": {
        "romantic": [("Brow Squeeze", 45)] + sym("Middle Brow Up", 90) + sym("Outer Brow Down", 20)
                    + sym("Lower Eye Lid Down", 20) + sym("Cheek Up", 20),               # pleading
        "reticent": [("Brow Squeeze", 50)] + sym("Middle Brow Up", 80) + sym("Outer Brow Down", 25)
                    + sym("Lower Eye Lid Down", 10) + sym("Cheek Up", 25),               # pleading, unsure
        "vulgar": [("Brow Squeeze", 20), ("Left Brow Outer Up", 50), ("Right Outer Brow Up", 50)]
                  + sym("Middle Brow Up", 60) + sym("Lower Eye Lid Up", 30) + sym("Cheek Up", 35)
                  + sym("Nose Up", 20),                                                  # hungry
        "mercantile": [("Left Brow Outer Up", 55), ("Right Middle Brow Down", 25)]
                      + sym("Lower Eye Lid Up", 35) + sym("Cheek Up", 25),               # knowing
    },
    "face": {
        "romantic": sym("Smile", 45) + sym("Cheek Up", 45) + sym("Middle Brow Up", 50)
                    + sym("Lower Eye Lid Up", 30),                                       # a soft smile
        "reticent": sym("Smile", 25) + sym("Middle Brow Up", 45) + sym("Lower Eye Lid Up", 20)
                    + sym("Cheek Up", 25),                                               # a shy half-smile
        "vulgar": [("Left Smile", 60), ("Right Smile", 15), ("Left Lip Corner Out", 30), ("Left Nose Up", 25)]
                  + sym("Lower Eye Lid Up", 55) + sym("Outer Brow Down", 25),            # a smirk, eyes narrowed
        "mercantile": [("Left Smile", 35), ("Right Smile", 20), ("Left Brow Outer Up", 45)]
                      + sym("Lower Eye Lid Up", 30) + sym("Cheek Up", 30),               # a knowing smile
    },
    # An eye roll (owner, 2026-09-25: "rolling eyes bc its very sexy and humans do it often during
    # sex"; hello bit 9): brows float up, the lower lids drop so the whites show under the half-closed
    # upper lid the held face already has, and the mouth falls open on it where the mouth is free.
    # Everyone's the same; "romantic" is the fallback every persona reaches.
    "roll": {
        "romantic": [("Brow Squeeze", 25), ("Jaw Open", 30)] + sym("Middle Brow Up", 85)
                    + sym("Lower Eye Lid Down", 45) + sym("Lower Lip Down", 30) + sym("Cheek Up", 20),
    },
}

for _table in (DEEP_BY,):
    for _set, _faces in _table.items():
        assert _set in {entry[0] for entry in SETS}, _set
        for _k, _face in _faces.items():
            assert not {n for n, _ in _face} & set(MOUTH), f"a deep face may not blend a mouth morph ({_set} {_k})"
for _set in DRIFT:
    assert _set in {entry[0] for entry in SETS}, _set
for _ctx in GLANCE.values():
    for _face in _ctx.values():
        assert {n for n, _ in _face} <= set(MORPHS)
        # The blink lids are Anatomy's lids layer during a glance.
        assert not {n for n, _ in _face} & {"Left Upper Eye Lid Down", "Right Upper Eye Lid Down"}

# A renamed set would otherwise lose its deep face without a word.
assert set(DEEP) <= {entry[0] for entry in SETS}, "DEEP names a set SETS does not have"
assert not {name for face in DEEP.values() for name, _ in face} & set(MOUTH), \
    "a deep face may not blend a mouth morph: the mouth is the contact mouth's and lip sync's"


def unprotected(set_id, extra):
    if set_id not in PROTECTED:
        return extra
    return [(name, value) for name, value in extra if "Eye Lid" not in name and "Brow" not in name]


def main():
    check_styles_match_the_plugin()
    out = io.StringIO()
    out.write(HEADER)

    emitted = []
    # The same values for FaceAuthority, which sends them to Anatomy's hook: morph id
    # -> intensity 0-100, per emitted set. The XML and this file are written from one
    # loop, so the two paths can never show different faces.
    faces = {}
    deep = {}
    for setID, note, settings, lock, level in SETS:
        # Rapport_Clear has no variants: it is the reset, and there is only one
        # way to put a face back to nothing.
        if level == 0:
            variants = [("", "", [])]
        else:
            variants = [("_" + tag, " -- " + how, layer(level))
                        for tag, how, layer in STYLES]

        for suffix, how, extra in variants:
            extra = unprotected(setID, extra)
            name_v = setID + suffix
            emitted.append(name_v)
            out.write("<!-- %s -->\n" % (note + how))
            out.write('<mfgSet id="%s">\n' % name_v)
            faces[name_v] = {str(ID[name]): value for name, value in overlay(settings, extra)}
            if setID in DEEP:
                deep[name_v] = {str(ID[name]): value for name, value in DEEP[setID]}
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

    # FaceAuthority's copy. "mouth" is the MOUTH set as morph ids: the morphs a
    # speaking actor's lip sync must keep, so they are left out while Rapport's
    # line plays.
    faces_path = root / "data" / "F4SE" / "Plugins" / "Rapport" / "faces.json"
    document = {
        "_comment": "GENERATED by tools/make_mfg.py -- edit that, not this. Morph id -> intensity 0-100.",
        "morphs": len(MORPHS),
        "mouth": sorted(ID[name] for name in MOUTH),
        "sets": faces,
        # The face at full depth, by the same set names, for the sets that have one:
        # every morph listed is blended, and only those. See DEEP.
        "deep": deep,
        # The expression pass (2026-09-25), all keyed by BASE set, not the styled name:
        # per-person deep faces, drift siblings, and glance faces. See DEEP_BY, DRIFT, GLANCE.
        "deepBy": {s: {k: {str(ID[n]): v for n, v in f} for k, f in faces_.items()} for s, faces_ in DEEP_BY.items()},
        "drift": {s: {k: {str(ID[n]): v for n, v in f} for k, f in sib.items()} for s, sib in DRIFT.items()},
        "glance": {c: {p: {str(ID[n]): v for n, v in f} for p, f in faces_.items()} for c, faces_ in GLANCE.items()},
    }
    with io.open(faces_path, 'w', encoding="utf-8", newline="\n") as fh:
        fh.write(json.dumps(document, indent=1) + "\n")
    print("%s: %d set(s), %d mouth morph(s), %d with a deep face"
          % (faces_path.name, len(faces), len(document["mouth"]), len(deep)))

    # The plugin builds these names by appending a style number, so a mismatch
    # here is a face that silently never appears. Printed so it can be checked.
    print("%s: %d set(s) = %d base(s) x %d style(s)"
          % (path.name, len(emitted), len(SETS) - 1, len(STYLES)))
    for name in emitted:
        print("   %s" % name)


main()
