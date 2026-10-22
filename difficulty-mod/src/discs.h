/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * What the difficulty mod uses of the game on each disc it knows: the addresses of the globals it
 * reads and writes, and the names of the game's functions it calls (as each disc's build of the
 * port names them: openrac_api.function). The PAL disc's (v2.00, SCES-50916) are OpenRAC's
 * games/rac1; the NTSC-U disc's (SCUS-97199) are the NTSC decomp's (games/rac1/ntsc), found from
 * the same code in its counterpart of each function (games/rac1/config/overlays/us_map.tsv).
 */
#ifndef DIFFICULTY_DISCS_H
#define DIFFICULTY_DISCS_H

#include <stdint.h>

typedef struct dm_disc {
    const char* version; /* openrac_api.version */

    /* The executable's, through relocate_data. */
    uint32_t hero;         /* the hero (Hero): +0x190 target speed, +0x22A8 health */
    uint32_t tick;         /* the tick constants, 8 floats: ratio, ratio^2, 1/ratio, seconds,
                              seconds^2, seconds^3, (an int), seconds (SetVideoModeTiming) */
    uint32_t max_health;   /* gMaxNanotech, s32 */
    uint32_t ammo;         /* the ammo of each item id, s32[37] */
    uint32_t item_in_hand; /* the item id in hand, s32 */
    uint32_t upgrades;     /* the special items' bytes: [4] Premium, [5] Ultra Nanotech */

    /* Level 0's program's, through relocate_level_data(0, ...). */
    uint32_t level_mode;  /* the game mode: 0 in play */
    uint32_t item_table;  /* the items' records, 0x18 bytes per item id: +0 price, +4 the other
                             price, +8 and +0xA ammo prices, +0xE max ammo */
    uint32_t load_state;  /* the level load's state: +4 the gameplay file's header */
    uint32_t pool;        /* the moby array's base; +4 the first spare slot, +8 the pool's end,
                             +0x10 the spare slots' pvars */
    uint32_t death_plane; /* the level's death height, f32 */
    uint32_t hit_ring;    /* the 64 pending hits, 0x40 bytes each (+0x2C the damage, +0x34 the
                             moby it is for) */

    /* The game's functions it calls. */
    const char* create_moby;  /* u32 (i32 class): CreateMoby */
    const char* delete_moby;  /* void (u32 moby): DeleteMoby */
    const char* build_matrix; /* void (u32 moby): MobyBuildMatrix (rows, sphere, grid cells) */
    const char* ground;       /* f32 (u32 pos, i32 flags, f32 up): ProbeGroundHeight */
    const char* line; /* i32 (u32 from, u32 to, i32 flags, u32 skip, u32 hit): the line test */
} dm_disc;

static const dm_disc k_dm_discs[] = {
    {
        "pal",
        0x0013F450u,
        0x0015EE60u,
        0x0015EEA0u,
        0x0013D530u,
        0x00140508u,
        0x0013D490u,
        0x0015F6A8u,
        0x001C43B0u,
        0x00173F00u,
        0x00160098u,
        0x0015F718u,
        0x00178200u,
        "func_0020D348",
        "func_0020D678",
        "func_L00_00251E30",
        "func_00214358",
        "func_L00_001EFFF0",
    },
    {
        "ntsc",
        0x0013F350u,
        0x0015ED60u,
        0x0015EDA0u,
        0x0013D428u,
        0x00140408u,
        0x0013D388u,
        0x0015F5C4u,
        0x001C40B0u,
        0x00173E00u,
        0x0015FFD8u,
        0x0015F638u,
        0x00178100u,
        "FUN_0020c4f8",
        "FUN_0020c828",
        "FUN_L00_00250df8",
        "FUN_00213508",
        "FUN_L00_001efc70",
    },
};

#endif /* DIFFICULTY_DISCS_H */
