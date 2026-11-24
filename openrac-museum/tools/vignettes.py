"""Small scenes all over the island, so no corner is bare: huts, canoes, campfires, totems,
hammocks, sunshades, signposts, fences, flower beds, stepping stones, driftwood, little piers.

Every patch of land gets one or two, chosen and turned by its place (fixed seeds: every install is
the same). They are drawn only (no collision), out of the paths, the gadget routes and the house."""

import math

import numpy as np

import house
import island
import scatter
from house import flower_pot, torch

SEA = island.SEA
C = island.CENTRE


def busy_points(layout):
    """Where nothing may go: the landmarks' spots and the gadget routes."""
    pts = [tuple(m["position"][:2]) for m in layout["mobys"]]
    pts += [tuple(p[:2]) for g in layout["grind_paths"] for p in g["points"][::3]]
    pts += [tuple(f["pool"][:2]) for f in layout["falls"]]
    pts += [tuple(layout["lookout"][:2]), tuple(layout["islet"][:2]), tuple(layout["volcano_rim"][:2]), (490, 403)]
    # The spiral stair: its stone core and the flights round it (12 out from its middle).
    if "stair" in layout:
        sx, sy = layout["stair"][:2]
        pts += [(sx + dx, sy + dy) for dx in (-8, 0, 8) for dy in (-8, 0, 8)]
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
    return zs[0]            # the middle's own height: the scene stands on it, not over a hollow


# --- the little scenes (each at P = (x, y), ground Z, turned YAW degrees) ---

def frame(yaw):
    f = np.array([math.cos(math.radians(yaw)), math.sin(math.radians(yaw))])
    return f, np.array([-f[1], f[0]])


# --- helpers: organic parts and Pokitaru's own little things ---

FLOWERS = (308, 306, 307)   # Pokitaru's white lily, red fruit, green leafy plant (shrubs)


def log(geo, a, b, z, radius, r, material="om_planks_dark"):
    """A round log from A to B at Z, a little bent, its ends cut."""
    pts = [np.array([*(a + (b - a) * t), z + radius * 0.9 + math.sin(t * math.pi) * radius * 0.15]) for t in np.linspace(0, 1, 5)]
    rad = [radius * (1.0 - 0.08 * abs(t - 0.5)) for t in np.linspace(0, 1, 5)]
    geo.tube(material, pts, rad, sides=9, uv_scale=1.5)


def flowers(geo, a, b, z, r, step=1.4):
    """Pokitaru's own flowers and leafy plants along A-B (shrubs, placed by museum.py)."""
    n = max(1, int(np.linalg.norm(b - a) / step))
    for k in range(n):
        q = a + (b - a) * (k + float(r.uniform(0.2, 0.8))) / n
        g = float(island.height(*q))
        geo.prop("shrub", FLOWERS[int(r.integers(len(FLOWERS)))], (q[0], q[1], g), float(r.uniform(0, 360)),
                 float(r.uniform(0.8, 1.3)))


def hut(geo, p, z, yaw, r):
    """A round beach hut: six bent poles, a plank floor, a thick thatch roof with a hanging fringe."""
    f, s = frame(yaw)
    for k in range(6):
        a = math.radians(yaw) + 2 * math.pi * k / 6 + math.pi / 6
        q = p + np.array([math.cos(a), math.sin(a)]) * 3.3
        lean = np.array([math.cos(a), math.sin(a)]) * 0.15
        geo.tube("om_planks_dark", [(q[0], q[1], z - 0.4), (*(q + lean * 0.5), z + 1.8), (*(q - lean * 0.2), z + 3.7)],
                 [0.24, 0.21, 0.19], sides=7)
    geo.lathe("om_planks_light", (p[0], p[1]), [(3.75, z + 0.3), (3.8, z + 0.6), (3.6, z + 0.72), (0.05, z + 0.74)],
              sides=14, uv_scale=2.0)
    geo.lathe("om_thatch", (p[0], p[1]), [(4.9, z + 3.05), (4.8, z + 3.45), (4.0, z + 4.2), (2.9, z + 5.2), (1.7, z + 6.2),
                                          (0.7, z + 7.0), (0.08, z + 7.35)], sides=16, uv_scale=1.6, wobble=0.035,
              seed=int(r.integers(1 << 20)))
    # The roof's underside, so it is not hollow from below.
    geo.lathe("om_thatch", (p[0], p[1]), [(0.1, z + 3.7), (3.0, z + 3.75), (4.75, z + 3.1)], sides=16, uv_scale=1.6)
    geo.lathe("om_planks_dark", (p[0], p[1]), [(0.22, z + 7.2), (0.3, z + 7.5), (0.18, z + 7.9), (0.02, z + 8.05)], sides=8)
    # A bench of planks inside, a pot of Pokitaru's flowers.
    q = p - f * 1.2
    geo.slab("om_planks", [(*(q - s * 1.7 - f * 0.45), z + 1.25), (*(q + s * 1.7 - f * 0.45), z + 1.25),
                           (*(q + s * 1.7 + f * 0.45), z + 1.25), (*(q - s * 1.7 + f * 0.45), z + 1.25)], (0, 0, 1), 0.12)
    for t in (-1.4, 1.4):
        geo.tube("om_planks_dark", [(*(q + s * t), z + 0.72), (*(q + s * t), z + 1.2)], 0.1, sides=6)
    flower_pot(geo, p + f * 1.6 + s * 1.6, z + 0.72, int(r.integers(1 << 30)), 0.8)


