/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Shared controller icons: where each supported disc keeps its fonts.
 *
 * The fonts are effect textures 1, 2 and 3 (GetEffectTex: PAL func_001F4868, games/rac1
 * src/game/draw.c; NTSC-U FUN_001f44b8). Each has a record of 16 bytes in the effect texture table,
 * +8 its texels' offset and +0xA its palette's, in quadwords from the effect bank the level loaded,
 * whose address the game keeps in a word. The game sends them to the GS from there each frame
 * (SetupGifPaging forgets their TEX0), so a texel changed in game memory is drawn the next frame.
 */
#ifndef CONTROLLER_ICONS_FONT_BANK_H
#define CONTROLLER_ICONS_FONT_BANK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct controller_icons_disc {
    const char* version; /* openrac_api.version */

    /* The executable's, through relocate_data. */
    uint32_t bank;  /* the effect bank's address, s32 (level_texture_payload_address) */
    uint32_t table; /* the effect texture table, 16 bytes a texture */
} controller_icons_disc;

/* NTSC-U's: PAL's less 0x100, as its neighbours 0x15F558 and 0x18D140 (port/game/rac1-ntsc
 * host/rac1_version.h). */
extern const controller_icons_disc k_controller_icons_discs[2];

/* The game's fonts (controller_icons_hash): the same in every level of the PAL disc. A font the mod does
 * not know is left as it is. */
extern const uint32_t k_controller_icons_game_fonts[1];

#define CONTROLLER_ICONS_RAM 0x02000000u /* the console's 32 MB: a font must lie within */
#define CONTROLLER_ICONS_FONT_BYTES (256u * 128u)

typedef struct controller_icons_fonts {
    uint8_t* fonts[3];
    uint8_t* palette;
    uint32_t bank;
} controller_icons_fonts;

const controller_icons_disc* controller_icons_disc_for(const char* version);
/* memory is the console's 32 MB; relocate_data maps the disc's static data addresses. */
int controller_icons_find_fonts(
    uint8_t* memory,
    uint32_t (*relocate_data)(uint32_t),
    const controller_icons_disc* disc,
    controller_icons_fonts* out
);
int controller_icons_is_game_font(uint32_t hash);

#ifdef __cplusplus
}
#endif

#endif /* CONTROLLER_ICONS_FONT_BANK_H */
