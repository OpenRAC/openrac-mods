"""The museum: a grand octagonal pavilion on stilts in the lagoon. Sixteen exhibit rooms in two
rings round a tall atrium under a spire, a gallery round the upper storey reached by a double
stair, a colonnade, a deck with planters and torches, a gateway with the museum's sign at the
south bridge, two bridges to the shore, and a tower whose top room only gadgets reach.

Plan: an octagon whose sides face east, north-east, north and so on. Each side's sector is one
room on each storey; the atrium is the octagon in the middle."""

import math

import numpy as np

import gate
import island

CX, CY = island.CENTRE
C2 = np.array([CX, CY])
F0 = island.HOUSE_FLOOR          # ground floor
STOREY = 7.5
F1 = F0 + STOREY                 # upper floor
TOP = F1 + STOREY                # top of the walls
R_IN = 12.0                      # the atrium's walls (apothem: centre to the middle of a side)
R_OUT = 32.0                     # the outer walls
GALLERY = 4.0                    # the upper gallery outside the outer walls
R_DECK = 42.0                    # the deck's edge
HALF = R_OUT                     # (older names, kept for the landmarks)
DECK = R_DECK - R_OUT
CELL = R_OUT - R_IN
WALL = 0.6
DOOR_W, DOOR_H = 5.0, 4.8
K = 1 / math.cos(math.radians(22.5))   # vertex radius over apothem

# The rooms, sector by sector from the east (0 degrees) anticlockwise: what each one's sign says.
GROUND = [("Launcher", "Tauri, Svelte, Rust"), ("Renderer", "GS to OpenGL and Vulkan"),
          ("Hostgen", "decomp C on the PC"), ("The Port", "native, no emulation"),
          ("Matching", "byte for byte"), ("Decompilation", "from MIPS to C"),
          ("Welcome", "Open Museum foyer"), ("Mods and SDK", "make the game yours")]
UPPER = [("Multiplayer", "co-op across the galaxy"), ("Speedrun Corner", "every bug kept"),
         ("Enhanced", "graphics beyond the PS2"), ("Android", "the galaxy in your pocket"),
         ("Cutscenes", "lights, camera, Clank"), ("OpenRAC Editor", "levels and cutscenes"),
         ("Devlog", "how it was made"), ("Thank You", "contributors and fans")]
DOORS = (0, 6)                   # the sectors with a door out: east and south (the bridges)

TOWER = (CX + 24.0, CY + 24.0)   # the tower on the north-east side
TOWER_HALF = 6.0
TOWER_TOP = TOP + 22.0           # the secret room's floor


def ang(deg):
    return np.array([math.cos(math.radians(deg)), math.sin(math.radians(deg))])


def vert(deg, apothem):
    """The octagon's corner at DEG (a sector's middle +- 22.5) at APOTHEM."""
    return C2 + ang(deg) * apothem * K


def mid(s, apothem):
    return C2 + ang(45 * s) * apothem


def trap(geo, material, s, r0, r1, z, thick=0.4, solid=True, uv=4.0):
    """Sector S's floor between apothems R0 and R1, its top at Z."""
    a, b = 45 * s - 22.5, 45 * s + 22.5
    q = np.array([[*vert(a, r0), z], [*vert(a, r1), z], [*vert(b, r1), z], [*vert(b, r0), z]])
    if np.cross(q[1] - q[0], q[3] - q[0])[2] < 0:
        q = q[[0, 3, 2, 1]]
    t = np.array([[p[0] / uv, p[1] / uv] for p in q], np.float32)
    geo.tri_quad(material, q, [0, 0, 1], t, solid)
    if thick:
        under = q[::-1] - np.array([0, 0, thick])
        geo.tri_quad(material, under, [0, 0, -1], t[::-1], False)
        e = np.array([q[1], q[2], q[2] - [0, 0, thick], q[1] - [0, 0, thick]]) if np.linalg.norm(q[1][:2] - C2) > np.linalg.norm(q[0][:2] - C2) else None
        if e is not None:
            n = np.append(ang(45 * s), 0)
            geo.tri_quad(material, e, list(n), np.array([[0, 0], [4, 0], [4, 0.2], [0, 0.2]], np.float32), False)


def side_wall(geo, material, s, apothem, z0, z1, holes=(), thick=WALL, solid=True):
    """The wall along sector S's side at APOTHEM; HOLES measured from its clockwise end."""
    geo.wall(material, vert(45 * s - 22.5, apothem), vert(45 * s + 22.5, apothem), z0, z1, thick, holes, solid=solid)


def side_len(apothem):
    return 2 * apothem * math.tan(math.radians(22.5))


def radial_wall(geo, material, deg, r0, r1, z0, z1, holes=(), thick=WALL * 0.7):
    geo.wall(material, vert(deg, r0), vert(deg, r1), z0, z1, thick, holes)


