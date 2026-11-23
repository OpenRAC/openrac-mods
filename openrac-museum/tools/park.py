"""The JET-Board park on the east lowland, past the east bridge: a concrete pad with two quarter
pipes, a fun box with kickers, a flat rail, and the board itself on a stand with its gold tile.

The board is ridden with the RAC-Board mod (D-pad Up gets on and off); its model and picture here
are RAC-Board's own (MIT, pictures/jetboard.*). Every top edge is a grind edge for the board.
"""

import json
import math
import struct
from pathlib import Path

import numpy as np

import island

PICTURES_DIR = Path(__file__).resolve().parent.parent / "pictures"

# The pad: its corners and its top. The ground under it is 225 to 231.5.
X0, X1, Y0, Y1 = 704.0, 756.0, 442.0, 476.0
TOP = island.SEA + 8.7
R = 3.6                       # the quarter pipes' radius
SWEEP = 72.0                  # and how far they turn up (degrees)
QP_Y = (446.0, 472.0)
STAND = (744.5, 472.0)        # the board on its stand
TILE = (740.0, 471.4)         # its tile
SIGN = (716.0, 477.6)


def clear(x, y, margin=0.0):
    """False on the park and the ramp up to it: nothing grows there."""
    return not (X0 - 4 - margin < x < X1 + 4 + margin and Y0 - 4 - margin < y < Y1 + 7 + margin)


def station():
    return ("jetboard", TILE[0], TILE[1], TOP)


def concrete():
    """Light grey concrete in big slabs, with dark joints."""
    rng = np.random.default_rng(77)
    n = 256
    v = 168 + rng.normal(0, 6, (n, n))
    v[:, :3] = 120
    v[:3, :] = 120
    rgb = np.stack([v * 1.0, v * 0.99, v * 0.95], -1)
    import textures
    return textures.to_png(rgb)


def load_board():
    """RAC-Board's board: triangles (game axes: x across, y along, z up), normals and UVs."""
    b = (PICTURES_DIR / "jetboard.glb").read_bytes()
    n = struct.unpack("<I", b[12:16])[0]
    j = json.loads(b[20:20 + n])
    data = b[20 + n + 8:]
    prim = j["meshes"][0]["primitives"][0]

    def acc(i):
        a = j["accessors"][i]
        v = j["bufferViews"][a["bufferView"]]
        k = {"VEC3": 3, "VEC2": 2, "SCALAR": 1}[a["type"]]
        kind = {5126: "<f4", 5125: "<u4", 5123: "<u2"}[a["componentType"]]
        out = np.frombuffer(data, kind, a["count"] * k, v.get("byteOffset", 0) + a.get("byteOffset", 0))
        return out.reshape(-1, k) if k > 1 else out

    p = acc(prim["attributes"]["POSITION"])
    nr = acc(prim["attributes"]["NORMAL"])
    uv = acc(prim["attributes"]["TEXCOORD_0"])
    idx = acc(prim["indices"]).reshape(-1, 3) if "indices" in prim else np.arange(len(p)).reshape(-1, 3)
    # glTF is y up: its -z is the game's y.
    swap = lambda a: np.stack([a[:, 0], -a[:, 2], a[:, 1]], 1)
    return swap(p), swap(nr), uv, idx


def board(geo, at, z, heading, scale):
    p, nr, uv, idx = load_board()
    c, s = math.cos(math.radians(heading)), math.sin(math.radians(heading))
    rot = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    pos = (p * scale) @ rot.T + np.array([at[0], at[1], z - p[:, 2].min() * scale])
    nrm = nr @ rot.T
    from build import light
    geo.mesh.add("om_jetboard", pos.astype(np.float32), nrm.astype(np.float32), uv, [light(n) for n in nrm], idx)


