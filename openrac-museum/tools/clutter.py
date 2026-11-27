"""The little things everywhere: crates to break, loose bolts to pick up, and the sea's own bits
(buoys, rafts, a wreck, old posts, coral and lilies). Fixed seeds, so every install is the same.
Crates and bolts are the game's own objects (geo.prop with a museum.TEMPLATES kind)."""

import math

import numpy as np

import island
import scatter
from vignettes import busy_points, free

SEA = island.SEA
C = island.CENTRE

# Crate kinds by chance: mostly bolts, some ammo, a few nanotech (health).
CRATES = (("bolt_crate", 0.74), ("ammo_crate", 0.16), ("nanotech_crate", 0.10))


def ground(x, y):
    return float(island.height(x, y))


def level_enough(x, y, r=1.2):
    zs = [ground(x + dx, y + dy) for dx, dy in ((0, 0), (r, 0), (-r, 0), (0, r), (0, -r))]
    return max(zs) - min(zs) < 0.5


def taken(x, y, spots, gap):
    return any(math.hypot(x - a, y - b) < gap for a, b in spots)


def land_spots(rng, count, busy, spots, gap, low=SEA + 0.8, high=SEA + 60):
    """COUNT random spots on dry, level ground away from the routes, the scenes and each other."""
    out = []
    for _ in range(count * 60):
        if len(out) >= count:
            break
        x, y = rng.uniform(C[0] - 330, C[0] + 330), rng.uniform(C[1] - 330, C[1] + 330)
        z = ground(x, y)
        if not (low < z < high) or not free(x, y, busy, 6.0) or taken(x, y, spots, gap):
            continue
        if island.normal(x, y)[2] < 0.9 or not level_enough(x, y):
            continue
        out.append((x, y, z))
        spots.append((x, y))
    return out


