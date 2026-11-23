"""Writes the OpenRAC Museum mod's data (mod/src/museum_data.h) from the exhibits: each tile's place
and words, cut into HUD lines, and the places of the mod's stations (the I-5 pad, the big head
plate, the effects case, the 3 AM door).

    python3 tools/mod_data.py            # rewrites mod/src/museum_data.h
    python3 tools/mod_data.py --check    # fails when the header is not up to date
"""

import argparse
import sys
import textwrap
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import exhibits  # noqa: E402

OUT = HERE.parent / "mod" / "src" / "museum_data.h"
# Characters a HUD line holds in the game's small font at the mod's scale.
LINE = 44
LINES_MAX = 5


class Null:
    """Takes the shapes the exhibits make and keeps nothing: only their places are needed."""

    def __getattr__(self, name):
        return lambda *a, **k: None


# Button names in the words, drawn as the player's device shows the button (the SDK's button
# tokens, OPENRAC_UI_CROSS ...): one glyph wide each.
TOKENS = {"{Cross}": "\x10", "{Circle}": "\x11", "{Triangle}": "\x12", "{Square}": "\x13",
          "{L1}": "\x14", "{R1}": "\x15", "{L2}": "\x16", "{R2}": "\x17"}


def tokens(text):
    for name, code in TOKENS.items():
        text = text.replace(name, code)
    return text


def c_string(text):
    text = text.replace("\\", "\\\\").replace('"', '\\"')
    # Octal escapes: always three digits, so a digit after one is never read into it.
    return '"' + "".join(f"\\{ord(ch):03o}" if ord(ch) < 32 else ch for ch in text) + '"'


def render():
    exhibits.build(Null(), [])
    out = ["/* SPDX-License-Identifier: GPL-3.0-or-later",
           " * Written by tools/mod_data.py from tools/exhibits.py: do not edit by hand. */",
           "#ifndef MUSEUM_DATA_H", "#define MUSEUM_DATA_H", "",
           "#include \"museum_types.h\"", "",
           f"#define MUSEUM_LINES_MAX {LINES_MAX}", "",
           "static const museum_station MUSEUM_STATIONS[] = {"]
    for key, x, y, z in exhibits.STATIONS:
        title, text = exhibits.WORDS[key]
        text = tokens(text)
        lines = textwrap.wrap(text, LINE)
        if len(lines) > LINES_MAX:
            raise SystemExit(f"{key}: {len(lines)} lines, at most {LINES_MAX}")
        if any(ord(ch) > 126 for ch in title + text):
            raise SystemExit(f"{key}: a character the HUD font has not")
        body = ", ".join(c_string(l) for l in lines)
        out.append(f"    {{{c_string(key)}, {x:.2f}f, {y:.2f}f, {z:.2f}f, {c_string(title)}, {len(lines)}, {{{body}}}}},")
    out += ["};", ""]
    for name, (x, y, z, r) in sorted(exhibits.ZONES.items()):
        out.append(f"static const museum_zone MUSEUM_ZONE_{name.upper()} = {{{x:.2f}f, {y:.2f}f, {z:.2f}f, {r:.2f}f}};")
    out += ["", "#endif", ""]
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    text = render()
    if args.check:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            raise SystemExit(f"{OUT} is out of date: run tools/mod_data.py")
        return
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
