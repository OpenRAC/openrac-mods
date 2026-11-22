"""The island's terrain (drawn) and its ground collision, from island.height."""

import math

import numpy as np

import island
from glb import Mesh

SUN = np.array([0.45, -0.35, 0.82])
SUN = SUN / np.linalg.norm(SUN)


def shade(normals, ao=None, warm=1.0):
    """Vertex colours at the port's half strength (0.5 is the game's normal brightness)."""
    lit = np.clip(normals @ SUN, 0, 1)
    light = 0.62 + 0.55 * lit
    if ao is not None:
        light = light * ao
    c = np.stack([light * warm, light, light * (2 - warm)], -1) * 0.5
    return np.clip(c, 0, 1)


# Pokitaru's rock pictures are dark: their ground is lit up to twice the normal (vertex colour 1.0).
BRIGHT = {"om_cliff": 2.9, "om_jungle": 2.0, "om_rock": 2.4, "om_basalt": 2.8, "om_wet_sand": 2.2,
          "om_grass": 1.35, "om_sand": 1.15}
WARM = np.array([1.0, 1.0, 0.78])   # Pokitaru's ground light is warm: less blue (its colours reach 2.0)


def grid(x0, y0, x1, y1, step):
    xs = np.arange(x0, x1 + step / 2, step)
    ys = np.arange(y0, y1 + step / 2, step)
    X, Y = np.meshgrid(xs, ys, indexing="xy")
    return X, Y


def material_of(z, n, x, y):
    """Pokitaru's look: grass on every flat ledge, layered rock with grass hanging over it on the
    risers, creepers here and there, sand and wet rock by the sea."""
    slope = 1 - n[..., 2]
    dv = np.hypot(x - island.VOLCANO[0], y - island.VOLCANO[1])
    creep = island.fbm(x, y, 40, 2, 3)
    m = np.full(z.shape, "om_grass", dtype=object)
    m[z < island.SEA + 4.5] = "om_sand"
    steep = (slope > 0.22) & (z > island.SEA + 2)
    m[steep] = "om_cliff"
    m[steep & (creep > 0.5) & (z < island.SEA + 120)] = "om_jungle"
    m[steep & (z < island.SEA + 7)] = "om_wet_sand"
    m[(z > island.SEA + 150) & (slope > 0.12)] = "om_rock"
    m[(dv < 70) & (z > island.SEA + 70) & (slope > 0.12)] = "om_basalt"
    m[(dv < 16) & (z < island.VOLCANO_TOP - 8)] = "om_lava"
    return m


def heightfield_mesh(x0, y0, x1, y1, step, keep=None, uv_scale=8.0):
    X, Y = grid(x0, y0, x1, y1, step)
    Z = island.height(X, Y)
    N = island.normal(X, Y)
    # Ambient occlusion: lower than the blurred ground round it is darker.
    blur = island.height(X + 6, Y) + island.height(X - 6, Y) + island.height(X, Y + 6) + island.height(X, Y - 6)
    ao = np.clip(1 - np.maximum(blur / 4 - Z, 0) * 0.04, 0.6, 1)
    h, w = X.shape
    idx = np.arange(h * w).reshape(h, w)
    a, b, c, d = idx[:-1, :-1], idx[:-1, 1:], idx[1:, :-1], idx[1:, 1:]
    tris = np.concatenate([np.stack([a, b, d], -1).reshape(-1, 3), np.stack([a, d, c], -1).reshape(-1, 3)])
    P = np.stack([X, Y, Z], -1).reshape(-1, 3)
    Nf = N.reshape(-1, 3)
    centre = P[tris].mean(1)
    cn = Nf[tris].mean(1)
    if keep is not None:
        tris = tris[keep(centre)]
        centre = P[tris].mean(1); cn = Nf[tris].mean(1)
    mats = material_of(centre[:, 2], cn, centre[:, 0], centre[:, 1])
    col = shade(Nf, ao.reshape(-1))
    mesh = Mesh()
    for name in sorted(set(mats)):
        t = tris[mats == name]
        used, inverse = np.unique(t.reshape(-1), return_inverse=True)
        p = P[used]; n = Nf[used]
        steep = n[:, 2] < 0.75
        # Walls: the picture runs along the slope, its top up (the hanging grass at each ledge).
        side = np.where(np.abs(n[:, 0]) > np.abs(n[:, 1]), p[:, 1], p[:, 0])
        tall = 10.0 if name in ("om_cliff", "om_jungle", "om_wet_sand") else uv_scale
        uv = np.where(steep[:, None], np.stack([side / tall, -p[:, 2] / tall], -1), p[:, :2] / uv_scale)
        cc = np.clip(col[used] * BRIGHT.get(name, 1.0) * WARM, 0, 2)
        if name == "om_lava":
            cc = np.full_like(cc, 0.9)
        mesh.add(name, p, n, uv, cc, inverse.reshape(-1, 3))
    return mesh, P, tris


