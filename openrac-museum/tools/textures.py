"""OpenRAC Museum's textures: drawn from noise here, or Pokitaru's own from your disc at install (DISC)."""

import io
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

SIZE = 256


def rng(seed):
    return np.random.default_rng(seed)


def fractal(size, seed, octaves=5, base=4):
    """Tileable value noise in 0..1."""
    r = rng(seed)
    out = np.zeros((size, size), np.float32)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        cells = base * (2 ** o)
        grid = r.random((cells, cells)).astype(np.float32)
        img = Image.fromarray((grid * 255).astype(np.uint8))
        # Tile by wrapping before resizing.
        tiled = Image.new("L", (cells * 3, cells * 3))
        for i in range(3):
            for j in range(3):
                tiled.paste(img, (i * cells, j * cells))
        big = tiled.resize((size * 3, size * 3), Image.BICUBIC)
        layer = np.asarray(big, np.float32)[size:2 * size, size:2 * size] / 255
        out += layer * amp
        total += amp
        amp *= 0.5
    return out / total


def to_png(rgb):
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def tint(noise, low, high):
    low, high = np.array(low, np.float32), np.array(high, np.float32)
    return low + (high - low) * noise[..., None]


def sand():
    n = fractal(SIZE, 1) * 0.7 + fractal(SIZE, 2, 3, 32) * 0.3
    return to_png(tint(n, (196, 170, 120), (242, 222, 170)))


def grass():
    n = fractal(SIZE, 3) * 0.5 + fractal(SIZE, 4, 3, 48) * 0.5
    return to_png(tint(n, (52, 112, 38), (120, 176, 62)))


def rock():
    n = fractal(SIZE, 5, 6, 3)
    streak = fractal(SIZE, 6, 3, 8)
    n = np.clip(n * 0.7 + np.abs(np.sin(streak * 18)) * 0.3, 0, 1)
    return to_png(tint(n, (78, 70, 66), (150, 138, 124)))


def basalt():
    n = fractal(SIZE, 7, 6, 4)
    return to_png(tint(n, (32, 30, 34), (82, 76, 78)))


