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


SAND_TOP = island.SEA + 4.5


def material_of(z, n, x, y):
    """Pokitaru's look: grass on every flat ledge, layered rock with grass hanging over it on the
    risers, creepers here and there, sand and wet rock by the sea."""
    slope = 1 - n[..., 2]
    dv = np.hypot(x - island.VOLCANO[0], y - island.VOLCANO[1])
    creep = island.fbm(x, y, 40, 2, 3)
    m = np.full(z.shape, "om_grass", dtype=object)
    m[z < SAND_TOP] = "om_sand"
    steep = (slope > 0.22) & (z > island.SEA + 2)
    m[steep] = "om_cliff"
    m[steep & (creep > 0.5) & (z < island.SEA + 120)] = "om_jungle"
    m[steep & (z < island.SEA + 7)] = "om_wet_sand"
    m[(z > island.SEA + 150) & (slope > 0.12)] = "om_rock"
    m[(dv < 70) & (z > island.SEA + 70) & (slope > 0.12)] = "om_basalt"
    m[(dv < 16) & (z < island.VOLCANO_TOP - 8)] = "om_lava"
    return m


def split_cells(X, Y, P, Nf, col, vmat):
    """The grid's triangles. A cell whose corners share a material is two triangles; a cell where
    they differ is a fan round its centre through the middles of its mixed edges, each corner
    keeping the part nearest it, so edges between materials run smooth instead of in teeth.
    Returns the points, normals, colours (new ones appended) and the triangles with their materials."""
    h, w = X.shape
    idx = np.arange(h * w).reshape(h, w)
    a, b, c, d = (q.reshape(-1) for q in (idx[:-1, :-1], idx[:-1, 1:], idx[1:, :-1], idx[1:, 1:]))
    same = (vmat[a] == vmat[b]) & (vmat[a] == vmat[c]) & (vmat[a] == vmat[d])
    tris = [np.stack([a[same], b[same], d[same]], -1), np.stack([a[same], d[same], c[same]], -1)]
    mats = [vmat[a[same]], vmat[a[same]]]
    pts, nrm, cls = [P], [Nf], [col]
    count = [len(P)]
    mids = {}

    def new(p, n, k):
        pts.append(p[None]); nrm.append(n[None]); cls.append(k[None])
        count[0] += 1
        return count[0] - 1

    def mid(i, j):
        key = (min(i, j), max(i, j))
        if key not in mids:
            t = 0.5
            # The shore line: where the ground crosses the sand's height, not the edge's middle.
            if {vmat[i], vmat[j]} == {"om_sand", "om_grass"} and abs(P[j, 2] - P[i, 2]) > 1e-4:
                t = float(np.clip((SAND_TOP - P[i, 2]) / (P[j, 2] - P[i, 2]), 0.15, 0.85))
            n = Nf[i] * (1 - t) + Nf[j] * t
            mids[key] = new(P[i] * (1 - t) + P[j] * t, n / np.linalg.norm(n), col[i] * (1 - t) + col[j] * t)
        return mids[key]

    count0 = len(P)

    def point(m):
        return P[m] if m < count0 else pts[m - count0 + 1][0]

    def colour(m):
        return col[m] if m < count0 else cls[m - count0 + 1][0]

    ft, fm = [], []
    for i, j, l, k in zip(a[~same], b[~same], d[~same], c[~same]):
        ring = (i, j, l, k)          # round the cell
        edge = {q: mid(ring[q], ring[(q + 1) % 4]) for q in range(4) if vmat[ring[q]] != vmat[ring[(q + 1) % 4]]}
        # The centre on the line between the edges' crossings, so that line runs straight.
        at = list(edge.values()) if len(edge) == 2 else list(ring)
        n = sum(nrm[m - count0 + 1][0] if m >= count0 else Nf[m] for m in at)
        centre = new(np.mean([point(m) for m in at], 0), n / np.linalg.norm(n),
                     np.mean([colour(m) for m in at], 0))
        for q in range(4):
            p0, p1 = ring[q], ring[(q + 1) % 4]
            if q not in edge:
                ft.append((p0, p1, centre)); fm.append(vmat[p0])
            else:
                m = edge[q]
                ft += [(p0, m, centre), (m, p1, centre)]
                fm += [vmat[p0], vmat[p1]]
    if ft:
        tris.append(np.array(ft)); mats.append(np.array(fm, dtype=object))
    return (np.concatenate(pts), np.concatenate(nrm), np.concatenate(cls),
            np.concatenate(tris), np.concatenate(mats))


