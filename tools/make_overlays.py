"""Generates Rapport's own overlay textures, materials and LooksMenu templates.

Run it after editing SWEAT or BLUSH; everything under data/Textures/Overlays/Rapport,
data/Materials/Overlays/Rapport and data/F4SE/Plugins/F4EE/Overlays/Rapport is
generated, not hand-maintained.

WHY THIS EXISTS AT ALL
----------------------
Rapport has always driven overlays it did not ship -- see Rapport_overlayData.xml,
which drives CumOverlays' templates and redistributes nothing. Sweat broke that
pattern for a simple reason: measured across all 16 overlay packs installed on the
development machine, there are 959 templates, and

    925 paint biped slot 3 (body)
     34 paint biped slot 4 (left hand)
      0 paint the head
      0 match sweat / blush / perspir by any spelling

There is nothing to drive. AAF cannot help either: its overlay vocabulary is
`template`, `alpha`, `isFemale` and nothing else (Data/AAF/common.xsd,
overlayType), so it can apply somebody's texture at an opacity but it cannot tint
one. The colour has to already be in the .dds. So for this one feature Rapport
authors its assets instead of borrowing them.

THE BODY TEXTURES ARE DELIBERATELY UV-AGNOSTIC
----------------------------------------------
Because the live body UV has NOT been established, not because two are known to
conflict -- an earlier version of this comment claimed the latter and could not
support it.

What is actually known. The body mesh the game loads is a BodySlide build:

    Meshes/Actors/Character/characterassets/FemaleBody.nif
        "Exported using Outfit Studio."
        Materials/actors/Character/BaseHumanFemale/basehumanFemaleskin.bgsm
        Textures/actors/character/basehumanfemale/femalebody_{d,n,s}.dds

Those textures are a smooth-skin replacer -- the normal map is very nearly flat,
so it carries no landmarks to read a UV off. The one texture on disk with clear
anatomy (custombody/FemaleBody_n.dds, 4096) is NOT what that mesh references, so
it cannot be assumed to describe the live layout either.

Placing sweat by coordinate against a layout that has not been established puts
it on a shin if the guess is wrong, and misplaced sweat reads as a rash. So the
body textures use NO positional weighting: isotropic beads and low-frequency
sheen, uniform over the sheet, correct under any layout. Establishing the live UV
(a probe texture, looked at in game) would allow anatomical placement later.

Note that morphs are NOT a concern here. A morph moves vertices and their UVs
move with them, so an overlay stays correctly mapped through any body shape or
BodySlide preset without doing anything.

The face is the opposite case and gets placed properly: the baked head diffuses in
Textures/Actors/Character/FaceCustomization/*/*_d.dds are an unambiguous front
unwrap -- eyes y~0.30, nose y~0.45, mouth y~0.55, ears x~0.10/0.90 -- so the blush
sits on the cheeks by measurement rather than by guess.

WORTH INVESTIGATING BEFORE EXTENDING THIS
-----------------------------------------
The same mesh references `template/SkinTemplate_Wet.bgsm` -- Fallout 4 ships its
OWN wet-skin material. That is a shader, not a texture: no art, no UV dependence,
body-agnostic by construction, and the correct way to render wet skin if it can be
driven per actor at runtime. Unproven: it is vanilla and lives in a BA2, and the
plausible mechanism (F4EE `bEnableSkinOverrides`, which is enabled) has no
skin-override data installed anywhere here to demonstrate it. If it works it would
likely replace the sweat textures below -- but not the blush, which is colour and
cannot come from a wetness shader.

KNOWN LIMITATIONS, STATED RATHER THAN DISCOVERED IN GAME
--------------------------------------------------------
- Pillow writes ONE mip level. Distant actors will alias slightly. Acceptable for a
  decal that only exists during a scene; fixable with texconv if it ever matters.
- The blush is an EXPERIMENT. Zero of the 959 installed templates target the head,
  so whether F4EE applies a slot-0 overlay at all is untested by anything on this
  machine. If it silently does nothing, that is the answer, and it cost one launch.
"""

import json
import pathlib
import random
import struct

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------------------
# BGEM
# ---------------------------------------------------------------------------
# A Bethesda effect-material is a fixed header, then length-prefixed
# NUL-terminated texture paths, then a tail. The length COUNTS the NUL: a 29
# character path is written as 30. Getting that off by one produces a file the
# engine reads as garbage rather than as an error.
#
# Byte-exact scaffolding, verified against a material the engine already loads
# (see the assert below). The earlier attempt to re-derive these fields from a
# field-by-field reading of the format produced a 65-byte header where the
# working one is 63 -- and because a BGEM is parsed positionally, those two bytes
# silently shift every field after them. It still "round-tripped" when checked by
# scanning for plausible strings, which is a test that cannot fail. Hence: exact
# bytes, and a test that compares against a known-good file.
_BGEM_HEAD = bytes.fromhex(
    "4247454d"                  # 'BGEM'
    "02000000"                  # version 2 (Fallout 4)
    "03000000"                  # tile U and V
    "00000000" "00000000"       # U offset, V offset
    "0000803f" "0000803f"       # U scale, V scale = 1.0
    "0000803f"                  # alpha = 1.0; per-overlay alpha lives in the XML
    "01"                        # alpha blend enabled
    "06000000"                  # blend src = SRC_ALPHA
    "07000000"                  # blend dst = INV_SRC_ALPHA
    "00"                        # alpha test ref
    "01010101"                  # alpha test, z write, z test, SSR
    "0000"                      # wetness-control SSR, decal
    "00000000" "00000000" "0000"    # two-sided .. refraction, padding
    "0000803f"                  # environment map mask scale = 1.0
    "00")                       # grayscale-to-palette colour off
