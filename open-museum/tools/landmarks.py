"""Everything round the museum: the waterfalls and the grotto behind one, the spiral stair and the
lookout, the grind rail, the swingshot rafts, the pier, and the easter eggs.

build() adds the drawn and solid shapes to a Geo and returns the layout: where place_museum.py puts
the game's own objects (swingshot targets, gold bolts, crates, lamps, grind paths, Ratchet)."""

import math

import numpy as np

import house
import island
import textures
from build import Geo

SEA = island.SEA
C = island.CENTRE


def rp(angle, r, dz=None):
    """A point at ANGLE (degrees about the island's centre) and distance R; on the ground, or DZ
    above the sea."""
    x = C[0] + math.cos(math.radians(angle)) * r
    y = C[1] + math.sin(math.radians(angle)) * r
    z = float(island.height(x, y)) if dz is None else SEA + dz
    return np.array([x, y, z])


def catmull(points, step=2.0):
    """A smooth line through POINTS, a point every STEP units or so."""
    p = [np.asarray(points[0])] + [np.asarray(q) for q in points] + [np.asarray(points[-1])]
    out = []
    for i in range(1, len(p) - 2):
        p0, p1, p2, p3 = p[i - 1], p[i], p[i + 1], p[i + 2]
        n = max(2, int(np.linalg.norm(p2 - p1) / step))
        for k in range(n):
            t = k / n
            t2, t3 = t * t, t * t * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                              + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(np.asarray(points[-1]))
    return [list(map(float, q)) for q in out]


def add_sign(geo, pictures, mats, key, title, sub, centre, facing, w=6, h=2.25, post=True):
    pictures[key] = textures.sign(title, sub)
    mats[key] = {"texture": key}
    geo.sign(key, centre, facing, w, h)
    # A plank board behind the picture, so the sign has a plain back.
    f = math.radians(facing)
    n2 = np.array([math.cos(f), math.sin(f)])
    s2 = np.array([-n2[1], n2[0]])
    c2 = np.array(centre[:2]) - n2 * 0.1
    geo.oriented_box("om_planks_dark", c2 - s2 * (w / 2 + 0.15), c2 + s2 * (w / 2 + 0.15), n2, 0.25,
                     centre[2] - h / 2 - 0.15, centre[2] + h / 2 + 0.15, solid=False)
    if post:
        f = math.radians(facing)
        back = np.array(centre) - np.array([math.cos(f), math.sin(f), 0]) * 0.2
        ground = float(island.height(back[0], back[1]))
        geo.box("om_planks_dark", (back[0] - 0.25, back[1] - 0.25, min(ground, back[2]) - 1),
                (back[0] + 0.25, back[1] + 0.25, back[2] + h / 2), solid=False)


def waterfall_sheet(geo, angle, top_r, top_z, bottom_r, bottom_z, width, material, offset=0.0, bulge=6.0):
    """A curved sheet of falling water facing the centre."""
    rows = 10
    d = np.array([math.cos(math.radians(angle)), math.sin(math.radians(angle))])
    side = np.array([-d[1], d[0]])
    for i in range(rows):
        for j in (0,):
            t0, t1 = i / rows, (i + 1) / rows
            def at(t, s):
                r = top_r + (bottom_r - top_r) * t - math.sin(t * math.pi) * bulge * 0.3 - offset
                z = top_z + (bottom_z - top_z) * (t ** 1.6)
                w = width * (1 + 0.25 * t)
                p = C + d * r + side * s * w / 2
                return [p[0], p[1], z]
            q = np.array([at(t0, -1), at(t0, 1), at(t1, 1), at(t1, -1)], np.float64)
            n = [-d[0], -d[1], 0.2]
            uv = np.array([[0, t0 * 4], [1, t0 * 4], [1, t1 * 4], [0, t1 * 4]], np.float32)
            geo.tri_quad(material, q, n, uv, solid=False, glow=[0.62, 0.64, 0.66], both=True)