# How big each ground picture lies (units a repeat): sand large, so its pattern does not show from far.
UV_SCALE = {"om_sand": 7.0}


def heightfield_mesh(x0, y0, x1, y1, step, keep=None, uv_scale=3.5):
    X, Y = grid(x0, y0, x1, y1, step)
    H = island.base_height
    Z = H(X, Y)
    N = island.normal(X, Y, fn=H)
    # Ambient occlusion: lower than the blurred ground round it is darker.
    blur = H(X + 6, Y) + H(X - 6, Y) + H(X, Y + 6) + H(X, Y - 6)
    ao = np.clip(1 - np.maximum(blur / 4 - Z, 0) * 0.04, 0.6, 1)
    P0 = np.stack([X, Y, Z], -1).reshape(-1, 3)
    N0 = N.reshape(-1, 3)
    vmat = material_of(P0[:, 2], N0, P0[:, 0], P0[:, 1])
    P, Nf, col, tris, mats = split_cells(X, Y, P0, N0, shade(N0, ao.reshape(-1)), vmat)
    if keep is not None:
        k = keep(P[tris].mean(1))
        tris, mats = tris[k], mats[k]
    # Broad light and dark patches over the flat ground, so its repeating pictures do not line up.
    patch = 0.86 + 0.28 * island.fbm(P[:, 0], P[:, 1], 55, 3, 11)
    mesh = Mesh()
    for name in sorted(set(mats)):
        t = tris[mats == name]
        used, inverse = np.unique(t.reshape(-1), return_inverse=True)
        p = P[used]; n = Nf[used]
        steep = n[:, 2] < 0.75
        # Walls: the picture runs along the slope, its top up (the hanging grass at each ledge).
        side = np.where(np.abs(n[:, 0]) > np.abs(n[:, 1]), p[:, 1], p[:, 0])
        tall = 4.0 if name in ("om_cliff", "om_jungle", "om_wet_sand") else uv_scale
        flat = UV_SCALE.get(name, uv_scale)
        uv = np.where(steep[:, None], np.stack([side / tall, -p[:, 2] / tall], -1), p[:, :2] / flat)
        cc = col[used] * BRIGHT.get(name, 1.0) * WARM
        if name in ("om_sand", "om_grass"):
            cc = cc * patch[used, None]
        cc = np.clip(cc, 0, 2)
        if name == "om_lava":
            cc = np.full_like(cc, 0.9)
        mesh.add(name, p, n, uv, cc, inverse.reshape(-1, 3))
    return mesh, P, tris


def inside_rock(c, inside):
    """Whether each point C (N, 3) is a unit or more inside the rock (INSIDE: the rock's density)."""
    if inside is None:
        return np.zeros(len(c), bool)
    return inside(c + np.array([0, 0, 1.0])) > 0.5


def island_terrain(inside=None):
    """The low ground and the sea floor near it; none where the rock stands on it."""
    def keep(c):
        r = np.hypot(c[:, 0] - island.CENTRE[0], c[:, 1] - island.CENTRE[1])
        k = (c[:, 2] > island.SEA - 14) | (r < 420)
        k[k] = ~inside_rock(c[k], inside)
        return k
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


def collision_mesh(inside=None):
    """The low ground the hero walks on (surface 0x1f): from the lagoon's floor up, none under the
    sea or inside the rock (the rock has its own collision, make_museum.rock_collision)."""
    X, Y = grid(island.CENTRE[0] - 404, island.CENTRE[1] - 404, min(island.CENTRE[0] + 404, 1020), island.CENTRE[1] + 404, 4)
    Z = island.base_height(X, Y)
    h, w = X.shape
    idx = np.arange(h * w).reshape(h, w)
    a, b, c, d = idx[:-1, :-1], idx[:-1, 1:], idx[1:, :-1], idx[1:, 1:]
    tris = np.concatenate([np.stack([a, b, d], -1).reshape(-1, 3), np.stack([a, d, c], -1).reshape(-1, 3)])
    P = np.stack([X, Y, Z], -1).reshape(-1, 3)
    c = P[tris].mean(1)
    z = P[tris][:, :, 2]
    lagoon = np.hypot(c[:, 0] - island.CENTRE[0], c[:, 1] - island.CENTRE[1]) < island.LAGOON_R + 24
    inworld = (c[:, 0] > 2) & (c[:, 1] > 2) & (c[:, 0] < 1022) & (c[:, 1] < 1022)
    tris = tris[((z.max(1) > island.SEA - 3) | lagoon) & inworld]
    c = P[tris].mean(1)
    tris = tris[~inside_rock(c, inside)]
    return P, tris
