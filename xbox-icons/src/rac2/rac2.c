/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The xbox-icons mod (README.md): where Going Commando keeps its fonts, on each disc.
 *
 * Its fonts are Ratchet & Clank's, kept the same way. The font prints pass effect textures 1, 2 and
 * 3 (the large, the small and the third font: 0x0027F538, 0x0027F5B8, 0x0027FBD8 and their
 * siblings, OpenRAC port/game/rac2/host/font.c) to GetEffectTex, 0x0027C878 (rac2-decomp
 * src/game/0027C878-g8.c). It reads the texture's record of 16 bytes in the effect texture table
 * (ref_0027C878_04), +8 the texels' offset and +0xA the palette's in quadwords from the effect bank
 * (ref_0027C878_06, a word), +0xC and +0xE the log2 of its width and height, and queues the upload
 * from bank memory. The level's loader (0x002EAEE4 on NTSC-U) sets the word, and 0x00291DE0 fills
 * the table from the level's core index (+0x58 count and table, +0x68 the bank). Every frame
 * 0x0027C540 (rac1's SetupGifPaging) zeroes each record's TEX0, so GetEffectTex uploads the font
 * from game memory again: a texel changed there is drawn the next frame.
 *
 * The glyphs are rac1's: the glyph tables (0x00263B50, 0x00263EF0, 0x00264290, the same bytes on
 * every disc) put 0x10 Cross at u 0, 0x13 Square at 24, 0x12 Triangle at 48, 0x11 Circle at 72,
 * 0x14 L1 at 96, 0x16 L2 at 120, 0x15 R1 at 144, 0x17 R2 at 168 and the sticks at 208 and 232, all
 * in row 96. In every level of the three discs the fonts are 256 x 128 texels of 8 bits with one
 * palette for the three, at bank + 0xC00, + 0x8C00, + 0x10C00 and the palette at + 0x800.
 *
 * The addresses are the executable's (each level's program has its own copy of the engine, which
 * relocate_data finds). Read from each disc's boot program: the code of GetEffectTex,
 * SetupGifPaging and the table's loader matched with NTSC-U's, address operands aside, and the
 * store of the bank's word found on each (NTSC-U 0x002EAEE4, PAL 0x002EBDFC, NTSC-U v2.00
 * 0x002EBD3C). PAL keeps both where NTSC-U v1.01 does; v2.00's data are 0x80 lower (rac2-decomp
 * docs/VERSIONS.md: its .bss and .data), the table as SetupGifPaging (0x0027CBA0 there) and
 * GetEffectTex (0x0027CED8) name it.
 */
#include "xbox_icons.h"

static const xbox_icons_disc k_discs[] = {
    {"ntsc", 0x001B1604u, 0x001B9740u},
    {"pal", 0x001B1604u, 0x001B9740u},
    {"ntsc2", 0x001B1584u, 0x001B96C0u},
};

/* The game's fonts (xbox_icons_hash), taken from every level of the three discs with OpenRAC's
 * level reader (editor/level.py load_wad_level: the core index's effect textures 1 to 3 and their
 * palette in the core data, which the game uploads as they are):
 *
 *   0xA38230EA  NTSC-U v1.01, all but five levels (Ratchet & Clank's own fonts, the same hash)
 *   0xE85191A2  NTSC-U v1.01 levels 1, 2, 5, 11 and 12 (touched-up letters and buttons);
 *              PAL and v2.00, every level but 4
 *   0xE49DBF6B  NTSC-U v1.01 level 4 (Barlow: 72 palette entries a shade apart)
 *   0x5BB96C3F  PAL and v2.00 level 4
 *
 * The four paint alike. A font the mod does not know is left as it is. */
static const uint32_t k_fonts[] = {0xA38230EAu, 0xE85191A2u, 0xE49DBF6Bu, 0x5BB96C3Fu};

const xbox_icons_game xbox_icons_rac2 = {
    "rac2", k_discs, sizeof k_discs / sizeof k_discs[0],
    "Going Commando's NTSC-U, PAL and NTSC-U v2.00 discs",
    k_fonts, sizeof k_fonts / sizeof k_fonts[0],
};