def planks(seed=9, base=(168, 112, 66), dark=(104, 64, 34), boards=8):
    """Wooden boards across the picture, with grain and seams."""
    r = rng(seed)
    grain = fractal(SIZE, seed, 4, 2)
    y = np.arange(SIZE)[:, None] * np.ones((1, SIZE))
    x = np.ones((SIZE, 1)) * np.arange(SIZE)[None, :]
    lines = np.sin((x / SIZE * 2 * np.pi * 3) + grain * 20) * 0.5 + 0.5
    board = (y // (SIZE // boards)).astype(int)
    shade = r.random(boards + 1)[board] * 0.35
    n = np.clip(0.35 + lines * 0.35 + shade, 0, 1)
    rgb = tint(n, dark, base)
    seam = (y % (SIZE // boards)) < 3
    rgb[seam] = np.array(dark) * 0.6
    # Butt joints, staggered per board.
    offsets = (r.random(boards + 1) * SIZE).astype(int)
    joint = ((x + offsets[board]) % SIZE) < 3
    rgb[joint] = np.array(dark) * 0.7
    return to_png(rgb)


def thatch():
    n = fractal(SIZE, 11, 4, 2)
    y = np.arange(SIZE)[:, None] * np.ones((1, SIZE))
    x = np.ones((SIZE, 1)) * np.arange(SIZE)[None, :]
    # Broad strands and few rows, soft: fine stripes shimmer in the distance (moire).
    strands = np.sin(x * (2 * np.pi * 10 / SIZE) + n * 9) * 0.5 + 0.5
    rows = (np.sin(y / SIZE * np.pi * 2 * 3) * 0.5 + 0.5) ** 4
    v = np.clip(0.38 + strands * 0.32 + (n - 0.5) * 0.22 - rows * 0.22, 0, 1)
    return to_png(tint(v, (112, 84, 42), (196, 162, 92)))


def water():
    n = fractal(SIZE, 13, 5, 4)
    caustic = np.abs(np.sin(fractal(SIZE, 14, 3, 6) * 25)) ** 4
    v = np.clip(n * 0.6 + caustic * 0.5, 0, 1)
    rgb = tint(v, (14, 98, 140), (90, 210, 220))
    return to_png(rgb)


def foam():
    n = fractal(SIZE, 15, 5, 8)
    v = np.clip((n - 0.35) * 2.2, 0, 1)
    rgb = tint(v, (120, 200, 230), (255, 255, 255))
    return to_png(rgb)


def falls():
    """Falling water: streaks along the picture's height."""
    x = fractal(SIZE, 16, 4, 16)
    streak = np.repeat(x[:1], SIZE, 0)
    v = np.clip(streak * 0.7 + fractal(SIZE, 17, 3, 4) * 0.3, 0, 1)
    return to_png(tint(v, (60, 160, 200), (235, 250, 255)))


def stone_path():
    n = fractal(SIZE, 18, 5, 6)
    cells = fractal(SIZE, 19, 1, 6)
    edge = np.abs(np.gradient(cells)[0]) + np.abs(np.gradient(cells)[1])
    v = np.clip(n * 0.8 - edge * 40, 0, 1)
    return to_png(tint(v, (90, 86, 80), (176, 168, 150)))


def lava():
    n = fractal(SIZE, 20, 5, 4)
    v = np.clip((n - 0.3) * 2, 0, 1) ** 2
    return to_png(tint(v, (60, 10, 0), (255, 190, 40)))


def solid(rgb):
    return to_png(np.ones((16, 16, 3)) * np.array(rgb, np.float32))


def font(size, heavy=False, serif=False):
    """A bold font of SIZE pixels: HEAVY a black one for the gate's letters, SERIF a bold serif for
    carved lines; the first one found on Linux or Windows."""
    paths = ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
             "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", "arialbd.ttf"]
    if heavy:
        paths = ["/usr/share/fonts/opentype/inter/InterDisplay-Black.otf", "/usr/share/fonts/opentype/inter/Inter-Black.otf",
                 "ariblk.ttf", "seguibl.ttf"] + paths
    if serif:
        paths = ["/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
                 "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf", "georgiab.ttf", "timesbd.ttf"] + paths
    for path in paths:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return ImageFont.load_default()


def sign(text, sub="", width=512, height=192, wood=True):
    """A carved wooden sign with gold letters."""
    if wood:
        board = Image.open(io.BytesIO(planks(31, (150, 98, 56), (84, 50, 26), 3))).resize((width, height))
    else:
        board = Image.new("RGB", (width, height), (24, 22, 30))
    draw = ImageDraw.Draw(board)
    draw.rectangle([4, 4, width - 5, height - 5], outline=(60, 34, 16), width=8)
    big = font(int(height * (0.36 if sub else 0.5)))
    box = draw.textbbox((0, 0), text, font=big)
    while box[2] - box[0] > width - 40 and big.size > 12:
        big = font(big.size - 4)
        box = draw.textbbox((0, 0), text, font=big)
    ty = (height * (0.18 if sub else 0.25))
    tx = (width - (box[2] - box[0])) / 2
    draw.text((tx + 3, ty + 3), text, font=big, fill=(40, 22, 8))
    draw.text((tx, ty), text, font=big, fill=(244, 196, 74))
    if sub:
        small = font(int(height * 0.16))
        box = draw.textbbox((0, 0), sub, font=small)
        while box[2] - box[0] > width - 40 and small.size > 10:
            small = font(small.size - 2)
            box = draw.textbbox((0, 0), sub, font=small)
        draw.text(((width - (box[2] - box[0])) / 2, height * 0.66), sub, font=small, fill=(250, 236, 200))
    buf = io.BytesIO()
    board.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def gear(draw, cx, cy, r, fill, teeth=12):
    """A gear's outline filled, for the museum's logo."""
    pts = []
    for k in range(teeth * 4):
        a = 2 * math.pi * k / (teeth * 4)
        rr = r if (k // 2) % 2 == 0 else r * 0.78
        pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr))
    draw.polygon(pts, fill=fill)
    draw.ellipse([cx - r * 0.42, cy - r * 0.42, cx + r * 0.42, cy + r * 0.42], fill=(20, 40, 60))


def welcome_sign(title, sub, width=1024, height=384):
    """The museum's big sign: a deep blue board, a gold double border with studs, gears either
    side, raised gold letters with a dark edge, a cream line under them."""
    img = Image.new("RGB", (width, height))
    px = np.zeros((height, width, 3), np.float32)
    yy = np.linspace(0, 1, height)[:, None]
    px[...] = (np.array([18, 60, 92]) * (1 - yy) + np.array([10, 28, 52]) * yy)[:, None, :] if False else 0
    top, bottom = np.array([24, 84, 118], np.float32), np.array([10, 30, 58], np.float32)
    px = top * (1 - yy[..., None]) + bottom * yy[..., None]
    px = np.broadcast_to(px, (height, width, 3)).copy()
    # A soft glow behind the letters.
    xx = np.linspace(-1, 1, width)[None, :]
    glow = np.exp(-((xx / 0.55) ** 2 + ((yy - 0.42) / 0.3) ** 2))
    px += glow[..., None] * np.array([60, 70, 40])
    img = Image.fromarray(np.clip(px, 0, 255).astype(np.uint8))
    draw = ImageDraw.Draw(img)
    gold, dark = (246, 200, 80), (60, 36, 10)
    draw.rounded_rectangle([6, 6, width - 7, height - 7], 28, outline=dark, width=14)
    draw.rounded_rectangle([10, 10, width - 11, height - 11], 24, outline=gold, width=8)
    draw.rounded_rectangle([30, 30, width - 31, height - 31], 16, outline=gold, width=3)
    for (x, y) in ((30, 30), (width - 31, 30), (30, height - 31), (width - 31, height - 31)):
        draw.ellipse([x - 12, y - 12, x + 12, y + 12], fill=gold, outline=dark, width=3)
    for x in np.linspace(80, width - 80, 9):
        draw.ellipse([x - 5, 18 - 5, x + 5, 18 + 5], fill=gold)
        draw.ellipse([x - 5, height - 19 - 5, x + 5, height - 19 + 5], fill=gold)
    gear(draw, 120, height * 0.45, 70, gold)
    gear(draw, width - 120, height * 0.45, 70, gold)
    big = font(118)
    box = draw.textbbox((0, 0), title, font=big)
    while box[2] - box[0] > width - 400 and big.size > 20:
        big = font(big.size - 4)
        box = draw.textbbox((0, 0), title, font=big)
    tx = (width - (box[2] - box[0])) / 2 - box[0]
    ty = height * 0.2
    draw.text((tx + 5, ty + 6), title, font=big, fill=(4, 10, 20))
    draw.text((tx, ty), title, font=big, fill=gold, stroke_width=4, stroke_fill=dark)
    draw.text((tx, ty - 2), title, font=big, fill=(255, 232, 140))
    small = font(40)
    box = draw.textbbox((0, 0), sub, font=small)
    sx = (width - (box[2] - box[0])) / 2
    draw.line([sx - 70, height * 0.74, sx - 20, height * 0.74], fill=gold, width=4)
    draw.line([sx + box[2] - box[0] + 20, height * 0.74, sx + box[2] - box[0] + 70, height * 0.74], fill=gold, width=4)
    draw.text((sx, height * 0.74 - 26), sub, font=small, fill=(250, 238, 206))
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def painting(seed, width=320, height=240):
    """A small landscape painting: a sky, a sun or planets, layered islands, the sea."""
    r = rng(seed + 500)
    yy = np.linspace(0, 1, height)[:, None, None]
    palettes = [((40, 70, 140), (250, 170, 90)), ((20, 30, 70), (190, 90, 140)), ((90, 170, 230), (230, 240, 250)),
                ((60, 20, 60), (250, 120, 60)), ((30, 110, 120), (210, 240, 200))]
    a, b = palettes[seed % len(palettes)]
    px = np.array(a, np.float32) * (1 - yy) + np.array(b, np.float32) * yy
    px = np.broadcast_to(px, (height, width, 3)).copy()
    img = Image.fromarray(px.astype(np.uint8))
    d = ImageDraw.Draw(img)
    for _ in range(int(r.integers(1, 3))):
        x, y, rad = r.uniform(40, width - 40), r.uniform(20, height * 0.4), r.uniform(10, 34)
        col = tuple(int(v) for v in r.uniform(150, 255, 3))
        d.ellipse([x - rad, y - rad, x + rad, y + rad], fill=col)
        if r.random() < 0.5:
            d.ellipse([x - rad * 1.8, y - rad * 0.3, x + rad * 1.8, y + rad * 0.3], outline=(240, 220, 170), width=2)
    sea_y = int(height * r.uniform(0.62, 0.74))
    for layer in range(3):
        shade = 40 + layer * 30
        base = sea_y - layer * 2
        x = -10
        while x < width:
            w = r.uniform(30, 90)
            h = r.uniform(20, 90) * (1 - layer * 0.25)
            col = (int(shade * 0.6), int(shade * 1.1 + 20), int(shade * 0.7))
            d.polygon([(x, base), (x + w * 0.15, base - h), (x + w * 0.85, base - h * r.uniform(0.7, 1)), (x + w, base)], fill=col)
            d.rectangle([x + w * 0.15, base - h - 4, x + w * 0.85, base - h + 2], fill=(70, 150, 60))
            x += w + r.uniform(10, 60)
    d.rectangle([0, sea_y, width, height], fill=(40, 150, 160))
    for k in range(12):
        y = r.uniform(sea_y + 4, height - 4)
        x = r.uniform(0, width - 40)
        d.line([x, y, x + r.uniform(10, 40), y], fill=(170, 230, 230), width=1)
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def rug(seed, width=256, height=180):
    """A woven rug: a border, a field, a diamond medallion, tassels."""
    r = rng(seed + 700)
    palettes = [((150, 40, 36), (230, 180, 80), (30, 70, 90)), ((30, 90, 110), (230, 210, 150), (140, 40, 40)),
                ((90, 50, 110), (230, 190, 90), (40, 110, 80)), ((170, 90, 30), (240, 225, 180), (60, 40, 30))]
    field, edge, accent = palettes[seed % len(palettes)]
    img = Image.new("RGB", (width, height), field)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, width - 1, height - 1], outline=edge, width=14)
    d.rectangle([18, 18, width - 19, height - 19], outline=accent, width=6)
    for k in range(0, width, 16):
        d.polygon([(k, 7), (k + 8, 0), (k + 16, 7), (k + 8, 14)], fill=accent)
        d.polygon([(k, height - 8), (k + 8, height - 15), (k + 16, height - 8), (k + 8, height - 1)], fill=accent)
    cx, cy = width / 2, height / 2
    for s, col in ((70, edge), (52, accent), (32, edge), (14, field)):
        d.polygon([(cx - s * 1.3, cy), (cx, cy - s * 0.85), (cx + s * 1.3, cy), (cx, cy + s * 0.85)], fill=col)
    arr = np.asarray(img).astype(np.float32)
    arr *= (0.9 + 0.1 * r.random((height, width, 1)))
    buf = io.BytesIO()
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).save(buf, "PNG", optimize=True)
    return buf.getvalue()


