"""The museum's gate sign: a thick carved board hung under the gate's arch, raised gold letters
standing out of it (real geometry, so the sun and the torches shade them), a gold cog with a bolt
on top, and the subtitle carved into the wood. Both faces read: the shore's and the house's."""

import numpy as np
from PIL import Image, ImageDraw

import textures
from build import light

TITLE = "OPENRAC"
SECOND = "MUSEUM"
SUBTITLE = "where the galaxy is rebuilt"
BOARD = "om_gate_board"


def text_rects(text, cap, ppu=14, spacing=0.0):
    """TEXT as rectangles (u0, u1, v0, v1) in units, its middle at u = 0, its baseline at v = 0,
    CAP units tall: rows of the rendered letters merged where they line up."""
    f = textures.font(int(cap * ppu * 1.42), heavy=True)
    probe = ImageDraw.Draw(Image.new("L", (8, 8)))
    widths = [probe.textbbox((0, 0), ch, font=f)[2] for ch in text]
    gap = int(spacing * ppu)
    w = sum(widths) + gap * (len(text) - 1) + 8
    box = probe.textbbox((0, 0), "H", font=f)
    img = Image.new("L", (w, box[3] + 8))
    d = ImageDraw.Draw(img)
    x = 4
    for ch, cw in zip(text, widths):
        d.text((x, 4), ch, font=f, fill=255)
        x += cw + gap
    m = np.array(img) > 127
    rows = np.nonzero(m.any(1))[0]
    top, base = rows.min(), rows.max() + 1
    scale = cap / (base - top)
    rects, open_ = [], {}
    for y in range(top, base + 1):
        row = m[y] if y < base else np.zeros(w, bool)
        edges = np.diff(np.concatenate([[0], row.astype(np.int8), [0]]))
        runs = set(zip(np.nonzero(edges == 1)[0], np.nonzero(edges == -1)[0]))
        for run in list(open_):
            if run not in runs:
                y0 = open_.pop(run)
                rects.append((run[0], run[1], y0, y))
        for run in runs:
            open_.setdefault(run, y)
    mid = w / 2
    return [((a - mid) * scale, (b - mid) * scale, (base - y1) * scale, (base - y0) * scale) for a, b, y0, y1 in rects]


def fit(rects, width):
    """RECTS narrowed (and lowered with them) to WIDTH units when wider."""
    span = max(r[1] for r in rects) - min(r[0] for r in rects)
    k = min(1.0, width / span)
    return [(a * k, b * k, c * k, d * k) for a, b, c, d in rects]


def slab(geo, material, poly, y0, y1, uv=None, side_material=None, glow=None):
    """A flat shape POLY [(x, z)] (star-shaped round its middle, counter-clockwise seen from -y)
    made solid between y0 (front, facing -y) and y1 (back). UV(x, z) maps the faces' picture."""
    poly = np.asarray(poly, np.float64)
    c = poly.mean(0)
    uv = uv or (lambda x, z: (x / 4.0, -z / 4.0))
    for y, normal in ((y0, (0.0, -1.0, 0.0)), (y1, (0.0, 1.0, 0.0))):
        pts = np.concatenate([[c], poly])
        p3 = np.array([[x, y, z] for x, z in pts])
        tris = [[0, k + 1, (k + 1) % len(poly) + 1] for k in range(len(poly))]
        if normal[1] > 0:
            tris = [[a, cc, b] for a, b, cc in tris]
        geo.mesh.add(material, p3, [normal] * len(p3), [uv(x, z) for x, z in pts], [glow or light(normal)] * len(p3), tris)
    run = 0.0
    for a, b in zip(poly, np.roll(poly, -1, 0)):
        e = b - a
        n = np.array([e[1], 0.0, -e[0]]); n /= np.linalg.norm(n) or 1
        L = float(np.linalg.norm(e))
        q = [[a[0], y0, a[1]], [b[0], y0, b[1]], [b[0], y1, b[1]], [a[0], y1, a[1]]]
        t = np.array([[run, 0], [run + L, 0], [run + L, (y1 - y0)], [run, (y1 - y0)]], np.float32) / 2.0
        geo.tri_quad(side_material or material, q, list(n), t, solid=False, glow=glow)
        run += L