def disc(geo, material, centre, radius, z, sides=20, glow=None):
    cx, cy = centre
    for k in range(sides):
        a0 = 2 * math.pi * k / sides; a1 = 2 * math.pi * (k + 1) / sides
        p = np.array([[cx, cy, z], [cx + math.cos(a0) * radius, cy + math.sin(a0) * radius, z],
                      [cx + math.cos(a1) * radius, cy + math.sin(a1) * radius, z]], np.float32)
        uv = (p[:, :2] - [cx, cy]) / (radius * 2) + 0.5
        geo.mesh.add(material, p, [[0, 0, 1]] * 3, uv, [glow or [0.6, 0.6, 0.6]] * 3, [[0, 1, 2]])


def pole_raft(geo, centre, z, size=3.2):
    """A small square raft on a pole: a landing between two swings."""
    x, y = centre
    ground = float(island.height(x, y))
    geo.cylinder("om_planks_dark", (x, y), 0.45, min(ground, SEA) - 2, z - 0.4, 8)
    geo.box("om_planks_light", (x - size, y - size, z - 0.5), (x + size, y + size, z))
    geo.box("om_gold", (x - size - 0.1, y - size - 0.1, z - 0.7), (x + size + 0.1, y + size + 0.1, z - 0.5), solid=False)


def spiral(geo, centre, z0, z1, half=6.0, width=4.0):
    """A square spiral stair round a stone core, from Z0 up to Z1. Returns its top landing."""
    cx, cy = centre
    c = half + width / 2
    corners = [(-c, -c), (c, -c), (c, c), (-c, c)]
    rise = 4.0
    flights = int(math.ceil((z1 - z0) / rise))
    rise = (z1 - z0) / flights
    geo.box("om_stone", (cx - half, cy - half, z0 - 2), (cx + half, cy + half, z1 + 3))
    geo.box("om_planks_dark", (cx - half - 0.3, cy - half - 0.3, z1 + 3), (cx + half + 0.3, cy + half + 0.3, z1 + 3.6))
    z = z0
    hw = width / 2
    for k in range(flights):
        a = np.array(corners[k % 4]); b = np.array(corners[(k + 1) % 4])
        # The landing at the corner.
        geo.box("om_planks", (cx + a[0] - hw, cy + a[1] - hw, z - 0.4), (cx + a[0] + hw, cy + a[1] + hw, z))
        d = (b - a) / np.linalg.norm(b - a)
        s0 = np.array([cx, cy]) + a + d * hw
        s1 = np.array([cx, cy]) + b - d * hw
        side = np.array([-d[1], d[0]]) * hw
        q = np.array([[*(s0 - side), z], [*(s1 - side), z + rise], [*(s1 + side), z + rise], [*(s0 + side), z]])
        n = np.cross(q[1] - q[0], q[3] - q[0]); n /= np.linalg.norm(n)
        if n[2] < 0:
            q = q[[0, 3, 2, 1]]; n = -n
        uv = np.array([[0, 0], [3, 0], [3, 1], [0, 1]], np.float32)
        geo.tri_quad("om_planks_light", q, list(n), uv)
        under = q - np.array([0, 0, 0.4])
        geo.tri_quad("om_planks_dark", under[::-1], list(-n), uv[::-1], solid=False)
        # An outer handrail (drawn), posts at both ends.
        outer = side * (1 if np.dot(side, a) > 0 else -1)
        for p, zz in ((s0, z), (s1, z + rise)):
            o = p + outer * 1.0
            geo.box("om_planks_dark", (o[0] - 0.12, o[1] - 0.12, zz), (o[0] + 0.12, o[1] + 0.12, zz + 1.3), solid=False)
        geo.beam("om_planks_dark", (*(s0 + outer), z + 1.3), (*(s1 + outer), z + rise + 1.3), 0.2)
        z += rise
    a = np.array(corners[flights % 4])
    top = (cx + a[0], cy + a[1], z)
    geo.box("om_planks", (top[0] - hw, top[1] - hw, z - 0.4), (top[0] + hw, top[1] + hw, z))
    return top