def canoe(geo, p, z, yaw, r):
    """A dug-out canoe: a smooth hull, a painted rim, two thwarts, a paddle in the sand."""
    f, s = frame(yaw)
    col = ("om_red", "om_teal", "om_orange")[int(r.integers(3))]
    L, rows, cols = 7.6, 14, 9
    P, uv = [], []
    for i in range(rows):
        t = i / (rows - 1)
        w = 0.95 * math.sin(math.pi * t) ** 0.6
        depth = 0.75 * math.sin(math.pi * t) ** 0.4
        rise = 0.35 * (2 * t - 1) ** 4
        row = []
        for j in range(cols):
            u = j / (cols - 1) * 2 - 1
            q = p + f * (t - 0.5) * L + s * u * w
            row.append((q[0], q[1], z + 0.8 + rise - depth * (1 - u * u)))
        P.append(row)
        uv.append([(t * L / 2, u) for u in np.linspace(0, 2, cols)])
    geo.surface("om_planks_dark", np.array(P), np.array(uv))
    for j in (0, cols - 1):
        geo.tube(col, [P[i][j] for i in range(rows)], 0.07, sides=5, caps=False)
    for t in (0.33, 0.66):
        i = int(t * (rows - 1))
        geo.tube("om_planks", [P[i][0], P[i][cols - 1]], 0.06, sides=5)
    blade = p + s * 1.6 + f * 1.0
    geo.tube("om_planks", [(blade[0], blade[1], z - 0.2), (*(blade + s * 0.4), z + 2.3)], 0.05, sides=6)
    geo.slab("om_planks", [(blade[0] - f[0] * 0.18, blade[1] - f[1] * 0.18, z - 0.2), (blade[0] + f[0] * 0.18, blade[1] + f[1] * 0.18, z - 0.2),
                           (blade[0] + f[0] * 0.24, blade[1] + f[1] * 0.24, z + 0.5), (blade[0] - f[0] * 0.24, blade[1] - f[1] * 0.24, z + 0.5)],
             (s[0], s[1], 0), 0.05)


def campfire(geo, p, z, yaw, r):
    """A ring of tilted stones, a tepee of logs over a bed of glowing coals; its flames are the
    mod's live particles (geo.flame)."""
    for k in range(10):
        a = 2 * math.pi * k / 10 + float(r.uniform(-0.15, 0.15))
        q = p + np.array([math.cos(a), math.sin(a)]) * float(r.uniform(1.2, 1.45))
        t = np.array([-math.sin(a), math.cos(a)]) * float(r.uniform(0.2, 0.32))
        lift = float(r.uniform(0.0, 0.12))
        geo.stone("om_stone", (q[0], q[1], z + lift * 0.3), float(r.uniform(0.28, 0.4)), int(r.integers(1 << 20)))
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
    # Over the tepee's point, where the flames are: from inside it the logs would throw spokes.
    geo.light((p[0], p[1], z + 1.6), (2.6, 1.4, 0.5), 11, size=0.6)
    geo.flame("fire", (p[0], p[1], z + 0.5))
    # Logs to sit on.
    for t in (-1, 1):
        q = p + f * 3.0 * t
        log(geo, q - s * 1.5, q + s * 1.5, z + 0.32, 0.32, r)