def build(geo, signs, posters, art=None):
    """Adds the house to GEO. SIGNS and POSTERS get (texture name, title, sub, centre, facing, w, h);
    ART gets the paintings and rugs: (texture name, kind, seed)."""
    art = art if art is not None else []
    # Stilts under the deck, rings of them.
    for r, count in ((6, 8), (17, 16), (27, 16), (37, 24), (R_DECK - 0.8, 32)):
        for k in range(count):
            p = C2 + ang(360 * k / count + 11.25) * r
            geo.cylinder("om_planks_dark", (p[0], p[1]), 0.75, float(island.height(p[0], p[1])) - 1.5, F0 - 0.4, 8)
    for s in range(8):
        trap(geo, "om_planks_light", s, 0.01, R_DECK, F0, thick=0.6)
    deck_railing(geo)
    for s in range(8):
        room_shell(geo, s)
        trap(geo, "om_planks", s, R_IN, R_OUT, F1, thick=0.4)                       # the upper floor
        trap(geo, "om_planks_light", s, R_OUT - 0.3, R_OUT + GALLERY, F1, thick=0.4)  # the gallery
    gallery_railing(geo)
    colonnade(geo)
    grand_stair(geo)
    atrium(geo)
    roofs(geo)
    atrium_lights(geo)
    decorate(geo)
    tower(geo)
    bridges(geo)
    gateway(geo, signs)
    rooms(geo, signs, posters, art)


def deck_railing(geo):
    rail_z = F0 + 1.6
    for s in range(8):
        a, b = vert(45 * s - 22.5, R_DECK - 0.3), vert(45 * s + 22.5, R_DECK - 0.3)
        length = np.linalg.norm(b - a)
        d = (b - a) / length
        gap = (length / 2 - 4, length / 2 + 4) if s in DOORS else None
        if s == 4:                                     # the west side: the grand stair's foot
            gap = (0.5, length - 0.5)
        for t in np.arange(0, length + 0.1, 2.2):
            if gap and gap[0] < t < gap[1]:
                continue
            p = a + d * t
            geo.box("om_planks_dark", (p[0] - 0.15, p[1] - 0.15, F0), (p[0] + 0.15, p[1] + 0.15, rail_z))
        spans = [(0, gap[0]), (gap[1], length)] if gap else [(0, length)]
        for t0, t1 in spans:
            if t1 - t0 < 0.3:
                continue
            pa, pb = a + d * t0, a + d * t1
            geo.beam("om_planks", (*pa, rail_z), (*pb, rail_z), 0.3)
            geo.wall("om_planks", pa, pb, F0, F0 + 1.0, 0.2, solid=True)


def room_shell(geo, s):
    """Sector S's walls on both storeys: outside (windows, doors), atrium side, and the radial wall
    on its clockwise side (with a door through)."""
    L = side_len(R_OUT)
    for storey, (z0, z1) in enumerate(((F0, F1), (F1, TOP))):
        if storey == 0 and s in DOORS:
            holes = [(L / 2 - DOOR_W / 2 - 0.5, L / 2 + DOOR_W / 2 + 0.5, 0, DOOR_H + 0.6),
                     (3.0, 6.0, 1.6, 5.6), (L - 6.0, L - 3.0, 1.6, 5.6)]
        elif storey == 1:
            holes = [(L / 2 - 2.0, L / 2 + 2.0, 0, DOOR_H), (3.0, 7.5, 1.4, 5.8), (L - 7.5, L - 3.0, 1.4, 5.8)]
        else:
            holes = [(3.5, 8.5, 1.4, 6.0), (L - 8.5, L - 3.5, 1.4, 6.0)]
        side_wall(geo, "om_planks", s, R_OUT, z0, z1, holes)
        window_frames(geo, s, z0, holes)
        # The atrium side: a wide way in below, an arch onto the atrium's void above (railed).
        Li = side_len(R_IN)
        if storey == 0:
            side_wall(geo, "om_planks_light", s, R_IN, z0, z1, [(Li / 2 - 3.2, Li / 2 + 3.2, 0, 5.8)])
        else:
            side_wall(geo, "om_planks_light", s, R_IN, z0, z1, [(1.2, Li - 1.2, 1.1, 6.0)])
        radial_wall(geo, "om_planks_light", 45 * s - 22.5, R_IN, R_OUT, z0, z1,
                    [((R_OUT - R_IN) * K / 2 - DOOR_W / 2, (R_OUT - R_IN) * K / 2 + DOOR_W / 2, 0, DOOR_H)])
        geo.box("om_planks_dark", (*(vert(45 * s - 22.5, R_OUT) - 0.9), F0), (*(vert(45 * s - 22.5, R_OUT) + 0.9), TOP + 0.4))
    # The beam round the outside between the storeys.
    a, b = vert(45 * s - 22.5, R_OUT + 0.35), vert(45 * s + 22.5, R_OUT + 0.35)
    geo.beam("om_planks_dark", (*a, F1 - 0.3), (*b, F1 - 0.3), 0.7)
    geo.beam("om_planks_dark", (*a, TOP - 0.2), (*b, TOP - 0.2), 0.7)


