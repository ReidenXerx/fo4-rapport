"""Generates Rapport's own overlay textures, materials and LooksMenu templates.

Run it after editing SWEAT; everything under data/Textures/Overlays/Rapport,
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
- There is no blush, and there will not be one via this route. Three slot-0 (head)
  templates were built, shipped and watched in game: the faces were UNCHANGED across
  several scenes. F4EE does not apply a head overlay -- which matches the census,
  where none of the 959 installed templates targets the head either. The experiment
  cost one launch and is closed. A facial flush needs the CharGen tint layer or worn
  geometry (the route Commonwealth Moisturizer takes), not an overlay.
"""

import json
import pathlib
import random
import shutil
import struct
import subprocess
import sys
import tempfile

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
    "01"                        # blending enabled
    "04000000"                  # blend src = DEST_COLOR  } MULTIPLY: skin x texture x base colour x scale,
    "01000000"                  # blend dst = ZERO        } lit and shadowed WITH the skin. Alpha blending
                                # (6/7) drew unlit: pale marks glowed at night (Complexion, measured in game
                                # 2026-10-03; shared memory fo4-overlay-bgem-multiply). porc's header.
    "00"                        # alpha test ref
    "01010101"                  # alpha test, z write, z test, SSR
    "0000"                      # wetness-control SSR, decal
    "00000000" "00000000" "0000"    # two-sided .. refraction, padding
    "0000803f"                  # environment map mask scale = 1.0
    "00")                       # grayscale-to-palette colour off
_BGEM_MID = bytes.fromhex("01000000000100000000")
# Multiply scale: the texture stores factor x NEUTRAL, the material scales it back.
SCALE = 2.0
NEUTRAL = 1.0 / SCALE


def _bgem_tail(base_colour):
    """Envmap-mask string, 6 bools, base colour 3f, base colour scale f, falloff 4f, lighting
    influence f, envmap min LOD u8, soft depth f -- Complexion's make_marks.py layout."""
    return (bytes.fromhex("0100000000" "000000000000") + struct.pack("<fff", *base_colour)
            + struct.pack("<f", SCALE) + bytes(16) + bytes(4) + bytes(1) + bytes(4))


assert len(_BGEM_HEAD) == 63, len(_BGEM_HEAD)
assert len(_BGEM_MID) == 10 and len(_bgem_tail((1.0, 1.0, 1.0))) == 52


def _bgem_string(path):
    raw = path.encode("ascii") + b"\x00"
    return struct.pack("<I", len(raw)) + raw


def write_bgem(dest, diffuse, normal, base_colour):
    """diffuse/normal are paths relative to Data/Textures, backslash-separated. base_colour undoes the
    texture's DECODED neutral (5-6-5 colour endpoints cannot store 0.5), so skin off the droplets is
    multiplied by exactly 1 -- not ~1.03, which tints the whole body pinker than the face."""
    blob = (_BGEM_HEAD + _bgem_string(diffuse) + _BGEM_MID
            + _bgem_string(normal) + _bgem_tail(base_colour))
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
SIZE_BODY = 2048

# (id, beads, max bead radius px, sheen peak alpha, bead peak alpha, runs)
#
# The FILM does the work, not the beads. The first cut of this had the balance
# inverted -- bright opaque discs on a faint wash -- and composited over skin it
# read as white paint spatter rather than as a wet body. Sweat is mostly a broad
# sheen with small beads sitting in it, so the sheen peak climbs hard with
# intensity while the bead alpha barely moves, and beads stay tiny: 2 px at 2048
# across a whole body is about right.
# The bead version (pale 1-2 px dots lifting the skin to x1.37) read as dandruff in the owner's photo,
# 2026-10-04 (Screenshot276), so the sweat is now a DAMP FILM: tools/damp_film.py.
# (id, darkening at full film, coverage 0..1, runs)
SWEAT_FILM = [
    ("Rapport_Sweat_1", 0.05, 0.35, 0),
    ("Rapport_Sweat_2", 0.08, 0.60, 12),
    ("Rapport_Sweat_3", 0.12, 0.90, 30),
]
SEXES = {"female": (1, "F", r"actors\character\basehumanfemale\FemaleBody_n.dds"),
         "male": (0, "M", r"actors\character\basehumanmale\BaseMaleBody_n.dds")}

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


