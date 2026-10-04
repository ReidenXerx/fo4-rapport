"""The sweat as a DAMP FILM, painted on the body in 3D (owner's choice, 2026-10-04).

The first multiply sweat was 1-2 px beads lightening the skin up to x1.37. In the owner's photo
(Screenshot276) they read as pale specks -- dandruff, not sweat: a droplet's shine is SPECULAR, and a
diffuse overlay cannot make it. What a diffuse overlay can make is what wet skin also does: it gets a
little darker and richer. So this paints a soft film that darkens with a slightly warm tint, gathered
where sweat gathers (back, chest, belly, upper thighs), with a few soft runs down with gravity, and the
body's own shine left to the game.

Geometry comes from the Complexion bud's tools (fo4-complexion tools/paint: body.UVMap gives every
texel's 3D position, normal and body region; seams.seam_fade fades a mark out before the neck and wrist
seams, the owner's rule for every body-wide overlay). One texture per sex: the two bodies' UVs differ.
"""
import pathlib
import sys

import numpy as np

COMPLEXION = pathlib.Path(r"C:\Users\DuduPhudu\Documents\Projects\fo4-complexion\tools\paint")
DATA = pathlib.Path(r"D:\SteamFreeGames\Fallout 4 AE\Data")

# Warm, so the darkened skin reads richer, not grey: blue and green darken a little more than red.
TINT = np.array([0.80, 1.00, 1.12])


def _complexion():
    if not (COMPLEXION / "body.py").exists():
        sys.exit("fo4-complexion's tools/paint (body.py, seams.py) is needed to paint the sweat: %s" % COMPLEXION)
    if str(COMPLEXION) not in sys.path:
        sys.path.insert(0, str(COMPLEXION))
    import body
    import seams
    return body, seams


_MAPS = {}


def uv_map(sex, size):
    body, _ = _complexion()
    key = (sex, size)
    if key not in _MAPS:
        _MAPS[key] = body.UVMap(DATA, sex, size)
    return _MAPS[key]


def _blobs(points, rng, count, radius):
    """A smooth 0..1 field over 3D points: soft Gaussian blobs centred on random body points, combined
    as a soft union (the max of them left flat-topped plateaus with rims)."""
    field = np.zeros(len(points))
    centres = points[rng.choice(len(points), size=count, replace=False)]
    for c in centres:
        r = radius * rng.uniform(0.7, 1.3)
        d2 = ((points - c) ** 2).sum(axis=1)
        blob = np.exp(-d2 / (2 * r * r)) * rng.uniform(0.25, 0.55)
        field = 1.0 - (1.0 - field) * (1.0 - blob)   # a soft union: overlaps add up, no flat-topped plateaus
    return field


def _box(a, r):
    """Box blur of radius r along both axes (cumulative sums); three passes approximate a Gaussian."""
    for axis in (0, 1):
        c = np.cumsum(np.pad(a, [(r + 1, r) if k == axis else (0, 0) for k in range(a.ndim)]), axis=axis)
        hi = np.take(c, np.arange(2 * r + 1, c.shape[axis]), axis=axis)
        lo = np.take(c, np.arange(0, c.shape[axis] - 2 * r - 1), axis=axis)
        a = (hi - lo) / (2 * r + 1)
    return a


def smooth_on_body(values, covered, radius):
    """values (size x size) averaged over nearby COVERED texels only: a soft border between body
    regions without bleeding the off-island background in."""
    w = covered.astype(np.float64)
    v = np.where(covered, values, 0.0)
    for _ in range(3):
        v, w = _box(v, radius), _box(w, radius)
    return np.where(w > 1e-6, v / np.maximum(w, 1e-6), 0.0)


def film_factor(sex, size, depth, coverage, runs, seed):
    """The multiply factor per texel (size x size x 3): 1 off the film, down to 1 - depth x TINT on it."""
    body, seams = _complexion()
    m = uv_map(sex, size)
    rng = np.random.default_rng(seed)
    cov = m.covered
    pos = m.position[cov]
    nrm = m.normal[cov]
    reg = m.region[cov]
    names = body.REGIONS
    # Where sweat gathers: the torso most, the legs (more the higher up), the arms a little; never the
    # head, hands, feet or genitals (the latter are the Anatomy body, not skin under a film).
    weight = np.zeros(len(pos))
    weight[reg == names.index("torso")] = 1.0
    weight[reg == names.index("arm")] = 0.45
    legs = reg == names.index("leg")
    if legs.any():
        z = pos[legs, 2]
        span = max(1e-6, z.max() - z.min())
        weight[legs] = 0.35 + 0.55 * ((z - z.min()) / span)
    # Soften the region borders: the regions follow whole triangles' bones, so the raw weight steps
    # along a jagged line (torso 1.0 against arm 0.45 drew an edge across the shoulder).
    grid = np.zeros(cov.shape)
    grid[cov] = weight
    weight = smooth_on_body(grid, cov, 24)[cov]
    # The film: soft patches, more of them at higher coverage.
    film = _blobs(pos, rng, int(10 + 22 * coverage), 6.0)
    film = np.clip(film + 0.15 * coverage, 0, 1)            # a faint film everywhere it gathers
    # Runs: thin soft tracks straight down from a start point, on the same side of the body.
    for _ in range(runs):
        i = rng.integers(len(pos))
        if weight[i] < 0.6:
            continue
        x0, y0, z0 = pos[i]
        n0 = nrm[i]
        length = rng.uniform(12.0, 28.0)
        width = rng.uniform(1.2, 2.2)
        dz = z0 - pos[:, 2]
        across = np.hypot(pos[:, 0] - x0, pos[:, 1] - y0)
        on = (dz >= 0) & (dz <= length) & ((nrm @ n0) > 0.6)
        head = np.clip(dz / 3.0, 0, 1)
        track = np.exp(-(across ** 2) / (2 * width * width)) * (1 - dz / length) * head * head * (3 - 2 * head)
        film = np.maximum(film, np.where(on, 0.35 * track, 0))   # faint: a damp trail, never a dark mark
    amount = np.clip(film * weight, 0, 1)
    fade = seams.seam_fade(m)[cov]
    amount *= fade
    factor = np.ones((size, size, 3))
    factor[cov] = 1.0 - depth * amount[:, None] * TINT[None, :]
    # Carry the edge colour a few texels past every UV island, so filtering and mipmaps do not blend
    # the off-island neutral into the film and draw a pale line along the UV seams.
    known = cov.copy()
    for _ in range(8):
        acc = np.zeros_like(factor)
        cnt = np.zeros((size, size))
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            k = np.roll(known, (dy, dx), axis=(0, 1))
            acc += np.roll(factor, (dy, dx), axis=(0, 1)) * k[..., None]
            cnt += k
        grow = ~known & (cnt > 0)
        factor[grow] = acc[grow] / cnt[grow][:, None]
        known |= grow
    return factor