def poster(title, lines, seed, width=256, height=384):
    """An empty exhibit board: a title and a frame for content added later."""
    r = rng(seed)
    hue = r.random(3) * 80 + 40
    img = Image.new("RGB", (width, height), tuple(int(v) for v in hue * 0.4))
    draw = ImageDraw.Draw(img)
    draw.rectangle([10, 10, width - 11, height - 11], outline=(244, 196, 74), width=6)
    f = font(28)
    box = draw.textbbox((0, 0), title, font=f)
    while box[2] - box[0] > width - 40 and f.size > 10:
        f = font(f.size - 2)
        box = draw.textbbox((0, 0), title, font=f)
    draw.text(((width - (box[2] - box[0])) / 2, 26), title, font=f, fill=(255, 240, 210))
    small = font(16)
    y = 80
    for line in lines:
        draw.text((24, y), line, font=small, fill=(230, 220, 200))
        y += 24
    draw.rectangle([24, height - 170, width - 25, height - 30], outline=(200, 190, 170), width=2)
    draw.text((40, height - 110), "(exhibit coming soon)", font=small, fill=(200, 190, 170))
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def wrap(draw, text, f, width):
    """TEXT cut into lines no wider than WIDTH pixels in font F."""
    lines = []
    for paragraph in text.split("\n"):
        line = ""
        for word in paragraph.split(" "):
            test = (line + " " + word).strip()
            if line and draw.textlength(test, font=f) > width:
                lines.append(line)
                line = word
            else:
                line = test
        lines.append(line)
    return lines


