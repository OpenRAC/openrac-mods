/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The xbox-icons mod (README.md): where Ratchet & Clank keeps its fonts, on each disc.
 *
 * The fonts are effect textures 1, 2 and 3 (GetEffectTex: PAL func_001F4868, games/rac1
 * src/game/draw.c; NTSC-U FUN_001f44b8). Each has a record of 16 bytes in the effect texture table,
 * +8 its texels' offset and +0xA its palette's, in quadwords from the effect bank the level loaded,
 * whose address the game keeps in a word. The game sends them to the GS from there each frame
 * (SetupGifPaging forgets their TEX0), so a texel changed in game memory is drawn the next frame.
 */
#include "xbox_icons.h"

/* NTSC-U's: PAL's less 0x100, as its neighbours 0x15F558 and 0x18D140 (port/game/rac1-ntsc
 * host/rac1_version.h). The bank's word is level_texture_payload_address, an s32. */
static const xbox_icons_disc k_discs[] = {
    {"pal", 0x0015F560u, 0x0018D540u},
    {"ntsc", 0x0015F460u, 0x0018D440u},
};

/* The game's fonts (xbox_icons_hash): the same in every level of the PAL disc. A font the mod does
 * not know is left as it is. */
static const uint32_t k_fonts[] = {0xA38230EAu};

const xbox_icons_game xbox_icons_rac1 = {
    "rac1", k_discs, sizeof k_discs / sizeof k_discs[0], "the PAL and NTSC-U discs",
    k_fonts, sizeof k_fonts / sizeof k_fonts[0],
};