def quarter_pipe(geo, x_lip_side, inward):
    """A quarter pipe along y (QP_Y) whose flat starts R in from X_LIP_SIDE, rising towards it.
    INWARD is +1 when the pad lies on +x of the pipe."""
    y0, y1 = QP_Y
    steps = 8
    pts = []
    for k in range(steps + 1):
        t = math.radians(SWEEP * k / steps)
        pts.append((x_lip_side + inward * (R - R * math.sin(t)), TOP + R * (1 - math.cos(t))))
    for (xa, za), (xb, zb) in zip(pts, pts[1:]):
        q = np.array([[xa, y0, za], [xb, y0, zb], [xb, y1, zb], [xa, y1, za]])
        n = np.cross(q[1] - q[0], q[3] - q[0])
        if n[2] < 0:
            q = q[[0, 3, 2, 1]]
            n = -n
        n = n / np.linalg.norm(n)
        uv = np.array([[p[1] / 4, (p[0] + p[2]) / 4] for p in q], np.float32)
        geo.tri_quad("om_concrete", q, list(n), uv)
        # The sides, down to the pad.
        for y, sy in ((y0, -1), (y1, 1)):
            side = np.array([[xa, y, TOP], [xb, y, TOP], [xb, y, zb], [xa, y, za]])
            if np.cross(side[1] - side[0], side[3] - side[0])[1] * sy < 0:
                side = side[[0, 3, 2, 1]]
            geo.tri_quad("om_concrete", side, [0, sy, 0], np.array([[p[0] / 4, p[2] / 4] for p in side], np.float32))
    lip_x, lip_z = pts[-1]
    # The deck behind the lip, its back wall, and the metal coping on the lip.
    deck0, deck1 = sorted((lip_x, lip_x - inward * 3.2))
    geo.box("om_concrete", (deck0, y0, TOP - 0.5), (deck1, y1, lip_z))
    geo.beam("om_rail", (lip_x, y0, lip_z - 0.05), (lip_x, y1, lip_z - 0.05), 0.22, solid=True)


def build(geo, pictures, mats):
    """Draws the park; returns its words' tile spot."""
    pictures["om_concrete"] = concrete()
    mats["om_concrete"] = {"texture": "om_concrete"}
    pictures["om_jetboard"] = (PICTURES_DIR / "jetboard.png").read_bytes()
    mats["om_jetboard"] = {"texture": "om_jetboard", "double": True}

    # The pad, its sides down into the ground.
    low = float(min(island.height(x, y) for x in (X0, X1) for y in (Y0, Y1))) - 1.0
    geo.box("om_concrete", (X0, Y0, low), (X1, Y1, TOP))
    # The ramp up from the bridge's side.
    gz = float(island.height(730.0, Y1 + 6.0))
    geo.slope("om_concrete", 724.0, 736.0, Y1, Y1 + 6.0, TOP, min(gz, TOP) - 0.1, "y", 1.5)

    quarter_pipe(geo, X0 + 4.0, 1)     # west: rises towards -x
    quarter_pipe(geo, X1 - 4.0, -1)    # east: rises towards +x

    # The fun box with a kicker each side; its long top edges are grind edges.
    fb0, fb1, fy0, fy1, fh = 726.0, 734.0, 453.0, 465.0, 1.0
    geo.box("om_concrete", (fb0, fy0, TOP - 0.1), (fb1, fy1, TOP + fh))
    geo.slope("om_concrete", fb0 - 4.5, fb0, fy0, fy1, TOP, TOP + fh, "x", 1.0)
    geo.slope("om_concrete", fb1, fb1 + 4.5, fy0, fy1, TOP + fh, TOP, "x", 1.0)
    for y in (fy0, fy1):
        geo.beam("om_rail", (fb0, y, TOP + fh - 0.05), (fb1, y, TOP + fh - 0.05), 0.16, solid=True)

    # The flat rail on posts, along the south side.
    ry, rz = 447.5, TOP + 0.9
    geo.beam("om_rail", (716.0, ry, rz), (744.0, ry, rz), 0.28, solid=True)
    for x in (717.0, 730.0, 743.0):
        geo.box("om_planks_dark", (x - 0.15, ry - 0.15, TOP), (x + 0.15, ry + 0.15, rz - 0.14), solid=False)

    # The board on its stand, turned to the ramp.
    sx, sy = STAND
    geo.box("om_white", (sx - 1.6, sy - 0.8, TOP), (sx + 1.6, sy + 0.8, TOP + 0.7))
    geo.box("om_gold", (sx - 1.7, sy - 0.9, TOP + 0.7), (sx + 1.7, sy + 0.9, TOP + 0.78))
    board(geo, STAND, TOP + 0.8, 90.0, 2.0)

    import exhibits
    exhibits.floor_picture(geo, "om_info", np.array(TILE), np.array([0.0, -1.0]), 1.6, TOP + 0.1)
    return SIGN
