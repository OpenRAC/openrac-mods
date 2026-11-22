"""The museum: a two-storey wooden house on stilts in the lagoon, 16 empty exhibit rooms round an
atrium, a wrap-round veranda, thatched roofs, two bridges to the shore and a tower whose top room
only gadgets reach."""

import math

import numpy as np

import island

CX, CY = island.CENTRE
F0 = island.HOUSE_FLOOR          # ground floor
STOREY = 7.0
F1 = F0 + STOREY                 # upper floor
TOP = F1 + STOREY                # top of the walls
CELL = 20.0                      # a room is CELL x CELL
HALF = CELL * 1.5                # the house is 3 x 3 cells: 60 x 60
DECK = 7.0                       # the veranda round it
WALL = 0.6
DOOR_W, DOOR_H = 5.0, 4.5

# Rooms by cell (col, row) with col, row in -1..1; (0, 0) is the atrium. Names are what each
# section's sign says; the user fills the rooms later.
GROUND = {(0, -1): ("Welcome", "Open Museum foyer"),
          (-1, -1): ("Decompilation", "from MIPS to C"),
          (-1, 0): ("Matching", "byte for byte"),
          (-1, 1): ("The Port", "native, no emulation"),
          (0, 1): ("Hostgen", "decomp C on the PC"),
          (1, 1): ("Renderer", "GS to OpenGL and Vulkan"),
          (1, 0): ("Launcher", "Tauri, Svelte, Rust"),
          (1, -1): ("Mods and SDK", "make the game yours")}
UPPER = {(0, -1): ("Devlog", "how it was made"),
         (-1, -1): ("OpenRAC Editor", "levels and cutscenes"),
         (-1, 0): ("Cutscenes", "lights, camera, Clank"),
         (-1, 1): ("Android", "the galaxy in your pocket"),
         (0, 1): ("Enhanced", "graphics beyond the PS2"),
         (1, 1): ("Speedrun Corner", "every bug kept"),
         (1, 0): ("Multiplayer", "co-op across the galaxy"),
         (1, -1): ("Thank You", "contributors and fans")}

TOWER = (CX + HALF - 6, CY + HALF - 6)   # the tower on the house's north-east corner
TOWER_HALF = 6.0
TOWER_TOP = TOP + 22.0                   # the secret room's floor


def cell_box(col, row):
    x0 = CX + (col - 0.5) * CELL
    y0 = CY + (row - 0.5) * CELL
    return x0, y0, x0 + CELL, y0 + CELL


