"""The island's rock: Pokitaru-style stacks and mesas with overhangs, built as one mesh by the
editor's terrain tools (openrac/tools/terrain_tools.py: a density field of rocks, surface nets).

The low ground (the lagoon, the beaches, the lowland round the house) stays a height field
(island.base_height); everything tall is rock from this recipe. island.height reads the rock's
top from the built mesh (island.set_rock_top), so placements stand on the rock.
"""

import math

import numpy as np

import island

SEA = island.SEA
C = island.CENTRE
FOOT = SEA - 16.0


def at(angle, r):
    return (float(C[0] + math.cos(math.radians(angle)) * r), float(C[1] + math.sin(math.radians(angle)) * r))


def rp(angle, r):
    """A point at ANGLE about the centre, R away as first drawn (island.pr draws it in)."""
    return at(angle, island.pr(r))


def rock(x, y, top, radius, seed, **kw):
    r = {"x": float(x), "y": float(y), "z0": kw.pop("z0", FOOT), "z1": SEA + top, "radius": float(radius),
         "undercut": 0.32, "bulge": 0.16, "taper": 0.12, "roughness": 0.22, "layering": 0.02,
         "layer": 7.0, "warp": 0.32, "seed": seed}
    r.update(kw)
    return r


def recipe():
    rocks = []
    seed = [100]

    def add(x, y, top, radius, **kw):
        seed[0] += 1
        rocks.append(rock(x, y, top, radius, seed[0], **kw))

    # The two waterfalls' cliffs: their faces stand over the pools, their tops at each fall's lip.
    f1, f2 = island.FALLS
    add(*at(f1["angle"], f1["r"] + 42), f1["lip"] - SEA, 40, stretch=[1.25, 0.9], yaw=f1["angle"] + 90, undercut=0.38, bulge=0.22)
    add(*at(f2["angle"], f2["r"] + 40), f2["lip"] - SEA, 38, stretch=[1.2, 0.9], yaw=f2["angle"] + 90, undercut=0.36, bulge=0.2)
    # The massif behind them, in tiers that climb north to the volcano.
    add(*rp(92, 222), 92, 34, undercut=0.3)
    add(*rp(52, 250), 104, 36, taper=0.18)
    add(*rp(30, 236), 70, 30, undercut=0.4, bulge=0.25)
    add(*rp(10, 225), 48, 24, undercut=0.42)
    add(*rp(140, 236), 64, 28, undercut=0.35)
    add(*rp(78, 300), 132, 40, taper=0.2)
    add(*rp(40, 320), 86, 32)

    # The volcano: a tall tapering spire, a crater at its top, a pillar for the shrine's ledge.
    vx, vy = island.VOLCANO
    add(vx, vy, island.VOLCANO_TOP - SEA, 46, taper=0.58, undercut=0.22, bulge=0.12, roughness=0.22, layer=13.0, layering=0.025, warp=0.22)
    lx, ly = island.VOLCANO_LEDGE
    d = np.array([lx - vx, ly - vy]) / 60.0
    add(lx + d[0] * 2, ly + d[1] * 2, island.LEDGE_Z - SEA - 0.5, 15, taper=0.05, undercut=0.25, roughness=0.15, warp=0.2)
    add(vx + d[0] * 36, vy + d[1] * 36, island.LEDGE_Z - SEA - 30, 24, taper=0.25)

    # The lookout's stack (its platform's legs stand on it) and the stair's knoll.
    lx, ly = island.LOOKOUT
    add(lx - 2, ly + 6, island.LOOKOUT_Z - SEA - 8, 15, undercut=0.4, bulge=0.28, taper=0.05)
    add(*rp(172, 228), 30, 22, undercut=0.42)
    add(*rp(200, 230), 26, 24, undercut=0.45, bulge=0.25)

    # The coast: mesas with sea chasms between them, so the island has no round edge.
    for (ang, r, top, rad) in [(-150, 210, 22, 26), (-122, 222, 16, 22), (-70, 200, 28, 26), (-48, 226, 40, 24),
                               (-25, 228, 22, 20), (-8, 240, 44, 26), (18, 205, 34, 20), (-170, 205, 14, 18),
                               (-105, 250, 30, 16), (-60, 268, 58, 18), (195, 300, 46, 20), (-140, 280, 52, 18)]:
        # Round the ship's pad they stay where they were, out of its way.
        add(*(at(ang, r) if -175 <= ((ang + 180) % 360) - 180 <= -100 else rp(ang, r)), top, rad,
            undercut=0.38, bulge=0.12, roughness=0.32, taper=0.18)

    # Boulders on the low ground, out of the paths.
    for (x, y, top, rad) in [(735, 385, 13, 11), (520, 335, 15, 12), (790, 565, 16, 12),
                             (560, 450, 9, 7), (700, 470, 8, 6)]:
        add(*island.pull(x, y), top, rad, undercut=0.4, bulge=0.1, roughness=0.35, layering=0.05, layer=4.0, taper=0.2)

    # Islets and sea stacks round the island, and the broad islands of the turbine town.
    for (x, y, rad, top) in island.ISLETS:
        add(x, y, top, rad, undercut=0.4, bulge=0.25, taper=0.05)
    for (x, y, rad, top) in island.STACKS:
        add(x, y, top * 1.6, rad * 1.2, undercut=0.45, bulge=0.3, taper=0.1)
    for (x, y, rad, top) in island.TOWN:
        add(x, y, top, rad * 0.8, undercut=0.35, bulge=0.18, stretch=[1.25, 0.85], yaw=x * 0.37)

    caves = []
    # The grotto behind the second waterfall: its hall, through the rock.
    g = island.GROTTO
    for r in np.linspace(g["r0"] - 4, g["r1"] + 1, 6):
        x, y = at(g["angle"], r)
        caves.append({"at": [x, y, SEA + 6.5], "radius": 8.5, "scale": [1, 1, 0.75]})
    # The crater.
    caves.append({"at": [float(vx), float(vy), island.VOLCANO_TOP + 6], "radius": 16, "scale": [1, 1, 0.8]})
    return {"rocks": rocks, "caves": caves, "blend": 5.0, "sea": SEA, "keep_below_sea": 5.0, "tile": 3.0, "lip": 4.0,
            "top_slope": 0.6, "creepers": 0.3, "min_top_area": 4.5, "lip_top_area": 6.0, "smooth_passes": 3, "tiles": {"wall": 5.0, "under": 5.0, "creeper": 4.0}, "uv": "wrap",
            "textures": {"top": "textures/om_grass.png", "lip": "textures/om_cliff.png", "wall": "textures/om_rock.png",
                         "creeper": "textures/om_jungle.png", "under": "textures/om_basalt.png",
                         "shore": "textures/om_wet_sand.png"}}


