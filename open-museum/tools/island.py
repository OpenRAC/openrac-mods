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


def height(x, y):
    """The ground's height at game X, Y (arrays or numbers)."""
    x = np.asarray(x, np.float64); y = np.asarray(y, np.float64)
    u, v = x - CENTRE[0], y - CENTRE[1]
    r = np.hypot(u, v)
    a = np.degrees(np.arctan2(v, u))
    wobble = (fbm(x, y, 140, 3, 0) - 0.5) * 60
    rr = r + wobble

    # The sea floor, the beach and the low land.
    land = SEA - 26 + 34 * smooth(330, 250, rr)          # up to SEA + 8 inland
    land = land + (fbm(x, y, 60, 3, 1) - 0.5) * 4 * smooth(250, 200, rr)
    beach = smooth(250, 215, rr)
    land = np.minimum(land, SEA + 2.5 + 4.5 * smooth(235, 200, rr))

    # The lagoon round the house: a smooth bowl, 7 units deep.
    lagoon = smooth(LAGOON_R + 22, LAGOON_R - 6, r)
    land = land * (1 - lagoon) + (SEA - 7) * lagoon

    # The mountains: a ridge on the north half, a steep face toward the house.
    north = smooth(15, 40, a) * smooth(165, 140, a)
    ridge_in = smooth(138, 162, r)                       # the cliff face
    ridge_out = smooth(290, 230, rr)
    peaks = 70 + 90 * fbm(x, y, 90, 4, 2)
    mountains = north * ridge_in * ridge_out * peaks
    # Notches where the waterfalls start.
    for f in FALLS:
        d = np.abs(((a - f["angle"] + 180) % 360) - 180)
        notch = smooth(7, 2, d) * smooth(140, 170, r)
        mountains = mountains * (1 - notch) + np.minimum(mountains, f["lip"] - SEA - 8) * notch
    land = np.where(mountains > 0.5, np.maximum(land, SEA + 4 + mountains), land)

    # The volcano, its crater glowing.
    dv = np.hypot(x - VOLCANO[0], y - VOLCANO[1])
    cone = (VOLCANO_TOP - SEA) * smooth(150, 18, dv) ** 1.25
    crater = smooth(14, 4, dv) * 30
    land = np.where(cone > 2, np.maximum(land, SEA + cone - crater), land)
    # The shrine's terrace, cut into the volcano's side.
    terrace = smooth(15, 11, np.hypot(x - VOLCANO_LEDGE[0], y - VOLCANO_LEDGE[1]))
    land = land * (1 - terrace) + np.minimum(land, LEDGE_Z) * terrace

    # The two streams from the falls' pools to the lagoon.
    for f in FALLS:
        d = np.abs(((a - f["angle"] + 180) % 360) - 180)
        stream = smooth(5, 1.5, d * r / 57.3 / 3) * smooth(LAGOON_R - 2, LAGOON_R + 6, r) * smooth(152, 140, r)
        land = land * (1 - stream) + np.minimum(land, SEA - 2.5) * stream
        # The pool at the foot of the fall.
        px = CENTRE[0] + math.cos(math.radians(f["angle"])) * (f["r"] - 10)
        py = CENTRE[1] + math.sin(math.radians(f["angle"])) * (f["r"] - 10)
        pool = smooth(16, 8, np.hypot(x - px, y - py))
        land = land * (1 - pool) + np.minimum(land, SEA - 4) * pool

    # The grotto behind the second waterfall: its floor, under a stone roof (landmarks.py).
    g = GROTTO
    dg = np.abs(((a - g["angle"] + 180) % 360) - 180)
    inside = smooth(g["half"] + 1.5, g["half"] - 0.5, dg * r / 57.2958) * smooth(g["r0"] - 2, g["r0"], r) * smooth(g["r1"] + 2, g["r1"], r)
    land = land * (1 - inside) + (SEA + 2.0) * inside

    # Offshore islets.
    for (ix, iy, ir, ih) in ISLETS:
        di = np.hypot(x - ix, y - iy)
        land = np.maximum(land, SEA - 20 + (ih + 20) * smooth(ir, ir * 0.3, di))
    return land


# Small islands around: (x, y, radius, height above SEA).
ISLETS = [(870, 250, 26, 6), (330, 250, 20, 14), (600, 140, 18, 4), (300, 800, 30, 38)]


def normal(x, y, e=0.5):
    hx = (height(x + e, y) - height(x - e, y)) / (2 * e)
    hy = (height(x, y + e) - height(x, y - e)) / (2 * e)
    n = np.stack([-hx, -hy, np.ones_like(hx)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def ring_point(angle, r, z=None):
    x = CENTRE[0] + math.cos(math.radians(angle)) * r
    y = CENTRE[1] + math.sin(math.radians(angle)) * r
    return [float(x), float(y), float(height(x, y) if z is None else z)]