def window_frames(geo, s, z0, holes):
    """Frames, sills, shutters and flower boxes round the windows of sector S's outer wall."""
    a, b = vert(45 * s - 22.5, R_OUT), vert(45 * s + 22.5, R_OUT)
    d = (b - a) / np.linalg.norm(b - a)
    out = ang(45 * s)
    for (h0, h1, hb, ht) in holes:
        if hb < 0.5:
            continue                                   # a door
        p0 = a + d * h0 + out * (WALL / 2 + 0.05)
        p1 = a + d * h1 + out * (WALL / 2 + 0.05)
        zb, zt = z0 + hb, z0 + ht
        for (q0, q1, zz0, zz1) in ((p0 - d * 0.35, p0, zb - 0.3, zt + 0.3), (p1, p1 + d * 0.35, zb - 0.3, zt + 0.3)):
            geo.oriented_box("om_planks_dark", q0, q1, out, 0.3, zz0, zz1, solid=False)
        geo.oriented_box("om_planks_dark", p0 - d * 0.35, p1 + d * 0.35, out, 0.3, zt, zt + 0.35, solid=False)
        geo.oriented_box("om_planks_dark", p0 - d * 0.5, p1 + d * 0.5, out, 0.7, zb - 0.35, zb, solid=False)
        # A mullion cross (the window's glazing bars).
        m = (p0 + p1) / 2
        geo.oriented_box("om_planks_dark", m - d * 0.07, m + d * 0.07, out, 0.12, zb, zt, solid=False)
        geo.oriented_box("om_planks_dark", p0, p1, out, 0.12, (zb + zt) / 2 - 0.07, (zb + zt) / 2 + 0.07, solid=False)
        # Shutters open against the wall, teal, and a flower box under the sill.
        w = (h1 - h0) / 2
        for q0, q1 in ((p0 - d * (0.4 + w), p0 - d * 0.4), (p1 + d * 0.4, p1 + d * (0.4 + w))):
            geo.oriented_box("om_teal", q0 + out * 0.05, q1 + out * 0.05, out, 0.12, zb, zt, solid=False)
        box_a, box_b = p0 + out * 0.45 - d * 0.2, p1 + out * 0.45 + d * 0.2
        geo.oriented_box("om_planks", box_a, box_b, out, 0.7, zb - 1.0, zb - 0.35, solid=False)
        flowers_row(geo, box_a + out * 0.0, box_b, zb - 0.35, s * 7 + int(h0))


FLOWER = ("om_red", "om_yellow", "om_orange", "om_white", "om_teal")


def flowers_row(geo, a, b, z, seed):
    """Little flowers along A-B at Z: green stems and leaves, bright heads."""
    r = np.random.default_rng(abs(int(seed)) + 900)
    n = max(3, int(np.linalg.norm(b - a) / 0.55))
    for k in range(n):
        p = a + (b - a) * (k + 0.5) / n
        h = 0.35 + r.random() * 0.35
        geo.cone("om_grass", (p[0], p[1]), 0.22, z, z + h, 5)
        c = FLOWER[int(r.integers(len(FLOWER)))]
        geo.cone(c, (p[0], p[1]), 0.2, z + h - 0.05, z + h + 0.25, 5, glow=None)


def flower_pot(geo, p, z, seed, size=1.0):
    """A clay pot of flowers on the floor."""
    r = np.random.default_rng(abs(int(seed)))
    geo.cylinder("om_orange", (p[0], p[1]), 0.55 * size, z, z + 0.8 * size, 10, solid=True)
    geo.cylinder("om_planks_dark", (p[0], p[1]), 0.6 * size, z + 0.7 * size, z + 0.85 * size, 10)
    for k in range(7):
        a = r.random() * 2 * math.pi
        d = r.random() * 0.35 * size
        q = (p[0] + math.cos(a) * d, p[1] + math.sin(a) * d)
        h = (0.6 + r.random() * 0.8) * size
        geo.cone("om_grass", q, 0.28 * size, z + 0.8 * size, z + 0.8 * size + h, 5)
        geo.cone(FLOWER[int(r.integers(len(FLOWER)))], q, 0.25 * size, z + 0.75 * size + h, z + 1.05 * size + h, 6)


def gallery_railing(geo):
    """The gallery's railing and its posts, open to the west where the grand stair arrives."""
    for s in range(8):
        a, b = vert(45 * s - 22.5, R_OUT + GALLERY - 0.2), vert(45 * s + 22.5, R_OUT + GALLERY - 0.2)
        length = np.linalg.norm(b - a)
        holes = [(length / 2 - 2.6, length / 2 + 2.6, 0, 3)] if s == 4 else []
        geo.wall("om_planks_dark", a, b, F1, F1 + 1.15, 0.25, holes)
        d = (b - a) / length
        for t in np.arange(0.6, length, 1.4):
            if s == 4 and abs(t - length / 2) < 2.8:
                continue
            p = a + d * t
            geo.box("om_planks_light", (p[0] - 0.1, p[1] - 0.1, F1 + 1.15), (p[0] + 0.1, p[1] + 0.1, F1 + 1.5), solid=False)
        geo.beam("om_planks", (*a, F1 + 1.5), (*b, F1 + 1.5), 0.3)


def colonnade(geo):
    """Round posts under the gallery's edge, carved capitals in gold."""
    for s in range(8):
        for t in (-22.5, -7.5, 7.5):
            deg = 45 * s + t
            apo = (R_OUT + GALLERY - 0.6)
            p = C2 + ang(deg) * apo / math.cos(math.radians(abs(t) if abs(t) < 22.5 else 22.5))
            geo.cylinder("om_planks_dark", (p[0], p[1]), 0.55, F0, F1 - 0.4, 10, solid=True)
            geo.cylinder("om_gold", (p[0], p[1]), 0.75, F1 - 0.9, F1 - 0.4, 10)
            geo.cylinder("om_gold", (p[0], p[1]), 0.75, F0, F0 + 0.35, 10)


