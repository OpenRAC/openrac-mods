#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Makes the Ice Gun's meshes and pictures (assets/*.glb, assets/*.png) from nothing but the
# numbers below: a soft glow sprite, a streak for the liquid nitrogen stream, a crystal block that
# the enemies freeze in, and a shard for it to shatter into. All of it is made here, procedurally;
# nothing comes from any game's files. Run: python3 tools/make_ice_meshes.py
import json
import math
import os
import struct
import sys
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")


def png(path, width, height, pixel):
    """Writes an RGBA PNG; pixel(x, y) returns (r, g, b, a) in 0..255."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(bytes(pixel(x, y)))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def clamp(v):
    return max(0, min(255, int(round(v))))


def glow_pixel(size):
    # A soft round glow: white at the centre, blue-white falling to clear at the edge.
    def pixel(x, y):
        dx = (x + 0.5) / size * 2 - 1
        dy = (y + 0.5) / size * 2 - 1
        d = math.sqrt(dx * dx + dy * dy)
        a = max(0.0, 1.0 - d)
        a = a * a * (3 - 2 * a)
        core = max(0.0, 1.0 - d * 1.8)
        return (clamp(170 + 85 * core), clamp(215 + 40 * core), 255, clamp(255 * a))

    return pixel


def streak_pixel(height):
    # Across the stream's width: a white core, a bright band, a wide soft glow (RAC3's core, band
    # and glow of the Lava Gun's ribbon), the same along its length.
    def pixel(x, y):
        v = abs((y + 0.5) / height * 2 - 1)
        glow = max(0.0, 1.0 - v) ** 2.2
        band = max(0.0, 1.0 - v * 1.7) ** 1.6
        core = max(0.0, 1.0 - v * 3.4) ** 1.2
        r = 40 + 130 * band + 85 * core
        g = 120 + 100 * band + 35 * core
        return (clamp(r), clamp(g), 255, clamp(255 * min(1.0, glow * 0.55 + band * 0.5 + core * 0.6)))

    return pixel


def ice_pixel(x, y):
    # Eight flat shades of a crystal's faces, light to deep, each a little clear.
    shades = [
        (240, 250, 255, 215),
        (196, 232, 255, 195),
        (160, 215, 255, 200),
        (222, 243, 255, 205),
        (140, 200, 250, 195),
        (180, 224, 255, 198),
        (250, 254, 255, 225),
        (150, 208, 252, 190),
    ]
    return shades[min(7, x // 4)]


def glb(path, positions, uvs, indices, texture_uri, outward=False):
    """A one-mesh .glb: positions (x, y, z), uvs, triangle indices, flat normals, one textured
    material (white, double sided, blended)."""
    # Flat normals: every triangle gets its own vertices.
    p, n, t = [], [], []
    for a, b, c in zip(indices[0::3], indices[1::3], indices[2::3]):
        va, vb, vc = positions[a], positions[b], positions[c]
        ux, uy, uz = vb[0] - va[0], vb[1] - va[1], vb[2] - va[2]
        wx, wy, wz = vc[0] - va[0], vc[1] - va[1], vc[2] - va[2]
        nx, ny, nz = uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx
        # A mesh around the origin: every face turned to look away from it, so each is lit the
        # same way whichever way the author wound it.
        if outward and nx * (va[0] + vb[0] + vc[0]) + ny * (va[1] + vb[1] + vc[1]) + nz * (va[2] + vb[2] + vc[2]) < 0:
            b, c = c, b
            nx, ny, nz = -nx, -ny, -nz
        length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
        for k in (a, b, c):
            p.append(positions[k])
            n.append((nx / length, ny / length, nz / length))
            t.append(uvs[k])
    blob = b"".join(struct.pack("<3f", *v) for v in p)
    blob += b"".join(struct.pack("<3f", *v) for v in n)
    blob += b"".join(struct.pack("<2f", *v) for v in t)
    count = len(p)
    lo = [min(v[i] for v in p) for i in range(3)]
    hi = [max(v[i] for v in p) for i in range(3)]
    doc = {
        "asset": {"version": "2.0", "generator": "ng-plus-plus make_ice_meshes.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": os.path.splitext(os.path.basename(path))[0]}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "material": 0, "mode": 4}]}],
        "materials": [{
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0, "roughnessFactor": 1},
            "doubleSided": True,
            "alphaMode": "BLEND",
        }],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"uri": texture_uri}],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": count * 12, "target": 34962},
            {"buffer": 0, "byteOffset": count * 12, "byteLength": count * 12, "target": 34962},
            {"buffer": 0, "byteOffset": count * 24, "byteLength": count * 8, "target": 34962},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": count, "type": "VEC3", "min": lo, "max": hi},
            {"bufferView": 1, "componentType": 5126, "count": count, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": count, "type": "VEC2"},
        ],
    }
    j = json.dumps(doc, separators=(",", ":")).encode()
    j += b" " * (-len(j) % 4)
    blob += b"\0" * (-len(blob) % 4)
    doc["buffers"][0]["byteLength"] = len(blob)
    j = json.dumps(doc, separators=(",", ":")).encode()
    j += b" " * (-len(j) % 4)
    total = 12 + 8 + len(j) + 8 + len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(j), b"JSON") + j)
        f.write(struct.pack("<I4s", len(blob), b"BIN\0") + blob)


def quad(x0, x1, y0, y1):
    pos = [(x0, y0, 0.0), (x1, y0, 0.0), (x1, y1, 0.0), (x0, y1, 0.0)]
    uv = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    return pos, uv, [0, 1, 2, 0, 2, 3]


def crystal():
    # A six-sided crystal block: a prism tapering to a point at both ends, taller than wide (1 tall,
    # 1 across), its faces each one flat shade of ice.png.
    sides = 6
    rim = 0.5
    pos = [(0.0, 0.0, 0.62), (0.0, 0.0, -0.62)]
    for k in range(sides):
        a = 2 * math.pi * k / sides + 0.3
        r = rim * (0.85 + 0.15 * ((k * 7) % 3) / 2.0)
        pos.append((r * math.cos(a), r * math.sin(a), 0.32))
        pos.append((r * math.cos(a), r * math.sin(a), -0.32))
    idx = []
    for k in range(sides):
        k2 = (k + 1) % sides
        t0, b0, t1, b1 = 2 + 2 * k, 3 + 2 * k, 2 + 2 * k2, 3 + 2 * k2
        idx += [t0, b0, b1, t0, b1, t1]  # the side
        idx += [0, t0, t1]               # the point on top
        idx += [1, b1, b0]               # the point below
    # Each face a shade: assign the uv of its first vertex by face number (flat normals split them).
    uvs_out = []
    for face in range(len(idx) // 3):
        u = ((face * 3 + 1) % 8) / 8.0 + 1.0 / 16.0
        for _ in range(3):
            uvs_out.append((u, 0.5))
    return pos, uvs_out, idx


def shard():
    # A long four-sided shard (a bipyramid) along x, 1 long.
    pos = [(-0.5, 0, 0), (0.5, 0, 0), (-0.15, 0.2, 0), (-0.15, 0, 0.2), (-0.15, -0.2, 0), (-0.15, 0, -0.2)]
    idx = []
    for k in range(4):
        a, b = 2 + k, 2 + (k + 1) % 4
        idx += [0, b, a, 1, a, b]
    uvs_out = []
    for face in range(len(idx) // 3):
        u = ((face * 3 + 2) % 8) / 8.0 + 1.0 / 16.0
        for _ in range(3):
            uvs_out.append((u, 0.5))
    return pos, uvs_out, idx


def main():
    os.makedirs(OUT, exist_ok=True)
    png(os.path.join(OUT, "ice_glow.png"), 64, 64, glow_pixel(64))
    png(os.path.join(OUT, "ice_streak.png"), 8, 64, streak_pixel(64))
    png(os.path.join(OUT, "ice_facets.png"), 32, 2, ice_pixel)
    pos, uv, idx = quad(-0.5, 0.5, -0.5, 0.5)
    glb(os.path.join(OUT, "ice_sprite.glb"), pos, uv, idx, "ice_glow.png")
    pos, uv, idx = quad(0.0, 1.0, -0.5, 0.5)
    # A streak is stretched along x; its picture runs across y.
    glb(os.path.join(OUT, "ice_streak.glb"), pos, uv, idx, "ice_streak.png")
    pos, uvs, idx = crystal()
    glb_expand(os.path.join(OUT, "ice_block.glb"), pos, uvs, idx)
    pos, uvs, idx = shard()
    glb_expand(os.path.join(OUT, "ice_shard.glb"), pos, uvs, idx)


def glb_expand(path, pos, uvs_per_corner, idx):
    # uvs are given per triangle corner (3 per face): expand the vertices to match, then write.
    ex_pos = [pos[i] for i in idx]
    ex_idx = list(range(len(idx)))
    glb(path, ex_pos, uvs_per_corner, ex_idx, "ice_facets.png", outward=True)


if __name__ == "__main__":
    main()
    print("wrote", os.path.normpath(OUT))
