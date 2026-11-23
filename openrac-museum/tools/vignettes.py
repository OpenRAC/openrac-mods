"""Small scenes all over the island, so no corner is bare: huts, canoes, campfires, totems,
hammocks, sunshades, signposts, fences, flower beds, stepping stones, driftwood, little piers.

Every patch of land gets one or two, chosen and turned by its place (fixed seeds: every install is
the same). They are drawn only (no collision), out of the paths, the gadget routes and the house."""

import math

import numpy as np

import house
import island
import scatter
from house import flower_pot, flowers_row, torch

SEA = island.SEA
C = island.CENTRE


def busy_points(layout):
    """Where nothing may go: the landmarks' spots and the gadget routes."""
    pts = [tuple(m["position"][:2]) for m in layout["mobys"]]
    pts += [tuple(p[:2]) for g in layout["grind_paths"] for p in g["points"][::3]]
    pts += [tuple(f["pool"][:2]) for f in layout["falls"]]
    pts += [tuple(layout["lookout"][:2]), tuple(layout["islet"][:2]), tuple(layout["volcano_rim"][:2]), (490, 403)]
    return np.array(pts)


def free(x, y, busy, margin):
    if not scatter.clear(x, y, 4):
        return False
    return len(busy) == 0 or np.min(np.hypot(busy[:, 0] - x, busy[:, 1] - y)) > margin


def flat_spot(x, y, r):
    """The ground's height at X, Y when the ground within R is flat enough to stand things on."""
    zs = [float(island.height(x + dx, y + dy)) for dx, dy in ((0, 0), (r, 0), (-r, 0), (0, r), (0, -r))]
    if max(zs) - min(zs) > 1.2:
        return None
    return float(np.mean(zs))


# --- the little scenes (each at P = (x, y), ground Z, turned YAW degrees) ---

def frame(yaw):
    f = np.array([math.cos(math.radians(yaw)), math.sin(math.radians(yaw))])
    return f, np.array([-f[1], f[0]])


def hut(geo, p, z, yaw, r):
    f, s = frame(yaw)
    w = 3.2
    for a in (-1, 1):
        for b in (-1, 1):
            q = p + f * a * w + s * b * w
            geo.cylinder("om_planks_dark", (q[0], q[1]), 0.28, z - 0.5, z + 3.6, 6)
    geo.cylinder("om_planks_light", (p[0], p[1]), w * 1.2, z + 0.4, z + 0.7, 8)
    geo.cone("om_thatch", (p[0], p[1]), w * 1.75, z + 3.4, z + 6.8, 8)
    geo.cone("om_gold", (p[0], p[1]), 0.4, z + 6.6, z + 7.6, 5)
    # A hammock-like bench and a pot inside, a lantern on a post.
    geo.oriented_box("om_red", p - s * 1.8 - f * 0.6, p + s * 1.8 - f * 0.6, f, 1.0, z + 1.0, z + 1.2, solid=False)
    flower_pot(geo, p + f * 1.6 + s * 1.6, z + 0.7, int(r.integers(1 << 30)), 0.8)


def canoe(geo, p, z, yaw, r):
    f, s = frame(yaw)
    col = ("om_red", "om_teal", "om_orange")[int(r.integers(3))]
    for k in range(-3, 4):
        w = 0.9 * (1 - (abs(k) / 4) ** 2)
        q = p + f * k * 1.1
        geo.oriented_box("om_planks_dark", q - f * 0.56, q + f * 0.56, s, w * 2, z, z + 0.7, solid=False)
        geo.oriented_box(col, q - f * 0.56, q + f * 0.56, s, w * 2 + 0.1, z + 0.55, z + 0.75, solid=False)
    paddle = p + s * 1.4
    geo.beam("om_planks", (*(paddle - f * 1.6), z + 0.1), (*(paddle + f * 1.6), z + 0.3), 0.15)
    geo.oriented_box("om_planks", paddle + f * 1.6 - s * 0.25, paddle + f * 2.4 - s * 0.25, s, 0.05, z + 0.05, z + 0.4, solid=False)