def raised_text(geo, material, rects, cx, cz, face_y, depth, facing):
    """RECTS standing DEPTH out of a face at FACE_Y, reading left to right from FACING (-1: the
    viewer is at -y)."""
    s = 1.0 if facing < 0 else -1.0
    for u0, u1, v0, v1 in rects:
        x0, x1 = sorted((cx + s * u0, cx + s * u1))
        y0, y1 = sorted((face_y, face_y + facing * depth))
        geo.box(material, (x0, y0, cz + v0), (x1, y1, cz + v1), solid=False, uv_scale=1.5)


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
         uv=lambda x, z: ((x - x0) / W, (zt + 0.7 - z) / (H + 0.7)), side_material="om_planks_dark")
    signs.append((BOARD, TITLE, SUBTITLE, None, 0, 0, 0))
    # A gold rim round the board's edge, both faces.
    for y in (gy - T / 2 - 0.06, gy + T / 2 + 0.06):
        for a, b in zip(poly, poly[1:] + poly[:1]):
            a3, b3 = np.array([a[0], y, a[1]]), np.array([b[0], y, b[1]])
            geo.beam("om_gold", a3, b3, 0.16)
    # The letters, on both faces: OPENRAC big, MUSEUM under it, spaced out.
    title = fit(text_rects(TITLE, 1.9, spacing=0.12), W - 2.4)
    second = fit(text_rects(SECOND, 0.8, spacing=0.55), W - 6.0)
    for facing, face in ((-1, gy - T / 2), (1, gy + T / 2)):
        raised_text(geo, "om_gold", title, gx, zb + 3.0, face, 0.32, facing)
        raised_text(geo, "om_gold", second, gx, zb + 1.75, face, 0.2, facing)
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
    """The board's face: dark carved wood, a band of carved triangles round it (the islands' tapa
    pattern) in ochre, the subtitle cut into the wood along the bottom."""
    r = np.random.default_rng(41)
    y = np.arange(height)[:, None]
    x = np.arange(width)[None, :]
    grain = (np.sin(y * 0.11 + np.sin(x * 0.006 + y * 0.02) * 3.0) * 0.5 + 0.5) * 0.5 \
        + r.random((height, width)) * 0.12
    plank = ((y // 100) % 2) * 0.06
    v = np.clip(0.32 + grain * 0.35 - plank, 0, 1)[..., None]
    px = np.array([46, 26, 12], np.float32) * (1 - v) + np.array([128, 78, 38], np.float32) * v
    img = Image.fromarray(np.clip(px, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    # The crest's band and the carved frame (the crest is the top 0.7 of 6.7 units).
    crest = int(height * 0.7 / 6.7)
    ochre, shadow, lit = (210, 150, 62), (24, 12, 4), (236, 196, 120)
    for inset, width_ in ((crest + 22, 6), (crest + 40, 2)):
        d.rectangle([inset - crest + 22, inset, width - 1 - (inset - crest + 22), height - 1 - (inset - crest)], outline=shadow, width=width_ + 2)
        d.rectangle([inset - crest + 22, inset, width - 1 - (inset - crest + 22), height - 1 - (inset - crest)], outline=ochre, width=width_)
    # Carved triangles in a band along the bottom and the top.
    for row_y, up in ((height - 34, True), (crest + 30, False)):
        for k in range(48, width - 48, 26):
            tri = [(k, row_y + (8 if up else -8)), (k + 13, row_y - (8 if up else -8)), (k + 26, row_y + (8 if up else -8))]
            d.polygon([(a + 2, b + 2) for a, b in tri], fill=shadow)
            d.polygon(tri, fill=ochre)
    # The subtitle, cut in: a dark cut with a lit lower edge.
    sub = " ".join(sub.upper())
    f = textures.font(34, serif=True)
    box = d.textbbox((0, 0), sub, font=f)
    while box[2] - box[0] > width - 300:
        f = textures.font(f.size - 2, serif=True)
        box = d.textbbox((0, 0), sub, font=f)
    tx = (width - (box[2] - box[0])) / 2 - box[0]
    ty = height - 90
    d.text((tx + 3, ty + 3), sub, font=f, fill=shadow)
    d.text((tx, ty), sub, font=f, fill=lit)
    for sx in (tx - 60, tx + box[2] - box[0] + 24):
        d.ellipse([sx, ty + 14, sx + 20, ty + 34], fill=ochre, outline=shadow, width=3)
    import io
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
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
