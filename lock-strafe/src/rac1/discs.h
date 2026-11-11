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
    uint32_t frame_scale; /* g_frame_scale_sq: the turns' accel and damping are scaled by it */
    uint32_t turn_step;   /* g_frame_time: a second's share in a tick, the turns' limits by it */
    /* Level 0's program's, through relocate_level_data(0, ...). */
    uint32_t level_mode; /* the game mode: 0 in play */
    uint32_t camera_yaw; /* the camera's yaw */
    uint32_t camera_pan; /* the follow camera's pan weight */
    uint32_t camera_now; /* gCamera.current: the camera slot in use (its id at +0x86) */
    uint32_t camera_pos; /* the camera's position (gCamera +0x140) */
    uint32_t mobys;      /* the list of live mobys the aim walks (suck_moby_list) */
    uint32_t reticles;   /* the reticle queue (3 entries of 0x30 bytes, the count at +0xC0) */
    /* The game's functions it calls. */
    const char* fast_cos;          /* float (float) */
    const char* fast_sin;          /* float (float) */
    const char* camera_slide;      /* void (int): the strafe camera's slide along with the hero */
    const char* camera_spring;     /* void (float, float): its lateral spring */
    const char* camera_pan_off;    /* void (void): its panning cleared */
    const char* camera_look_along; /* void (u32 direction, float, float): look along a heading */
    const char* enter_state;       /* int (int state, int animate): HeroEnterMoveState */
    const char* stick_direction;   /* int (void): stick_direction_vs_facing */
    const char* reticle;           /* EffectQueue_Add: a reticle queued for this frame */
    int reticle_args;              /* its prototype's order (LOCK_STRAFE_RETICLE_ARGS_*) */
} lock_strafe_disc;

/* EffectQueue_Add's parameters as each disc's build declares them (a call through the pointer
 * must match it: Windows passes by position). The same registers either way. */
enum {
    /* (owner, colour, size, angle, u32 point, sprite, mode, float) */
    LOCK_STRAFE_RETICLE_ARGS_PAL = 0,
    /* (owner, colour, u32 point, sprite, mode, size, angle, float) */
    LOCK_STRAFE_RETICLE_ARGS_NTSC = 1
};

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
        0x00166F00u,
        0x00166EC0u,
        0x001600A4u,
        0x00169140u,
        "func_001F9F90",
        "func_001F9FA8",
        "func_L00_002E9A18",
        "func_L00_002E9A40",
        "func_L00_002E9AF8",
        "func_L00_002E9DC8",
        "func_L00_00222B80",
        "func_L00_00222580",
        "func_L00_001EE2E0",
        LOCK_STRAFE_RETICLE_ARGS_PAL,
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
        0x00166E00u,
        0x00166DC0u,
        0x0015FFE4u,
        0x00169040u,
        "FUN_001f9dc8",
        "FUN_001f9de0",
        "FUN_L00_002e8568",
        "FUN_L00_002e8590",
        "FUN_L00_002e8648",
        "FUN_L00_002e8918",
        "FUN_L00_002223f8",
        "FUN_L00_00221df8",
        "FUN_L00_001edf60",
        LOCK_STRAFE_RETICLE_ARGS_NTSC,
    },
};

#endif /* LOCK_STRAFE_DISCS_H */