def png(img):
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def board(title, paragraphs, seed, width=512, height=704, quote=None):
    """An exhibit board: a title, short paragraphs, and a game string in a box when QUOTE."""
    r = rng(seed)
    hue = r.random(3) * 50 + 30
    img = Image.new("RGB", (width, height), tuple(int(v) for v in hue * 0.45))
    draw = ImageDraw.Draw(img)
    draw.rectangle([8, 8, width - 9, height - 9], outline=(244, 196, 74), width=8)
    f = font(48)
    while draw.textlength(title, font=f) > width - 60 and f.size > 16:
        f = font(f.size - 2)
    draw.text(((width - draw.textlength(title, font=f)) / 2, 30), title, font=f, fill=(255, 236, 190))
    draw.line([60, 100, width - 60, 100], fill=(244, 196, 74), width=3)
    size = 30
    while True:
        body = font(size)
        lines = []
        for p in paragraphs:
            lines += wrap(draw, p, body, width - 64) + [""]
        need = len(lines) * (size + 8) + (0 if quote is None else 30 + len(wrap(draw, quote, font(size, serif=True), width - 96)) * (size + 8))
        if need < height - 150 or size <= 14:
            break
        size -= 2
    y = 120
    for line in lines:
        draw.text((32, y), line, font=body, fill=(240, 232, 214))
        y += size + 8
    if quote is not None:
        q = font(size, serif=True)
        ql = wrap(draw, quote, q, width - 96)
        box_h = len(ql) * (size + 8) + 24
        draw.rectangle([32, y, width - 33, y + box_h], fill=(16, 14, 20), outline=(200, 190, 170), width=2)
        for k, line in enumerate(ql):
            draw.text((48, y + 12 + k * (size + 8)), line, font=q, fill=(150, 240, 150))
    return png(img)