def campfire(geo, p, z, yaw, r):
    """A ring of tilted stones, a tepee of logs over a bed of glowing coals; its flames are the
    mod's live particles (geo.flame)."""
    for k in range(10):
        a = 2 * math.pi * k / 10 + float(r.uniform(-0.15, 0.15))
        q = p + np.array([math.cos(a), math.sin(a)]) * float(r.uniform(1.2, 1.45))
        t = np.array([-math.sin(a), math.cos(a)]) * float(r.uniform(0.2, 0.32))
        lift = float(r.uniform(0.0, 0.12))
        geo.beam("om_stone", (*(q - t), z + 0.05), (*(q + t), z + 0.12 + lift), float(r.uniform(0.32, 0.46)))
    # The coals: small glowing chunks at every tilt, hotter in the middle.
    for k in range(14):
        rr = float(r.uniform(0, 0.75))
        a = float(r.uniform(0, 2 * math.pi))
        q = p + np.array([math.cos(a), math.sin(a)]) * rr
        d = np.array([float(r.uniform(-1, 1)), float(r.uniform(-1, 1)), float(r.uniform(-0.3, 0.3))])
        d = d / np.linalg.norm(d) * float(r.uniform(0.1, 0.2))
        hot = 1 - rr / 0.75
        glow = [1.9, 0.55 + 0.7 * hot, 0.2 + 0.25 * hot]
        geo.beam("om_planks_dark", (q[0] - d[0], q[1] - d[1], z + 0.08 - d[2]),
                 (q[0] + d[0], q[1] + d[1], z + 0.12 + d[2]), float(r.uniform(0.12, 0.2)), glow=glow)
    # The tepee: logs leaning in to a point over the coals, their feet charred.
    top = (p[0], p[1], z + 1.25)
    for k in range(6):
        a = math.radians(yaw) + 2 * math.pi * k / 6 + float(r.uniform(-0.2, 0.2))
        foot = p + np.array([math.cos(a), math.sin(a)]) * float(r.uniform(0.75, 0.95))
        geo.beam("om_planks_dark", (*foot, z + 0.05), top, 0.16)
        geo.beam("om_planks_dark", (*foot, z + 0.05), (*(foot + (p - foot) * 0.35), z + 0.45), 0.17,
                 glow=[0.9, 0.35, 0.15])
    f, s = frame(yaw)
    geo.light((p[0], p[1], z + 1.0), (1.7, 0.95, 0.4), 10)
    geo.flame("fire", (p[0], p[1], z + 0.5))
    # Logs to sit on.
    for t in (-1, 1):
        q = p + f * 3.0 * t
        geo.beam("om_planks_dark", (*(q - s * 1.4), z + 0.3), (*(q + s * 1.4), z + 0.3), 0.6)


def totem(geo, p, z, yaw, r):
    cols = ["om_red", "om_teal", "om_yellow", "om_orange"]
    r.shuffle(cols)
    zz = z
    for k, c in enumerate(cols[:3]):
        h = 1.4
        geo.box(c, (p[0] - 0.7, p[1] - 0.7, zz), (p[0] + 0.7, p[1] + 0.7, zz + h), solid=False)
        f, s = frame(yaw)
        eye = p + f * 0.72
        for t in (-0.3, 0.3):
            e = eye + s * t
            geo.box("om_white", (e[0] - 0.12, e[1] - 0.12, zz + 0.8), (e[0] + 0.12, e[1] + 0.12, zz + 1.05), solid=False)
        zz += h
    geo.oriented_box("om_gold", p - frame(yaw)[1] * 1.8, p + frame(yaw)[1] * 1.8, frame(yaw)[0], 0.3, zz - 0.4, zz - 0.1, solid=False)
    geo.cone("om_gold", (p[0], p[1]), 0.8, zz, zz + 1.2, 4)


def sunshade(geo, p, z, yaw, r):
    f, s = frame(yaw)
    geo.cylinder("om_white", (p[0], p[1]), 0.08, z - 0.5, z + 3.0, 6)
    c = ("om_red", "om_teal", "om_yellow")[int(r.integers(3))]
    geo.cone(c, (p[0], p[1]), 2.2, z + 2.4, z + 3.4, 8)
    for t in (-1, 1):                      # two deck chairs
        q = p + s * 1.4 * t - f * 0.8
        geo.oriented_box("om_planks_light", q - f * 1.0, q + f * 0.6, s, 0.8, z + 0.25, z + 0.4, solid=False)
        geo.beam("om_white", (*(q + f * 0.6), z + 0.4), (*(q + f * 1.2), z + 1.2), 0.75)
    geo.oriented_box(("om_orange", "om_teal")[int(r.integers(2))], p + f * 2.4 - s * 0.3, p + f * 2.4 + s * 0.3, f, 0.12, z - 0.3, z + 2.2, solid=False)  # a surfboard