def build(geo, signs, posters):
    """Adds the house to GEO. SIGNS and POSTERS get (texture name, title, sub, centre, facing, w, h)."""
    x0, y0, x1, y1 = CX - HALF, CY - HALF, CX + HALF, CY + HALF

    # Stilts and the deck.
    floor_bottom = island.SEA - 8
    for gx in np.arange(x0 - DECK, x1 + DECK + 0.1, 10):
        for gy in np.arange(y0 - DECK, y1 + DECK + 0.1, 10):
            if x0 + 2 < gx < x1 - 2 and y0 + 2 < gy < y1 - 2:
                continue
            geo.cylinder("om_planks_dark", (gx, gy), 0.7, floor_bottom, F0 - 0.4, 8)
    geo.box("om_planks_light", (x0 - DECK, y0 - DECK, F0 - 0.6), (x1 + DECK, y1 + DECK, F0))
    # Railing round the deck, with gaps for the two bridges (south and east) and the dive (west).
    rail_z = F0 + 1.6
    edges = [((x0 - DECK, y0 - DECK), (x1 + DECK, y0 - DECK), "south"),
             ((x1 + DECK, y0 - DECK), (x1 + DECK, y1 + DECK), "east"),
             ((x1 + DECK, y1 + DECK), (x0 - DECK, y1 + DECK), "north"),
             ((x0 - DECK, y1 + DECK), (x0 - DECK, y0 - DECK), "west")]
    for a, b, side in edges:
        a, b = np.array(a), np.array(b)
        length = np.linalg.norm(b - a)
        d = (b - a) / length
        gap = (length / 2 - 4, length / 2 + 4) if side in ("south", "east", "west") else None
        for s in np.arange(0, length + 0.1, 2.5):
            if gap and gap[0] < s < gap[1]:
                continue
            p = a + d * s
            geo.box("om_planks_dark", (p[0] - 0.15, p[1] - 0.15, F0), (p[0] + 0.15, p[1] + 0.15, rail_z))
        spans = [(0, gap[0]), (gap[1], length)] if gap else [(0, length)]
        for s0, s1 in spans:
            pa, pb = a + d * s0, a + d * s1
            geo.beam("om_planks", (*pa, rail_z), (*pb, rail_z), 0.3)
            # An invisible-height solid rail so the hero does not walk off by accident.
            geo.wall("om_planks", pa, pb, F0, F0 + 1.0, 0.2, solid=True)

    # The outer walls of both storeys, with a door in the middle of each side's cells and windows.
    for storey, (z0, z1) in enumerate(((F0, F1), (F1, TOP))):
        for side in range(4):
            if side == 0:
                a, b = (x0, y0), (x1, y0)
            elif side == 1:
                a, b = (x1, y0), (x1, y1)
            elif side == 2:
                a, b = (x1, y1), (x0, y1)
            else:
                a, b = (x0, y1), (x0, y0)
            holes = []
            for k in range(3):
                mid = CELL * k + CELL / 2
                if storey == 0 and ((side == 0 and k == 1) or (side == 1 and k == 1)):
                    holes.append((mid - DOOR_W / 2 - 1, mid + DOOR_W / 2 + 1, 0, DOOR_H + 0.8))
                else:
                    holes.append((mid - 3.5, mid + 3.5, 2.2, 5.2))       # a wide window
            geo.wall("om_planks", a, b, z0, z1, WALL, holes)
        # The beam between storeys and the corner posts, darker.
        geo.box("om_planks_dark", (x0 - 0.5, y0 - 0.5, z1 - 0.5), (x1 + 0.5, y1 + 0.5, z1), solid=False)
    for (px, py) in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        geo.box("om_planks_dark", (px - 0.8, py - 0.8, F0), (px + 0.8, py + 0.8, TOP + 0.5))

    # Inner walls between the cells, each with a doorway, both storeys.
    for storey, (z0, z1) in enumerate(((F0, F1), (F1, TOP))):
        for k in (-0.5, 0.5):
            # Walls along y at x = CX + k*CELL, and along x at y = CY + k*CELL.
            for j in (-1, 0, 1):
                ya, yb = CY + (j - 0.5) * CELL, CY + (j + 0.5) * CELL
                xa, xb = CX + (j - 0.5) * CELL, CX + (j + 0.5) * CELL
                mid = CELL / 2
                door = [(mid - DOOR_W / 2, mid + DOOR_W / 2, 0, DOOR_H)]
                atrium_side = (j == 0)
                if storey == 1 and atrium_side:
                    # The upper rooms open onto the atrium's balcony through a wide arch.
                    door = [(mid - 6, mid + 6, 0, 5.5)]
                geo.wall("om_planks_light", (CX + k * CELL, ya), (CX + k * CELL, yb), z0, z1, WALL * 0.7, door)
                geo.wall("om_planks_light", (xa, CY + k * CELL), (xb, CY + k * CELL), z0, z1, WALL * 0.7, door)

    # Floors: the upper floor everywhere but the atrium; a balcony ring round the atrium.
    ax0, ay0, ax1, ay1 = cell_box(0, 0)
    for col in (-1, 0, 1):
        for row in (-1, 0, 1):
            bx0, by0, bx1, by1 = cell_box(col, row)
            geo.box("om_planks_light", (bx0, by0, F0), (bx1, by1, F0 + 0.05), solid=False)
            if (col, row) == (-1, 0):
                # A stairwell along the west wall (the stairs come up at its north end).
                geo.box("om_planks", (bx0 + 5.5, by0, F1 - 0.4), (bx1, by1, F1))
                geo.box("om_planks", (bx0, by1 - 2, F1 - 0.4), (bx0 + 5.5, by1, F1))
                geo.box("om_planks", (bx0, by0, F1 - 0.4), (bx0 + 5.5, by0 + 1.5, F1))
                geo.wall("om_planks_dark", (bx0 + 5.5, by0 + 1.5), (bx0 + 5.5, by1 - 2), F1, F1 + 1.2, 0.25)
            elif (col, row) != (0, 0):
                geo.box("om_planks", (bx0, by0, F1 - 0.4), (bx1, by1, F1))
    bw = 3.5
    for (lo, hi) in (((ax0, ay0), (ax1, ay0 + bw)), ((ax0, ay1 - bw), (ax1, ay1)),
                     ((ax0, ay0 + bw), (ax0 + bw, ay1 - bw)), ((ax1 - bw, ay0 + bw), (ax1, ay1 - bw))):
        geo.box("om_planks", (lo[0], lo[1], F1 - 0.4), (hi[0], hi[1], F1))
    # The balcony's railing.
    ix0, iy0, ix1, iy1 = ax0 + bw, ay0 + bw, ax1 - bw, ay1 - bw
    for a, b in (((ix0, iy0), (ix1, iy0)), ((ix1, iy0), (ix1, iy1)), ((ix1, iy1), (ix0, iy1)), ((ix0, iy1), (ix0, iy0))):
        geo.wall("om_planks_dark", a, b, F1, F1 + 1.2, 0.25)

    # The stairs: in the Matching room (west), a straight flight up along +y.
    sx0, sy0, sx1, sy1 = cell_box(-1, 0)
    steps = 14
    run = (sy1 - sy0 - 4) / steps
    for k in range(steps):
        z = F0 + STOREY * (k + 1) / steps
        geo.box("om_planks_dark", (sx0 + 1, sy0 + 2 + k * run, F0), (sx0 + 5, sy0 + 2 + (k + 1) * run, z), solid=False)
    geo.slope("om_planks_dark", sx0 + 1, sx0 + 5, sy0 + 2, sy1 - 2, F0, F1, "y", 0.2, solid=True)
    # The stair's hole in the upper floor: the floor box above is cut by a smaller box layout.
    # (The upper floor over the stairs is left as a landing at its top end.)

    # The atrium: four great posts and a skylight lantern.
    for (px, py) in ((ax0 + 1, ay0 + 1), (ax1 - 1, ay0 + 1), (ax1 - 1, ay1 - 1), (ax0 + 1, ay1 - 1)):
        geo.box("om_planks_dark", (px - 0.6, py - 0.6, F0), (px + 0.6, py + 0.6, TOP + 6), solid=True)

    roofs(geo, x0, y0, x1, y1, ax0, ay0, ax1, ay1)
    decorate(geo, x0, y0, x1, y1)
    tower(geo)
    bridges(geo)

    # The rooms' signs (over the door to the atrium side) and their empty exhibit boards.
    for storey, rooms in ((0, GROUND), (1, UPPER)):
        z0 = F0 if storey == 0 else F1
        for (col, row), (title, sub) in rooms.items():
            bx0, by0, bx1, by1 = cell_box(col, row)
            mx, my = (bx0 + bx1) / 2, (by0 + by1) / 2
            key = f"om_sign_{storey}_{col + 1}{row + 1}"
            # The sign on the wall facing into the room from the atrium side's opposite wall.
            facing = math.degrees(math.atan2(-row, -col)) if (col, row) != (0, 0) else 0
            inward = np.array([-col, -row], np.float64)
            inward = inward / (np.linalg.norm(inward) or 1)
            back = np.array([mx, my]) - inward * (CELL / 2 - WALL)
            signs.append((key, title, sub, (back[0], back[1], z0 + 5.2), facing, 8, 3))
            # Four empty boards on the side walls and three plinths.
            side = np.array([-inward[1], inward[0]])
            for k, off in enumerate((-5, 5)):
                for s in (-1, 1):
                    p = np.array([mx, my]) + side * s * (CELL / 2 - WALL) + inward * off
                    face = math.degrees(math.atan2(-side[1] * s, -side[0] * s))
                    posters.append((f"{key}_b{k}{(s + 1) // 2}", title, (p[0], p[1], z0 + 3.0), face, 4, 5))
            for k in (-1, 0, 1):
                p = np.array([mx, my]) + side * k * 5 - inward * 1.5
                plinth(geo, p, z0)

    # The atrium's centre: a round pool of light, the logo turns above it (an animated prop).
    geo.cylinder("om_gold", (CX, CY), 3.2, F0, F0 + 0.6, 24, solid=True)
    geo.cylinder("om_white", (CX, CY), 2.6, F0 + 0.6, F0 + 1.0, 24, solid=True)


