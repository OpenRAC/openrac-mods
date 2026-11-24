"""The museum mod's live effects: its sprites (mod/fx/*.glb and .png, drawn by the mod as props)
and the places of the effects that come from the house and the exhibits (light shafts, screens).

    python3 tools/fx.py        # writes mod/fx/

Each sprite is one square of 1 unit in its x and y, facing +z, with a picture of its own: the mod
turns, stretches, tints and blends it (a glow added, foam laid on the water).
"""

import json
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

OUT = HERE.parent / "mod" / "fx"
SIZE = 128

# Where the sun is, as the light bake has it (make_museum.py's sun: azimuth -35, 40 degrees up).
SUN_UP, SUN_FROM = 40.0, -35.0


def sun_down():
    """The unit way the sunlight travels (down and away from the sun)."""
    e, a = math.radians(SUN_UP), math.radians(SUN_FROM)
    return np.array([-math.cos(e) * math.cos(a), -math.cos(e) * math.sin(a), -math.sin(e)])


def grid():
    t = (np.arange(SIZE) + 0.5) / SIZE * 2 - 1
    return np.meshgrid(t, t)


def picture(alpha, rgb=(255, 255, 255)):
    a = np.clip(alpha, 0, 1)
    img = np.zeros((SIZE, SIZE, 4), np.uint8)
    img[..., :3] = rgb
    img[..., 3] = (a * 255).round().astype(np.uint8)
    return Image.fromarray(img, "RGBA")


def glow():
    x, y = grid()
    r = np.hypot(x, y)
    return picture(np.exp(-(r * 2.2) ** 2) * (r < 1))


def star():
    x, y = grid()
    r = np.hypot(x, y)
    rays = np.exp(-np.abs(x) * 26) * np.exp(-np.abs(y) * 2.6) + np.exp(-np.abs(y) * 26) * np.exp(-np.abs(x) * 2.6)
    return picture((rays + np.exp(-(r * 5) ** 2)) * (r < 1))


def shaft():
    """A beam's cross-section along x, soft at its sides and ends (either end may be the top)."""
    x, y = grid()
    side = np.cos(np.clip(x, -1, 1) * math.pi / 2) ** 2
    ends = np.clip((1 - np.abs(y)) * 4, 0, 1) ** 1.5
    return picture(side * ends * 0.9)


def foam():
    """A band of foam along x: thick froth at its leading edge (+y, the shore side), lace behind it,
    soft at its ends."""
    rng = np.random.default_rng(7)
    x, y = grid()
    noise = np.zeros((SIZE, SIZE))
    for scale, w in ((4, 0.35), (8, 0.3), (16, 0.2), (32, 0.15)):
        n = Image.fromarray((rng.random((scale, scale)) * 255).astype(np.uint8))
        noise += w * np.asarray(n.resize((SIZE, SIZE), Image.BICUBIC), np.float64) / 255
    edge = np.exp(-((y - 0.35) * 2.6) ** 2)
    lace = np.clip((noise - 0.36) * 4.0, 0, 1) * np.clip((0.8 - np.abs(y + 0.1)) * 2.0, 0, 1)
    ends = np.clip((1 - np.abs(x)) * 3, 0, 1)
    return picture(np.clip(edge * (0.55 + 0.6 * noise) + lace * 0.8, 0, 1) * ends)


def rect():
    """A screen's face: even, soft only at its very edge."""
    x, y = grid()
    edge = np.clip((1 - np.maximum(np.abs(x), np.abs(y))) * 12, 0, 1)
    return picture(edge)


def tile():
    """A gold tile's glow: a soft square ring, brightest at its edge."""
    x, y = grid()
    d = np.maximum(np.abs(x), np.abs(y))
    return picture(np.exp(-((d - 0.82) * 9) ** 2) + 0.25 * (d < 0.82), (255, 214, 120))


SPRITES = {"glow": glow, "star": star, "shaft": shaft, "foam": foam, "rect": rect, "tile": tile}