def totem(geo, p, z, yaw, r):
    """A carved totem: three painted, bulging faces, wings at the top, a beaked head."""
    cols = ["om_red", "om_teal", "om_yellow", "om_orange"]
    r.shuffle(cols)
    f, s = frame(yaw)
    zz = z - 0.3
    seed = int(r.integers(1 << 20))
    for k, c in enumerate(cols[:3]):
        h = 1.45
        geo.lathe(c, (p[0], p[1]), [(0.62, zz), (0.78, zz + h * 0.3), (0.82, zz + h * 0.55), (0.7, zz + h * 0.85),
                                    (0.6, zz + h)], sides=10, wobble=0.05, seed=seed + k)
        eye = p + f * 0.74
        for t in (-0.3, 0.3):
            e = eye + s * t
            geo.lathe("om_white", (e[0], e[1]), [(0.001, zz + 0.82), (0.13, zz + 0.85), (0.13, zz + 1.02), (0.001, zz + 1.05)], sides=6)
        geo.tube("om_planks_dark", [(*(p + f * 0.6), zz + 0.45), (*(p + f * 0.95), zz + 0.55)], [0.14, 0.06], sides=6)
        zz += h
    for t in (-1, 1):
        geo.slab("om_gold", [(*(p + s * t * 0.5), zz - 0.5), (*(p + s * t * 2.2), zz - 0.1), (*(p + s * t * 2.0), zz + 0.3),
                             (*(p + s * t * 0.5), zz + 0.1)], (f[0], f[1], 0), 0.14)
    geo.lathe("om_gold", (p[0], p[1]), [(0.6, zz), (0.7, zz + 0.4), (0.45, zz + 0.9), (0.05, zz + 1.3)], sides=10)
    geo.tube("om_orange", [(*(p + f * 0.5), zz + 0.6), (*(p + f * 1.1), zz + 0.45)], [0.18, 0.02], sides=6)


def sunshade(geo, p, z, yaw, r):
    """A beach umbrella over two loungers, and a surfboard stuck in the sand."""
    f, s = frame(yaw)
    geo.tube("om_white", [(p[0], p[1], z - 0.5), (p[0], p[1], z + 3.3)], 0.07, sides=6)
    c = ("om_red", "om_teal", "om_yellow")[int(r.integers(3))]
    geo.lathe(c, (p[0], p[1]), [(2.45, z + 2.5), (2.1, z + 2.85), (1.3, z + 3.18), (0.5, z + 3.36), (0.03, z + 3.42)],
              sides=16, wobble=0.03, seed=int(r.integers(1 << 20)))
    geo.lathe("om_white", (p[0], p[1]), [(0.1, z + 3.35), (0.12, z + 3.5), (0.03, z + 3.6)], sides=6)
    for t in (-1, 1):                      # two loungers: a curved bed on a frame
        q = p + s * 1.45 * t - f * 0.6
        rows = []
        for i, u in enumerate(np.linspace(-1.0, 1.0, 9)):
            back = max(0.0, u - 0.35) / 0.65
            zz = z + 0.45 + back * back * 0.75
            o = q + f * (u * 1.0 - back * 0.25)
            rows.append([(*(o + s * v), zz) for v in (-0.38, 0.0, 0.38)])
        geo.surface("om_white", np.array(rows), np.array([[(v, u) for v in (0, 0.5, 1)] for u in np.linspace(0, 3, 9)]))
        for v in (-0.36, 0.36):
            geo.tube("om_planks_light", [r_[:2] + (r_[2] - 0.06,) for r_ in [tuple(rows[i][1 if v == 0 else (0 if v < 0 else 2)]) for i in range(9)]], 0.05, sides=5)
            for u in (-0.9, 0.3):
                leg = q + f * u + s * v
                geo.tube("om_planks_light", [(leg[0], leg[1], z), (leg[0], leg[1], z + 0.42)], 0.045, sides=5)
    board = p + f * 2.5
    out = np.array([*(f), 0.0])
    pts = []
    for a in np.linspace(0, 2 * np.pi, 14, endpoint=False):
        pts.append((board[0] + s[0] * math.cos(a) * 0.32, board[1] + s[1] * math.cos(a) * 0.32,
                    z + 1.0 + math.sin(a) * 1.35))
    geo.slab(("om_orange", "om_teal")[int(r.integers(2))], pts, out, 0.1)


def signpost(geo, p, z, yaw, r):
    """A post with three arrow boards, each a little askew."""
    geo.tube("om_planks_dark", [(p[0], p[1], z - 0.4), (p[0], p[1], z + 3.1)], [0.16, 0.13], sides=7)
    geo.lathe("om_planks_dark", (p[0], p[1]), [(0.14, z + 3.1), (0.18, z + 3.2), (0.02, z + 3.35)], sides=7)
    for k in range(3):
        a = yaw + k * 110 + float(r.uniform(-15, 15))
        f, s = frame(a)
        zz = z + 2.6 - k * 0.55
        tilt = float(r.uniform(-0.06, 0.06))
        out = [(*(p + f * 0.15), zz - 0.2 + tilt), (*(p + f * 1.5), zz - 0.2 - tilt), (*(p + f * 1.9), zz),
               (*(p + f * 1.5), zz + 0.2 - tilt), (*(p + f * 0.15), zz + 0.2 + tilt)]
        geo.slab(("om_planks", "om_planks_light")[k % 2], out, (s[0], s[1], 0), 0.08)