# Where the deck's planters stand (scatter.decor puts Pokitaru's bushes in them).
def planters():
    x0, y0, x1, y1 = CX - HALF, CY - HALF, CX + HALF, CY + HALF
    out = []
    for (px, py) in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        out.append((px + math.copysign(DECK - 3, px - CX), py + math.copysign(DECK - 3, py - CY)))
    for d in (-1, 1):                      # beside the south and east doors
        out.append((CX + d * 7, y0 - DECK + 2.5))
        out.append((x1 + DECK - 2.5, CY + d * 7))
    return out


def torch(geo, x, y, z0, height=3.2):
    """A tiki torch: a pole, a basket, a flame that glows."""
    geo.cylinder("om_planks_dark", (x, y), 0.18, z0, z0 + height, 6)
    geo.cylinder("om_thatch", (x, y), 0.4, z0 + height, z0 + height + 0.6, 8)
    geo.cone("om_orange", (x, y), 0.36, z0 + height + 0.6, z0 + height + 1.6, 8, glow=[1.6, 1.1, 0.5])
    geo.cone("om_yellow", (x, y), 0.2, z0 + height + 0.6, z0 + height + 1.25, 8, glow=[1.8, 1.6, 0.8])


def decorate(geo, x0, y0, x1, y1):
    """Planters, tiki torches and banners: the house's holiday look."""
    for (px, py) in planters():
        geo.box("om_planks_dark", (px - 1.3, py - 1.3, F0), (px + 1.3, py + 1.3, F0 + 0.9), solid=False)
        geo.box("om_grass", (px - 1.1, py - 1.1, F0 + 0.9), (px + 1.1, py + 1.1, F0 + 0.95), solid=False)
    # Torches on the deck's corners and by each bridge.
    for (px, py) in ((x0 - DECK + 0.6, y0 - DECK + 0.6), (x1 + DECK - 0.6, y0 - DECK + 0.6),
                     (x1 + DECK - 0.6, y1 + DECK - 0.6), (x0 - DECK + 0.6, y1 + DECK - 0.6),
                     (CX - 4.6, y0 - DECK + 0.6), (CX + 4.6, y0 - DECK + 0.6),
                     (x1 + DECK - 0.6, CY - 4.6), (x1 + DECK - 0.6, CY + 4.6)):
        torch(geo, px, py, F0)
    # Long banners between the upper windows, on every side, in the museum's colours.
    colours = ("om_red", "om_teal", "om_yellow", "om_orange")
    for side, (a, b, out) in enumerate((((x0, y0), (x1, y0), (0, -1)), ((x1, y0), (x1, y1), (1, 0)),
                                        ((x1, y1), (x0, y1), (0, 1)), ((x0, y1), (x0, y0), (-1, 0)))):
        a, b, out = np.array(a, float), np.array(b, float), np.array(out, float)
        d = (b - a) / np.linalg.norm(b - a)
        for k, s in enumerate((3.2, CELL * 3 - 3.2)):      # by the corners: clear of the windows and signs
            p = a + d * s + out * (WALL + 0.12)
            m = colours[(side + k) % 4]
            geo.oriented_box(m, p - d * 1.1, p + d * 1.1, out, 0.06, F1 + 0.4, TOP - 0.8, solid=False)
            geo.oriented_box("om_gold", p - d * 1.25, p + d * 1.25, out, 0.2, TOP - 0.8, TOP - 0.5, solid=False)
    # Lanterns hung under the eaves on the porch corners: glowing balls of light.
    for (px, py) in ((x0 - 2, y0 - 2), (x1 + 2, y0 - 2), (x1 + 2, y1 + 2), (x0 - 2, y1 + 2)):
        geo.cylinder("om_planks_dark", (px, py), 0.05, TOP - 2.5, TOP, 4)
        geo.cylinder("om_red", (px, py), 0.5, TOP - 3.6, TOP - 2.5, 8)
        geo.cone("om_yellow", (px, py), 0.3, TOP - 3.4, TOP - 2.7, 8, glow=[1.8, 1.5, 0.8])