def island_terrain():
    """The island, its islets and the sea floor near it."""
    def keep(c):
        r = np.hypot(c[:, 0] - island.CENTRE[0], c[:, 1] - island.CENTRE[1])
        return (c[:, 2] > island.SEA - 14) | (r < 420)
    mesh, _, _ = heightfield_mesh(-120, -120, 1144, 1144, 4, keep)
    return mesh


def sea(radius=6000, rings=24, segments=64, z=island.SEA - 0.4, uv=24.0):
    """The sea: a flat disc round the island, its colour fading to the horizon."""
    pts, cols, faces = [[512, 512, z]], [[0.5, 0.5, 0.55]], []
    rs = [radius * (i / rings) ** 2 for i in range(1, rings + 1)]
    for ri, r in enumerate(rs):
        for s in range(segments):
            a = 2 * math.pi * s / segments
            pts.append([512 + math.cos(a) * r, 512 + math.sin(a) * r, z])
            fade = 0.5 + 0.1 * (ri / rings)
            cols.append([fade * 0.95, fade, fade * 1.05])
    for s in range(segments):
        faces.append([0, 1 + s, 1 + (s + 1) % segments])
    for ri in range(rings - 1):
        o0, o1 = 1 + ri * segments, 1 + (ri + 1) * segments
        for s in range(segments):
            s1 = (s + 1) % segments
            faces.append([o0 + s, o1 + s, o1 + s1])
            faces.append([o0 + s, o1 + s1, o0 + s1])
    p = np.array(pts, np.float32)
    mesh = Mesh()
    mesh.add("om_water", p, np.tile([0, 0, 1], (len(p), 1)), p[:, :2] / uv, cols, faces)
    return mesh


# Islands on the horizon: (angle, distance, radius, height).
FAR = [(15, 1700, 380, 330), (60, 2300, 520, 520), (100, 1900, 300, 260), (150, 2600, 650, 620),
       (205, 2100, 420, 300), (250, 2900, 700, 480), (300, 1800, 260, 180), (335, 2400, 500, 410)]


def far_islands():
    """Islands on the horizon in Pokitaru's layers: rock walls, a grass top on every tier."""
    mesh = Mesh()
    rng = np.random.default_rng(7)
    for k, (ang, dist, rad, hgt) in enumerate(FAR):
        cx = 512 + math.cos(math.radians(ang)) * dist
        cy = 512 + math.sin(math.radians(ang)) * dist
        n_grid = 44
        step = rad * 2.4 / n_grid
        xs = cx - rad * 1.2 + np.arange(n_grid + 1) * step
        ys = cy - rad * 1.2 + np.arange(n_grid + 1) * step
        X, Y = np.meshgrid(xs, ys, indexing="xy")
        # A few peaks, each a stack of tiers.
        peaks = [(cx, cy, rad, hgt)] + [(cx + rng.uniform(-0.5, 0.5) * rad, cy + rng.uniform(-0.5, 0.5) * rad,
                                          rad * rng.uniform(0.3, 0.55), hgt * rng.uniform(0.5, 1.25)) for _ in range(3)]
        Z = np.full(X.shape, island.SEA - 40.0)
        for (px, py, pr, ph) in peaks:
            d = np.hypot(X - px, Y - py) + (island.fbm(X, Y, pr * 0.6, 3, k) - 0.5) * pr * 0.5
            t = island.smooth(pr, pr * 0.15, d)
            tiers = 5
            q = t ** 0.8 * tiers
            stairs = (np.floor(q) + island.smooth(0.55, 0.85, q - np.floor(q))) / tiers
            Z = np.maximum(Z, island.SEA - 40 + (ph + 40) * stairs)
        hx = np.gradient(Z, step, axis=1); hy = np.gradient(Z, step, axis=0)
        N = np.stack([-hx, -hy, np.ones_like(hx)], -1)
        N /= np.linalg.norm(N, axis=-1, keepdims=True)
        h, w = X.shape
        idx = np.arange(h * w).reshape(h, w)
        a, b, c, d = idx[:-1, :-1], idx[:-1, 1:], idx[1:, :-1], idx[1:, 1:]
        tris = np.concatenate([np.stack([a, b, d], -1).reshape(-1, 3), np.stack([a, d, c], -1).reshape(-1, 3)])
        P = np.stack([X, Y, Z], -1).reshape(-1, 3)
        Nf = N.reshape(-1, 3)
        tris = tris[P[tris][:, :, 2].max(1) > island.SEA - 2]
        cn = Nf[tris].mean(1)
        cz = P[tris][:, :, 2].mean(1)
        col = shade(Nf) * 0.8 + 0.12  # hazy
        names = np.where(cn[:, 2] > 0.8, "om_grass", "om_cliff")
        names[(cz < island.SEA + 6) & (cn[:, 2] > 0.8)] = "om_sand"
        for name in ("om_grass", "om_cliff", "om_sand"):
            t = tris[names == name]
            if not len(t):
                continue
            used, inv = np.unique(t.reshape(-1), return_inverse=True)
            p = P[used]
            if name == "om_cliff":
                uv = np.stack([(p[:, 0] + p[:, 1]) / 40, -p[:, 2] / (hgt / 5)], -1)
            else:
                uv = p[:, :2] / 40
            mesh.add(name, p, Nf[used], uv, np.clip(col[used] * BRIGHT.get(name, 1.0) * WARM, 0, 2), inv.reshape(-1, 3))
    return mesh