def grand_stair(geo):
    """Two flights up the west side to the gallery, meeting at a landing over the middle."""
    x0, x1 = CX - R_DECK + 1.0, CX - R_OUT - GALLERY + 0.1
    w = 0.5
    for sign in (-1, 1):
        y_far, y_near = CY + sign * 16.5, CY + sign * 2.6
        y0, y1 = min(y_far, y_near), max(y_far, y_near)
        z_at_y0 = F0 if sign < 0 else F1
        z_at_y1 = F1 if sign < 0 else F0
        geo.slope("om_planks_light", x0, x1, y0, y1, z_at_y0, z_at_y1, "y", 0.4, solid=True)
        # Steps drawn over it, and a handrail on the outside.
        n = 14
        for k in range(n):
            t0, t1 = k / n, (k + 1) / n
            ya, yb = y_far + (y_near - y_far) * t0, y_far + (y_near - y_far) * t1
            z = F0 + STOREY * t1
            geo.box("om_planks_dark", (x0, min(ya, yb), z - 0.25), (x1, max(ya, yb), z + 0.05), solid=False)
        geo.beam("om_planks_dark", (x0 + 0.2, y_far, F0 + 1.3), (x0 + 0.2, y_near, F1 + 1.3), 0.25)
        for t in np.linspace(0, 1, 5):
            y = y_far + (y_near - y_far) * t
            geo.box("om_planks_dark", (x0 + 0.05, y - 0.12, F0 + STOREY * t), (x0 + 0.35, y + 0.12, F0 + STOREY * t + 1.3), solid=False)
    geo.box("om_planks_light", (x0, CY - 2.6, F1 - 0.4), (CX - R_OUT + 0.2, CY + 2.6, F1))
    geo.wall("om_planks_dark", (x0 + 0.2, CY - 2.6), (x0 + 0.2, CY + 2.6), F1, F1 + 1.15, 0.25)
    for y in (CY - 2.6, CY + 2.6):
        torch(geo, x0 + 0.4, y, F1, 2.4)
    # Big pots of flowers at the stair's feet.
    for y in (CY - 17.5, CY + 17.5):
        flower_pot(geo, (x0 + 2.0, y), F0, int(y), 1.6)


def atrium(geo):
    """The atrium: a fountain under the turning logo, light from a ring of high windows."""
    geo.cylinder("om_stone", (CX, CY), 6.0, F0, F0 + 0.9, 24, solid=True)
    geo.cylinder("om_water", (CX, CY), 5.4, F0 + 0.9, F0 + 0.95, 24)
    geo.cylinder("om_gold", (CX, CY), 1.6, F0 + 0.9, F0 + 1.6, 16, solid=True)
    geo.cylinder("om_white", (CX, CY), 1.0, F0 + 1.6, F0 + 2.2, 16, solid=True)
    # A rug of the museum's colours round the fountain, and benches.
    for s in range(8):
        a, b = 45 * s - 22.5, 45 * s + 22.5
        q = np.array([[*vert(a, 6.6), F0 + 0.04], [*vert(a, R_IN - 1.0), F0 + 0.04], [*vert(b, R_IN - 1.0), F0 + 0.04],
                      [*vert(b, 6.6), F0 + 0.04]])
        if np.cross(q[1] - q[0], q[3] - q[0])[2] < 0:
            q = q[[0, 3, 2, 1]]
        geo.tri_quad("om_red" if s % 2 else "om_teal", q, [0, 0, 1], np.zeros((4, 2), np.float32), False)
    # The drum above the roof: windows all round under the spire.
    for s in range(8):
        Li = side_len(R_IN)
        side_wall(geo, "om_planks", s, R_IN, TOP, TOP + 8.0, [(1.6, Li / 2 - 0.5, 1.5, 6.5), (Li / 2 + 0.5, Li - 1.6, 1.5, 6.5)])
        geo.box("om_gold", (*(vert(45 * s - 22.5, R_IN) - 0.45), TOP), (*(vert(45 * s - 22.5, R_IN) + 0.45), TOP + 8.6), solid=False)
    # Hanging lanterns in the atrium.
    for s in range(0, 8, 2):
        p = mid(s, R_IN - 2.5)
        geo.cylinder("om_planks_dark", (p[0], p[1]), 0.05, F1 + 3, TOP + 6, 4)
        geo.cylinder("om_red", (p[0], p[1]), 0.6, F1 + 1.6, F1 + 3.0, 8)
        geo.cone("om_yellow", (p[0], p[1]), 0.4, F1 + 1.9, F1 + 2.8, 8, glow=[1.8, 1.5, 0.8])
        geo.light((p[0], p[1], F1 + 1.4), (1.1, 0.8, 0.45), 11)


def atrium_lights(geo):
    for z in (F0 + 4.5, F1 + 4.5, TOP + 3.0):
        geo.light((CX, CY, z), (1.1, 0.95, 0.72), 16)