def signpost(geo, p, z, yaw, r):
    geo.box("om_planks_dark", (p[0] - 0.15, p[1] - 0.15, z - 0.4), (p[0] + 0.15, p[1] + 0.15, z + 3.2), solid=False)
    for k in range(3):
        a = yaw + k * 110 + float(r.uniform(-15, 15))
        f, s = frame(a)
        zz = z + 2.6 - k * 0.55
        geo.oriented_box(("om_planks", "om_planks_light")[k % 2], p, p + f * 1.6, s, 0.1, zz - 0.2, zz + 0.2, solid=False)
        geo.cone("om_planks", (*(p + f * 1.75),), 0.2, zz - 0.2, zz + 0.2, 3)


def fence(geo, p, z, yaw, r):
    f, s = frame(yaw)
    pts = [p + f * (k - 2) * 2.4 + s * float(r.uniform(-0.3, 0.3)) for k in range(5)]
    for q in pts:
        g = float(island.height(*q))
        geo.box("om_planks_dark", (q[0] - 0.12, q[1] - 0.12, g - 0.3), (q[0] + 0.12, q[1] + 0.12, g + 1.3), solid=False)
    for a, b in zip(pts, pts[1:]):
        ga, gb = float(island.height(*a)), float(island.height(*b))
        geo.beam("om_planks", (*a, ga + 1.1), (*b, gb + 1.1), 0.12)
        geo.beam("om_planks", (*a, ga + 0.6), (*b, gb + 0.6), 0.12)
    flowers_row(geo, pts[0] + s * 0.6, pts[-1] + s * 0.6, z, int(r.integers(1 << 20)))


def flower_bed(geo, p, z, yaw, r):
    f, s = frame(yaw)
    for k in range(5):
        q = p + f * float(r.uniform(-2.5, 2.5)) + s * float(r.uniform(-2.5, 2.5))
        g = float(island.height(*q))
        flowers_row(geo, q - f * 0.9, q + f * 0.9, g, int(r.integers(1 << 20)))
    for k in range(6):
        a = 2 * math.pi * k / 6
        q = p + np.array([math.cos(a), math.sin(a)]) * 3.3
        g = float(island.height(*q))
        geo.box("om_stone", (q[0] - 0.4, q[1] - 0.4, g - 0.2), (q[0] + 0.4, q[1] + 0.4, g + 0.3), solid=False)


def driftwood(geo, p, z, yaw, r):
    f, s = frame(yaw)
    geo.beam("om_planks_dark", (*(p - f * 2.6), z + 0.2), (*(p + f * 2.6), z + 0.35), 0.55)
    geo.beam("om_planks_dark", (*(p + f * 1.0), z + 0.3), (*(p + f * 1.8 + s * 1.2), z + 0.6), 0.25)
    for k in range(3):
        q = p + s * float(r.uniform(1.5, 3)) + f * float(r.uniform(-2, 2))
        geo.box("om_white", (q[0] - 0.15, q[1] - 0.15, z), (q[0] + 0.15, q[1] + 0.15, z + 0.15), solid=False)  # shells


def barrels(geo, p, z, yaw, r):
    f, s = frame(yaw)
    for k, (a, b) in enumerate(((0, 0), (1.3, 0.2), (0.6, 1.2), (0.6, 0.5))):
        q = p + f * a + s * b
        zz = z + (1.1 if k == 3 else 0)
        geo.cylinder("om_planks", (q[0], q[1]), 0.55, zz, zz + 1.1, 10)
        geo.cylinder("om_planks_dark", (q[0], q[1]), 0.58, zz + 0.25, zz + 0.35, 10, cap=False)
        geo.cylinder("om_planks_dark", (q[0], q[1]), 0.58, zz + 0.75, zz + 0.85, 10, cap=False)
    q = p - f * 1.6
    geo.box("om_planks_light", (q[0] - 0.7, q[1] - 0.7, z), (q[0] + 0.7, q[1] + 0.7, z + 1.4), solid=False)
    geo.box("om_planks_dark", (q[0] - 0.75, q[1] - 0.75, z + 0.6), (q[0] + 0.75, q[1] + 0.75, z + 0.8), solid=False)


def hammock(geo, p, z, yaw, r):
    f, s = frame(yaw)
    a, b = p - f * 3.0, p + f * 3.0
    for q in (a, b):
        geo.cylinder("om_planks_dark", (q[0], q[1]), 0.22, z - 0.3, z + 2.4, 6)
        geo.cone("om_grass", (q[0], q[1]), 1.3, z + 2.2, z + 3.4, 6)
    pts = [a + (b - a) * t for t in np.linspace(0, 1, 7)]
    zs = [z + 1.9 - 0.9 * math.sin(math.pi * t) for t in np.linspace(0, 1, 7)]
    col = ("om_red", "om_teal", "om_yellow")[int(r.integers(3))]
    for (q0, z0), (q1, z1) in zip(zip(pts, zs), zip(pts[1:], zs[1:])):
        geo.oriented_box(col, q0, q1, s, 1.3, min(z0, z1) - 0.05, max(z0, z1) + 0.05, solid=False)


