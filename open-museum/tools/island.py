"""The island's shape: one height function used by the terrain, the collision and the placements.

Game coordinates: x and y across, z up, about a metre a unit. The island sits in the middle of
the game's 0-1024 collision world, at CENTRE.
"""

import math

import numpy as np

CENTRE = np.array([620.0, 520.0])  # east of the ship's take-off path (path 59)
SEA = 223.0           # the sea's and the lagoon's surface (Pokitaru's ocean is at 223)
LAGOON_R = 78.0       # the lagoon around the house
HOUSE_FLOOR = SEA + 8.0

# The two waterfalls: where each falls from the cliff (angle in degrees from +x, about CENTRE).
FALLS = [{"angle": 68.0, "lip": SEA + 62.0, "r": 150.0},
         {"angle": 118.0, "lip": SEA + 48.0, "r": 146.0}]
GROTTO = {"angle": 118.0, "r0": 146.0, "r1": 168.0, "half": 7.0, "roof": SEA + 13.0}
VOLCANO = CENTRE + np.array([-40.0, 232.0])
VOLCANO_TOP = SEA + 205.0
# The lookout on the mountain (its platform's middle) and the shrine's ledge high on the volcano.
LOOKOUT = (float(CENTRE[0] + math.cos(math.radians(150)) * 192), float(CENTRE[1] + math.sin(math.radians(150)) * 192))
_d = np.array(LOOKOUT) - VOLCANO
VOLCANO_LEDGE = tuple(VOLCANO + _d / np.linalg.norm(_d) * 60.0)
LEDGE_Z = SEA + 145.0  # a terrace cut into the volcano's side for the shrine


def _noise_table(seed):
    r = np.random.default_rng(seed)
    return r.random((64, 64)).astype(np.float64)


_N = [_noise_table(s) for s in (101, 102, 103, 104)]


def value_noise(x, y, table, scale):
    gx, gy = x / scale, y / scale
    ix, iy = np.floor(gx).astype(int), np.floor(gy).astype(int)
    fx, fy = gx - ix, gy - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    t = table
    a = t[ix % 64, iy % 64]; b = t[(ix + 1) % 64, iy % 64]
    c = t[ix % 64, (iy + 1) % 64]; d = t[(ix + 1) % 64, (iy + 1) % 64]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def fbm(x, y, scale, octaves=4, k=0):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total = total + value_noise(x, y, _N[(k + o) % 4], scale / (2 ** o)) * amp
        norm += amp
        amp *= 0.5
    return total / norm


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def angle_of(x, y):
    return np.degrees(np.arctan2(y - CENTRE[1], x - CENTRE[0]))


# The low ground: lobes of land round the lagoon (x, y, radius), not one round island. The rock
# (rocks.py) stands on and round them.
LOBES = [(620, 520, 128), (515, 425, 58), (603, 352, 40), (765, 445, 52), (468, 585, 46), (442, 725, 26),
         (700, 420, 40), (540, 610, 40), (720, 600, 36)]


def base_height(x, y):
    """The low ground's height at game X, Y: the sea floor, the beaches, the lowland, the lagoon."""
    x = np.asarray(x, np.float64); y = np.asarray(y, np.float64)
    u, v = x - CENTRE[0], y - CENTRE[1]
    r = np.hypot(u, v)
    a = np.degrees(np.arctan2(v, u))
    # Distance into the land (negative inland), its edge wandering.
    wx = x + (fbm(x, y, 90, 3, 0) - 0.5) * 70
    wy = y + (fbm(x + 300, y, 90, 3, 1) - 0.5) * 70
    d = np.full(x.shape, 1e9)
    for (lx, ly, lr) in LOBES:
        d = np.minimum(d, np.hypot(wx - lx, wy - ly) - lr)
    d = d + (fbm(x, y, 25, 2, 2) - 0.5) * 14
    land = SEA - 24 + 31 * smooth(45, -8, d)                # up to SEA + 7 inland
    land = np.minimum(land, SEA + 2.0 + 5.5 * smooth(-4, -30, d))
    land = land + (fbm(x, y, 45, 3, 1) - 0.5) * 3 * smooth(-10, -30, d)

    # The lagoon round the house: a smooth bowl, 7 units deep.
    lagoon = smooth(LAGOON_R + 22, LAGOON_R - 6, r)
    land = land * (1 - lagoon) + (SEA - 7) * lagoon

    # The two streams from the falls' pools to the lagoon.
    for f in FALLS:
        d = np.abs(((a - f["angle"] + 180) % 360) - 180)
        stream = smooth(5, 1.5, d * r / 57.3 / 3) * smooth(LAGOON_R - 2, LAGOON_R + 6, r) * smooth(160, 140, r)
        land = land * (1 - stream) + np.minimum(land, SEA - 2.5) * stream
        # The pool at the foot of the fall.
        px = CENTRE[0] + math.cos(math.radians(f["angle"])) * (f["r"] - 10)
        py = CENTRE[1] + math.sin(math.radians(f["angle"])) * (f["r"] - 10)
        pool = smooth(18, 9, np.hypot(x - px, y - py))
        land = land * (1 - pool) + np.minimum(land, SEA - 4) * pool

    # The grotto behind the second waterfall: its floor, inside the rock (rocks.py's cave).
    g = GROTTO
    inside = in_grotto(x, y, 1.5)
    land = land * (1 - inside) + (SEA + 2.0) * inside
    return land