_BGEM_MID = bytes.fromhex("01000000000100000000")
_BGEM_TAIL = bytes.fromhex(
    "01000000000000000000000000803f0000803f0000803f"
    "0000000000000000000000000000000000000000000000000000000000")

assert len(_BGEM_HEAD) == 63, len(_BGEM_HEAD)
assert len(_BGEM_MID) == 10 and len(_BGEM_TAIL) == 52


def _bgem_string(path):
    raw = path.encode("ascii") + b"\x00"
    return struct.pack("<I", len(raw)) + raw


def write_bgem(dest, diffuse, normal):
    """diffuse/normal are paths relative to Data/Textures, backslash-separated."""
    blob = (_BGEM_HEAD + _bgem_string(diffuse) + _BGEM_MID
            + _bgem_string(normal) + _BGEM_TAIL)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(blob)
    return len(blob)


# ---------------------------------------------------------------------------
# textures
# ---------------------------------------------------------------------------
# Sweat is a specular phenomenon being faked with a diffuse decal, which sets the
# whole design: bright near-white beads read as droplets catching light, and a
# broad faint film reads as a sheen. The failure mode is painting rather than
# wetting, so the peak alphas stay well under half -- a droplet is a highlight,
# not a spot of white.
SWEAT_RGB = (242, 244, 248)      # very slightly cool; warm white reads as grease
BLUSH_RGB = (206, 84, 82)

SIZE_BODY = 2048
SIZE_FACE = 1024

# (id, beads, max bead radius px, sheen peak alpha, bead peak alpha, runs)
#
# The FILM does the work, not the beads. The first cut of this had the balance
# inverted -- bright opaque discs on a faint wash -- and composited over skin it
# read as white paint spatter rather than as a wet body. Sweat is mostly a broad
# sheen with small beads sitting in it, so the sheen peak climbs hard with
# intensity while the bead alpha barely moves, and beads stay tiny: 2 px at 2048
# across a whole body is about right.
SWEAT = [
    ("Rapport_Sweat_1", 2200, 1, 26,  55,   0),
    ("Rapport_Sweat_2", 5200, 2, 46,  80, 180),
    ("Rapport_Sweat_3", 9000, 2, 68, 105, 620),
]

# (id, cheek alpha, nose-bridge alpha, spread multiplier)
BLUSH = [
    ("Rapport_Blush_1", 42, 16, 1.00),
    ("Rapport_Blush_2", 78, 30, 1.08),
    ("Rapport_Blush_3", 100, 42, 1.10),
]