def stone_lantern(geo, p, z, yaw, r):
    geo.box("om_stone", (p[0] - 0.6, p[1] - 0.6, z - 0.2), (p[0] + 0.6, p[1] + 0.6, z + 0.4), solid=False)
    geo.box("om_stone", (p[0] - 0.25, p[1] - 0.25, z + 0.4), (p[0] + 0.25, p[1] + 0.25, z + 1.4), solid=False)
    geo.box("om_yellow", (p[0] - 0.4, p[1] - 0.4, z + 1.4), (p[0] + 0.4, p[1] + 0.4, z + 2.0), solid=False, glow=[1.8, 1.6, 0.9])
    geo.light((p[0], p[1], z + 1.7), (1.3, 1.0, 0.55), 7)
    geo.cone("om_stone", (p[0], p[1]), 0.9, z + 2.0, z + 2.7, 4)


def tide_pool(geo, p, z, yaw, r):
    for k in range(10):
        a = 2 * math.pi * k / 10 + float(r.uniform(-0.2, 0.2))
        d = float(r.uniform(2.0, 3.0))
        q = p + np.array([math.cos(a), math.sin(a)]) * d
        g = float(island.height(*q))
        s = float(r.uniform(0.3, 0.7))
        geo.box("om_stone", (q[0] - s, q[1] - s, g - 0.3), (q[0] + s, q[1] + s, g + s * 0.8), solid=False)
    geo.cylinder("om_water", (p[0], p[1]), 2.0, z - 0.1, z + 0.08, 12)


ON_GRASS = (hut, campfire, totem, signpost, fence, flower_bed, barrels, hammock, stone_lantern, flower_bed, fence)
ON_SAND = (canoe, sunshade, driftwood, tide_pool, campfire, barrels, sunshade, driftwood)
ON_ROCK = (totem, stone_lantern, flower_bed, hut, signpost, campfire)


def place(geo, layout, cell=22.0):
    """One scene per patch of land (CELL units): grass, sand or a rock's top each get their own."""
    busy = busy_points(layout)
    out = []
    for gx in np.arange(C[0] - 470, C[0] + 470, cell):
        for gy in np.arange(C[1] - 470, C[1] + 470, cell):
            seed = int((gx * 7919 + gy * 104729) % 1000003)
            r = np.random.default_rng(seed)
            for attempt in range(5):
                x = gx + float(r.uniform(0, cell)); y = gy + float(r.uniform(0, cell))
                z = flat_spot(x, y, 3.0)
                if z is None or z < SEA + 0.6 or not free(x, y, busy, 9.0):
                    continue
                top = float(island.rock_top(x, y))
                on_rock = np.isfinite(top) and top >= z - 0.5 and z > SEA + 10
                kinds = ON_ROCK if on_rock else (ON_SAND if z < SEA + 4.6 else ON_GRASS)
                fn = kinds[int(r.integers(len(kinds)))]
                yaw = float(r.uniform(0, 360))
                # Face the scene towards the house, half the time: things look placed, not thrown.
                if r.random() < 0.5:
                    yaw = math.degrees(math.atan2(C[1] - y, C[0] - x))
                fn(geo, np.array([x, y]), z, yaw, r)
                out.append((fn.__name__, round(x, 1), round(y, 1), round(z, 1)))
                break
    stepping_stones(geo)
    return out


def stepping_stones(geo):
    """Stones from the ship's pad to the gate, and from the east bridge to the OPENRAC letters."""
    for a, b in (((498.0, 412.0), (house.CX, house.CY - island.LAGOON_R - 26)),
                 ((house.CX + island.LAGOON_R + 30, house.CY), (house.CX + 146, house.CY - 50))):
        a, b = np.array(a), np.array(b)
        n = int(np.linalg.norm(b - a) / 2.6)
        d = (b - a) / np.linalg.norm(b - a)
        s = np.array([-d[1], d[0]])
        for k in range(n):
            q = a + (b - a) * (k + 0.5) / n + s * math.sin(k * 0.7) * 0.6
            g = float(island.height(*q))
            if g < SEA + 0.3:
                continue
            w = 0.8 + 0.25 * math.sin(k * 1.7)
            geo.cylinder("om_stone", (q[0], q[1]), w, g - 0.2, g + 0.12, 7)
