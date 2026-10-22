/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * What the keyboard and mouse mod reads and calls of the game on each disc it knows, by the names
 * each disc's build of the port gives them (openrac_api.function). The PAL disc's are games/rac1's
 * in OpenRAC, the NTSC-U disc's its rac1/ntsc decompilation's, as in the lock-strafe's discs.h.
 */
#ifndef KBM_DISCS_H
#define KBM_DISCS_H

#include <stdint.h>

typedef struct kbm_disc {
    const char* version; /* openrac_api.version */
    /* The executable's: used as they are. */
    uint32_t hero;         /* the hero (Hero) */
    uint32_t quick_select; /* the Quick Select's eight slots: item ids, 0 for an empty one */
    /* Level 0's program's, through relocate_level_data(0, ...). */
    uint32_t level_mode; /* the game mode: 0 in play, 3 the pause menu, 5 a vendor, ... */
    /* The game's functions the camera's reset calls. */
    const char* fast_cos;          /* float (float) */
    const char* fast_sin;          /* float (float) */
    const char* camera_look_along; /* void (u32 direction, float, float): look along a heading */
} kbm_disc;

/* In the hero, on both discs. */
#define KBM_HERO_FACING 0x98u    /* f32: the way he faces (rot.z) */
#define KBM_HERO_ITEM 0x10B8u    /* i32: the item in hand (items[0].item_id) */
#define KBM_HERO_VEHICLE 0x20A4u /* u8: 0 on foot */
#define KBM_HERO_PENDING 0x20B8u /* i32: the item to take next (what the Quick Select writes) */

static const kbm_disc k_kbm_discs[] = {
    {"pal", 0x0013F450u, 0x00141FA0u, 0x0015F6A8u, "func_001F9F90", "func_001F9FA8", "func_L00_002E9DC8"},
    {"ntsc", 0x0013F350u, 0x00141EA0u, 0x0015F5C4u, "FUN_001f9dc8", "FUN_001f9de0", "FUN_L00_002e8918"},
};

#endif /* KBM_DISCS_H */