def climbed(c):
    """Where gadgets take the hero above the fine ground: the lookout's slope up to the volcano's
    ledge, and the bottle's islet. (All the mountains would not fit the level's memory.)"""
    look = np.array(island.LOOKOUT)
    ledge = np.array(island.VOLCANO_LEDGE)
    d = ledge - look
    t = np.clip(((c[:, :2] - look) @ d) / (d @ d), 0, 1)
    near_line = np.hypot(*(c[:, :2] - (look + t[:, None] * d)).T) < 18
    # The grind rail's first stretch, over the mountain's foot.
    rail = np.array([island.CENTRE[0] - 30, island.CENTRE[1] + 105]) - look
    t = np.clip(((c[:, :2] - look) @ rail) / (rail @ rail), 0, 1)
    near_line |= np.hypot(*(c[:, :2] - (look + t[:, None] * rail)).T) < 14
    near_line |= np.hypot(c[:, 0] - look[0], c[:, 1] - look[1]) < 34   # round the lookout and its stair
    ix, iy, ir, _ = island.ISLETS[3]
    return near_line | (np.hypot(c[:, 0] - ix, c[:, 1] - iy) < ir + 4)


def collision_mesh():
    """The ground the hero walks on: the island within reach, a ground surface (0x1f)."""
    def keep(c):
        r = np.hypot(c[:, 0] - island.CENTRE[0], c[:, 1] - island.CENTRE[1])
        ix, iy = island.ISLETS[3][:2]                     # the bottle's islet, reached by swingshot
        near = np.hypot(c[:, 0] - ix, c[:, 1] - iy) < island.ISLETS[3][2] + 4
        return ((r < 290) | near) & (c[:, 0] > 2) & (c[:, 1] > 2) & (c[:, 0] < 1022) & (c[:, 1] < 1022)
    X, Y = grid(island.CENTRE[0] - 404, island.CENTRE[1] - 404, min(island.CENTRE[0] + 404, 1020), island.CENTRE[1] + 404, 4)
    Z = island.height(X, Y)
    h, w = X.shape
    idx = np.arange(h * w).reshape(h, w)
    a, b, c, d = idx[:-1, :-1], idx[:-1, 1:], idx[1:, :-1], idx[1:, 1:]
    tris = np.concatenate([np.stack([a, b, d], -1).reshape(-1, 3), np.stack([a, d, c], -1).reshape(-1, 3)])
    P = np.stack([X, Y, Z], -1).reshape(-1, 3)
    tris = tris[keep(P[tris].mean(1))]
    # Only where the hero can be: from the lagoon's floor up the slopes he can climb. The sea
    # floor and the high peaks have none, which keeps the level's collision about Pokitaru's size
    # (it shares the console's 32 MB of memory with the rest of the level).
    z = P[tris][:, :, 2]
    c = P[tris].mean(1)
    lagoon = np.hypot(c[:, 0] - island.CENTRE[0], c[:, 1] - island.CENTRE[1]) < island.LAGOON_R + 24
    tris = tris[((z.max(1) > island.SEA - 3) | lagoon) & (z.min(1) < island.SEA + 30)]
    # The mountains and the volcano on a coarser grid (8 units), so the hero who climbs them with
    # gadgets still stands on them; it overlaps the fine grid's top by a few units.
    X2, Y2 = grid(island.CENTRE[0] - 404, island.CENTRE[1] - 120, min(island.CENTRE[0] + 300, 1020), island.CENTRE[1] + 400, 4)
    Z2 = island.height(X2, Y2)
    h2, w2 = X2.shape
    idx2 = np.arange(h2 * w2).reshape(h2, w2) + len(P)
    a, b, c, d = idx2[:-1, :-1], idx2[:-1, 1:], idx2[1:, :-1], idx2[1:, 1:]
    tris2 = np.concatenate([np.stack([a, b, d], -1).reshape(-1, 3), np.stack([a, d, c], -1).reshape(-1, 3)])
    P = np.concatenate([P, np.stack([X2, Y2, Z2], -1).reshape(-1, 3)])
    z2 = P[tris2][:, :, 2]
    c2 = P[tris2].mean(1)
    tris2 = tris2[keep(c2) & (z2.max(1) > island.SEA + 26) & climbed(c2)]
    return P, np.concatenate([tris, tris2])
