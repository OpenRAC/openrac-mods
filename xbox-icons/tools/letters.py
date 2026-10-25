#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
#
# Writes src/letters.h, the letters the xbox-icons mod paints (README.md): A, X and Y drawn as
# strokes 8 times over and averaged down to a 24 x 16 glyph cell, the face button's B and the
# shoulder letters as pixel art. Each pixel is a coverage, 0 to 4 (".-+*#"). Needs Pillow:
#
#   python3 xbox-icons/tools/letters.py > xbox-icons/src/letters.h
from PIL import Image, ImageDraw

W, H, SS = 24, 16, 8


def strokes(draw):
    im = Image.new("L", (W * SS, H * SS), 0)
    draw(ImageDraw.Draw(im), lambda v: v * SS)
    im = im.resize((W, H), Image.BOX)
    return ["".join(".-+*#"[(im.getpixel((x, y)) * 4 + 127) // 255] for x in range(W)) for y in range(H)]


def line(d, s, pts, w=2.3):
    d.line([(s(x), s(y)) for x, y in pts], fill=255, width=int(s(w)), joint="curve")


def pixels(rows, x0, y0):
    out = [["."] * W for _ in range(H)]
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            out[y0 + y][x0 + x] = ch
    return ["".join(r) for r in out]


# The face buttons' letters, about the sphere's middle (9.5, 7.5).
def a(d, s):
    line(d, s, [(5.3, 12.6), (9.5, 3.0), (13.7, 12.6)])
    line(d, s, [(7.0, 9.6), (12.0, 9.6)], 2.0)


def x(d, s):
    line(d, s, [(5.6, 3.4), (13.4, 12.2)])
    line(d, s, [(13.4, 3.4), (5.6, 12.2)])


def y(d, s):
    line(d, s, [(5.6, 3.2), (9.5, 8.0), (13.4, 3.2)])
    line(d, s, [(9.5, 7.4), (9.5, 12.4)])


B = ["######+..", "##...+##.", "##....##.", "##...+##.", "######+..",
     "##...-##+", "##....+##", "##....+##", "##...-##*", "#######+."]

# The shoulders' letters, two to a pill, 2 pixels apart about its middle.
SHOULDER = {
    "L": ["##....", "##....", "##....", "##....", "##....", "##....", "##....", "######", "######"],
    "R": ["#####+", "##..##", "##..##", "#####.", "##.##.", "##..##", "##..##", "##..##", "##..##"],
    "B": ["#####+", "##..##", "##..##", "#####.", "##..##", "##..##", "##..##", "##..##", "#####+"],
    "T": ["######", "######", "..##..", "..##..", "..##..", "..##..", "..##..", "..##..", "..##.."],
}


def word(first, second):
    rows = [["."] * W for _ in range(H)]
    for k, ch in enumerate((first, second)):
        for yy, r in enumerate(SHOULDER[ch]):
            for xx, v in enumerate(r):
                rows[3 + yy][5 + k * 8 + xx] = v
    return ["".join(r) for r in rows]


GLYPHS = [
    ("a", strokes(a)), ("b", pixels(B, 5, 3)), ("x", strokes(x)), ("y", strokes(y)),
    ("lb", word("L", "B")), ("lt", word("L", "T")), ("rb", word("R", "B")), ("rt", word("R", "T")),
]

print("/* SPDX-License-Identifier: GPL-3.0-or-later")
print(" * Copyright (c) 2026 the OpenRAC contributors")
print(" *")
print(" * Written by tools/letters.py: the letters xbox-icons paints, one 24 x 16 glyph cell each, a")
print(" * coverage per pixel (\".-+*#\", 0 to 4). */")
print("#ifndef XBOX_ICONS_LETTERS_H")
print("#define XBOX_ICONS_LETTERS_H")
print()
for name, rows in GLYPHS:
    print(f"static const char* const k_letter_{name}[16] = {{")
    for r in rows:
        print(f'    "{r}",')
    print("};")
print()
print("#endif /* XBOX_ICONS_LETTERS_H */")
