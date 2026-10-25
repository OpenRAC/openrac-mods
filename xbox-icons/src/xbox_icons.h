/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The xbox-icons mod (README.md): where the game keeps its fonts, on each disc.
 *
 * The fonts are effect textures 1, 2 and 3 (GetEffectTex: PAL func_001F4868, games/rac1
 * src/game/draw.c; NTSC-U FUN_001f44b8). Each has a record of 16 bytes in the effect texture table,
 * +8 its texels' offset and +0xA its palette's, in quadwords from the effect bank the level loaded,
 * whose address the game keeps in a word. The game sends them to the GS from there each frame
 * (SetupGifPaging forgets their TEX0), so a texel changed in game memory is drawn the next frame.
 */
#ifndef XBOX_ICONS_H
#define XBOX_ICONS_H

#include <stdint.h>

typedef struct xbox_icons_disc {
    const char* version; /* openrac_api.version */

    /* The executable's, through relocate_data. */
    uint32_t bank;  /* the effect bank's address, s32 (level_texture_payload_address) */
    uint32_t table; /* the effect texture table, 16 bytes a texture */
} xbox_icons_disc;

/* NTSC-U's: PAL's less 0x100, as its neighbours 0x15F558 and 0x18D140 (port/game/rac1-ntsc
 * host/rac1_version.h). */
static const xbox_icons_disc k_xbox_icons_discs[] = {
    {"pal", 0x0015F560u, 0x0018D540u},
    {"ntsc", 0x0015F460u, 0x0018D440u},
};

/* The game's fonts (xbox_icons_hash): the same in every level of the PAL disc. A font the mod does
 * not know is left as it is. */
static const uint32_t k_xbox_icons_game_fonts[] = {0xA38230EAu};

#define XBOX_ICONS_RAM 0x02000000u /* the console's 32 MB: a font must lie within */
#define XBOX_ICONS_FONT_BYTES (256u * 128u)

#endif /* XBOX_ICONS_H */