def roofs(geo):
    """Eight thatched roof planes from the eaves up to the drum, a tall spire on the drum, gold
    finials, and a crest along each hip."""
    eave = R_OUT + GALLERY + 2.5
    z_eave, z_top = TOP - 0.6, TOP + 5.5
    for s in range(8):
        a, b = 45 * s - 22.5, 45 * s + 22.5
        q = np.array([[*vert(a, eave), z_eave], [*vert(b, eave), z_eave], [*vert(b, R_IN), z_top], [*vert(a, R_IN), z_top]])
        n = np.cross(q[1] - q[0], q[3] - q[0]); n /= np.linalg.norm(n)
        if n[2] < 0:
            q = q[[0, 3, 2, 1]]; n = -n
        uv = np.array([[p[0] / 6, p[1] / 6] for p in q], np.float32)
        geo.tri_quad("om_thatch", q, list(n), uv, solid=True, both=True)
        geo.beam("om_planks_dark", (*vert(a, eave), z_eave), (*vert(a, R_IN), z_top), 0.6)
        geo.beam("om_red", (*vert(a, eave), z_eave - 0.4), (*vert(b, eave), z_eave - 0.4), 0.5)   # a painted fascia
        # Little dormer gables on every other side, for a broken skyline.
        if s % 2 == 0:
            m = mid(s, eave - 6.5)
            d = ang(45 * s)
            sd = np.array([-d[1], d[0]])
            zb = z_eave + (z_top - z_eave) * (6.5 / (eave - R_IN))
            ridge = [*(m - d * 3.0), zb + 4.0]
            front = [*(m + d * 2.5), zb + 3.2]
            for sgn in (-1, 1):
                p = np.array([[*(m + d * 2.5 + sd * sgn * 3.2), zb], front, ridge, [*(m - d * 3.0 + sd * sgn * 3.2), zb]])
                nn = np.cross(p[1] - p[0], p[3] - p[0]); nn /= np.linalg.norm(nn)
                if nn[2] < 0:
                    p = p[[0, 3, 2, 1]]; nn = -nn
                geo.tri_quad("om_thatch", p, list(nn), np.array([[0, 0], [1, 0], [1, 1], [0, 1]], np.float32), solid=False, both=True)
            geo.oriented_box("om_planks", m + d * 2.4 - sd * 2.4, m + d * 2.4 + sd * 2.4, d, 0.3, zb - 0.5, zb + 2.6, solid=False)
            geo.oriented_box("om_teal", m + d * 2.6 - sd * 1.0, m + d * 2.6 + sd * 1.0, d, 0.1, zb + 0.3, zb + 2.0, solid=False)
    # The spire on the drum, its own little eaves, a gold finial and a flag.
    zs = TOP + 8.0
    ring = [np.array([*vert(45 * s - 22.5, R_IN + 2.5), zs]) for s in range(8)]
    peak = np.array([CX, CY, zs + 17.0])
    for k in range(8):
        a, b = ring[k], ring[(k + 1) % 8]
        p = np.array([a, b, peak, peak])
        n = np.cross(b - a, peak - a); n /= np.linalg.norm(n)
        if n[2] < 0:
            p = p[[1, 0, 2, 3]]; n = -n
        geo.tri_quad("om_thatch", p, list(n), np.array([[0, 0], [4, 0], [2, 4], [2, 4]], np.float32), solid=True, both=True)
        geo.beam("om_planks_dark", tuple(a), tuple(peak), 0.45)
    geo.cylinder("om_gold", (CX, CY), 0.7, zs + 16.0, zs + 18.5, 10)
    geo.cone("om_gold", (CX, CY), 1.2, zs + 18.5, zs + 21.0, 10, glow=[1.5, 1.3, 0.7])
    geo.cylinder("om_planks_dark", (CX, CY), 0.08, zs + 21.0, zs + 25.0, 4)
    geo.oriented_box("om_red", np.array([CX + 0.1, CY]), np.array([CX + 3.6, CY]), np.array([0, 1.0]), 0.06, zs + 23.0, zs + 24.9, solid=False)


def decorate(geo):
    """The deck: planters, tiki torches, banners and lanterns."""
    for (px, py) in planters():
        geo.cylinder("om_planks_dark", (px, py), 1.4, F0, F0 + 0.9, 10, solid=True)
        geo.cylinder("om_grass", (px, py), 1.2, F0 + 0.9, F0 + 0.95, 10)
    for s in range(8):
        for t in (-1, 1):
            p = vert(45 * s + t * 22.5, R_DECK - 1.2)
            if s == 4:
                continue
            torch(geo, p[0], p[1], F0)
    colours = ("om_red", "om_teal", "om_yellow", "om_orange")
    for s in range(8):
        out = ang(45 * s)
        a, b = vert(45 * s - 22.5, R_OUT), vert(45 * s + 22.5, R_OUT)
        d = (b - a) / np.linalg.norm(b - a)
        L = side_len(R_OUT)
        p = a + d * L / 2 + out * (WALL + 0.12)
        if s in DOORS or s == 4:
            continue
        geo.oriented_box(colours[s % 4], p - d * 1.2, p + d * 1.2, out, 0.06, F1 + 0.6, TOP - 0.8, solid=False)
        geo.oriented_box("om_gold", p - d * 1.35, p + d * 1.35, out, 0.2, TOP - 0.8, TOP - 0.5, solid=False)
    for s in range(8):
        p = vert(45 * s - 22.5, R_OUT + GALLERY + 1.6)
        geo.cylinder("om_planks_dark", (p[0], p[1]), 0.05, TOP - 2.8, TOP - 0.4, 4)
        geo.cylinder("om_red", (p[0], p[1]), 0.5, TOP - 3.9, TOP - 2.8, 8)
        geo.cone("om_yellow", (p[0], p[1]), 0.3, TOP - 3.7, TOP - 3.0, 8, glow=[1.8, 1.5, 0.8])
        geo.light((p[0], p[1], TOP - 4.2), (1.0, 0.72, 0.4), 8)