def fence(geo, p, z, yaw, r):
    """A rustic fence: round posts, sagging rails, Pokitaru's flowers along it."""
    f, s = frame(yaw)
    pts = [p + f * (k - 2) * 2.4 + s * float(r.uniform(-0.3, 0.3)) for k in range(5)]
    gs = [float(island.height(*q)) for q in pts]
    for q, g in zip(pts, gs):
        geo.tube("om_planks_dark", [(q[0], q[1], g - 0.3), (q[0], q[1], g + 1.3)], [0.13, 0.11], sides=7)
        geo.lathe("om_planks_dark", (q[0], q[1]), [(0.11, g + 1.3), (0.06, g + 1.4), (0.01, g + 1.43)], sides=7)
    for (a, ga), (b, gb) in zip(zip(pts, gs), zip(pts[1:], gs[1:])):
        for h in (1.1, 0.6):
            mid = (a + b) / 2
            geo.tube("om_planks", [(*a, ga + h), (*mid, (ga + gb) / 2 + h - 0.08), (*b, gb + h)], 0.07, sides=6)
    flowers(geo, pts[0] + s * 0.7, pts[-1] + s * 0.7, z, r, 1.6)


def flower_bed(geo, p, z, yaw, r):
    """A ring of stones round Pokitaru's flowers and a flower patch."""
    f, s = frame(yaw)
    geo.prop("shrub", 49, (p[0], p[1], float(island.height(*p))), float(r.uniform(0, 360)), 0.55)
    for k in range(6):
        q = p + f * float(r.uniform(-2.2, 2.2)) + s * float(r.uniform(-2.2, 2.2))
        geo.prop("shrub", FLOWERS[int(r.integers(len(FLOWERS)))], (q[0], q[1], float(island.height(*q))),
                 float(r.uniform(0, 360)), float(r.uniform(0.9, 1.4)))
    for k in range(11):
        a = 2 * math.pi * k / 11 + float(r.uniform(-0.1, 0.1))
        q = p + np.array([math.cos(a), math.sin(a)]) * 3.3
        geo.stone("om_stone", (q[0], q[1], float(island.height(*q))), float(r.uniform(0.35, 0.5)), int(r.integers(1 << 20)))


def driftwood(geo, p, z, yaw, r):
    """A bleached, bent trunk with a broken branch, and Pokitaru's starfish beside it."""
    f, s = frame(yaw)
    bend = s * float(r.uniform(-0.6, 0.6))
    pts = [(*(p + f * t * 2.8 + bend * (1 - t * t)), z + 0.25 + 0.1 * t) for t in np.linspace(-1, 1, 6)]
    geo.tube("om_planks_dark", pts, [0.32, 0.36, 0.34, 0.3, 0.26, 0.2], sides=8)
    root = p - f * 2.8 + bend * 0
    for k in range(4):
        a = 2 * math.pi * k / 4 + float(r.uniform(-0.4, 0.4))
        d = np.array([math.cos(a), math.sin(a)])
        geo.tube("om_planks_dark", [(*root, z + 0.3), (*(root - f * 0.4 + d * 0.5), z + 0.3 + d[1] * 0.4),
                                    (*(root - f * 0.7 + d * 0.9), z + 0.2 + d[1] * 0.6)], [0.16, 0.1, 0.03], sides=5)
    geo.tube("om_planks_dark", [(*(p + f * 1.0), z + 0.4), (*(p + f * 1.6 + s * 0.8), z + 0.75), (*(p + f * 2.0 + s * 1.3), z + 0.8)],
             [0.14, 0.1, 0.06], sides=6)
    for k in range(int(r.integers(1, 3))):
        q = p + s * float(r.uniform(1.5, 3)) + f * float(r.uniform(-2, 2))
        geo.prop("shrub", (373, 371)[k % 2], (q[0], q[1], float(island.height(*q))), float(r.uniform(0, 360)), 1.0)