def coarse_height(q, step=4.0):
    """The highest corner of the collision cell over Q (ground.py's mountain grid)."""
    ox, oy = island.CENTRE[0] - 404, island.CENTRE[1] - 120
    i, j = math.floor((q[0] - ox) / step), math.floor((q[1] - oy) / step)
    return max(float(island.height(ox + (i + a) * step, oy + (j + b) * step)) for a in (0, 1) for b in (0, 1))


def volcano_rafts(a, za, b, zb, reach=25.0, rise=19.0):
    """Rafts from A (deck ZA) to B (deck ZB), each above the mountain, each pull within reach."""
    cand = []
    for t in np.linspace(0.04, 0.86, 42):
        p = a + (b - a) * t
        ground = max(coarse_height(p + o) for o in (np.array([3, 3]), np.array([-3, 3]), np.array([3, -3]), np.array([-3, -3])))
        cand.append((p, max(za + (zb - za) * t, ground + 5.0)))
    cand.append((b, zb))

    def clear(p0, z0, p1, z1):
        for t in np.linspace(0.05, 0.95, 12):
            q = p0 + (p1 - p0) * t
            if z0 + 0.5 + (z1 + 6 - z0 - 0.5) * t < coarse_height(q) + 1.5:
                return False
        return True

    nodes = [(a, za)] + cand
    # The fewest rafts: a breadth-first search over the pulls that work.
    prev = {0: None}
    queue = [0]
    while queue:
        i = queue.pop(0)
        p0, z0 = nodes[i]
        for j in range(i + 1, len(nodes)):
            p1, z1 = nodes[j]
            if j in prev:
                continue
            last = j == len(nodes) - 1
            horizontal = float(np.linalg.norm(p1 - p0))
            if horizontal < (4 if last else 10) or math.hypot(horizontal, z1 + 6 - z0) > reach or z1 - z0 > rise:
                continue
            if not clear(p0, z0, p1, z1):
                continue
            prev[j] = i
            queue.append(j)
    if len(nodes) - 1 not in prev:
        raise RuntimeError("no chain of rafts reaches the ledge")
    out, k = [], prev[len(nodes) - 1]
    while k:
        p, z = nodes[k]
        out.append(((float(p[0]), float(p[1])), float(z)))
        k = prev[k]
    return out[::-1]


def grapple_chain(geo, moby, rafts, note):
    """Rafts ((x, y), deck z), each with a Versatarget 6 units over its middle."""
    for (p, z) in rafts:
        pole_raft(geo, p, z)
        moby("swing_target", (p[0], p[1], z + 6.0), note=note)


def platform(geo, centre, z, half, legs=True, rail=True, gaps=()):
    x, y = centre
    geo.box("om_planks_light", (x - half, y - half, z - 0.5), (x + half, y + half, z))
    if legs:
        for sx in (-1, 1):
            for sy in (-1, 1):
                gx, gy = x + sx * (half - 0.6), y + sy * (half - 0.6)
                g = float(island.height(gx, gy))
                geo.box("om_planks_dark", (gx - 0.5, gy - 0.5, min(g, SEA) - 2), (gx + 0.5, gy + 0.5, z - 0.5))
    if rail:
        edges = [((x - half, y - half), (x + half, y - half), "s"), ((x + half, y - half), (x + half, y + half), "e"),
                 ((x + half, y + half), (x - half, y + half), "n"), ((x - half, y + half), (x - half, y - half), "w")]
        for a, b, side in edges:
            if side in gaps:
                length = np.linalg.norm(np.subtract(b, a))
                geo.wall("om_planks_dark", a, b, z, z + 1.1, 0.25, [(length / 2 - 2.5, length / 2 + 2.5, 0, 2)])
            else:
                geo.wall("om_planks_dark", a, b, z, z + 1.1, 0.25)