def plate(text, sub="", width=512, height=128):
    """A small brass name plate."""
    img = Image.new("RGB", (width, height), (198, 156, 70))
    draw = ImageDraw.Draw(img)
    draw.rectangle([4, 4, width - 5, height - 5], outline=(110, 76, 24), width=6)
    f = font(int(height * (0.4 if sub else 0.5)))
    while draw.textlength(text, font=f) > width - 30 and f.size > 10:
        f = font(f.size - 2)
    draw.text(((width - draw.textlength(text, font=f)) / 2, height * (0.12 if sub else 0.22)), text, font=f, fill=(40, 24, 6))
    if sub:
        s = font(int(height * 0.24))
        while draw.textlength(sub, font=s) > width - 30 and s.size > 8:
            s = font(s.size - 2)
        draw.text(((width - draw.textlength(sub, font=s)) / 2, height * 0.6), sub, font=s, fill=(60, 36, 10))
    return png(img)


def message(text, where, width=1024, height=192):
    """A line from the game's code, white on dark, with where it is from under it."""
    img = Image.new("RGB", (width, height), (20, 22, 30))
    draw = ImageDraw.Draw(img)
    draw.rectangle([4, 4, width - 5, height - 5], outline=(244, 196, 74), width=6)
    f = font(72, serif=True)
    while draw.textlength(text, font=f) > width - 60 and f.size > 14:
        f = font(f.size - 2)
    draw.text(((width - draw.textlength(text, font=f)) / 2, 30), text, font=f, fill=(240, 240, 232))
    s = font(30)
    draw.text(((width - draw.textlength(where, font=s)) / 2, height - 56), where, font=s, fill=(244, 196, 74))
    return png(img)