def barrels(geo, p, z, yaw, r):
    """Bulging barrels with iron hoops, one stacked, and one of the game's bolt crates."""
    f, s = frame(yaw)
    for k, (a, b) in enumerate(((0, 0), (1.25, 0.2), (0.6, 1.15), (0.6, 0.45))):
        q = p + f * a + s * b
        zz = z + (1.12 if k == 3 else 0)
        geo.lathe("om_planks", (q[0], q[1]), [(0.001, zz), (0.46, zz), (0.56, zz + 0.3), (0.6, zz + 0.55), (0.56, zz + 0.8),
                                              (0.46, zz + 1.1), (0.001, zz + 1.1)], sides=12, uv_scale=1.2)
        for h in (0.18, 0.92):
            rr = 0.51 + 0.09 * math.sin(math.pi * h / 1.1)
            geo.lathe("om_planks_dark", (q[0], q[1]), [(rr + 0.02, zz + h - 0.05), (rr + 0.03, zz + h + 0.05)], sides=12)
    q = p - f * 1.8
    geo.prop("bolt_crate", 0, (q[0], q[1], z), yaw, 1.0)


def hammock(geo, p, z, yaw, r):
    """A striped hammock slung on ropes between two posts with leafy Pokitaru plants at their feet."""
    f, s = frame(yaw)
    a, b = p - f * 3.0, p + f * 3.0
    for q in (a, b):
        geo.tube("om_planks_dark", [(q[0], q[1], z - 0.3), (*(q + s * 0.05), z + 1.3), (q[0], q[1], z + 2.5)], [0.2, 0.18, 0.15], sides=7)
        geo.lathe("om_planks_dark", (q[0], q[1]), [(0.15, z + 2.5), (0.08, z + 2.6), (0.01, z + 2.63)], sides=7)
        geo.prop("shrub", 307, (q[0], q[1], float(island.height(*q))), float(r.uniform(0, 360)), 1.4)
    col = ("om_red", "om_teal", "om_yellow")[int(r.integers(3))]
    end_a, end_b = a + f * 0.9, b - f * 0.9
    rows = []
    for t in np.linspace(0, 1, 12):
        c = end_a + (end_b - end_a) * t
        sag = math.sin(math.pi * t)
        w = 0.25 + 0.5 * sag
        rows.append([(*(c + s * v * w), z + 1.85 - 0.85 * sag + 0.12 * v * v * sag) for v in np.linspace(-1, 1, 5)])
    geo.surface(col, np.array(rows), np.array([[(v, t * 3) for v in np.linspace(0, 1, 5)] for t in np.linspace(0, 1, 12)]))
    for q, e in ((a, end_a), (b, end_b)):
        for v in (-1, 1):
            geo.tube("om_white", [(q[0], q[1], z + 2.2), (*(e + s * v * 0.25), z + 1.85)], 0.025, sides=4, caps=False)


def stone_lantern(geo, p, z, yaw, r):
    """One of Pokitaru's own lamps, in a ring of stones."""
    geo.prop("lamp", 0, (p[0], p[1], z), yaw, 1.0)
    for k in range(5):
        a = 2 * math.pi * k / 5 + float(r.uniform(-0.2, 0.2))
        q = p + np.array([math.cos(a), math.sin(a)]) * 1.1
        geo.stone("om_stone", (q[0], q[1], float(island.height(*q))), float(r.uniform(0.25, 0.4)), int(r.integers(1 << 20)))


def tide_pool(geo, p, z, yaw, r):
    """A rock pool: rounded stones round still water, Pokitaru's coral and starfish in it."""
    for k in range(12):
        a = 2 * math.pi * k / 12 + float(r.uniform(-0.15, 0.15))
        d = float(r.uniform(2.0, 2.7))
        q = p + np.array([math.cos(a), math.sin(a)]) * d
        geo.stone("om_stone", (q[0], q[1], float(island.height(*q)) - 0.1), float(r.uniform(0.45, 0.8)), int(r.integers(1 << 20)))
    geo.lathe("om_water", (p[0], p[1]), [(0.01, z + 0.06), (2.2, z + 0.06), (2.2, z - 0.1)], sides=18)
    geo.prop("tie", 696, (p[0] + 0.6, p[1] - 0.4, z - 0.2), float(r.uniform(0, 360)), 0.12)
    geo.prop("shrub", 373, (p[0] - 0.8, p[1] + 0.5, z + 0.05), float(r.uniform(0, 360)), 1.0)
    geo.prop("shrub", 371, (p[0] + 0.2, p[1] + 1.0, z + 0.05), float(r.uniform(0, 360)), 0.6)


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
            geo.stone("om_stone", (q[0], q[1], g - 0.25), w, 7000 + k, flat=0.45)
