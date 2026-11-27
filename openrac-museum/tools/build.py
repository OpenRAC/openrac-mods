"""Shapes for the museum: boxes, slabs and quads, drawn and solid.

Each shape goes into the drawn mesh with a material, and, when solid, into the collision as
ground (0x1f). Coordinates are the game's (z up)."""

import math

import numpy as np

from glb import Mesh
from ground import SUN

FACES = [  # normal, corner order (indices into the 8 corners: bit0 x, bit1 y, bit2 z)
    ((1, 0, 0), (1, 3, 7, 5)), ((-1, 0, 0), (2, 0, 4, 6)),
    ((0, 1, 0), (3, 2, 6, 7)), ((0, -1, 0), (0, 1, 5, 4)),
    ((0, 0, 1), (4, 5, 7, 6)), ((0, 0, -1), (2, 3, 1, 0)),
]


def light(n, base=1.0):
    n = np.asarray(n, np.float64)
    n = n / (np.linalg.norm(n) or 1)
    v = (0.62 + 0.6 * max(0.0, float(n @ SUN))) * base * 0.62
    return [min(v * 1.06, 2), min(v, 2), min(v * 0.86, 2)]


class Geo:
    def __init__(self):
        self.mesh = Mesh()
        self.solid = []        # triangles [(p0, p1, p2), surface]
        self.count = 0
        self.lights = []       # point lights for the bake: {"at", "colour", "radius"}
        self.flames = []       # where the mod's live effects play: (kind, x, y, z)
        self.props = []        # Pokitaru's own models to place: (kind, number, x, y, z, yaw, scale)

    def flame(self, kind, at):
        """A place for the mod's effects: "torch" (embers), "fire" (embers and smoke), "mist"."""
        self.flames.append((kind, *(round(float(v), 2) for v in at)))

    def light(self, at, colour=(1.5, 1.0, 0.5), radius=9.0, size=0.5):
        """A point light for the bake; SIZE the ball it shines from (soft shadows)."""
        self.lights.append({"at": [float(v) for v in at], "colour": list(colour), "radius": float(radius),
                            "size": float(size)})

    # --- the drawn and solid shapes ---
    def tri_quad(self, material, corners, normal, uv, solid=True, surface=0x1F, glow=None, both=False):
        corners = np.asarray(corners, np.float32)
        col = [glow] * 4 if glow else [light(normal)] * 4
        self.mesh.add(material, corners, [normal] * 4, uv, col, [[0, 1, 2], [0, 2, 3]])
        if both:
            back = [-x for x in normal]
            # The underside a little apart: two faces in one place fight for the pixels, and once
            # the light is baked (lit top, dark underside) that shows as blotches.
            under = corners[::-1] + np.asarray(back, np.float32) * 0.1
            self.mesh.add(material, under, [back] * 4, uv[::-1], [light(back)] * 4,
                          [[0, 1, 2], [0, 2, 3]])
        if solid:
            self.solid.append(((corners[0], corners[1], corners[2]), surface))
            self.solid.append(((corners[0], corners[2], corners[3]), surface))
        self.count += 1

    def box(self, material, lo, hi, solid=True, uv_scale=4.0, skip=(), surface=0x1F, glow=None):
        lo, hi = np.asarray(lo, np.float64), np.asarray(hi, np.float64)
        if np.any(hi - lo <= 1e-4):
            return
        corner = [np.array([hi[0] if i & 1 else lo[0], hi[1] if i & 2 else lo[1], hi[2] if i & 4 else lo[2]])
                  for i in range(8)]
        for normal, order in FACES:
            if normal in skip:
                continue
            ps = [corner[i] for i in order]
            # UVs from the face's two long directions.
            axis = [k for k in range(3) if normal[k] == 0]
            uv = [[p[axis[0]] / uv_scale, -p[axis[1]] / uv_scale] for p in ps] if axis[1] == 2 else \
                 [[p[axis[0]] / uv_scale, p[axis[1]] / uv_scale] for p in ps]
            self.tri_quad(material, ps, list(map(float, normal)), np.array(uv, np.float32), solid, surface, glow)

    def block(self, lo, hi, surface=0x1F):
        """A box in the collision only: a round shape the hero bumps into like a box."""
        corner = [np.array([hi[0] if i & 1 else lo[0], hi[1] if i & 2 else lo[1], hi[2] if i & 4 else lo[2]], np.float32)
                  for i in range(8)]
        for normal, order in FACES:
            c = [corner[i] for i in order]
            self.solid.append(((c[0], c[1], c[2]), surface))
            self.solid.append(((c[0], c[2], c[3]), surface))

    def beam(self, material, a, b, size, solid=False, uv_scale=4.0, glow=None):
        """A square beam from A to B (any direction), SIZE thick."""
        a, b = np.asarray(a, np.float64), np.asarray(b, np.float64)
        d = b - a
        length = np.linalg.norm(d)
        if length < 1e-4:
            return
        f = d / length
        up = np.array([0, 0, 1.0]) if abs(f[2]) < 0.95 else np.array([1.0, 0, 0])
        s = np.cross(f, up); s /= np.linalg.norm(s)
        u = np.cross(s, f)
        h = size / 2
        ring = [(-h, -h), (h, -h), (h, h), (-h, h)]
        for k in range(4):
            (s0, u0), (s1, u1) = ring[k], ring[(k + 1) % 4]
            p0 = a + s * s0 + u * u0; p1 = a + s * s1 + u * u1
            p2 = b + s * s1 + u * u1; p3 = b + s * s0 + u * u0
            n = s * (s0 + s1) + u * (u0 + u1)
            uv = np.array([[0, 0], [size / uv_scale, 0], [size / uv_scale, length / uv_scale], [0, length / uv_scale]],
                          np.float32)
            self.tri_quad(material, [p0, p1, p2, p3], list(n / np.linalg.norm(n)), uv, solid, glow=glow)
        # Ends.
        for end, sign in ((a, -1), (b, 1)):
            ps = [end + s * s0 + u * u0 for s0, u0 in ring]
            if sign < 0:
                ps = ps[::-1]
            self.tri_quad(material, ps, list(f * sign), np.zeros((4, 2), np.float32), solid, glow=glow)

    def slope(self, material, x0, x1, y0, y1, z0, z1, axis="y", thick=0.5, uv_scale=4.0, solid=True):
        """A ramp or roof plane between two heights along AXIS, with a thickness."""
        if axis == "y":
            top = [[x0, y0, z0], [x1, y0, z0], [x1, y1, z1], [x0, y1, z1]]
        else:
            top = [[x0, y0, z0], [x1, y0, z1], [x1, y1, z1], [x0, y1, z0]]
        top = np.array(top, np.float64)
        n = np.cross(top[1] - top[0], top[3] - top[0]); n /= np.linalg.norm(n)
        if n[2] < 0:
            top = top[[0, 3, 2, 1]]; n = -n
        uv = np.array([[p[0] / uv_scale, p[1] / uv_scale] for p in top], np.float32)
        self.tri_quad(material, top, list(n), uv, solid)
        bottom = top - np.array([0, 0, thick])
        self.tri_quad(material, bottom[::-1], list(-n), uv[::-1], False)

    def wall(self, material, a, b, z0, z1, thick, holes=(), uv_scale=4.0, solid=True):
        """A straight wall from A to B (x, y), Z0 to Z1, with HOLES: (from, to, bottom, top) along it."""
        a, b = np.asarray(a, np.float64), np.asarray(b, np.float64)
        length = np.linalg.norm(b - a)
        d = (b - a) / length
        n = np.array([-d[1], d[0]])
        cuts = sorted(holes)
        spans, pos = [], 0.0
        for (h0, h1, hb, ht) in cuts:
            if h0 > pos:
                spans.append((pos, h0, z0, z1))
            spans.append((h0, h1, z0, z0 + hb))
            spans.append((h0, h1, z0 + ht, z1))
            pos = h1
        if pos < length:
            spans.append((pos, length, z0, z1))
        for (s0, s1, zb, zt) in spans:
            if zt - zb < 1e-3 or s1 - s0 < 1e-3:
                continue
            p0 = a + d * s0; p1 = a + d * s1
            self.oriented_box(material, p0, p1, n, thick, zb, zt, uv_scale, solid)

    def oriented_box(self, material, p0, p1, n, thick, zb, zt, uv_scale=4.0, solid=True):
        h = thick / 2
        c = [p0 - n * h, p1 - n * h, p1 + n * h, p0 + n * h]
        lo = [np.array([q[0], q[1], zb]) for q in c]
        hi = [np.array([q[0], q[1], zt]) for q in c]
        d = p1 - p0
        dl = np.linalg.norm(d)
        dn = d / dl if dl else d
        sides = [(lo[0], lo[1], hi[1], hi[0], -n), (lo[2], lo[3], hi[3], hi[2], n),
                 (lo[1], lo[2], hi[2], hi[1], dn), (lo[3], lo[0], hi[0], hi[3], -dn)]
        for (q0, q1, q2, q3, nn) in sides:
            nn3 = [float(nn[0]), float(nn[1]), 0.0]
            run0 = q0[0] * dn[0] + q0[1] * dn[1] if abs(nn3[0] * dn[0] + nn3[1] * dn[1]) < 0.5 else q0[0] * n[0] + q0[1] * n[1]
            run1 = q1[0] * dn[0] + q1[1] * dn[1] if abs(nn3[0] * dn[0] + nn3[1] * dn[1]) < 0.5 else q1[0] * n[0] + q1[1] * n[1]
            uv = np.array([[run0 / uv_scale, -zb / uv_scale], [run1 / uv_scale, -zb / uv_scale],
                           [run1 / uv_scale, -zt / uv_scale], [run0 / uv_scale, -zt / uv_scale]], np.float32)
            self.tri_quad(material, [q0, q1, q2, q3], nn3, uv, solid)
        top = [hi[0], hi[1], hi[2], hi[3]]
        uv = np.array([[q[0] / uv_scale, q[1] / uv_scale] for q in top], np.float32)
        if np.cross(top[1] - top[0], top[3] - top[0])[2] < 0:
            top = top[::-1]; uv = uv[::-1]
        self.tri_quad(material, top, [0.0, 0.0, 1.0], uv, solid)

    def cylinder(self, material, centre, radius, z0, z1, sides=12, solid=False, uv_scale=4.0, cap=True):
        cx, cy = centre
        for k in range(sides):
            a0 = 2 * math.pi * k / sides; a1 = 2 * math.pi * (k + 1) / sides
            p = [[cx + math.cos(a0) * radius, cy + math.sin(a0) * radius, z0],
                 [cx + math.cos(a1) * radius, cy + math.sin(a1) * radius, z0],
                 [cx + math.cos(a1) * radius, cy + math.sin(a1) * radius, z1],
                 [cx + math.cos(a0) * radius, cy + math.sin(a0) * radius, z1]]
            am = (a0 + a1) / 2
            arc = 2 * math.pi * radius / sides / uv_scale
            uv = np.array([[k * arc, -z0 / uv_scale], [(k + 1) * arc, -z0 / uv_scale],
                           [(k + 1) * arc, -z1 / uv_scale], [k * arc, -z1 / uv_scale]], np.float32)
            self.tri_quad(material, p, [math.cos(am), math.sin(am), 0], uv, solid)
            if cap:
                top = [[cx, cy, z1], [cx, cy, z1], p[3], p[2]]
                self.mesh.add(material, np.array([[cx, cy, z1], p[2], p[3]], np.float32), [[0, 0, 1]] * 3,
                              np.array([[cx / uv_scale, cy / uv_scale], [p[2][0] / uv_scale, p[2][1] / uv_scale],
                                        [p[3][0] / uv_scale, p[3][1] / uv_scale]], np.float32),
                              [light([0, 0, 1])] * 3, [[0, 1, 2]])
                if solid:
                    self.solid.append(((np.array([cx, cy, z1]), np.array(p[2]), np.array(p[3])), 0x1F))

    def cone(self, material, centre, radius, z0, z1, sides=12, uv_scale=4.0, glow=None):
        cx, cy = centre
        for k in range(sides):
            a0 = 2 * math.pi * k / sides; a1 = 2 * math.pi * (k + 1) / sides
            p = np.array([[cx + math.cos(a0) * radius, cy + math.sin(a0) * radius, z0],
                          [cx + math.cos(a1) * radius, cy + math.sin(a1) * radius, z0],
                          [cx, cy, z1]], np.float32)
            n = np.cross(p[1] - p[0], p[2] - p[0]); n /= np.linalg.norm(n)
            uv = np.array([[k / sides * 4, 0], [(k + 1) / sides * 4, 0], [(k + 0.5) / sides * 4, 2]], np.float32)
            self.mesh.add(material, p, [n] * 3, uv, [glow or light(n)] * 3, [[0, 1, 2]])

    # --- smooth shapes: a grid of points, smooth normals ---
    def surface(self, material, P, uv, wrap=False, glow=None, flip=False):
        """A smooth sheet through the grid P (rows x columns x 3), UV (rows x columns x 2). WRAP: the
        columns close round. Normals are the grid's own, so curves light smoothly."""
        P = np.asarray(P, np.float64)
        rows, cols = P.shape[:2]
        du = np.roll(P, -1, 1) - np.roll(P, 1, 1) if wrap else np.gradient(P, axis=1)
        dv = np.gradient(P, axis=0)
        N = np.cross(du, dv)
        if flip:
            N = -N
        N /= np.maximum(np.linalg.norm(N, axis=-1, keepdims=True), 1e-9)
        if wrap:   # the seam: one more column, the first again, its own UVs
            P = np.concatenate([P, P[:, :1]], 1); N = np.concatenate([N, N[:, :1]], 1)
            uv = np.concatenate([uv, uv[:, :1] + np.array([1.0, 0.0]) * (uv[0, 1, 0] - uv[0, 0, 0]) * cols], 1)
            cols += 1
        idx = np.arange(rows * cols).reshape(rows, cols)
        a, b, c, d = idx[:-1, :-1], idx[:-1, 1:], idx[1:, :-1], idx[1:, 1:]
        tris = np.concatenate([np.stack([a, b, d], -1).reshape(-1, 3), np.stack([a, d, c], -1).reshape(-1, 3)])
        if flip:
            tris = tris[:, ::-1]
        Nf = N.reshape(-1, 3)
        col = [glow] * len(Nf) if glow else [light(n) for n in Nf]
        self.mesh.add(material, P.reshape(-1, 3).astype(np.float32), Nf.astype(np.float32),
                      np.asarray(uv, np.float32).reshape(-1, 2), col, tris)

    def lathe(self, material, centre, profile, sides=12, uv_scale=2.0, glow=None, wobble=0.0, seed=0):
        """A turned shape round CENTRE (x, y): PROFILE is (radius, z) from the bottom up. WOBBLE
        roughens the radius (a carved or hewn look)."""
        rng = np.random.default_rng(seed)
        a = np.linspace(0, 2 * np.pi, sides, endpoint=False)
        rough = 1 + rng.uniform(-wobble, wobble, (len(profile), sides))
        P = np.array([[[centre[0] + math.cos(t) * r * rough[i, j], centre[1] + math.sin(t) * r * rough[i, j], z]
                       for j, t in enumerate(a)] for i, (r, z) in enumerate(profile)])
        arc = 2 * np.pi * max(r for r, _ in profile) / uv_scale
        uv = np.array([[[j / sides * arc, -z / uv_scale] for j in range(sides)] for _, z in profile])
        self.surface(material, P, uv, wrap=True, glow=glow, flip=True)

    def tube(self, material, path, radius, sides=8, uv_scale=2.0, glow=None, caps=True):
        """A round tube along PATH (points), RADIUS one number or one per point: logs, branches,
        ropes, posts."""
        path = np.asarray(path, np.float64)
        radii = np.broadcast_to(np.asarray(radius, np.float64), (len(path),))
        t = np.gradient(path, axis=0)
        t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-9)
        up = np.array([0, 0, 1.0]) if abs(t[0][2]) < 0.9 else np.array([1.0, 0, 0])
        rings, n0 = [], None
        for i, (p, d) in enumerate(zip(path, t)):
            n = np.cross(d, up if n0 is None else n0)
            n /= np.linalg.norm(n) or 1
            n0 = np.cross(n, d)
            a = np.linspace(0, 2 * np.pi, sides, endpoint=False)
            rings.append([p + (np.cos(x) * n + np.sin(x) * n0) * radii[i] for x in a])
        P = np.array(rings)
        length = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))])
        uv = np.array([[[j / sides * 2 * np.pi * radii.max() / uv_scale, -length[i] / uv_scale] for j in range(sides)]
                       for i in range(len(path))])
        self.surface(material, P, uv, wrap=True, glow=glow)
        if caps:
            for i, d in ((0, -t[0]), (-1, t[-1])):
                ring = P[i]
                c = ring.mean(0)
                for j in range(sides):
                    tri = np.array([c, ring[j], ring[(j + 1) % sides]], np.float32)
                    if np.cross(tri[1] - tri[0], tri[2] - tri[0]) @ d < 0:
                        tri = tri[[0, 2, 1]]
                    self.mesh.add(material, tri, [d] * 3, (tri[:, :2] / uv_scale).astype(np.float32),
                                  [glow or light(d)] * 3, [[0, 1, 2]])

    def stone(self, material, at, size, seed, flat=0.6):
        """A rounded, uneven stone sitting on AT (x, y, ground z): a lathe roughened by SEED."""
        x, y, z = at
        h = size * flat
        prof = [(size * 0.75, z - h * 0.3), (size, z + h * 0.15), (size * 0.85, z + h * 0.6), (size * 0.45, z + h * 0.92),
                (size * 0.05, z + h)]
        self.lathe(material, (x, y), prof, sides=10, uv_scale=1.5, wobble=0.18, seed=seed)

    def crystals(self, material, at, height, radius, seed, glow=None, count=4):
        """A cluster of six-sided crystals leaning out of AT (x, y, z), the tallest HEIGHT."""
        rng = np.random.default_rng(seed)
        x, y, z = at
        for k in range(count):
            h = height * (1.0 if k == 0 else rng.uniform(0.4, 0.75))
            r = radius * (1.0 if k == 0 else rng.uniform(0.45, 0.7))
            a = rng.uniform(0, 2 * np.pi)
            lean = 0.0 if k == 0 else rng.uniform(0.35, 0.7)
            d = np.array([math.cos(a) * lean, math.sin(a) * lean, 1.0]); d /= np.linalg.norm(d)
            base = np.array([x + math.cos(a) * radius * 0.6 * (k > 0), y + math.sin(a) * radius * 0.6 * (k > 0), z - 0.2])
            self.tube(material, [base, base + d * h * 0.75, base + d * h], [r, r, 0.02], sides=6, glow=glow)

    def paper_lantern(self, centre, z0, z1, radius, glow=(1.8, 1.4, 0.7)):
        """A round paper lantern from Z0 to Z1 with dark caps, glowing."""
        x, y = centre
        h = z1 - z0
        body = [(radius * s, z0 + h * f) for f, s in ((0.08, 0.55), (0.25, 0.9), (0.5, 1.0), (0.75, 0.9), (0.92, 0.55))]
        self.lathe("om_red", (x, y), body, 12, glow=list(glow))
        for zc, zz in ((z0, z0 + h * 0.08), (z1 - h * 0.08, z1)):
            self.cylinder("om_planks_dark", (x, y), radius * 0.58, zc, zz, 12)

    def slab(self, material, outline, normal, thick, uv_scale=2.0, glow=None):
        """A flat plate of THICK through the convex OUTLINE (points round it, in its plane)."""
        o = np.asarray(outline, np.float64)
        n = np.asarray(normal, np.float64); n /= np.linalg.norm(n)
        front, back = o + n * thick / 2, o - n * thick / 2
        c = o.mean(0)
        for pts, nn in ((front, n), (back[::-1], -n)):
            cc = c + nn * thick / 2
            for k in range(len(pts)):
                tri = np.array([cc, pts[k], pts[(k + 1) % len(pts)]], np.float32)
                if np.cross(tri[1] - tri[0], tri[2] - tri[0]) @ nn < 0:
                    tri = tri[[0, 2, 1]]
                uvv = (tri[:, :2] + tri[:, 2:3]) / uv_scale
                self.mesh.add(material, tri, [nn] * 3, uvv.astype(np.float32), [glow or light(nn)] * 3, [[0, 1, 2]])
        for k in range(len(o)):
            a, b = k, (k + 1) % len(o)
            q = [front[a], front[b], back[b], back[a]]
            sn = np.cross(q[1] - q[0], q[3] - q[0])
            if sn @ ((q[0] + q[1]) / 2 - c) < 0:
                q = q[::-1]; sn = -sn
            l = np.linalg.norm(q[1] - q[0]) / uv_scale
            self.tri_quad(material, q, list(sn / (np.linalg.norm(sn) or 1)),
                          np.array([[0, 0], [l, 0], [l, thick / uv_scale], [0, thick / uv_scale]], np.float32), False, glow=glow)

    def prop(self, kind, number, at, yaw=0.0, scale=1.0):
        """One of Pokitaru's own models placed here: kind "tie", "shrub" or a moby kind
        (museum.TEMPLATES); museum.py places it."""
        self.props.append((kind, number, *(round(float(v), 3) for v in at), round(float(yaw), 2), round(float(scale), 3)))

    def sign(self, material, centre, facing_deg, width, height, solid=False):
        """A flat picture (a sign, a poster) facing FACING_DEG, its middle at CENTRE."""
        f = math.radians(facing_deg)
        n = np.array([math.cos(f), math.sin(f), 0.0])
        s = np.array([-n[1], n[0], 0.0])  # the picture's right, seen from the front
        c = np.asarray(centre, np.float64) + n * 0.06
        w, h = width / 2, height / 2
        ps = [c - s * w - [0, 0, h], c + s * w - [0, 0, h], c + s * w + [0, 0, h], c - s * w + [0, 0, h]]
        uv = np.array([[0, 1], [1, 1], [1, 0], [0, 0]], np.float32)
        self.tri_quad(material, ps, list(n), uv, solid, glow=[0.55, 0.55, 0.55])

    def collision_triangles(self):
        return self.solid
