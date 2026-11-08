/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * What the difficulty mod uses of Going Commando on each disc OpenRAC plays (games/rac2/game.json
 * "versions"): the addresses of the globals it reads and writes. The USA v1.01 disc's (ntsc,
 * SCUS-97268) are rac2-decomp's (games/rac2/decomp), as the port's C of the functions the mod hooks
 * names them: Hero_TakeDamage (func_L00_002A4EF8) reads the hero record and the armour beside the
 * max health, Weapon_TakeAmmo (func_L00_002D3F30) the item in hand, the ammo and level 0's weapon
 * table, the max health's recount (func_L00_002D5EB8) the boosts. PAL's (SCES-51607) and USA
 * v2.00's (ntsc2, SCUS-97268 Greatest Hits) are the same names in each release's build of the port
 * (port/game/rac2-<release>/release_names.json, rac2_addresses.h), each checked against that
 * release's C of the same functions (PAL's func_L00_002A4D58 and func_L00_002D4458, v2.00's
 * func_L00_002A4EF8 and func_L00_002D42D0). The fields of what they point at are the same on the
 * three discs (port/game/rac2/README.md: the moby and hero records are the same, read by the same
 * code).
 */
#ifndef DIFFICULTY_RAC2_DISCS_H
#define DIFFICULTY_RAC2_DISCS_H

#include <stdint.h>

typedef struct dm2_disc {
    const char* version; /* openrac_api.version */
    float tick_seconds;  /* a game tick: the disc's frame rate (hostgen.json frame_rate) */

    /* Every program's (below the level programs' 0x1A7C80: relocate_data leaves them). */
    uint32_t hero;       /* the hero record (D_00189E20): +0x1B0 his target ground speed, f32;
                            +0x1248 the item in hand, s32; +0x2290 his moby; +0x24CC his health,
                            s32 (g_health) */
    uint32_t ammo;       /* the ammo of each item number, s32 (g_weaponAmmo) */
    uint32_t max_health; /* the max health, s32 (g_maxHealth: the save block's +0x10) */
    uint32_t boosts;     /* the ten nanotech boosts, a byte each (the save block's +0x38) */

    /* The boot program's, each level's copy through relocate_data. */
    uint32_t game_state; /* the game's state, s32: 0 in play (g_nGameState) */

    /* Level 0's program's, through relocate_level_data(0, ...): the items' records, 0xE0 bytes
       each (g_weaponTable's level copy): +0x80 the price, s32; +0x88 the ammo's price, u16; +0x8E
       the most ammo, u16. */
    uint32_t weapon_table;
} dm2_disc;

static const dm2_disc k_dm2_discs[] = {
    {"ntsc",
     1.0f / 60.0f,
     0x00189E20u,
     0x00139688u,
     0x001A7A00u,
     0x001A7A28u,
     0x001A8BB0u,
     0x00262570u},
    {"pal",
     1.0f / 50.0f,
     0x00189EA0u,
     0x00139708u,
     0x001A7A88u,
     0x001A7AB0u,
     0x001A8C60u,
     0x00262570u},
    {"ntsc2",
     1.0f / 60.0f,
     0x00189E20u,
     0x00139688u,
     0x001A7A08u,
     0x001A7A30u,
     0x001A8BB0u,
     0x002624F0u},
};

#endif /* DIFFICULTY_RAC2_DISCS_H */