def planters():
    """Where the deck's planters stand (scatter.decor puts Pokitaru's bushes in them)."""
    out = []
    for s in range(8):
        if s == 4:
            continue
        for t in (-1, 1):
            p = mid(s, R_DECK - 4.0) + np.array([-ang(45 * s)[1], ang(45 * s)[0]]) * t * (5.2 if s in DOORS else 9.0)
            out.append((float(p[0]), float(p[1])))
    return out


def gateway(geo, signs):
    """The museum's gate at the south bridge's shore end: carved posts, an arched beam, a thatched
    cap, the big sign hung under it, banners and torches."""
    d = np.array([0.0, -1.0])
    g = C2 + d * (island.LAGOON_R + 22)
    gz = float(island.height(g[0], g[1]))
    gz = max(gz, F0 - 0.5)                  # the bridge's deck passes under the sign
    x0, x1 = g[0] - 9.5, g[0] + 9.5
    top = gz + 13.0
    for x in (x0, x1):
        geo.box("om_planks_dark", (x - 0.9, g[1] - 0.9, gz - 2), (x + 0.9, g[1] + 0.9, top), solid=True)
        geo.box("om_gold", (x - 1.1, g[1] - 1.1, gz + 0.0), (x + 1.1, g[1] + 1.1, gz + 0.5), solid=False)
        geo.box("om_gold", (x - 1.1, g[1] - 1.1, top - 0.6), (x + 1.1, g[1] + 1.1, top), solid=False)
        geo.cone("om_gold", (x, g[1]), 1.0, top, top + 2.2, 4, glow=[1.3, 1.1, 0.6])
        torch(geo, x + (2.2 if x > g[0] else -2.2), g[1] - 1.5, gz, 3.4)
        for side in (-1, 1):
            gate.banner(geo, signs, "red" if side < 0 else "teal", x, g[1] + side * 0.97, side, 1.4, top - 8.5, top - 1.0)
    # The arch: beams stepping up to a peak, a thatched cap over it.
    pts = [(x0, top - 1.2), (g[0] - 5, top + 1.2), (g[0], top + 2.0), (g[0] + 5, top + 1.2), (x1, top - 1.2)]
    for (xa, za), (xb, zb) in zip(pts, pts[1:]):
        geo.beam("om_planks_dark", (xa, g[1], za), (xb, g[1], zb), 1.1)
        geo.beam("om_gold", (xa, g[1] - 0.6, za - 0.65), (xb, g[1] - 0.6, zb - 0.65), 0.2)
    for sgn in (-1, 1):
        q = np.array([[x0 - 1.5, g[1] + sgn * 2.2, top + 0.2], [x1 + 1.5, g[1] + sgn * 2.2, top + 0.2],
                      [x1 + 1.5, g[1], top + 3.0], [x0 - 1.5, g[1], top + 3.0]])
        n = np.cross(q[1] - q[0], q[3] - q[0]); n /= np.linalg.norm(n)
        if n[2] < 0:
            q = q[[0, 3, 2, 1]]; n = -n
        geo.tri_quad("om_thatch", q, list(n), np.array([[0, 0], [4, 0], [4, 1], [0, 1]], np.float32), solid=False, both=True)
    # The sign, hung under the arch: carved, with raised gold letters (gate.py).
    gate.build(geo, signs, g[0], g[1], top)
    # Flower pots and flags on poles either side of the path.
    for x in (g[0] - 6.6, g[0] + 6.6):
        flower_pot(geo, (x, g[1] - 3.5), gz, int(x * 3), 1.3)
    for k, x in enumerate((g[0] - 14, g[0] + 14)):
        z = float(island.height(x, g[1] - 2))
        geo.cylinder("om_planks_dark", (x, g[1] - 2), 0.18, z - 1, z + 14, 6)
        geo.cone("om_gold", (x, g[1] - 2), 0.35, z + 14, z + 14.8, 6)
        geo.beam("om_planks_dark", (x - 1.3, g[1] - 2.25, z + 13.2), (x + 1.3, g[1] - 2.25, z + 13.2), 0.12)
        gate.banner(geo, signs, ("red", "teal")[k], x, g[1] - 2.25, -1, 2.4, z + 7.6, z + 13.1)