def write_sprite(name, img, out):
    img.save(out / f"{name}.png", optimize=True)
    P = np.array([[-0.5, -0.5, 0], [0.5, -0.5, 0], [0.5, 0.5, 0], [-0.5, 0.5, 0]], np.float32)
    N = np.tile(np.array([0, 0, 1], np.float32), (4, 1))
    T = np.array([[0, 1], [1, 1], [1, 0], [0, 0]], np.float32)
    I = np.array([0, 1, 2, 0, 2, 3], np.uint32)
    blob = P.tobytes() + N.tobytes() + T.tobytes() + I.tobytes()
    views, off = [], 0
    for part, target in ((P, 34962), (N, 34962), (T, 34962), (I, 34963)):
        views.append({"buffer": 0, "byteOffset": off, "byteLength": part.nbytes, "target": target})
        off += part.nbytes
    j = {"asset": {"version": "2.0", "generator": "openrac-museum tools/fx.py"}, "scene": 0,
         "scenes": [{"nodes": [0]}], "nodes": [{"name": name, "mesh": 0}],
         "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                    "indices": 3, "material": 0}]}],
         "materials": [{"name": f"fx_{name}", "alphaMode": "BLEND", "doubleSided": True,
                        "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0,
                                                 "roughnessFactor": 1.0}}],
         "textures": [{"source": 0, "sampler": 0}],
         "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
         "images": [{"uri": f"{name}.png"}], "buffers": [{"byteLength": len(blob)}], "bufferViews": views,
         "accessors": [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
                        "min": [-0.5, -0.5, 0.0], "max": [0.5, 0.5, 0.0]},
                       {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
                       {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
                       {"bufferView": 3, "componentType": 5125, "count": 6, "type": "SCALAR"}]}
    text = json.dumps(j, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    blob += b"\0" * (-len(blob) % 4)
    (out / f"{name}.glb").write_bytes(b"glTF" + struct.pack("<II", 2, 28 + len(text) + len(blob))
                                      + struct.pack("<I", len(text)) + b"JSON" + text
                                      + struct.pack("<I", len(blob)) + b"BIN\0" + blob)


def shafts():
    """Sunbeams through the house's windows that face the sun, and down from the atrium's drum:
    (x, y, z of the window's middle, the beam's unit way, its width, its length)."""
    import house
    down = sun_down()
    out = []
    for s in range(8):
        a, b = house.vert(45 * s - 22.5, house.R_OUT), house.vert(45 * s + 22.5, house.R_OUT)
        d = (b - a) / np.linalg.norm(b - a)
        outward = np.array([*house.ang(45 * s), 0.0])
        if np.dot(-down, outward) < 0.15:
            continue                        # this side is in shade: no sun comes in
        L = house.side_len(house.R_OUT)
        for storey, z0 in enumerate((house.F0, house.F1)):
            if storey == 0 and s in house.DOORS:
                holes = [(3.0, 6.0, 1.6, 5.6), (L - 6.0, L - 3.0, 1.6, 5.6)]
            elif storey == 1:
                holes = [(3.0, 7.5, 1.4, 5.8), (L - 7.5, L - 3.0, 1.4, 5.8)]
            else:
                holes = [(3.5, 8.5, 1.4, 6.0), (L - 8.5, L - 3.5, 1.4, 6.0)]
            for h0, h1, hb, ht in holes:
                c = a + d * (h0 + h1) / 2
                z = z0 + (hb + ht) / 2
                # Down to the floor along the sun's way.
                length = (z - z0) / -down[2] + 1.0
                out.append((float(c[0]), float(c[1]), float(z), *map(float, down), float(h1 - h0) * 0.8, float(length)))
    # The drum: one beam down through the atrium from its sunny side.
    up = -down
    p = np.array([house.CX, house.CY, house.TOP + 4.0]) + np.array([up[0], up[1], 0]) / np.hypot(up[0], up[1]) * (house.R_IN - 1.5)
    length = (p[2] - house.F0) / -down[2]
    out.append((*map(float, p), *map(float, down), 4.5, float(length)))
    return out


def coast(step=1.0, spacing=4.5):
    """Points round the island's waterline, SPACING apart: (x, y, the way out to sea in radians,
    whether the shore there is steep rock (1) or a beach (0))."""
    import island
    cx, cy = island.CENTRE
    xs = np.arange(cx - 400, cx + 400, step)
    ys = np.arange(cy - 400, cy + 400, step)
    X, Y = np.meshgrid(xs, ys)
    H = island.height(X, Y)
    wet = H < island.SEA
    edge = wet & ~(np.roll(wet, 1, 0) & np.roll(wet, -1, 0) & np.roll(wet, 1, 1) & np.roll(wet, -1, 1))
    gy, gx = np.gradient(H, step)
    out, taken = [], {}
    order = np.argwhere(edge)
    np.random.default_rng(3).shuffle(order)
    for i, j in order:
        x, y = float(X[i, j]), float(Y[i, j])
        cell = (int(x // spacing), int(y // spacing))
        near = [q for dx in (-1, 0, 1) for dy in (-1, 0, 1) for q in taken.get((cell[0] + dx, cell[1] + dy), [])]
        if any((x - a) ** 2 + (y - b) ** 2 < spacing ** 2 for a, b in near):
            continue
        g = np.array([gx[i, j], gy[i, j]])
        if np.hypot(*g) < 1e-3:
            continue
        taken.setdefault(cell, []).append((x, y))
        steep = 1.0 if np.hypot(*g) > 1.2 else 0.0
        out.append((x, y, float(math.atan2(-g[1], -g[0])), steep))
    return out


def screens(signs):
    """The exhibits' screens (signs whose key is a screen's): (x, y, z, facing in degrees, w, h)."""
    out = []
    for key, _title, _sub, centre, face, w, h in signs:
        if centre is not None and (key.startswith("om_scr_") or key.startswith("om_screen_")):
            out.append((*map(float, centre), float(face), float(w), float(h)))
    return out


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, make in SPRITES.items():
        write_sprite(name, make(), OUT)
    print(f"wrote {len(SPRITES)} sprites into {OUT}")


if __name__ == "__main__":
    main()