def rail_model(geo, points, post_every=8, solid_posts=False):
    for a, b in zip(points, points[1:]):
        geo.beam("om_rail", a, b, 0.35)
    for k in range(0, len(points), post_every):
        p = points[k]
        g = float(island.height(p[0], p[1]))
        bottom = max(min(g, SEA) - 3, p[2] - 80)
        geo.beam("om_planks_dark", (p[0], p[1], bottom), (p[0], p[1], p[2] - 0.3), 0.4, solid=solid_posts)


def build(geo: Geo, pictures, mats):
    layout = {"mobys": [], "grind_paths": [], "lamps": []}
    mobys = layout["mobys"]

    def moby(kind, p, yaw=0.0, note=""):
        p = list(p)
        if len(p) == 2:
            p.append(float(island.height(p[0], p[1])) + 0.8)
        mobys.append({"kind": kind, "position": [float(v) for v in p], "yaw": float(yaw), "note": note})

    # --- The waterfalls ---
    falls = []
    for k, f in enumerate(island.FALLS):
        top_r = 170.0 if k == 1 else 162.0
        top = rp(f["angle"], top_r)
        top_z = float(top[2]) + 0.6
        sheet = Geo()
        waterfall_sheet(sheet, f["angle"], top_r, top_z, f["r"] - 4, SEA + 0.2, 9.0 if k == 0 else 12.0, "om_falls")
        foam = Geo()
        pool = rp(f["angle"], f["r"] - 10)
        disc(foam, "om_foam", (pool[0], pool[1]), 9, SEA + 0.15, 24, [0.62, 0.62, 0.62])
        falls.append({"sheet": sheet, "foam": foam, "pool": pool})
        # Rocks round the pool's edge.
        for j in range(9):
            a = f["angle"] + (j - 4) * 9
            p = rp(a, f["r"] - 10 + 14 + (j % 2) * 2)
            s = 1.5 + (j % 3)
            geo.box("om_rock", (p[0] - s, p[1] - s, p[2] - 2), (p[0] + s, p[1] + s, p[2] + s * 0.8))
    layout["falls"] = [{"pool": list(map(float, f["pool"]))} for f in falls]

    # --- The grotto behind the second fall ---
    g = island.GROTTO
    mid = rp(g["angle"], (g["r0"] + g["r1"]) / 2, 2.0)
    d = np.array([math.cos(math.radians(g["angle"])), math.sin(math.radians(g["angle"]))])
    side = np.array([-d[1], d[0]])
    half_w = g["half"] * (g["r0"] + g["r1"]) / 2 / 57.3 + 2
    corners = [C + d * (g["r0"] - 1) + side * half_w, C + d * (g["r0"] - 1) - side * half_w,
               C + d * (g["r1"] + 3) - side * half_w, C + d * (g["r1"] + 3) + side * half_w]
    roof = np.array([[c[0], c[1], g["roof"]] for c in corners])
    geo.tri_quad("om_rock", roof, [0, 0, -1], np.array([[0, 0], [3, 0], [3, 3], [0, 3]], np.float32), solid=True, both=True)
    # Side walls of stone.
    for s in (-1, 1):
        a = C + d * (g["r0"] - 1) + side * s * half_w
        b = C + d * (g["r1"] + 3) + side * s * half_w
        geo.wall("om_rock", a, b, SEA + 1, g["roof"], 2.0)
    back = C + d * (g["r1"] + 2)
    geo.wall("om_rock", back - side * half_w, back + side * half_w, SEA + 1, g["roof"], 2.0)
    facing = g["angle"] + 180
    add_sign(geo, pictures, mats, "om_egg_grotto", "THE DECOMP CAVE", "you matched this secret 1:1",
             (back[0] - d[0] * 1.2, back[1] - d[1] * 1.2, SEA + 6.5), facing, 7, 2.6, post=False)
    # Glowing crystals in the grotto.
    for j in range(7):
        p = C + d * (g["r0"] + 3 + j * 2.6) + side * ((j % 2) * 2 - 1) * (half_w - 1.5)
        geo.cone("om_teal", (p[0], p[1]), 0.6 + 0.2 * (j % 3), SEA + 2, SEA + 4 + (j % 3), 5, glow=[0.4, 0.95, 0.9])
    moby("gold_bolt", C + d * (g["r1"] - 1), note="grotto: behind the second waterfall")
    for j in range(3):
        moby("bolt_crate", np.append(C + d * (g["r0"] + 6) + side * (j - 1) * 2.2, SEA + 2.0))

    # --- The spiral stair and the lookout ---
    base = rp(166, 172)
    top = spiral(geo, (base[0], base[1]), float(base[2]), SEA + 82.0)
    look = rp(150, 192)
    look[2] = SEA + 82.0
    platform(geo, (look[0], look[1]), look[2], 8.0, gaps=("w", "e", "s"))
    # The bridge from the stair's top to the lookout.
    a = np.array(top[:2]); b = np.array([look[0], look[1]])
    dd = (b - a) / np.linalg.norm(b - a)
    sd = np.array([-dd[1], dd[0]]) * 2.2
    q = np.array([[*(a - sd), look[2]], [*(b - sd), look[2]], [*(b + sd), look[2]], [*(a + sd), look[2]]])
    if np.cross(q[1] - q[0], q[3] - q[0])[2] < 0:
        q = q[[0, 3, 2, 1]]
    geo.tri_quad("om_planks_light", q, [0, 0, 1], np.array([[0, 0], [8, 0], [8, 1], [0, 1]], np.float32))
    for s in (-1, 1):
        geo.beam("om_planks_dark", (*(a + sd * s * 1.1), look[2] + 1.2), (*(b + sd * s * 1.1), look[2] + 1.2), 0.2)
        geo.wall("om_planks_dark", a + sd * s * 1.1, b + sd * s * 1.1, look[2], look[2] + 1.0, 0.2)
    for t in np.linspace(0.1, 0.9, 4):
        p = a + (b - a) * t
        gz = float(island.height(*p))
        geo.box("om_planks_dark", (p[0] - 0.4, p[1] - 0.4, gz - 1), (p[0] + 0.4, p[1] + 0.4, look[2] - 0.4))
    add_sign(geo, pictures, mats, "om_sign_lookout", "LOOKOUT", "Grindboots: tower  Swingshot: volcano",
             (look[0] + 5, look[1] + 7.6, look[2] + 2.2), -90, 6, 2.25, post=False)
    layout["lookout"] = list(map(float, look))
    layout["lamps"] += [list(map(float, (look[0] + sx * 7, look[1] + sy * 7, look[2]))) for sx in (-1, 1) for sy in (-1, 1)]
    layout["lamps"].append([float(base[0]) + 11, float(base[1]) - 11, float(base[2])])

    # --- The grind rail: lookout to the tower's top room ---
    tx, ty = house.TOWER
    start = [look[0] + 4.0, look[1] + 1, look[2] + 1.0]   # over the lookout's floor: jump up onto it
    ctrl = [start, [look[0] + 40, look[1] + 6, look[2] - 4], [C[0] - 30, C[1] + 105, SEA + 64],
            [C[0] + 20, C[1] + 92, SEA + 58], [tx + 14, ty + 36, SEA + 52], [tx + 2, ty + 15, house.TOWER_TOP + 1.2]]
    rail = catmull(ctrl, 2.0)
    rail_model(geo, rail)
    layout["grind_paths"].append({"name": "lookout_to_tower", "points": rail})
    moby("bolt_crate", (look[0] + 7, look[1] - 5, look[2]))

    # --- The lagoon loop: a rail round the lagoon on posts, from the east bridge to the first fall ---
    loop = []
    for k in range(0, 61):
        a_deg = -20 + k * 1.5
        r = island.LAGOON_R + 14
        p = rp(a_deg, r)
        loop.append([float(p[0]), float(p[1]), SEA + 11 + 3 * math.sin(k / 60 * math.pi * 2)])
    ramp_start = rp(-24, island.LAGOON_R + 14)
    loop.insert(0, [float(ramp_start[0]), float(ramp_start[1]), SEA + 9.5])
    rail_model(geo, loop, 6)
    layout["grind_paths"].append({"name": "lagoon_loop", "points": loop})
    # A stand at its start to jump on from.
    platform(geo, (ramp_start[0] + 3, ramp_start[1] - 3), SEA + 8.5, 3.0, rail=False)
    geo.slope("om_planks_light", ramp_start[0] + 0, ramp_start[0] + 6, ramp_start[1] - 18, ramp_start[1] - 6, float(island.height(ramp_start[0] + 3, ramp_start[1] - 18)), SEA + 8.5, "y", 0.3)

    # --- The swingshot rafts: north shore to the tower ---
    # A Versatarget pulls Ratchet up to it and lets go, so each one hangs 6 units over the raft
    # he drops on (as Pokitaru's own one does over its ledge). Each pull is under 30 units (its reach).
    jetty = (tx, C[1] + 100)
    platform(geo, jetty, SEA + 8.0, 4.0, rail=False)
    rafts = [((tx, C[1] + 84), SEA + 22.0), ((tx, C[1] + 66), SEA + 34.0), ((tx, ty + 26), SEA + 41.0)]
    grapple_chain(geo, moby, rafts, "north swing chain to the tower")
    moby("swing_target", (tx, ty + house.TOWER_HALF + 1.5, house.TOWER_TOP + 4.5), note="north swing chain to the tower")
    add_sign(geo, pictures, mats, "om_sign_swing", "SWING TO THE TOWER", "Swingshot only", (tx - 7.5, C[1] + 100 - 3.8, SEA + 9.5), -90)

    # --- The secret archive: the tower's top room ---
    duck(geo, (tx + 2, ty - 1.5), house.TOWER_TOP)
    add_sign(geo, pictures, mats, "om_egg_archive", "SECRET ARCHIVE", "rubber duck: 1,000 bugs found",
             (tx - house.TOWER_HALF + 0.4, ty, house.TOWER_TOP + 3), 0, 5, 1.9, post=False)
    moby("gold_bolt", (tx - 2, ty + 2, house.TOWER_TOP + 0.5), note="tower top")

    # --- The pier on the south beach, with the welcome sign ---
    pier = rp(-95, 205)
    pier_z = SEA + 3.5
    for k in range(7):
        p = rp(-95, 205 + k * 6, 0)
        geo.box("om_planks_light", (p[0] - 3, p[1] - 3, pier_z - 0.4), (p[0] + 3, p[1] + 3, pier_z))
        for s in (-1, 1):
            geo.cylinder("om_planks_dark", (p[0] + s * 2.7, p[1]), 0.35, SEA - 8, pier_z + 1.0, 6)
    add_sign(geo, pictures, mats, "om_sign_pier", "OPEN MUSEUM", "this way to the house",
             (pier[0] + 4, pier[1] - 2, pier[2] + 2.8), 90)

    # --- Message in a bottle, on the far west islet: swingshot over the sea ---
    islet = island.ISLETS[3]
    ix, iy = islet[0], islet[1]
    iz = float(island.height(ix, iy))
    coast = np.array([430.0, 735.0])
    cz = max(float(island.height(*coast)), SEA + 6)
    hops = []
    n = 7
    for k in range(1, n):
        t = k / n
        p = coast + (np.array([ix, iy]) - coast) * t
        hops.append(((float(p[0]), float(p[1])), cz + (iz - cz) * t + 2.0 * math.sin(t * math.pi)))
    grapple_chain(geo, moby, hops, "swing chain to the bottle islet")
    moby("swing_target", (ix + (coast[0] - ix) * 0.08, iy + (coast[1] - iy) * 0.08, iz + 6), note="swing chain to the bottle islet")
    platform(geo, coast, cz, 3.5, rail=False)
    geo.cylinder("om_glass", (ix + 1, iy), 0.5, iz, iz + 1.6, 10)
    geo.cylinder("om_planks_dark", (ix + 1, iy), 0.25, iz + 1.6, iz + 2.1, 6)
    add_sign(geo, pictures, mats, "om_egg_bottle", "MESSAGE IN A BOTTLE", "hello from the Emotion Engine",
             (ix - 3, iy - 2, iz + 2.5), -60)
    moby("gold_bolt", (ix, iy + 2, iz + 0.6), note="bottle islet")
    layout["islet"] = [float(ix), float(iy), iz]

    # --- The sunken chest by the house's west deck ---
    cx, cy = house.CX - house.HALF - 14, house.CY
    floor = float(island.height(cx, cy))
    geo.box("om_planks_dark", (cx - 1.6, cy - 1.0, floor), (cx + 1.6, cy + 1.0, floor + 1.4))
    geo.box("om_gold", (cx - 1.7, cy - 1.1, floor + 1.2), (cx + 1.7, cy + 1.1, floor + 1.5))
    add_sign(geo, pictures, mats, "om_egg_chest", "0x1F", "the ground under everything", (cx, cy + 2, floor + 3), 90, 3, 1.1)

    # --- The volcano's shrine, on a ledge high on its side: Swingshot from the lookout ---
    v = island.VOLCANO
    d = np.array(look[:2]) - v
    d /= np.linalg.norm(d)
    ledge = v + d * 60.0
    lz = max(coarse_height(ledge + np.array([a, b])) for a in (-4, 4) for b in (-4, 4)) + 0.6
    platform(geo, ledge, lz, 4.5, legs=False, rail=False)
    add_sign(geo, pictures, mats, "om_egg_volcano", "UNDEFINED BEHAVIOUR", "do not feed the volcano",
             (ledge[0], ledge[1] + 4.0, lz + 2.4), -90, 6, 2.2, post=False)
    moby("gold_bolt", (ledge[0] + 1.5, ledge[1], lz + 0.6), note="volcano ledge")
    moby("nanotech_crate", (ledge[0] - 2, ledge[1] - 1.5, lz), note="volcano ledge")
    rafts = volcano_rafts(np.array(look[:2]), float(look[2]), ledge, lz)
    grapple_chain(geo, moby, rafts, "swing chain up the volcano")
    moby("swing_target", (ledge[0], ledge[1], lz + 6.0), note="swing chain up the volcano")
    layout["volcano_rim"] = [float(ledge[0]), float(ledge[1]), lz]
    layout["volcano_rafts"] = [[p[0], p[1], z] for (p, z) in rafts]

    # --- Crates spelling OPENRAC on the east beach: read them from the tower's top ---
    letters = {"O": ["111", "101", "101", "101", "111"], "P": ["111", "101", "111", "100", "100"],
               "E": ["111", "100", "110", "100", "111"], "N": ["101", "111", "111", "111", "101"],
               "R": ["110", "101", "110", "101", "101"], "A": ["010", "101", "111", "101", "101"],
               "C": ["111", "100", "100", "100", "111"]}
    word = "OPENRAC"
    origin = np.array([C[0] + 150, C[1] - 60])
    for i, ch in enumerate(word):
        for row, bits in enumerate(letters[ch]):
            for col, bit in enumerate(bits):
                if bit == "1":
                    p = origin + np.array([col * 1.6, -(row * 1.6)]) + np.array([0, -i * 7.0])
                    # The word reads along -y, seen from the north-west (the tower).
                    q = np.array([origin[0] + (4 - row) * 1.6, origin[1] - i * 7.0 - col * 1.6])
                    moby("bolt_crate", (q[0], q[1], float(island.height(*q))), note="OPENRAC letters")

    # --- Lamps along the bridges and round the deck ---
    for k in range(-3, 4):
        layout["lamps"].append([house.CX + k * 9.5, house.CY - house.HALF - house.DECK + 0.8, house.F0])
    for k in range(4):
        p = rp(-90, island.LAGOON_R + 10 + k * 7)
        layout["lamps"].append([float(p[0]) + 3.6, float(p[1]), float(p[2])])
        layout["lamps"].append([float(p[0]) - 3.6, float(p[1]), float(p[2])])

    # --- Ratchet's start (where the ship sets him down stays the template's) ---
    layout["start"] = [498.82, 406.0, 230.0]

    # --- The animated props ---
    layout["animated"] = {"logo": {"position": [house.CX, house.CY, house.F0 + 4.0], "spin": 0.6},
                          "falls": [{"position": [0, 0, 0], "move": [0, 0, -1], "distance": 1.5, "speed": 3.0},
                                    {"position": [0, 0, 0], "move": [0, 0, -1], "distance": 1.5, "speed": 2.6}]}
    build.falls = falls
    return layout


