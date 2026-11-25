// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// What the lock-strafe uses of the game, on each disc it knows: the addresses of the hero, the pad
// and the globals it reads, and the names of the game's functions it calls (as each disc's build
// of the port names them: openrac_api.function). The PAL disc's are games/rac1's; the NTSC-U
// disc's come from OpenRAC's port/game/rac1-ntsc/host/rac1_version.h, which maps the one to the
// other.

#pragma once

#include <cstdint>

namespace lock_strafe::rac1 {

// Where one disc keeps what the strafe reads and the names of the game's functions it calls.
struct Disc {
    // The value of openrac_api.version that names the disc.
    const char* version;

    // The executable's, used as they are: the hero (Hero) and the first pad's state (PadState).
    std::uint32_t hero;
    std::uint32_t pad;

    // The executable's, through relocate: g_frame_scale_sq (the turns' accel and damping are
    // scaled by it) and g_frame_time (a second's share in a tick, the turns' limits scaled by it).
    std::uint32_t frame_scale;
    std::uint32_t turn_step;

    // Level 0's program's, through relocate_level(0, ...): the game mode (0 in play), the camera's
    // yaw, the follow camera's pan weight, and gCamera.current, the camera slot in use (its id at
    // +0x86).
    std::uint32_t level_mode;
    std::uint32_t camera_yaw;
    std::uint32_t camera_pan;
    std::uint32_t camera_now;

    // The game's functions it calls, by the name each disc's port gives them. The signature is in
    // the comment on each.
    const char* fast_cos;           // float (float)
    const char* fast_sin;           // float (float)
    const char* camera_slide;       // void (int): the strafe camera's slide along with the hero
    const char* camera_spring;      // void (float, float): its lateral spring
    const char* camera_pan_off;     // void (void): its panning cleared
    const char* camera_look_along;  // void (u32 direction, float, float): look along a heading
    const char* enter_state;        // int (int state, int animate): HeroEnterMoveState
    const char* stick_direction;    // int (void): stick_direction_vs_facing
};

// The discs the mod knows, in the field order of Disc (seen in game code): matched by
// openrac_api.version at load.
inline constexpr Disc kDiscs[] = {
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
        "func_001F9F90",
        "func_001F9FA8",
        "func_L00_002E9A18",
        "func_L00_002E9A40",
        "func_L00_002E9AF8",
        "func_L00_002E9DC8",
        "func_L00_00222B80",
        "func_L00_00222580",
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
        "FUN_001f9dc8",
        "FUN_001f9de0",
        "FUN_L00_002e8568",
        "FUN_L00_002e8590",
        "FUN_L00_002e8648",
        "FUN_L00_002e8918",
        "FUN_L00_002223f8",
        "FUN_L00_00221df8",
    },
};

}  // namespace lock_strafe::rac1
