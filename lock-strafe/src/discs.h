/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * What the lock-strafe uses of the game, on each disc it knows: the addresses of the hero, the pad
 * and the globals it reads, and the names of the game's functions it calls (as each disc's build
 * of the port names them: openrac_api.function). The PAL disc's are games/rac1's; the NTSC-U disc's
 * come from OpenRAC's port/game/rac1-ntsc/host/rac1_version.h, which maps the one to the other.
 */
#ifndef LOCK_STRAFE_DISCS_H
#define LOCK_STRAFE_DISCS_H

#include <stdint.h>

typedef struct lock_strafe_disc {
    const char* version; /* openrac_api.version */
    /* The executable's: used as they are. */
    uint32_t hero; /* the hero (Hero) */
    uint32_t pad;  /* the first pad's state (PadState) */
    /* The executable's, through relocate_data. */
    uint32_t frame_scale; /* the tick's length, for the standing turn's rates */
    uint32_t turn_step;   /* the turn's step, for its limit */
    /* Level 0's program's, through relocate_level_data(0, ...). */
    uint32_t level_mode; /* the game mode: 0 in play */
    uint32_t camera_yaw; /* the camera's yaw */
    uint32_t camera_pan; /* the follow camera's pan weight */
    /* The game's functions it calls. */
    const char* fast_cos;          /* float (float) */
    const char* fast_sin;          /* float (float) */
    const char* camera_slide;      /* void (int): the strafe camera's slide along with the hero */
    const char* camera_spring;     /* void (float, float): its lateral spring */
    const char* camera_pan_off;    /* void (void): its panning cleared */
    const char* camera_look_along; /* void (u32 direction, float, float): look along a heading */
} lock_strafe_disc;

static const lock_strafe_disc k_lock_strafe_discs[] = {
    {
        "pal",
        0x0013F450u,
        0x0013CA40u,
        0x0015EE64u,
        0x0015EE6Cu,
        0x0015F6A8u,
        0x00166ED8u,
        0x0015F040u,
        "func_001F9F90",
        "func_001F9FA8",
        "func_L00_002E9A18",
        "func_L00_002E9A40",
        "func_L00_002E9AF8",
        "func_L00_002E9DC8",
    },
    {
        "ntsc",
        0x0013F350u,
        0x0013C940u,
        0x0015ED64u,
        0x0015ED6Cu,
        0x0015F5C4u,
        0x00166DD8u,
        0x0015EF40u,
        "FUN_001f9dc8",
        "FUN_001f9de0",
        "FUN_L00_002e8568",
        "FUN_L00_002e8590",
        "FUN_L00_002e8648",
        "FUN_L00_002e8918",
    },
};

#endif /* LOCK_STRAFE_DISCS_H */
