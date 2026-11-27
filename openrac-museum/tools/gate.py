"""The museum's gate sign: a thick lacquered board hung under the gate's arch with OpenRAC's logo
and MUSEUM under it on its face, a gold rim, and a gold cog with a bolt on top. Both faces read:
the shore's and the house's."""

from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import textures
from build import light

TITLE = "OPENRAC"
SECOND = "MUSEUM"
SUBTITLE = ""
# OpenRAC's logo for dark backgrounds (OpenRAC's docs/assets/openrac-logo-dark.svg, drawn to a picture).
LOGO = Path(__file__).resolve().parent.parent / "pictures" / "openrac-logo.png"
BOARD = "om_gate_board"
INTER_SEMIBOLD = "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf"


def slab(geo, material, poly, y0, y1, uv=None, side_material=None, glow=None, mirror_back=False):
    """A flat shape POLY [(x, z)] (star-shaped round its middle, counter-clockwise seen from -y)
    made solid between y0 (front, facing -y) and y1 (back). UV(x, z) maps the faces' picture;
    MIRROR_BACK: the back's picture turned round, so it reads from behind too."""
    poly = np.asarray(poly, np.float64)
    c = poly.mean(0)
    uv = uv or (lambda x, z: (x / 4.0, -z / 4.0))
    for y, normal in ((y0, (0.0, -1.0, 0.0)), (y1, (0.0, 1.0, 0.0))):
        pts = np.concatenate([[c], poly])
        p3 = np.array([[x, y, z] for x, z in pts])
        tris = [[0, k + 1, (k + 1) % len(poly) + 1] for k in range(len(poly))]
        if normal[1] > 0:
            tris = [[a, cc, b] for a, b, cc in tris]
        uvs = [uv(x, z) for x, z in pts]
        if mirror_back and normal[1] > 0:
            uvs = [(1.0 - u, v) for u, v in uvs]
        geo.mesh.add(material, p3, [normal] * len(p3), uvs, [glow or light(normal)] * len(p3), tris)
    run = 0.0
    for a, b in zip(poly, np.roll(poly, -1, 0)):
        e = b - a
        n = np.array([e[1], 0.0, -e[0]]); n /= np.linalg.norm(n) or 1
        L = float(np.linalg.norm(e))
        q = [[a[0], y0, a[1]], [b[0], y0, b[1]], [b[0], y1, b[1]], [a[0], y1, a[1]]]
        t = np.array([[run, 0], [run + L, 0], [run + L, (y1 - y0)], [run, (y1 - y0)]], np.float32) / 2.0
        geo.tri_quad(side_material or material, q, list(n), t, solid=False, glow=glow)
        run += L