def plinth(geo, p, z0):
    geo.box("om_white", (p[0] - 1, p[1] - 1, z0), (p[0] + 1, p[1] + 1, z0 + 1.1))
    geo.box("om_gold", (p[0] - 1.1, p[1] - 1.1, z0 + 1.1), (p[0] + 1.1, p[1] + 1.1, z0 + 1.25))


def roofs(geo, x0, y0, x1, y1, ax0, ay0, ax1, ay1):
    """A hipped thatch roof from the eaves up to the atrium's lantern, and the lantern's own."""
    eave = 5.0
    rise = 9.0
    e0, f0, e1, f1 = x0 - eave, y0 - eave, x1 + eave, y1 + eave
    i0, j0, i1, j1 = ax0 - 2, ay0 - 2, ax1 + 2, ay1 + 2
    zt, zr = TOP - 0.5, TOP - 0.5 + rise
    quads = [
        ([e0, f0, zt], [e1, f0, zt], [i1, j0, zr], [i0, j0, zr]),
        ([e1, f0, zt], [e1, f1, zt], [i1, j1, zr], [i1, j0, zr]),
        ([e1, f1, zt], [e0, f1, zt], [i0, j1, zr], [i1, j1, zr]),
        ([e0, f1, zt], [e0, f0, zt], [i0, j0, zr], [i0, j1, zr]),
    ]
    for q in quads:
        q = np.array(q, np.float64)
        n = np.cross(q[1] - q[0], q[3] - q[0]); n /= np.linalg.norm(n)
        uv = np.array([[p[0] / 6, p[1] / 6] for p in q], np.float32)
        geo.tri_quad("om_thatch", q, list(n), uv, solid=True, both=True)
    # The lantern: posts with open sides (light in), and a pointed roof.
    lz = zr
    for (px, py) in ((i0, j0), (i1, j0), (i1, j1), (i0, j1)):
        geo.box("om_planks_dark", (px - 0.5, py - 0.5, lz - 1), (px + 0.5, py + 0.5, lz + 6))
    geo.box("om_planks_dark", (i0 - 0.6, j0 - 0.6, lz + 5.5), (i1 + 0.6, j1 + 0.6, lz + 6.2))
    peak = [(i0 + i1) / 2, (j0 + j1) / 2, lz + 15]
    ring = [[i0 - 3, j0 - 3, lz + 6], [i1 + 3, j0 - 3, lz + 6], [i1 + 3, j1 + 3, lz + 6], [i0 - 3, j1 + 3, lz + 6]]
    for k in range(4):
        a, b = np.array(ring[k]), np.array(ring[(k + 1) % 4])
        p = np.array([a, b, peak, peak], np.float64)
        n = np.cross(b - a, np.array(peak) - a); n /= np.linalg.norm(n)
        uv = np.array([[0, 0], [4, 0], [2, 3], [2, 3]], np.float32)
        geo.tri_quad("om_thatch", p, list(n), uv, solid=True, both=True)


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
        start = np.array([CX, CY]) + d * (HALF + DECK)
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