def screen(lines, width=512, height=384):
    """An old monitor's screen: green text on black."""
    img = Image.new("RGB", (width, height), (6, 14, 8))
    draw = ImageDraw.Draw(img)
    f = font(22)
    for k, line in enumerate(lines):
        draw.text((18, 18 + k * 30), line, font=f, fill=(120, 255, 140))
    return png(img)


def arrow(symbol, size=128):
    """A floor tile with one of the code's buttons on it."""
    img = Image.new("RGB", (size, size), (40, 46, 70))
    draw = ImageDraw.Draw(img)
    draw.rectangle([4, 4, size - 5, size - 5], outline=(244, 196, 74), width=6)
    f = font(int(size * 0.7))
    box = draw.textbbox((0, 0), symbol, font=f)
    draw.text(((size - (box[2] - box[0])) / 2 - box[0], (size - (box[3] - box[1])) / 2 - box[1]), symbol, font=f,
              fill=(255, 240, 200))
    return png(img)


def all_textures():
    return {
        "om_sand": sand(), "om_grass": grass(), "om_rock": rock(), "om_basalt": basalt(),
        "om_planks": planks(), "om_planks_dark": planks(21, (120, 76, 44), (66, 40, 20)),
        "om_planks_light": planks(22, (196, 150, 98), (128, 88, 52), 6),
        "om_thatch": thatch(), "om_water": water(), "om_foam": foam(), "om_falls": falls(),
        "om_stone": stone_path(), "om_lava": lava(), "om_path": stone_path(),
        "om_cliff": rock(), "om_jungle": to_png(tint(fractal(SIZE, 41), (30, 60, 30), (70, 120, 50))),
        "om_wet_sand": sand(),
        "om_gold": solid((236, 186, 64)), "om_white": solid((236, 232, 222)),
        "om_rail": solid((150, 156, 168)), "om_red": solid((176, 46, 38)),
        "om_teal": solid((40, 150, 150)), "om_glass": solid((90, 170, 110)),
        "om_yellow": solid((250, 210, 40)), "om_orange": solid((240, 120, 30)),
    }


# Pokitaru's own pictures, from the level as extracted from your disc: the island then looks like
# the game's own planets. Missing ones keep the drawn pictures above.
DISC = {
    "om_grass": "terrain_0003.png",       # grass
    "om_cliff": "terrain_0012.png",       # rock with grass hanging over its top: the layered cliffs
    "om_jungle": "terrain_0009.png",      # creepers over dark rock
    "om_rock": "terrain_0017.png",        # bare rock, the peaks
    "om_sand": "terrain_0022.png",
    "om_wet_sand": "terrain_0027.png",    # rock with sand at its foot: the shore
    "om_basalt": "terrain_0016.png",
    "om_stone": "terrain_0006.png",       # cobbles
    "om_path": "terrain_0005.png",        # cobbles with grass
    "om_planks": "tie_0006.png",          # the house's walls
    "om_planks_light": "tie_0018.png",    # floors and decks
    "om_planks_dark": "terrain_0001.png", # posts and frames
}


def from_disc(folder):
    """The pictures of DISC found in FOLDER (the level's textures/), as their files' bytes. They are
    kept exactly as the disc has them: a shared map then names them instead of holding them."""
    out = {}
    if folder is None:
        return out
    for name, file in DISC.items():
        path = Path(folder) / file
        if path.is_file():
            out[name] = path.read_bytes()
    return out


# ---- The exhibits' own pictures ----

GOLD = (244, 196, 74)
INK = (240, 232, 214)
PICTURES_DIR = Path(__file__).resolve().parent.parent / "pictures"


def mono(size):
    """A monospaced font for code; the first one found on Linux or Windows."""
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", "consolab.ttf", "courbd.ttf"):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return font(size)


def any_script(size):
    """A font that also has Japanese letters (for names); the first one found."""
    for path in ("/usr/share/fonts/truetype/fonts-japanese-gothic.ttf", "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",
                 "YuGothB.ttc", "msgothic.ttc", "meiryob.ttc"):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return font(size)