# How much a droplet brightens the skin under it, at full sweat intensity: factor = 1 + LIFT x
# intensity x the sweat's slightly cool tint. Bead peaks (~0.41) land near x1.4, what the old alpha
# blend toward near-white gave a mid skin tone -- but now lit, and dark at night.
LIFT = 1.0


TEXCONV = [pathlib.Path(r"D:\xEdit.4.1.5f\Edit Scripts\Texconvx64.exe"),
           pathlib.Path(r"D:\DynDOLOD\Edit Scripts\Texconvx64.exe")]
# What an eye sees as a patch, as a multiply factor over 8x8 texel areas. Clean skin must decode to the
# neutral EXACTLY (BC7 does: 128); inside the sweat film, which is itself a few-percent lift with random
# variation, the decoded sweat may differ from the painted by up to PATCH_LIMIT. Measured 2026-10-04 on
# Rapport_Sweat_1: clean-skin blocks within 0.003, the film's worst block 0.012 (1-2 px beads are harder
# for BC7 than Complexion's broad marks, which reach 0.001-0.003).
PATCH_LIMIT = 0.02


def _texconv():
    for tool in TEXCONV:
        if tool.exists():
            return tool
    found = shutil.which("texconv")
    if not found:
        sys.exit("texconv not found (Texconvx64.exe from xEdit or DynDOLOD)")
    return pathlib.Path(found)