def duck(geo, centre, z):
    """A rubber duck, the debugger's best friend."""
    x, y = centre
    geo.box("om_yellow", (x - 1.2, y - 0.8, z), (x + 1.0, y + 0.8, z + 1.2))
    geo.box("om_yellow", (x + 0.3, y - 0.55, z + 1.2), (x + 1.2, y + 0.55, z + 2.2))
    geo.box("om_orange", (x + 1.2, y - 0.3, z + 1.45), (x + 1.8, y + 0.3, z + 1.75))
    geo.box("om_basalt", (x + 0.9, y - 0.56, z + 1.8), (x + 1.05, y - 0.45, z + 1.95), solid=False)
    geo.box("om_basalt", (x + 0.9, y + 0.45, z + 1.8), (x + 1.05, y + 0.56, z + 1.95), solid=False)
    geo.box("om_yellow", (x - 1.5, y - 0.4, z + 0.8), (x - 1.1, y + 0.4, z + 1.5))


def logo():
    """The atrium's turning logo: a gold gear ring round a white bolt."""
    g = Geo()
    cx, cy, z = 0.0, 0.0, 0.0
    teeth = 16
    for k in range(teeth * 2):
        a0 = 2 * math.pi * k / (teeth * 2); a1 = 2 * math.pi * (k + 1) / (teeth * 2)
        r_out = 3.0 if k % 2 == 0 else 2.6
        p0 = (math.cos(a0) * 2.0, math.sin(a0) * 2.0); p1 = (math.cos(a1) * 2.0, math.sin(a1) * 2.0)
        q0 = (math.cos(a0) * r_out, math.sin(a0) * r_out); q1 = (math.cos(a1) * r_out, math.sin(a1) * r_out)
        for zz, n in ((0.4, [0, 0, 1]), (-0.4, [0, 0, -1])):
            quad = np.array([[*p0, zz], [*q0, zz], [*q1, zz], [*p1, zz]])
            if n[2] < 0:
                quad = quad[::-1]
            g.tri_quad("om_gold", quad, n, np.array([[0, 0], [1, 0], [1, 1], [0, 1]], np.float32), solid=False)
        am = (a0 + a1) / 2
        quad = np.array([[*q0, -0.4], [*q1, -0.4], [*q1, 0.4], [*q0, 0.4]])
        g.tri_quad("om_gold", quad, [math.cos(am), math.sin(am), 0], np.array([[0, 0], [1, 0], [1, 1], [0, 1]], np.float32), solid=False)
    # A hexagonal bolt head in the middle, stood up so the gear turns about z like a wheel on its side.
    g.cylinder("om_white", (0, 0), 1.2, -0.6, 0.6, 6)
    return g


def animated(mats):
    out = {}
    for k, f in enumerate(getattr(build, "falls", [])):
        out[f"falls_{k}"] = f["sheet"]
        out[f"foam_{k}"] = f["foam"]
    out["logo"] = logo()
    return out