def framed(img, border=10):
    """IMG with the museum's gold border."""
    draw = ImageDraw.Draw(img)
    w, h = img.size
    draw.rectangle([border // 2, border // 2, w - 1 - border // 2, h - 1 - border // 2], outline=GOLD, width=border)
    return img


def photo(name, width=1024, height=640, caption=None):
    """A picture from pictures/ fitted on a dark mount, with an optional CAPTION under it."""
    img = Image.new("RGB", (width, height), (24, 22, 26))
    pic = Image.open(PICTURES_DIR / name).convert("RGB")
    room_h = height - (70 if caption else 30)
    scale = min((width - 40) / pic.width, room_h / pic.height)
    pic = pic.resize((int(pic.width * scale), int(pic.height * scale)), Image.LANCZOS)
    img.paste(pic, ((width - pic.width) // 2, 15 + (room_h - pic.height) // 2))
    if caption:
        draw = ImageDraw.Draw(img)
        f = font(30)
        draw.text(((width - draw.textlength(caption, font=f)) / 2, height - 52), caption, font=f, fill=GOLD)
    return png(framed(img))


def code_card(panels, title=None, width=1024, height=640, note=None):
    """Code side by side: PANELS is a list of (heading, lines), each drawn in its own column."""
    img = Image.new("RGB", (width, height), (18, 20, 28))
    draw = ImageDraw.Draw(img)
    top = 24
    if title:
        f = font(40)
        draw.text(((width - draw.textlength(title, font=f)) / 2, 20), title, font=f, fill=GOLD)
        top = 80
    # The panels one under the other, each as tall as its lines.
    longest = max(max(len(l) for l in lines) for _, lines in panels)
    rows = sum(len(lines) for _, lines in panels)
    room = height - top - (90 if note else 30) - len(panels) * 62
    size = 40
    while size > 10 and (mono(size).getlength("M" * longest) > width - 100 or rows * (size + 6) > room):
        size -= 1
    m = mono(size)
    hf = font(26)
    y = top
    for head, lines in panels:
        h = 50 + len(lines) * (size + 6)
        draw.rectangle([26, y, width - 27, y + h], fill=(8, 10, 14), outline=(90, 90, 100), width=2)
        draw.text((44, y + 10), head, font=hf, fill=(150, 200, 255))
        for n, line in enumerate(lines):
            draw.text((44, y + 46 + n * (size + 6)), line, font=m, fill=(200, 235, 190))
        y += h + 12
    if note:
        f = font(30)
        draw.text(((width - draw.textlength(note, font=f)) / 2, height - 66), note, font=f, fill=INK)
    return png(framed(img))


def card(title, lines, width=1024, height=640, big=None):
    """A dark card: TITLE in gold, BIG (one line, large) when given, and short LINES under it."""
    img = Image.new("RGB", (width, height), (26, 24, 32))
    draw = ImageDraw.Draw(img)
    f = font(56)
    while draw.textlength(title, font=f) > width - 80 and f.size > 20:
        f = font(f.size - 2)
    draw.text(((width - draw.textlength(title, font=f)) / 2, 40), title, font=f, fill=GOLD)
    y = 140
    if big:
        b = font(96, serif=True)
        while draw.textlength(big, font=b) > width - 80 and b.size > 20:
            b = font(b.size - 2)
        draw.text(((width - draw.textlength(big, font=b)) / 2, y), big, font=b, fill=(255, 250, 240))
        y += b.size + 50
    body = font(34)
    for line in lines:
        for part in wrap(draw, line, body, width - 120):
            draw.text(((width - draw.textlength(part, font=body)) / 2, y), part, font=body, fill=INK)
            y += 46
        y += 10
    return png(framed(img))


def renames(rows, width=1024, height=640):
    """Names then and now: ROWS of (old name, final name)."""
    img = Image.new("RGB", (width, height), (26, 24, 32))
    draw = ImageDraw.Draw(img)
    f = font(50)
    t = "E3 2002 demo  ->  the game"
    draw.text(((width - draw.textlength(t, font=f)) / 2, 40), t, font=f, fill=GOLD)
    size = 46
    while size > 16 and (max(draw.textlength(o, font=font(size, serif=True)) for o, _ in rows) > width / 2 - 70
                         or max(draw.textlength(n, font=font(size)) for _, n in rows) > width / 2 - 80):
        size -= 2
    a, b = font(size, serif=True), font(size)
    for k, (old, new) in enumerate(rows):
        y = 150 + k * 110
        draw.text((width / 2 - 40 - draw.textlength(old, font=a), y), old, font=a, fill=(255, 210, 150))
        draw.text((width / 2 - 16, y), "->", font=b, fill=(160, 160, 170))
        draw.text((width / 2 + 50, y), new, font=b, fill=INK)
    return png(framed(img))


def names_wall(title, names, width=1024, height=1024):
    """A wall of names, in columns, under TITLE."""
    img = Image.new("RGB", (width, height), (30, 24, 20))
    draw = ImageDraw.Draw(img)
    f = font(64)
    draw.text(((width - draw.textlength(title, font=f)) / 2, 40), title, font=f, fill=GOLD)
    cols = 2
    per = (len(names) + cols - 1) // cols
    n = any_script(44)
    for k, name in enumerate(names):
        c, r = divmod(k, per)
        x = width * (c + 0.5) / cols
        y = 160 + r * min(70, (height - 220) / max(per, 1))
        draw.text((x - draw.textlength(name, font=n) / 2, y), name, font=n, fill=INK)
    return png(framed(img))


def floor_plan(ground, upper, here=None, width=1024, height=576):
    """The museum's two floors as two octagons of room names; HERE is (storey, sector)."""
    img = Image.new("RGB", (width, height), (24, 30, 36))
    draw = ImageDraw.Draw(img)
    f = font(44)
    t = "Museum map"
    draw.text(((width - draw.textlength(t, font=f)) / 2, 20), t, font=f, fill=GOLD)
    small = font(20)
    for storey, names in ((0, ground), (1, upper)):
        cx, cy, r = width * (0.27 + 0.46 * storey), height * 0.57, 190
        head = "Ground floor" if storey == 0 else "Upper floor"
        hf = font(28)
        draw.text((cx - draw.textlength(head, font=hf) / 2, cy - r - 52), head, font=hf, fill=(150, 200, 255))
        draw.ellipse([cx - 38, cy - 38, cx + 38, cy + 38], outline=(120, 170, 200), width=3)
        for s, (name, _) in enumerate(names):
            # East at the right, as the map is drawn with north up; sectors count anticlockwise.
            a = math.radians(45 * s)
            x, y = cx + math.cos(a) * r * 0.72, cy - math.sin(a) * r * 0.72
            fill = (255, 230, 120) if here == (storey, s) else INK
            lines = wrap(draw, name, small, 120)
            for k, line in enumerate(lines):
                draw.text((x - draw.textlength(line, font=small) / 2, y - 12 * len(lines) + k * 24), line, font=small, fill=fill)
            if here == (storey, s):
                draw.ellipse([x - 70, y - 34, x + 70, y + 34], outline=(255, 210, 80), width=3)
    if here is not None:
        hf = font(24)
        draw.text((30, height - 44), "You are here: the gold ring", font=hf, fill=(255, 210, 80))
    return png(framed(img))


def info_tile(size=256):
    """The floor tile that shows an exhibit's words: a gold i on a dark disc, and STEP HERE."""
    img = Image.new("RGB", (size, size), (34, 30, 40))
    draw = ImageDraw.Draw(img)
    draw.rectangle([6, 6, size - 7, size - 7], outline=GOLD, width=10)
    r = size * 0.3
    c = size / 2
    draw.ellipse([c - r, c - r - 18, c + r, c + r - 18], fill=(20, 60, 70), outline=GOLD, width=6)
    f = font(int(size * 0.42), serif=True)
    box = draw.textbbox((0, 0), "i", font=f)
    draw.text((c - (box[2] - box[0]) / 2 - box[0], c - 18 - (box[3] - box[1]) / 2 - box[1]), "i", font=f, fill=(255, 236, 190))
    s = font(int(size * 0.1))
    t = "STEP HERE"
    draw.text((c - draw.textlength(t, font=s) / 2, size - 52), t, font=s, fill=GOLD)
    return png(img)