def top_raster(V, F, cell=0.5, lo=-150.0, size=2800):
    """The rock's highest upward surface over each CELL's middle (-inf where there is no rock):
    each upward triangle is read exactly at the cell middles it covers, so things set on it
    neither float nor sink."""
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    nz = fn[:, 2] / np.maximum(np.linalg.norm(fn, axis=1), 1e-9)
    T = V[F[nz > 0.15]].astype(np.float64)
    grid = np.full((size, size), -np.inf, np.float32)
    i0 = np.floor((T[:, :, 0].min(1) - lo) / cell - 0.5).astype(int)
    i1 = np.ceil((T[:, :, 0].max(1) - lo) / cell - 0.5).astype(int)
    j0 = np.floor((T[:, :, 1].min(1) - lo) / cell - 0.5).astype(int)
    j1 = np.ceil((T[:, :, 1].max(1) - lo) / cell - 0.5).astype(int)
    span = np.maximum(i1 - i0, j1 - j0) + 1
    for n in np.unique(span):
        sel = np.nonzero(span == n)[0]
        di, dj = np.meshgrid(np.arange(n), np.arange(n), indexing="ij")
        di, dj = di.ravel(), dj.ravel()
        for c in range(0, len(sel), max(1, 400000 // (n * n))):
            s = sel[c:c + max(1, 400000 // (n * n))]
            I = i0[s, None] + di[None]; J = j0[s, None] + dj[None]
            px = lo + (I + 0.5) * cell; py = lo + (J + 0.5) * cell
            a, b, cc = T[s, 0], T[s, 1], T[s, 2]
            v0x = b[:, 0] - a[:, 0]; v0y = b[:, 1] - a[:, 1]
            v1x = cc[:, 0] - a[:, 0]; v1y = cc[:, 1] - a[:, 1]
            den = v0x * v1y - v1x * v0y
            den = np.where(np.abs(den) < 1e-12, 1e-12, den)
            wx = px - a[:, 0, None]; wy = py - a[:, 1, None]
            u = (wx * v1y[:, None] - v1x[:, None] * wy) / den[:, None]
            v = (v0x[:, None] * wy - wx * v0y[:, None]) / den[:, None]
            ok = (u >= -1e-6) & (v >= -1e-6) & (u + v <= 1 + 1e-6) & (I >= 0) & (J >= 0) & (I < size) & (J < size)
            z = a[:, 2, None] + u * (b[:, 2] - a[:, 2])[:, None] + v * (cc[:, 2] - a[:, 2])[:, None]
            np.maximum.at(grid, (I[ok], J[ok]), z[ok].astype(np.float32))
    return {"grid": grid, "lo": lo, "cell": cell}
