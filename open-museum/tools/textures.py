"""Open Museum's textures: drawn from noise here, or Pokitaru's own from your disc at install (DISC)."""

import io
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
    strands = np.sin(x * 0.9 + n * 30) * 0.5 + 0.5
    rows = (np.sin(y / SIZE * np.pi * 2 * 6) * 0.5 + 0.5) ** 3
    v = np.clip(strands * 0.6 + n * 0.4 - rows * 0.4, 0, 1)
    return to_png(tint(v, (96, 72, 34), (204, 170, 96)))


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


def font(size):
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                 "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"):
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
    """The pictures of DISC found in FOLDER (the level's textures/), as PNG bytes."""
    out = {}
    if folder is None:
        return out
    for name, file in DISC.items():
        path = Path(folder) / file
        if path.is_file():
            im = Image.open(path).convert("RGB")
            buf = io.BytesIO()
            im.save(buf, "PNG")
            out[name] = buf.getvalue()
    return out
