"""The flight to the museum: its planet picture (the globe the ship flies to, 128x128) and caption
(256x32), drawn here. The port shows them on the flight in place of the template planet's
(flight_picture.png and flight_caption.png in the level's textures)."""

import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import textures

CAPTION = "OpenRAC Museum"
# The game's caption blue.
BLUE = (29, 123, 211)
FONTS = ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
         "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf")


def caption():
    """The name, centred, cap height about 12 pixels as the game's captions."""
    font = next((ImageFont.truetype(f, 16) for f in FONTS if Path(f).is_file()), None) or ImageFont.load_default()
    mask = Image.new("L", (256 * 4, 32 * 4))
    big = font.font_variant(size=74) if hasattr(font, "font_variant") else font
    d = ImageDraw.Draw(mask)
    box = d.textbbox((0, 0), CAPTION, font=big)
    w, h = box[2] - box[0], box[3] - box[1]
    d.text(((1024 - w) // 2 - box[0], (128 - h) // 2 - box[1]), CAPTION, font=big, fill=255)
    mask = mask.resize((256, 32), Image.LANCZOS)
    out = Image.new("RGBA", (256, 32), BLUE + (0,))
    out.putalpha(mask)
    return out


def picture():
    """A small sea planet with green islands, lit from the upper left, dark on the far side."""
    n = 128
    sea = textures.fractal(256, 71, octaves=6, base=6)
    land = textures.fractal(256, 72, octaves=6, base=8)
    y, x = np.mgrid[0:n, 0:n].astype(np.float32)
    cx = cy = (n - 1) / 2
    r = 60.0
    dx, dy = (x - cx) / r, (y - cy) / r
    d2 = dx * dx + dy * dy
    inside = d2 < 1.0
    dz = np.sqrt(np.clip(1.0 - d2, 0, 1))
    # Sphere coordinates into the noise.
    lon = (np.arctan2(dx, dz) / math.pi * 0.5 + 0.5) * 255
    lat = (np.arcsin(np.clip(dy, -1, 1)) / math.pi + 0.5) * 255
    li, la = lon.astype(int) % 256, lat.astype(int) % 256
    h = land[la, li]
    s = sea[la, li]
    rgb = np.zeros((n, n, 3), np.float32)
    deep = np.array([22, 40, 92], np.float32)
    shallow = np.array([40, 92, 150], np.float32)
    rgb[:] = deep + (shallow - deep) * s[..., None]
    isl = h > 0.6
    beach = (h > 0.585) & ~isl
    green = np.array([70, 128, 60], np.float32) + (np.array([120, 170, 80], np.float32) - [70, 128, 60]) * ((h - 0.6) / 0.15).clip(0, 1)[..., None]
    rgb[isl] = green[isl]
    rgb[beach] = [170, 160, 110]
    # Clouds.
    cloud = textures.fractal(256, 73, octaves=6, base=8)[la, li]
    c = ((cloud - 0.6) / 0.15).clip(0, 1)[..., None] * 0.6
    rgb = rgb * (1 - c) + 225 * c
    # Light from the upper left and towards the viewer.
    light = np.array([-0.55, -0.45, 0.7], np.float32)
    light /= np.linalg.norm(light)
    lam = (dx * light[0] + dy * light[1] + dz * light[2]).clip(0, 1)
    shade = 0.05 + 0.85 * lam ** 1.3
    rgb = (rgb * shade[..., None]).clip(0, 255)
    # A thin blue rim of air.
    rim = ((np.sqrt(d2) - 0.9) / 0.1).clip(0, 1) * inside
    rgb = rgb * (1 - rim[..., None] * 0.5) + np.array([90, 150, 230]) * rim[..., None] * 0.5 * (0.3 + lam[..., None])
    alpha = ((1.0 - np.sqrt(d2)) * r + 0.5).clip(0, 1) * 255
    img = Image.fromarray(np.dstack([rgb, alpha]).astype(np.uint8), "RGBA")
    return img


def write(folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    picture().save(folder / "flight_picture.png")
    caption().save(folder / "flight_caption.png")


if __name__ == "__main__":
    import sys
    write(sys.argv[1] if len(sys.argv) > 1 else ".")