def rooms(geo, signs, posters, art):
    """Each room: its sign over the way to the atrium, framed paintings and exhibit boards on the
    side walls, a rug, plinths with exhibits, flower pots in its corners, a bench."""
    for storey, names in ((0, GROUND), (1, UPPER)):
        z0 = F0 if storey == 0 else F1
        for s, (title, sub) in enumerate(names):
            out = ang(45 * s)
            tang = np.array([-out[1], out[0]])
            key = f"om_sign_{storey}_{s}"
            for r in (R_IN + 5.5, R_OUT - 5.0):                          # the room's warm ceiling lights
                c = mid(s, r)
                geo.light((c[0], c[1], z0 + 5.4), (1.5, 1.25, 0.9), 18)
            # The sign on the outer wall's inside, high, facing the atrium.
            p = mid(s, R_OUT - WALL / 2 - 0.05)
            signs.append((key, title, sub, (p[0], p[1], z0 + 6.4 if not (storey == 0 and s in DOORS) else z0 + 6.3), 45 * s + 180,
                          7.0 if not (storey == 0 and s in DOORS) else 6.0, 1.6 if storey == 0 and s in DOORS else 1.9))
            # The two radial walls: on each, a painting and an exhibit board either side of the door.
            for edge, deg in ((0, 45 * s - 22.5), (1, 45 * s + 22.5)):
                a, b = vert(deg, R_IN), vert(deg, R_OUT)
                d = (b - a) / np.linalg.norm(b - a)
                n = np.array([-d[1], d[0]])
                if np.dot(n, mid(s, 20) - a) < 0:
                    n = -n
                length = np.linalg.norm(b - a)
                for k, t in enumerate((length * 0.22, length * 0.78)):
                    c = a + d * t + n * (WALL * 0.35 + 0.05)
                    face = math.degrees(math.atan2(n[1], n[0]))
                    if (k + edge) % 2 == 0:
                        pkey = f"om_art_{storey}_{s}_{edge}{k}"
                        art.append((pkey, "painting", storey * 100 + s * 10 + edge * 2 + k))
                        frame(geo, c, n, d, z0 + 3.4, 4.2, 3.2)
                        signs.append((pkey, None, None, (c[0] + n[0] * 0.2, c[1] + n[1] * 0.2, z0 + 3.4), face, 3.6, 2.6))
                    else:
                        posters.append((f"{key}_b{edge}{k}", title, (c[0], c[1], z0 + 3.3), face, 3.2, 4.2))
            # The rug, the plinths with their exhibits, pots, a bench.
            rkey = f"om_rug_{storey}_{s}"
            art.append((rkey, "rug", storey * 10 + s))
            rc = mid(s, (R_IN + R_OUT) / 2 + 1.0)
            q = np.array([[*(rc - tang * 5.0 - out * 3.5), z0 + 0.06], [*(rc + tang * 5.0 - out * 3.5), z0 + 0.06],
                          [*(rc + tang * 5.0 + out * 3.5), z0 + 0.06], [*(rc - tang * 5.0 + out * 3.5), z0 + 0.06]])
            if np.cross(q[1] - q[0], q[3] - q[0])[2] < 0:
                q = q[[0, 3, 2, 1]]
                uvq = np.array([[0, 0], [0, 1], [1, 1], [1, 0]], np.float32)
            else:
                uvq = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], np.float32)
            geo.tri_quad(rkey, q, [0, 0, 1], uvq, solid=False, glow=[0.62, 0.62, 0.62])
            if not (storey == 0 and s in DOORS):
                for k in (-1, 0, 1):
                    pp = rc + tang * k * 3.6 + out * 0.5
                    plinth(geo, pp, z0, storey * 31 + s * 3 + k)
            for t in (-1, 1):
                pp = mid(s, R_OUT - 2.2) + tang * t * (side_len(R_OUT - 2.2) / 2 - 1.6)
                flower_pot(geo, pp, z0, storey * 97 + s * 11 + t)
            bc = mid(s, R_IN + 2.6)
            geo.oriented_box("om_planks_dark", bc - tang * 2.0, bc + tang * 2.0, out, 0.9, z0 + 0.5, z0 + 0.8)
            for t in (-1.7, 1.7):
                pp = bc + tang * t
                geo.box("om_planks_dark", (pp[0] - 0.15, pp[1] - 0.15, z0), (pp[0] + 0.15, pp[1] + 0.15, z0 + 0.5), solid=False)


def frame(geo, c, n, d, z, w, h):
    """A gold picture frame on a wall at C (facing N, along D)."""
    for (q0, q1, zb, zt) in ((c - d * w / 2, c + d * w / 2, z + h / 2 - 0.3, z + h / 2),
                             (c - d * w / 2, c + d * w / 2, z - h / 2, z - h / 2 + 0.3),
                             (c - d * w / 2, c - d * (w / 2 - 0.3), z - h / 2, z + h / 2),
                             (c + d * (w / 2 - 0.3), c + d * w / 2, z - h / 2, z + h / 2)):
        geo.oriented_box("om_gold", q0 + n * 0.1, q1 + n * 0.1, n, 0.2, zb, zt, solid=False)
    geo.oriented_box("om_planks_dark", c - d * w / 2, c + d * w / 2, n, 0.15, z - h / 2, z + h / 2, solid=False)


def plinth(geo, p, z0, seed):
    """A white plinth with a gold top and an exhibit on it: one of a few small things."""
    geo.box("om_white", (p[0] - 0.8, p[1] - 0.8, z0), (p[0] + 0.8, p[1] + 0.8, z0 + 1.1))
    geo.box("om_gold", (p[0] - 0.9, p[1] - 0.9, z0 + 1.1), (p[0] + 0.9, p[1] + 0.9, z0 + 1.22))
    z = z0 + 1.22
    kind = seed % 5
    if kind == 0:      # a giant bolt
        geo.cylinder("om_gold", (p[0], p[1]), 0.5, z, z + 0.35, 6)
        geo.cylinder("om_rail", (p[0], p[1]), 0.22, z + 0.35, z + 1.3, 8)
    elif kind == 1:    # a gear on its side
        geo.cylinder("om_rail", (p[0], p[1]), 0.6, z, z + 0.25, 12)
        for k in range(8):
            a = 2 * math.pi * k / 8
            q = (p[0] + math.cos(a) * 0.65, p[1] + math.sin(a) * 0.65)
            geo.box("om_rail", (q[0] - 0.12, q[1] - 0.12, z), (q[0] + 0.12, q[1] + 0.12, z + 0.25), solid=False)
    elif kind == 2:    # a crystal
        geo.cone("om_teal", (p[0], p[1]), 0.4, z, z + 1.4, 5, glow=[0.4, 0.95, 0.9])
    elif kind == 3:    # a little planet with a ring
        geo.cylinder("om_orange", (p[0], p[1]), 0.45, z + 0.2, z + 1.0, 12)
        geo.cylinder("om_yellow", (p[0], p[1]), 0.8, z + 0.55, z + 0.62, 16)
    else:              # a game disc on a stand
        geo.box("om_planks_dark", (p[0] - 0.1, p[1] - 0.3, z), (p[0] + 0.1, p[1] + 0.3, z + 0.3), solid=False)
        geo.cylinder("om_white", (p[0], p[1]), 0.55, z + 0.3, z + 0.36, 16)