def in_grotto(x, y, soft=0.0):
    g = GROTTO
    u, v = x - CENTRE[0], y - CENTRE[1]
    r = np.hypot(u, v)
    dg = np.abs(((np.degrees(np.arctan2(v, u)) - g["angle"] + 180) % 360) - 180)
    if soft:
        return smooth(g["half"] + soft, g["half"] - 0.5, dg * r / 57.2958) * smooth(g["r0"] - 6, g["r0"] - 4, r) * smooth(g["r1"] + 2, g["r1"], r)
    return (dg * r / 57.2958 < g["half"]) & (r > g["r0"] - 5) & (r < g["r1"])


_ROCK = None


def set_rock_top(raster):
    """The rock's top (rocks.top_raster), read by height() from now on."""
    global _ROCK
    _ROCK = raster


def rock_top(x, y):
    x = np.asarray(x, np.float64); y = np.asarray(y, np.float64)
    if _ROCK is None:
        return np.full(x.shape, -np.inf)
    g, lo, cell = _ROCK["grid"], _ROCK["lo"], _ROCK["cell"]
    i = np.clip(((x - lo) / cell).astype(int), 0, g.shape[0] - 1)
    j = np.clip(((y - lo) / cell).astype(int), 0, g.shape[1] - 1)
    return g[i, j].astype(np.float64)


def height(x, y):
    """The ground's height at game X, Y (arrays or numbers): the rock's top where there is rock
    (but the grotto's floor inside it), else the low ground."""
    x = np.asarray(x, np.float64); y = np.asarray(y, np.float64)
    base = base_height(x, y)
    top = rock_top(x, y)
    return np.where(in_grotto(x, y) | ~np.isfinite(top), base, np.maximum(base, top))


def terraced(h, x, y, step):
    """Layers of rock with flat tops, like Pokitaru's cliffs: steep risers every STEP units,
    their edges wobbling so no two ledges are the same."""
    h = np.asarray(h, np.float64)
    wob = (fbm(x, y, 22, 2, 2) - 0.5) * step * 0.8
    q = (h + wob) / step
    base = np.floor(q)
    frac = q - base
    stairs = (base + smooth(0.45, 0.8, frac)) * step - wob
    stairs = np.minimum(stairs, h + step * 0.5)
    return np.where(h > 0.5, np.maximum(0.65 * stairs + 0.35 * h, 0), h)


# Small islands around: (x, y, radius, height above SEA).
ISLETS = [(870, 250, 26, 6), (330, 250, 20, 14), (600, 140, 18, 4), (300, 800, 30, 38)]
# Tall stacks in the sea round the island, out of the gadget routes.
STACKS = [(905, 400, 10, 24), (930, 470, 7, 16), (880, 640, 12, 30), (845, 800, 9, 20), (760, 860, 14, 34),
          (520, 860, 8, 18), (690, 175, 9, 22), (470, 160, 11, 26), (395, 300, 7, 18), (960, 560, 8, 14),
          (790, 250, 6, 12), (965, 760, 10, 26)]
# Broad islands further out, the turbine town of Pokitaru on them (scatter.town).
TOWN = [(1010, 300, 46, 9), (1015, 690, 40, 11), (800, 70, 44, 8), (400, 70, 40, 12)]
# Knolls on the island's low land, away from the house, the pad and the bridges.
KNOLLS = [(760, 470, 22, 16), (735, 385, 16, 12), (520, 330, 18, 14), (690, 330, 20, 18), (780, 560, 15, 12)]


def normal(x, y, e=0.5, fn=None):
    fn = fn or height
    hx = (fn(x + e, y) - fn(x - e, y)) / (2 * e)
    hy = (fn(x, y + e) - fn(x, y - e)) / (2 * e)
    n = np.stack([-hx, -hy, np.ones_like(hx)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def ring_point(angle, r, z=None):
    x = CENTRE[0] + math.cos(math.radians(angle)) * r
    y = CENTRE[1] + math.sin(math.radians(angle)) * r
    return [float(x), float(y), float(height(x, y) if z is None else z)]