def sweat_alpha(size, count, max_r, sheen_peak, drop_peak, trails, seed):
    rng = random.Random(seed)

    # Low-frequency film. effect_noise is gaussian around 128; everything below
    # the midpoint is discarded so the sheen is patchy rather than a flat wash.
    sheen = Image.effect_noise((64, 64), 64).resize((size, size), Image.BICUBIC)
    sheen = sheen.filter(ImageFilter.GaussianBlur(size / 90.0))
    sheen = sheen.point(lambda v: int(max(0, v - 128) * sheen_peak / 64.0))

    beads = Image.new("L", (size, size), 0)
    pen = ImageDraw.Draw(beads)

    # Beads CLUSTER. Scattering them uniformly is what made the first cut read as
    # spatter: real sweat collects in patches and leaves skin between them, and
    # clustering is the one placement rule that needs no knowledge of the UV.
    centres = [(rng.randrange(size), rng.randrange(size))
               for _ in range(max(8, count // 90))]
    spread = size / 14.0

    for _ in range(count):
        cx, cy = centres[rng.randrange(len(centres))]
        x = int(rng.gauss(cx, spread)) % size
        y = int(rng.gauss(cy, spread)) % size
        r = rng.randint(1, max_r)
        if rng.random() < 0.06:
            r += 1
        # Vary each bead. Identical alpha on every dot is the other half of what
        # made it look printed rather than wet.
        a = int(drop_peak * rng.uniform(0.45, 1.0))
        pen.ellipse((x - r, y - r, x + r, y + r), fill=a)

    for _ in range(trails):
        # A run: a bead that lost, tapering as it goes. Vertical in UV space is
        # not vertical on the body, but a short streak reads as a run in any
        # orientation, which is exactly the property the UV ambiguity demands.
        cx, cy = centres[rng.randrange(len(centres))]
        x = int(rng.gauss(cx, spread)) % size
        y = int(rng.gauss(cy, spread)) % size
        length = rng.randint(10, 40)
        for step in range(length):
            a = int(drop_peak * 0.5 * (1.0 - step / float(length)))
            if a <= 0:
                break
            pen.point(((x + rng.randint(-1, 1)) % size, (y + step) % size), fill=a)

    beads = beads.filter(ImageFilter.GaussianBlur(1.3))
    return ImageChops.lighter(sheen, beads)


def blush_alpha(size, cheek, bridge, spread):
    a = Image.new("L", (size, size), 0)
    pen = ImageDraw.Draw(a)

    def blob(cx, cy, rx, ry, value):
        pen.ellipse((int((cx - rx) * size), int((cy - ry) * size),
                     int((cx + rx) * size), int((cy + ry) * size)), fill=value)

    # Measured off the baked face diffuses, not guessed. Cheeks sit between the
    # eye line (0.30) and the mouth (0.55), outboard of the nose.
    blob(0.285, 0.445, 0.105 * spread, 0.080 * spread, cheek)
    blob(0.715, 0.445, 0.105 * spread, 0.080 * spread, cheek)
    # Across the bridge -- what makes a flush read as heat rather than as makeup.
    blob(0.500, 0.395, 0.090 * spread, 0.036 * spread, bridge)
    # Ears go red before anything else does.
    blob(0.105, 0.380, 0.045, 0.055, int(cheek * 0.8))
    blob(0.895, 0.380, 0.045, 0.055, int(cheek * 0.8))

    return a.filter(ImageFilter.GaussianBlur(size / 26.0))


def save_dds(dest, rgb, alpha):
    im = Image.merge("RGBA", (
        Image.new("L", alpha.size, rgb[0]),
        Image.new("L", alpha.size, rgb[1]),
        Image.new("L", alpha.size, rgb[2]),
        alpha))
    dest.parent.mkdir(parents=True, exist_ok=True)
    # BC3/DXT5: interpolated alpha, which a soft-edged decal needs. DXT1 would
    # give this a one-bit alpha and a hard jagged rim around every droplet.
    im.save(dest, "DDS", pixel_format="DXT5")
    return dest.stat().st_size


# ---------------------------------------------------------------------------

BODY_NORMAL = r"actors\character\basehumanfemale\FemaleBody_n.dds"
# The head's normal is per-character and baked, so there is no single correct
# path. The body normal is used as a stand-in: for an alpha decal the normal only
# perturbs lighting on the painted texels, and the blush is soft enough that it
# does not read.
FACE_NORMAL = BODY_NORMAL


def main():
    tex = ROOT / "data" / "Textures" / "Overlays" / "Rapport"
    mat = ROOT / "data" / "Materials" / "Overlays" / "Rapport"
    tpl = ROOT / "data" / "F4SE" / "Plugins" / "F4EE" / "Overlays" / "Rapport"

    templates = []

    for setID, count, max_r, sheen, drop, trails in SWEAT:
        alpha = sweat_alpha(SIZE_BODY, count, max_r, sheen, drop, trails,
                            seed=hash(setID) & 0xFFFF)
        n = save_dds(tex / (setID + ".dds"), SWEAT_RGB, alpha)
        label = "Rapport - Sweat %d" % (SWEAT.index(
            (setID, count, max_r, sheen, drop, trails)) + 1)
        write_bgem(mat / (setID + ".bgem"),
                   "Overlays\\Rapport\\" + setID + ".dds", BODY_NORMAL)
        print("  %-18s body  %7d bytes" % (setID, n))
        templates.append({
            "id": setID, "name": label,
            "slots": [{"slot": 3,
                       "material": "overlays\\Rapport\\" + setID + ".BGEM"}],
            "playable": True, "transformable": True, "sort": 0, "gender": 2,
        })

    for setID, cheek, bridge, spread in BLUSH:
        alpha = blush_alpha(SIZE_FACE, cheek, bridge, spread)
        n = save_dds(tex / (setID + ".dds"), BLUSH_RGB, alpha)
        label = "Rapport - Flush %d" % (BLUSH.index(
            (setID, cheek, bridge, spread)) + 1)
        write_bgem(mat / (setID + ".bgem"),
                   "Overlays\\Rapport\\" + setID + ".dds", FACE_NORMAL)
        print("  %-18s face  %7d bytes   (EXPERIMENT: slot 0)" % (setID, n))
        templates.append({
            "id": setID, "name": label,
            "slots": [{"slot": 0,
                       "material": "overlays\\Rapport\\" + setID + ".BGEM"}],
            "playable": True, "transformable": True, "sort": 0, "gender": 2,
        })

    tpl.mkdir(parents=True, exist_ok=True)
    out = tpl / "overlays.json"
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(templates, fh, indent=2)
        fh.write("\n")

    # gender 2 = both. The shipped packs split male and female templates because
    # their art differs; sweat and a flush do not.
    print("\n%s: %d template(s)" % (out.relative_to(ROOT), len(templates)))
    for t in templates:
        print("   %-18s slot %d" % (t["id"], t["slots"][0]["slot"]))


main()
