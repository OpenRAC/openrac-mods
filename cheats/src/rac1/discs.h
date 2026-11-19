/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * What the cheats mod uses of Ratchet & Clank's memory on each disc it knows: the globals the ammo
 * refill reads and writes. The same globals the difficulty mod (difficulty-mod/src/rac1/discs.h:
 * ammo, item_table), ammo-max (ammo-max/src/ammo_max.h) and the item randomizer
 * (item-randomizer/src/discs.h: owned) use, from the decompilation: PAL's are OpenRAC's games/rac1
 * (weapon_ammo_counts, the owned bytes D_0013D5C8 and the items' table D_L00_001C43B0), NTSC-U's
 * the same globals in games/rac1/ntsc. The hero (g_hero, D_0013F450 on PAL) is the difficulty mod's
 * `hero`; his air under water is struct Hero's underwater_air (include/structs/hero.h), which
 * try_transition_hero_water_state (src/overlays/shared/hero_vehicle_and_collision.c) takes down each
 * tick he swims without the O2 Mask, sends him up for air at 0, and puts back to full out of the
 * water.
 */
#ifndef CHEATS_RAC1_DISCS_H
#define CHEATS_RAC1_DISCS_H

#include <stdint.h>

/* One disc's addresses. */
typedef struct cheats_disc {
    const char* version; /* openrac_api.version */

    /* The executable's, through relocate_data. */
    uint32_t ammo;  /* the ammo of each item id, s32[37] (weapon_ammo_counts) */
    uint32_t owned; /* the items owned, u8[37] (GiveItem sets them) */
    uint32_t hero;  /* the hero (struct Hero): +0x22A0 his air under water, s32 */

    /* Level 0's program's, through relocate_level_data(0, ...). */
    uint32_t item_table; /* the items' records, 0x18 bytes per item id: +0xE max ammo (u16) */
} cheats_disc;

static const cheats_disc k_cheats_discs[] = {
    {"pal", 0x0013D530u, 0x0013D5C8u, 0x0013F450u, 0x001C43B0u},
    {"ntsc", 0x0013D428u, 0x0013D4C0u, 0x0013F350u, 0x001C40B0u},
};

/* Where the hero's air is, and how much a full breath is (the game puts it back to this). */
#define CHEATS_HERO_AIR 0x22A0u
#define CHEATS_FULL_AIR 10000

/* How many item ids the game has, 0..0x24: the length of the ammo and owned arrays. */
#define CHEATS_ITEMS 37

/* The size of one record of the items' table, and where its max ammo is. */
#define CHEATS_ITEM_SIZE 0x18u
#define CHEATS_ITEM_MAX_AMMO 0xEu

#endif /* CHEATS_RAC1_DISCS_H */