def tower(geo):
    """A tall lookout on the north-east corner. Its top room has no stairs: gadgets only."""
    tx, ty = TOWER
    h = TOWER_HALF
    base = TOP - 0.5
    # Four legs and cross braces from the roof up.
    for (px, py) in ((tx - h, ty - h), (tx + h, ty - h), (tx + h, ty + h), (tx - h, ty + h)):
        geo.box("om_planks_dark", (px - 0.6, py - 0.6, F0), (px + 0.6, py + 0.6, TOWER_TOP))
    for z in np.arange(base, TOWER_TOP, 6):
        for a, b in (((tx - h, ty - h), (tx + h, ty - h)), ((tx + h, ty - h), (tx + h, ty + h)),
                     ((tx + h, ty + h), (tx - h, ty + h)), ((tx - h, ty + h), (tx - h, ty - h))):
            geo.beam("om_planks_dark", (*a, z), (*b, z + 6), 0.35)
    # The top room: a floor, low walls with wide openings, a balcony all round, a roof.
    z0 = TOWER_TOP
    geo.box("om_planks", (tx - h - 3, ty - h - 3, z0 - 0.5), (tx + h + 3, ty + h + 3, z0))
    for a, b in (((tx - h, ty - h), (tx + h, ty - h)), ((tx + h, ty - h), (tx + h, ty + h)),
                 ((tx + h, ty + h), (tx - h, ty + h)), ((tx - h, ty + h), (tx - h, ty - h))):
        geo.wall("om_planks_light", a, b, z0, z0 + 5.5, 0.5, [(h - 2.5, h + 2.5, 0, 4.2)])
    geo.box("om_planks_dark", (tx - h - 3.5, ty - h - 3.5, z0 + 5.5), (tx + h + 3.5, ty + h + 3.5, z0 + 6))
    geo.cone("om_thatch", (tx, ty), h * 1.7, z0 + 6, z0 + 14, 4)


def bridges(geo):
    """Two plank bridges from the deck to the shore: south (the way in) and east."""
    for direction in ((0, -1), (1, 0)):
        d = np.array(direction, np.float64)
        start = np.array([CX, CY]) + d * (R_DECK - 0.3)
        end = np.array([CX, CY]) + d * (island.LAGOON_R + 30)
        side = np.array([-d[1], d[0]])
        length = np.linalg.norm(end - start)
        k = int(length / 2)
        for i in range(k):
            a = start + d * (i * length / k)
            b = start + d * ((i + 1) * length / k)
            # A gentle arch over the water.
            t0, t1 = i / k, (i + 1) / k
            z0 = F0 + 2.5 * math.sin(math.pi * t0) - 0.3 * t0
            z1 = F0 + 2.5 * math.sin(math.pi * t1) - 0.3 * t1
            ground = island.height(*b)
            if t1 > 0.75 and ground > z1:
                break
            c0 = [a + side * 3, a - side * 3]
            c1 = [b + side * 3, b - side * 3]
            q = np.array([[*c0[1], z0], [*c1[1], z1], [*c1[0], z1], [*c0[0], z0]])
            n = np.cross(q[1] - q[0], q[3] - q[0]); n /= np.linalg.norm(n)
            if n[2] < 0:
                q = q[[0, 3, 2, 1]]; n = -n
            uv = np.array([[0, 0], [0.5, 0], [0.5, 1.5], [0, 1.5]], np.float32)
            geo.tri_quad("om_planks_light", q, list(n), uv, solid=True)
            # Rope posts.
            if i % 2 == 0:
                for s in (-1, 1):
                    p = a + side * 3.2 * s
                    geo.box("om_planks_dark", (p[0] - 0.15, p[1] - 0.15, z0 - 0.2), (p[0] + 0.15, p[1] + 0.15, z0 + 1.5))
        # The bridge's ends onto the beach.


def torch(geo, x, y, z0, height=3.2):
    """A tiki torch: a pole, a basket, a flame that glows."""
    geo.cylinder("om_planks_dark", (x, y), 0.18, z0, z0 + height, 6)
    geo.cylinder("om_thatch", (x, y), 0.4, z0 + height, z0 + height + 0.6, 8)
    geo.cone("om_orange", (x, y), 0.36, z0 + height + 0.6, z0 + height + 1.6, 8, glow=[1.6, 1.1, 0.5])
    geo.cone("om_yellow", (x, y), 0.2, z0 + height + 0.6, z0 + height + 1.25, 8, glow=[1.8, 1.6, 0.8])
    geo.light((x, y, z0 + height + 1.0), (1.5, 0.95, 0.45), 9)


