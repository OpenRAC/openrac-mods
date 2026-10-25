/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The xbox-icons mod (README.md): Xbox buttons painted over the PlayStation 2 ones of the game's
 * three fonts, from the game's own pixels.
 *
 * A font is an effect texture, 256 x 128 texels of 8 bits, and the three share one palette of 256
 * colours (32-bit, alpha 0x80 opaque, in the GS's CSM1 order: within each 32 entries the middle two
 * groups of 8 trade places). The pad icons are the cells of row 96, 24 x 16 texels each (the glyph
 * tables, port/assets/world/font.h):
 *
 *   u    0     24      48        72      96  120  144  168      208, 232
 *   byte 0x10  0x13    0x12      0x11    0x14 0x16 0x15 0x17     0x18, 0x19
 *        Cross Square  Triangle  Circle  L1   L2   R1   R2       the sticks (kept)
 *        A     X       Y         B       LB   LT   RB   RT
 *
 * Painting, for each font: the four face buttons' sphere without its symbol (at each texel the
 * least coloured of the four buttons; a texel all four symbols cover takes the nearest clean one),
 * the shoulders a pill of the same sphere (its left half, its middle column, its right half); then
 * the letters (letters.h) in colours of their own, each coverage a palette entry, with a darker ring
 * as the game's symbols have. The new colours go to palette entries no texel uses any more: of the
 * 256, the game's symbols alone use about a hundred.
 */
#ifndef XBOX_ICONS_PAINT_H
#define XBOX_ICONS_PAINT_H

#include <stdint.h>

#define XBOX_ICONS_WIDTH 256u      /* texels a row of a font */
#define XBOX_ICONS_HEIGHT 128u
#define XBOX_ICONS_ROW 96u         /* the pad icons' row of cells */
#define XBOX_ICONS_CELLS_WIDTH 192u /* the eight cells painted, u 0..191 */

/* FNV-1a over the eight cells of each font and the palette: which fonts these are (the game's, or
 * painted). */
uint32_t xbox_icons_hash(uint8_t* const fonts[3], const uint8_t* palette);

/* Paints the eight cells of each font and the palette entries the letters use. Returns 0, or 1 when
 * too few palette entries are free (nothing is changed then). */
int xbox_icons_paint(uint8_t* const fonts[3], uint8_t* palette);

#endif /* XBOX_ICONS_PAINT_H */
