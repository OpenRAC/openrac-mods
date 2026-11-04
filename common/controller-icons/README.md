# Shared controller glyph painter

The existing Xbox icon painter, factored into a small C11 static library and extended with white
Steam Deck ABXY/L1/L2/R1/R2 glyphs. Each native mod links its own copy into its plugin; there is
no shared runtime library and no dependency on an installed Xbox mod.

`paint.c` preserves pixels and palette entries used outside the eight controller cells and refuses
to change anything when there are too few free palette entries. `font_bank.c` validates the 32 MB
memory bounds and resolves the PAL/NTSC-U texture banks. Only known original font fingerprints
are eligible for painting; unknown replacement fonts stay untouched.

The glyph masks are original procedural artwork. No font bitmap or other asset from a game disc
is distributed. To regenerate them (Pillow is a development-only dependency):

```sh
python3 xbox-icons/tools/letters.py > common/controller-icons/letters.h
```

The shared pixel tests run as `xbox_icons_paint`; the Deck library's load test also exercises the
real frame callback on generated PAL and NTSC font banks, without loading an Xbox plugin.
GPL-3.0-or-later; original copyright notices and contributors are retained.