def crates(geo, rng, busy, spots):
    """Groups of one to four crates, one sometimes stacked, all over the island."""
    for x, y, z in land_spots(rng, 70, busy, spots, 14.0):
        n = int(rng.choice([1, 2, 2, 3, 3, 4]))
        yaw = float(rng.uniform(0, 90))
        f = np.array([math.cos(math.radians(yaw)), math.sin(math.radians(yaw))])
        s = np.array([-f[1], f[0]])
        placed = []
        for k in range(n):
            q = np.array([x, y]) + (f * (k % 2) + s * (k // 2)) * 1.15
            gz = ground(*q)
            if abs(gz - z) > 0.4:
                continue
            kind = pick(rng)
            geo.prop(kind, 0, (q[0], q[1], gz), yaw, 1.0)
            placed.append((q, gz))
        if len(placed) >= 2 and rng.random() < 0.35:
            q, gz = placed[0]
            geo.prop(pick(rng), 0, (q[0], q[1], gz + 1.0), yaw, 1.0)


def pick(rng):
    t = rng.random()
    for kind, p in CRATES:
        if t < p:
            return kind
        t -= p
    return CRATES[0][0]


def bolts(geo, rng, busy, spots):
    """Loose bolts in short trails (a line or an arc of three to six), as the game sets them."""
    for x, y, z in land_spots(rng, 55, busy, spots, 12.0, low=SEA + 0.6):
        n = int(rng.integers(3, 7))
        a = rng.uniform(0, 2 * math.pi)
        bend = rng.uniform(-0.35, 0.35)
        kind = "bolt" if rng.random() < 0.6 else "bolt_hexnut"
        q = np.array([x, y])
        for k in range(n):
            gz = ground(*q)
            if gz < SEA + 0.4:
                break
            geo.prop(kind, 0, (q[0], q[1], gz + 0.6), float(rng.uniform(0, 360)), 1.0)
            a += bend
            q = q + np.array([math.cos(a), math.sin(a)]) * 1.6


def coast_points(rng, count, depth_lo, depth_hi, spots, gap, r_lo=200, r_hi=330):
    """COUNT spots where the sea floor lies between DEPTH_LO and DEPTH_HI under the sea."""
    out = []
    for _ in range(count * 200):
        if len(out) >= count:
            break
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(island.pr(r_lo), island.pr(r_hi))
        x, y = C[0] + math.cos(a) * r, C[1] + math.sin(a) * r
        d = SEA - ground(x, y)
        if not (depth_lo < d < depth_hi) or taken(x, y, spots, gap) or not scatter.clear(x, y, 6):
            continue
        if math.hypot(x - 490, y - 403) < 60:       # the ship's way in
            continue
        out.append((x, y, ground(x, y)))
        spots.append((x, y))
    return out


def buoy(geo, x, y, rng):
    """A red and white buoy afloat, a lamp glowing on top."""
    z = SEA - 0.9
    geo.lathe("om_red", (x, y), [(0.05, z), (0.7, z + 0.2), (0.95, z + 0.8), (0.95, z + 1.2), (0.7, z + 1.45)], 14)
    geo.lathe("om_white", (x, y), [(0.7, z + 1.45), (0.5, z + 1.7), (0.42, z + 2.4)], 12)
    geo.lathe("om_red", (x, y), [(0.42, z + 2.4), (0.3, z + 2.6), (0.22, z + 2.75)], 12)
    geo.lathe("om_yellow", (x, y), [(0.02, z + 2.72), (0.2, z + 2.8), (0.22, z + 3.0), (0.15, z + 3.15), (0.02, z + 3.2)], 10,
              glow=[1.8, 1.5, 0.8])
    geo.light((x, y, z + 3.4), (1.2, 0.9, 0.5), 7)
    # The anchor chain.
    geo.tube("om_rail", [(x, y, z + 0.1), (x + 0.6, y + 0.4, z - 2.5), (x + 1.4, y + 0.6, ground(x + 1.4, y + 0.6))], 0.05, 5,
             caps=False)


def raft(geo, x, y, rng):
    """A little raft of round logs lashed together, a crate on it: jump over from the shore."""
    yaw = float(rng.uniform(0, 180))
    f = np.array([math.cos(math.radians(yaw)), math.sin(math.radians(yaw))])
    s = np.array([-f[1], f[0]])
    z = SEA - 0.1                               # the logs half under water
    for k in range(-3, 4):
        a = np.array([x, y]) + s * k * 0.42 - f * 1.6
        b = a + f * 3.2 + f * float(rng.uniform(-0.2, 0.2))
        geo.tube("om_planks_dark", [(*a, z), (*b, z)], 0.22, 8, uv_scale=1.5)
    for t in (-1.1, 1.1):
        c = np.array([x, y]) + f * t
        geo.tube("om_thatch", [(*(c - s * 1.5), z + 0.22), (*(c + s * 1.5), z + 0.22)], 0.05, 5)
    lo = np.array([x, y]) - f * 1.6 - s * 1.5
    hi = np.array([x, y]) + f * 1.6 + s * 1.5
    geo.block((min(lo[0], hi[0]), min(lo[1], hi[1]), z - 0.6), (max(lo[0], hi[0]), max(lo[1], hi[1]), z + 0.2))
    geo.prop(pick(rng), 0, (x, y, z + 0.2), yaw, 1.0)


def posts(geo, x, y, rng):
    """The rotten posts of an old jetty, sticking out of the water in two rows."""
    yaw = float(rng.uniform(0, 180))
    f = np.array([math.cos(math.radians(yaw)), math.sin(math.radians(yaw))])
    s = np.array([-f[1], f[0]])
    for k in range(int(rng.integers(3, 6))):
        for side in (-1, 1):
            if rng.random() < 0.25:
                continue
            q = np.array([x, y]) + f * k * 3.0 + s * side * 1.4
            top = SEA + float(rng.uniform(0.4, 2.2))
            lean = s * side * float(rng.uniform(0, 0.25))
            geo.tube("om_planks_dark", [(q[0], q[1], ground(*q) - 0.5), (*(q + lean), top)], [0.24, 0.2], 9)
            geo.lathe("om_planks_dark", tuple(q + lean), [(0.2, top), (0.12, top + 0.12), (0.02, top + 0.16)], 9)


def wreck(geo, x, y, rng):
    """An old boat run aground on a sandbar, half under the sea, its mast broken."""
    yaw = float(rng.uniform(0, 360))
    f = np.array([math.cos(math.radians(yaw)), math.sin(math.radians(yaw))])
    s = np.array([-f[1], f[0]])
    L, rows, cols = 16.0, 18, 11
    tilt = 0.3
    P, uv = [], []
    zb = min(ground(x, y) + 0.3, SEA - 1.3)     # half sunk: the rim just over the sea
    for i in range(rows):
        t = i / (rows - 1)
        w = 2.6 * math.sin(math.pi * (0.08 + 0.84 * t)) ** 0.5
        depth = 2.2 * math.sin(math.pi * t) ** 0.35
        rise = 1.2 * (2 * t - 1) ** 4
        row = []
        for j in range(cols):
            u = j / (cols - 1) * 2 - 1
            q = np.array([x, y]) + f * (t - 0.5) * L + s * u * w
            row.append((q[0], q[1], zb + 2.2 + rise - depth * (1 - u * u) + u * tilt * w))
        P.append(row)
        uv.append([(t * L / 3, v) for v in np.linspace(0, 3, cols)])
    P = np.array(P)
    geo.surface("om_planks_dark", P, np.array(uv))
    geo.surface("om_planks_dark", P[:, ::-1] + np.array([0, 0, 0.0]), np.array(uv)[:, ::-1])   # the inside
    for j in (0, cols - 1):
        geo.tube("om_planks", list(P[:, j]), 0.16, 6, caps=False)
    # Ribs where planks are gone, and the stump of the mast.
    for t in (0.3, 0.45, 0.6):
        i = int(t * (rows - 1))
        geo.tube("om_planks", [P[i, 0], P[i, cols // 2] + np.array([0, 0, 0.1]), P[i, -1]], 0.12, 6)
    m = np.array([x, y]) + f * 1.0
    geo.tube("om_planks_dark", [(m[0], m[1], zb), (*(m + s * 0.3), zb + 5.5)], [0.32, 0.26], 10)
    geo.tube("om_planks_dark", [(*(m + s * 2.5 + f * 1.5), SEA + 0.1), (*(m + s * 4.5 + f * 3.8), SEA + 0.3)], 0.25, 9)   # the fallen top
    geo.prop("bolt_crate", 0, (*(np.array([x, y]) - f * 3.5), ground(*(np.array([x, y]) - f * 3.5)) + 0.0), yaw, 1.0)


def coral(geo, x, y, z, rng):
    """A patch of Pokitaru's coral, starfish and shells on the sea floor."""
    for _ in range(int(rng.integers(3, 7))):
        q = np.array([x, y]) + rng.normal(0, 1.6, 2)
        gz = ground(*q)
        if gz > SEA - 0.6:
            continue
        t = rng.random()
        if t < 0.45:
            geo.prop("tie", 696, (q[0], q[1], gz - 0.2), float(rng.uniform(0, 360)), float(rng.uniform(0.08, 0.18)))
        elif t < 0.7:
            geo.prop("shrub", 371, (q[0], q[1], gz), float(rng.uniform(0, 360)), float(rng.uniform(0.8, 1.4)))
        elif t < 0.85:
            geo.prop("shrub", 373, (q[0], q[1], gz), float(rng.uniform(0, 360)), 1.0)
        else:
            geo.prop("tie", 1513, (q[0], q[1], gz - 0.4), float(rng.uniform(0, 360)), float(rng.uniform(0.3, 0.6)))


def lilies(geo, x, y, rng):
    """A raft of white lilies floating in a quiet shallow."""
    for _ in range(int(rng.integers(4, 9))):
        q = np.array([x, y]) + rng.normal(0, 1.8, 2)
        if ground(*q) > SEA - 0.3:
            continue
        geo.prop("shrub", 308, (q[0], q[1], SEA + 0.05), float(rng.uniform(0, 360)), float(rng.uniform(0.8, 1.2)))


def place(geo, layout):
    rng = np.random.default_rng(2026)
    busy = busy_points(layout)
    spots = [(v[1], v[2]) for v in layout.get("vignettes", [])]
    crates(geo, rng, busy, spots)
    bolts(geo, rng, busy, spots)
    sea = []
    things = layout.setdefault("sea_things", [])
    for x, y, z in coast_points(rng, 12, 2.5, 12.0, sea, 40.0, 220, 420):
        buoy(geo, x, y, rng)
        things.append(("buoy", round(x, 1), round(y, 1)))
    for x, y, z in coast_points(rng, 7, 0.6, 3.0, sea, 25.0):
        raft(geo, x, y, rng)
        things.append(("raft", round(x, 1), round(y, 1)))
    for x, y, z in coast_points(rng, 6, 0.3, 3.0, sea, 25.0, 200, 420):
        posts(geo, x, y, rng)
        things.append(("posts", round(x, 1), round(y, 1)))
    for x, y, z in coast_points(rng, 2, 0.3, 4.0, sea, 30.0, 180, 450):
        wreck(geo, x, y, rng)
        things.append(("wreck", round(x, 1), round(y, 1)))
    for x, y, z in coast_points(rng, 45, 0.5, 2.0, sea, 9.0):
        coral(geo, x, y, z, rng)
        things.append(("coral", round(x, 1), round(y, 1)))
    for x, y, z in coast_points(rng, 18, 0.3, 1.5, sea, 12.0):
        lilies(geo, x, y, rng)
