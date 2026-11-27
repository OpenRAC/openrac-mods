"""The museum's light for the mobys and the hero, from the baked light of the ground and the house.

The game lights a moby from one of the level's light sets (two directional lights each) plus its
own ambient colour; the hero takes both from the level's light zones as he walks (the first zone
whose circle holds him, if he is in its box too). Here:

  - SETS: the light sets. Set 0 is the sun the bake used, so the hero's light falls the way the
    shadows on the ground do; set 1 stays Pokitaru's; sets 2 to 4 light the house, the tower and
    the dark corners outside.
  - zones(): the house (its atrium, its rooms, the tower, the doors blending in and out), the
    darkest corners of the island the hero can reach, and the open island last. Each zone's
    ambient colour is measured from the bake: how much light its floor got against the sunny
    open ground.
  - moby_light(): the set and colour of a placed moby, by the zone it stands in.

Colours are the game's bytes (128 is full light); a set's colours and directions are floats, a
direction the way its light travels.
"""

import math

import numpy as np

import fx
import house
import island

# The hero in the open sun: Pokitaru's own moby colour (79, 78, 51) with set 0.
OPEN = np.array([79.0, 78.0, 51.0])
SETS_HOUSE, SET_TOWER, SET_SHADE = 2, 3, 4


def unit(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


def light_set(ca, da, cb, db, back=-0.17):
    return [*map(float, ca), back, *map(float, unit(da)), 0.0, *map(float, cb), 0.0, *map(float, unit(db)), 0.0]


def sets(level):
    """The light sets: set 0 the bake's sun, 1 Pokitaru's own, then the house's, the tower's and
    the shade's, each as bright as LEVEL (zone kind -> its share of the open light) says."""
    down = fx.sun_down()
    up_from_ground = unit([-down[0], -down[1], 0.45])
    out = [light_set([0.98, 0.92, 0.7], down, [0.22, 0.27, 0.33], up_from_ground),
           [0.227, 0.259, 0.259, 0.0, 0.643, 0.0, -0.766, 0.0, 0.745, 0.714, 0.78, 0.0, -0.866, -0.0, -0.5, 0.0]]
    flat = unit([down[0], down[1], -0.25])          # window light: low, from the sunny side
    for kind in ("house", "tower", "shade"):
        k = level[kind]
        if kind == "shade":
            out.append(light_set(np.array([0.9, 0.86, 0.75]) * k, [0, 0, -1], np.array([0.3, 0.34, 0.4]) * k,
                                 up_from_ground))
        else:
            out.append(light_set(np.array([1.0, 0.85, 0.62]) * k, flat, np.array([0.35, 0.33, 0.32]) * k,
                                 [0, 0, -1]))
    return out


def baked_light(prims):
    """Points of the baked surfaces that face up, and the light each got (its colour over the
    colour it had before the bake): (P, light rgb)."""
    P, L = [], []
    for p, before in prims:
        n = p.attrs["NORMAL"]
        up = n[:, 2] > 0.7
        if getattr(p, "keep", None) is not None:
            up &= ~p.keep
        c = p.attrs["COLOR_0"][:, :3][up]
        P.append(p.pos[up])
        L.append(c / np.clip(before[up], 0.05, 2))
    return np.concatenate(P), np.concatenate(L)


def mean_light(P, L, inside):
    m = inside(P)
    if m.sum() < 8:
        return None
    return np.median(L[m], axis=0)


def octagon_box(apothem, z0, z1, yaw=22.5):
    """A zone's box round the house's middle: APOTHEM across, Z0 to Z1, its x axis up (the
    colour blends from the bottom to the top)."""
    a = math.radians(yaw)
    zc, hz = (z0 + z1) / 2, (z1 - z0) / 2
    return {"centre": [house.CX, house.CY, zc], "x": [0, 0, hz],
            "y": [math.cos(a) * apothem, math.sin(a) * apothem, 0], "z": [-math.sin(a) * apothem, math.cos(a) * apothem, 0],
            "reach": apothem}


def zones(P, L, spots=()):
    """The light zones, in the order the game must test them, each {centre, x, y, z (the box's
    half axes), reach, colour_a, colour_b, set_a, set_b, name}, and the sets' levels."""
    cx, cy = house.CX, house.CY
    sunny = (P[:, 0] - cx) ** 2 + (P[:, 1] - cy) ** 2 > 60 ** 2
    open_light = np.percentile(L[sunny].sum(1), 90) / 3

    def rho_of(inside, fallback):
        got = mean_light(P, L, inside)
        if got is None:
            return fallback, np.ones(3)
        level = float(got.mean() / open_light)
        return min(max(level, 0.08), 1.0), got / max(got.mean(), 1e-6)

    def colour(level, tint):
        # A zone whose floor got LEVEL of the open ground's light gives the hero LEVEL of his open
        # ambient (and its set LEVEL of the sun), tinted as its light is.
        c = OPEN * level * np.clip(tint, 0.7, 1.3)
        return [int(v) for v in np.clip(np.round(c), 4, 255)] + [128]

    def ring(z0, z1):
        def f(Q):
            r = np.hypot(Q[:, 0] - cx, Q[:, 1] - cy)
            return (r > house.R_IN + 1) & (r < house.R_OUT - 1) & (Q[:, 2] > z0) & (Q[:, 2] < z1)
        return f

    def atrium(Q):
        return (np.hypot(Q[:, 0] - cx, Q[:, 1] - cy) < house.R_IN - 1) & (Q[:, 2] > house.F0 - 0.5) & (Q[:, 2] < house.F0 + 2)

    tx, ty = house.TOWER

    def tower(Q):
        return (np.abs(Q[:, 0] - tx) < house.TOWER_HALF) & (np.abs(Q[:, 1] - ty) < house.TOWER_HALF) & \
            (Q[:, 2] > house.TOWER_TOP - 0.5) & (Q[:, 2] < house.TOWER_TOP + 2)

    low, low_tint = rho_of(ring(house.F0 - 0.5, house.F0 + 2), 0.35)
    high, high_tint = rho_of(ring(house.F1 - 0.5, house.F1 + 2), 0.4)
    mid, mid_tint = rho_of(atrium, 0.6)
    top, top_tint = rho_of(tower, 0.35)
    level = {"house": (low + high) / 2, "tower": top}
    out = []
    # The tower's room: a tall box from the ground floor to its roof, so its circle (which the
    # rooms below share) lights them as the rooms' zone does at the bottom.
    hz = (house.TOWER_TOP + 6 - (house.F0 - 1)) / 2
    out.append({"name": "Tower", "centre": [tx, ty, house.F0 - 1 + hz], "x": [0, 0, hz],
                "y": [house.TOWER_HALF + 1, 0, 0], "z": [0, house.TOWER_HALF + 1, 0], "reach": house.TOWER_HALF + 1,
                "colour_a": colour(low, low_tint), "colour_b": colour(top, top_tint),
                "set_a": SETS_HOUSE, "set_b": SET_TOWER})
    # The doors: from the room (inside) to the open (outside) across the doorway.
    for s in house.DOORS:
        a = np.array([*house.ang(45 * s), 0.0])
        side = np.array([-a[1], a[0], 0.0])
        c = np.array([cx, cy, house.F0 + 3.5]) + a * house.R_OUT
        out.append({"name": f"Door_{s}", "centre": list(c), "x": list(a * 5.0), "y": list(side * 5.0),
                    "z": [0, 0, 5.0], "reach": 5.0, "colour_a": colour(low, low_tint), "colour_b": [int(v) for v in OPEN] + [128],
                    "set_a": SETS_HOUSE, "set_b": 0})
    out.append({"name": "Atrium", **octagon_box(house.R_IN - 0.5, house.F0 - 1, house.TOP + 30),
                "colour_a": colour(mid, mid_tint), "colour_b": colour(mid, mid_tint), "set_a": SETS_HOUSE, "set_b": SETS_HOUSE})
    out.append({"name": "Rooms", **octagon_box(house.R_OUT, house.F0 - 1, house.TOP + 2),
                "colour_a": colour(low, low_tint), "colour_b": colour(high, high_tint), "set_a": SETS_HOUSE, "set_b": SETS_HOUSE})
    # The darkest corners outside, as many as the game holds: the worst first.
    shade, level["shade"] = dark_corners(P, L, open_light, 32 - len(out) - 1, colour, spots)
    out += shade
    # The open island: Pokitaru's colour and the sun.
    out.append({"name": "Island", "centre": [cx, cy, island.SEA + 100], "x": [700, 0, 0], "y": [0, 700, 0],
                "z": [0, 0, 160], "reach": 700.0, "colour_a": [int(v) for v in OPEN] + [128],
                "colour_b": [int(v) for v in OPEN] + [128], "set_a": 0, "set_b": 0})
    return out, level


def dark_corners(P, L, open_light, most, colour, spots=(), cell=3.0, darker=0.6):
    """Circles over the walkable ground outside the house that the bake left darker than DARKER
    of the open light (the foot of the cliffs, under the rock, the shade of the trees), merged
    into at most MOST zones, the biggest dark patches first. A dark cell near one of SPOTS
    (x, y: the small scenes, where the player stops) counts four times."""
    cx, cy = house.CX, house.CY
    away = np.hypot(P[:, 0] - cx, P[:, 1] - cy) > house.R_DECK + 4
    Q, M = P[away], L[away].mean(1)
    ij = np.floor(Q[:, :2] / cell).astype(np.int64)
    key = ij[:, 0] * 100000 + ij[:, 1]
    # Each cell's floors one by one (the ground under an overhang apart from the rock's top):
    # points of one cell within 2 units of height of each other make one floor.
    order = np.lexsort((Q[:, 2], key))
    key, Q, M = key[order], Q[order], M[order]
    new_floor = np.r_[True, (key[1:] != key[:-1]) | (np.diff(Q[:, 2]) > 2.0)]
    floor = np.cumsum(new_floor)
    cells = {}
    for k, q, m in zip(floor, Q, M):
        cells.setdefault(k, []).append((q, m))
    dark = []
    for k, items in cells.items():
        z = max(q[2] for q, _ in items)
        if z < island.SEA + 0.5:
            continue                         # under the water
        level = float(np.median([m for _, m in items]) / open_light)
        if level < darker:
            q = np.mean([q for q, _ in items], axis=0)
            dark.append((q, level))
    if not dark:
        return [], 0.4
    spots = np.asarray(spots, np.float64).reshape(-1, 2)
    weight = np.array([4.0 if len(spots) and np.hypot(*(spots - q[:2]).T).min() < 10 else 1.0 for q, _ in dark])
    zones_out, levels = [], []
    left = list(dark)
    while left and len(zones_out) < most:
        pts = np.array([q[:2] for q, _ in left])
        hs = np.array([q[2] for q, _ in left])
        # The circle of radius up to 18 that holds the most dark cells.
        best, best_n, best_r = 0, 0, 0
        for i in range(len(left)):
            # One floor's height round the middle: a dark ledge high over it is another patch.
            d = np.where(np.abs(hs - hs[i]) < 6.0, np.hypot(*(pts - pts[i]).T), np.inf)
            for r in (6.0, 10.0, 14.0, 18.0):
                n = float(weight[d < r].sum())
                if n * (6.0 / r) > best_n:
                    best, best_n, best_r = i, n * (6.0 / r), r
        d = np.where(np.abs(hs - hs[best]) < 6.0, np.hypot(*(pts - pts[best]).T), np.inf)
        taken = d < best_r
        if taken.sum() < 3:
            break
        group = [left[j] for j in np.nonzero(taken)[0]]
        c = np.mean([q for q, _ in group], axis=0)
        z0, z1 = min(q[2] for q, _ in group), max(q[2] for q, _ in group)
        # From a little under the lowest floor to the hero's height over the highest: no higher,
        # so standing on the rock over a lit overhang is not in its shade.
        c[2] = (z0 + z1) / 2 + 1.5
        level = float(np.mean([v for _, v in group]))
        zones_out.append({"name": f"Shade_{len(zones_out):02}", "centre": [float(c[0]), float(c[1]), float(c[2])],
                          "x": [best_r, 0, 0], "y": [0, best_r, 0], "z": [0, 0, float((z1 - z0) / 2 + 4.5)], "reach": best_r,
                          "colour_a": colour(level, np.ones(3)), "colour_b": colour(level, np.ones(3)),
                          "set_a": SET_SHADE, "set_b": SET_SHADE})
        levels.append(level)
        left = [left[j] for j in np.nonzero(~taken)[0]]
        weight = weight[~taken]
    return zones_out, float(np.mean(levels)) if levels else 0.4


def inside_zone(z, p):
    """Whether point P is in zone Z as the game tests it (its circle, then its box): and where
    along its x axis (0 at A, 1 at B)."""
    c = np.array(z["centre"])
    if (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 >= z["reach"] ** 2:
        return None
    B = np.array([z["x"], z["y"], z["z"]]).T
    local = np.linalg.solve(B, np.asarray(p, float) - c)
    if np.any(np.abs(local) > 1):
        return False
    return 0.5 * (local[0] + 1)


def moby_light(zones_list, p):
    """The light set and colour (bytes) of a moby standing at P: those of the zone the game would
    light the hero from there; None outside every zone (the moby keeps its own)."""
    for z in zones_list:
        t = inside_zone(z, p)
        if t is None:
            continue
        if t is False:
            return None
        a, b = np.array(z["colour_a"][:3], float), np.array(z["colour_b"][:3], float)
        rgb = [int(round(v)) for v in a + (b - a) * t]
        return (z["set_a"] if t < 0.5 else z["set_b"]), rgb
    return None