def cog(geo, cx, y, cz, r, teeth=14, thick=0.4):
    pts = []
    for k in range(teeth * 4):
        a = 2 * np.pi * (k + 0.5) / (teeth * 4)
        rr = r * (1.0 if (k // 2) % 2 == 0 else 0.8)
        pts.append((cx + np.cos(a) * rr, cz + np.sin(a) * rr))
    slab(geo, "om_gold", pts, y - thick / 2, y + thick / 2, uv=lambda x, z: (x / 1.5, z / 1.5))
    # The bolt in its middle: a hex nut, darker, standing out of both faces.
    nut = [(cx + np.cos(np.pi / 6 + k * np.pi / 3) * r * 0.5, cz + np.sin(np.pi / 6 + k * np.pi / 3) * r * 0.5) for k in range(6)]
    slab(geo, "om_planks_dark", nut, y - thick / 2 - 0.2, y + thick / 2 + 0.2, uv=lambda x, z: (x, z))
    hole = [(cx + np.cos(k * np.pi / 4) * r * 0.2, cz + np.sin(k * np.pi / 4) * r * 0.2) for k in range(8)]
    # The nanotech glow in the bolt (over 0.9: the bake keeps it).
    slab(geo, "om_teal", hole, y - thick / 2 - 0.25, y + thick / 2 + 0.25, uv=lambda x, z: (x, z), glow=[0.45, 1.0, 0.95])


def build(geo, signs, gx, gy, top):
    """The sign under the gate's arch at (GX, GY), the arch's posts' tops at TOP."""
    W, H = 16.4, 6.0
    zb, zt = top - 9.2, top - 9.2 + H
    x0, x1 = gx - W / 2, gx + W / 2
    ch = 0.9
    # The board: chamfered corners, a notch-cut crest in the middle of its top for the cog.
    poly = [(x0 + ch, zb), (x1 - ch, zb), (x1, zb + ch), (x1, zt - ch), (x1 - ch, zt),
            (gx + 2.6, zt), (gx + 1.8, zt + 0.7), (gx - 1.8, zt + 0.7), (gx - 2.6, zt), (x0 + ch, zt), (x0, zt - ch), (x0, zb + ch)]
    T = 0.6
    slab(geo, BOARD, poly, gy - T / 2, gy + T / 2,
         uv=lambda x, z: ((x - x0) / W, (zt + 0.7 - z) / (H + 0.7)), side_material="om_planks_dark", mirror_back=True)
    signs.append((BOARD, TITLE, SUBTITLE, None, 0, 0, 0))
    # A gold rim round the board's edge, both faces.
    for y in (gy - T / 2 - 0.06, gy + T / 2 + 0.06):
        for a, b in zip(poly, poly[1:] + poly[:1]):
            a3, b3 = np.array([a[0], y, a[1]]), np.array([b[0], y, b[1]])
            geo.beam("om_gold", a3, b3, 0.16)
    # The cog on the crest, and its ropes up to the arch.
    cog(geo, gx, gy, zt + 1.0, 1.55)
    for x in (gx - 6.4, gx + 6.4):
        geo.beam("om_planks_dark", (x, gy, top + 0.5), (x, gy, zt - 0.1), 0.14)
        geo.box("om_gold", (x - 0.3, gy - T / 2 - 0.1, zt - 0.5), (x + 0.3, gy + T / 2 + 0.1, zt + 0.05), solid=False)
    # Warm light on both faces, from below, as from lanterns at the posts.
    for side in (-1, 1):
        for x in (gx - 4.5, gx + 4.5):
            geo.light((x, gy + side * 3.2, zb - 1.5), (0.9, 0.62, 0.32), 9.0)
    return zb, zt


def board_picture(sub, width=1024, height=400):
    """The board's face in OpenRAC's look (the launcher's STYLE.md): the darkest ink, a soft gold
    light from the top, OpenRAC's logo and MUSEUM in a picked chip under it. Centred on the
    letters "OpenRAC" alone: the feathers over the O would pull it to the left and up."""
    k = 2
    W, H = width * k, height * k
    top = int(H * 0.7 / 6.7)                     # the crest's part of the picture
    ink, amber = np.array([18, 18, 18]), np.array([245, 192, 64])
    yy, xx = np.mgrid[0:H, 0:W]
    light = np.exp(-(((xx - W / 2) / (W * 0.45)) ** 2 + ((yy - top) / (H * 0.5)) ** 2)) * 0.15
    arr = 255 - (255 - ink[None, None]) * (1 - light[..., None] * amber / 255)
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(img)
    logo = Image.open(LOGO).convert("RGBA")
    # The letters' box in the logo picture: the O's ring (x 330-819, y 160-654) to the C.
    lx0, lx1, ly0, ly1 = 330, logo.width, 160, 654
    s = W * 0.46 / (lx1 - lx0)
    logo = logo.resize((int(logo.width * s), int(logo.height * s)), Image.LANCZOS)
    f = ImageFont.truetype(INTER_SEMIBOLD, 112) if Path(INTER_SEMIBOLD).exists() else textures.font(160)
    word, spacing = "MUSEUM", 44
    tw = sum(d.textlength(c, font=f) for c in word) + spacing * (len(word) - 1)
    chip_h, gap = 170, 70
    block = (ly1 - ly0) * s + gap + chip_h
    y0 = top + (H - top - block) / 2                 # the letters' top
    x0 = (W - (lx1 - lx0) * s) / 2                  # the O's left edge
    img.paste(logo, (int(round(x0 - lx0 * s)), int(round(y0 - ly0 * s))), logo)
    cy = y0 + (ly1 - ly0) * s + gap + chip_h / 2
    on = tuple(int(25 * 0.78 + c * 0.22) for c in amber)
    d.rounded_rectangle([W / 2 - tw / 2 - 80, cy - chip_h / 2, W / 2 + tw / 2 + 80, cy + chip_h / 2], 24, fill=on)
    x = W / 2 - tw / 2
    for c in word:
        d.text((x, cy), c, font=f, fill=tuple(int(v) for v in amber), anchor="lm")
        x += d.textlength(c, font=f) + spacing
    import io
    buf = io.BytesIO()
    img.resize((width, height), Image.LANCZOS).save(buf, "PNG", optimize=True)
    return buf.getvalue()


COLOURS = {"red": ((150, 28, 22), (214, 64, 40)), "teal": ((10, 92, 98), (34, 160, 160))}


def banner(geo, signs, colour, x, y, facing, width, z0, z1):
    """A cloth banner hung flat, its picture on both faces (FACING -1: its front to -y)."""
    key = f"om_banner_{colour}"
    if not any(s[0] == key for s in signs):
        signs.append((key, colour, "", None, 0, 0, 0))
    w = width / 2
    q = np.array([[x - w, y, z0], [x + w, y, z0], [x + w, y, z1], [x - w, y, z1]])
    uv = np.array([[0, 1], [1, 1], [1, 0], [0, 0]], np.float32)
    if facing > 0:
        q = q[[1, 0, 3, 2]]
    geo.tri_quad(key, q, [0.0, float(facing), 0.0], uv, solid=False, both=True)


def banner_picture(colour, width=128, height=512):
    """Cloth: a deep colour with soft folds, a gold edge, carved triangles at the head, a gold
    cog with a bolt, and a fringed foot."""
    dark, bright = COLOURS[colour]
    x = np.arange(width)[None, :] / width
    y = np.arange(height)[:, None] / height
    fold = (np.sin(x * np.pi * 5 + y * 1.5) * 0.5 + 0.5) * 0.7 + 0.3 * (1 - y * 0.4)
    px = np.array(dark, np.float32) * (1 - fold[..., None]) + np.array(bright, np.float32) * fold[..., None]
    img = Image.fromarray(np.clip(px, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    gold, shadow = (236, 186, 76), (40, 14, 8)
    d.rectangle([6, 6, width - 7, height - 7], outline=shadow, width=6)
    d.rectangle([6, 6, width - 7, height - 7], outline=gold, width=4)
    for k in range(10, width - 10, 18):
        d.polygon([(k, 22), (k + 9, 38), (k + 18, 22)], fill=gold)
    cx, cy, r = width / 2, height * 0.42, width * 0.32
    textures.gear(d, cx + 2, cy + 2, r, shadow)
    textures.gear(d, cx, cy, r, gold)
    hexagon = [(cx + np.cos(np.pi / 6 + k * np.pi / 3) * r * 0.45, cy + np.sin(np.pi / 6 + k * np.pi / 3) * r * 0.45) for k in range(6)]
    d.polygon(hexagon, fill=dark, outline=shadow)
    d.ellipse([cx - r * 0.18, cy - r * 0.18, cx + r * 0.18, cy + r * 0.18], fill=gold)
    for k in range(3):
        yy = height * (0.66 + k * 0.06)
        d.line([18, yy, width - 18, yy], fill=gold, width=3)
    for k in range(8, width - 8, 10):
        d.line([k, height - 40, k, height - 10], fill=gold, width=4)
    import io
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def picture(key, title, sub):
    """The picture of a gate part listed in signs without a place of its own."""
    if key.startswith("om_banner_"):
        return banner_picture(title)
    return board_picture(sub)