def save_dds(dest, painted):
    """The multiply texture (painted: an RGB image of factor x NEUTRAL), encoded BC7 with mipmaps.
    BC1/DXT5 keep colours as 5-6-5 endpoints: neutral 128 decodes to (132,130,132), and a soft edge a
    few levels off neutral decodes 2-5% wrong in mixed blocks (Complexion, measured 2026-10-04). BC7
    holds 128 exactly. Returns (size, the base colour, the decoded neutral, the worst 8x8 patch error)."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        png = pathlib.Path(tmp) / (dest.stem + ".png")
        painted.save(png)
        r = subprocess.run([str(_texconv()), "-nologo", "-y", "-ft", "dds", "-f", "BC7_UNORM", "-bcmax", "-m", "0",
                            "-o", tmp, str(png)], capture_output=True, text=True)
        made = pathlib.Path(tmp) / (dest.stem + ".DDS")
        if not made.exists():
            made = pathlib.Path(tmp) / (dest.stem + ".dds")
        if r.returncode != 0 or not made.exists():
            sys.exit("texconv failed on %s: %s %s" % (png, r.stdout[-300:], r.stderr[-300:]))
        shutil.copyfile(made, dest)
    # Read it back as the GPU will. The most common decoded texel is skin with no sweat.
    decoded = Image.open(dest).convert("RGB")
    # Every texel painted exactly neutral (skin with no sweat, off the islands) must decode to 128.
    import numpy as np
    pnt8 = np.asarray(painted)
    dec8 = np.asarray(decoded)
    # Whole 8x8 areas of clean skin must decode to it exactly (a texel right at the film's edge may move a
    # few levels in a mixed BC7 block; the patch check below bounds what that does to an area).
    h8, w8 = pnt8.shape[0] // 8 * 8, pnt8.shape[1] // 8 * 8
    clean = np.all((pnt8[:h8, :w8] == 128).reshape(h8 // 8, 8, w8 // 8, 8, 3), axis=(1, 3, 4))
    if clean.any():
        blocks = dec8[:h8, :w8].astype(int).reshape(h8 // 8, 8, w8 // 8, 8, 3)
        off = np.abs(blocks.transpose(0, 2, 1, 3, 4)[clean] - 128).max()
        if off > 1:
            sys.exit("%s: clean skin decodes up to %d levels off the neutral 128" % (dest.name, off))
    neutral = (128, 128, 128)
    base = tuple(255.0 / (SCALE * v) for v in neutral)
    # Patch check: the factor the game draws (texel x scale x base) against the painted factor
    # (texel / neutral), averaged over 8x8 texel areas.
    dec = np.asarray(decoded, dtype=np.float64) / 255.0 * SCALE * np.array(base)
    pnt = np.asarray(painted, dtype=np.float64) / (NEUTRAL * 255.0)
    h, w = dec.shape[0] // 8 * 8, dec.shape[1] // 8 * 8
    diff = (dec[:h, :w] - pnt[:h, :w]).reshape(h // 8, 8, w // 8, 8, 3).mean(axis=(1, 3))
    worst = float(np.abs(diff).max())
    if worst > PATCH_LIMIT:
        sys.exit("%s: worst 8x8 patch is off by %.4f (limit %.2f)" % (dest.name, worst, PATCH_LIMIT))
    return dest.stat().st_size, base, neutral, worst


# ---------------------------------------------------------------------------

BODY_NORMAL = r"actors\character\basehumanfemale\FemaleBody_n.dds"


def main():
    tex = ROOT / "data" / "Textures" / "Overlays" / "Rapport"
    mat = ROOT / "data" / "Materials" / "Overlays" / "Rapport"
    # LooksMenu reads Overlays\<plugin FILE name>\overlays.json for each loaded plugin, then only
    # Overlays\Loose\*.json (expired6978/F4SEPlugins f4ee/OverlayInterface.cpp LoadOverlayMods). The
    # folder was "Rapport" from 0.1.1 to 0.2.5, so the templates never loaded (Complexion, 2026-10-02;
    # every other overlay mod on this install names its folder after its plugin file).
    tpl = ROOT / "data" / "F4SE" / "Plugins" / "F4EE" / "Overlays" / "Rapport.esp"

    templates = []

    import damp_film
    import numpy as np
    for old in list(tex.glob("Rapport_Sweat_?.dds")) + list(mat.glob("Rapport_Sweat_?.bgem")):
        old.unlink()   # the one-texture-for-both-sexes files of 0.2.6-0.2.8
    for n, (setID, depth, coverage, runs) in enumerate(SWEAT_FILM, start=1):
        label = "Rapport - Sweat %d" % n
        for sex, (gender, tag, normal) in SEXES.items():
            factor = damp_film.film_factor(sex, SIZE_BODY, depth, coverage, runs, seed=1000 * n + gender)
            # Dither the film by up to half a level so its gentle gradients do not band into contour
            # rings (13% of darkening is only ~17 levels); clean skin is left exactly neutral.
            level = factor * NEUTRAL * 255.0
            jitter = np.random.default_rng(7 * n + gender).uniform(-0.5, 0.5, level.shape)
            level = np.where(factor < 1.0 - 1e-6, level + jitter, level)
            rgb = np.clip(np.floor(level + 0.5), 0, 255).astype(np.uint8)
            name = "%s_%s" % (setID, tag)
            size, base, neutral, worst = save_dds(tex / (name + ".dds"), Image.fromarray(rgb, "RGB"))
            write_bgem(mat / (name + ".bgem"), "Overlays\\Rapport\\" + name + ".dds", normal, base)
            print("  %-20s %-6s %8d bytes  BC7, darkest x%.3f, worst 8x8 patch %.4f" % (
                name, sex, size, float(factor.min()), worst))
            templates.append({
                "id": setID, "name": label,
                "slots": [{"slot": 3, "material": "overlays\\Rapport\\" + name + ".BGEM"}],
                "playable": True, "transformable": True, "sort": 0, "gender": gender,
            })

    tpl.mkdir(parents=True, exist_ok=True)
    out = tpl / "overlays.json"
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(templates, fh, indent=2)
        fh.write("\n")

    # One template per sex, the same id in each map. There is no "both": LooksMenu clamps a gender
    # above 1 to female (OverlayInterface.cpp ~1080), so the old gender 2 meant women only.
    print("\n%s: %d template(s)" % (out.relative_to(ROOT), len(templates)))
    for t in templates:
        print("   %-18s slot %d" % (t["id"], t["slots"][0]["slot"]))


main()
