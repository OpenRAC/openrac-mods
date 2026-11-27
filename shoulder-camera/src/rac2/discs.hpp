// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Going Commando: what the shoulder camera reads of the game, per disc. NTSC-U v1.01's addresses
// are rac2-decomp's globals (symbol_addrs/usa) named in the comments; PAL's and NTSC-U v2.00's are
// the same globals at those discs' addresses, as OpenRAC's port/game/rac2-<disc>/host/
// rac2_addresses.h gives them. The camera's and the hero's fields are the same on the three discs
// (seen in game code).

#pragma once

#include <cstdint>
#include <string_view>

namespace shoulder_camera::rac2 {

// One disc's addresses.
struct Disc {
    const char* version;          // openrac_api.version
    std::uint32_t hero;           // the hero's block (g_heroPos - 0x80): kHero* fields
    std::uint32_t hero_moby;      // his moby, 0 for none (g_pHeroMoby)
    std::uint32_t hero_state;     // s32: his state (g_heroState); +0x20 his vehicle byte
    std::uint32_t health;         // s32: his health, 0 dead (g_health)
    std::uint32_t game_state;     // s32: 0 play (g_nGameState; the boot program's: relocated)
    std::uint32_t camera_matrix;  // the last view, rows of four floats (g_cameraMatrix; relocated)
};

// clang-format off: one disc per row, the columns in the order of Disc's members.
inline constexpr Disc kDiscs[] = {
    {"ntsc",  0x00189E20u, 0x0018C0B0u, 0x0018C0B4u, 0x0018C2ECu, 0x001A8BB0u, 0x001B5570u},
    {"pal",   0x00189EA0u, 0x0018C130u, 0x0018C134u, 0x0018C36Cu, 0x001A8C60u, 0x001B5570u},
    {"ntsc2", 0x00189E20u, 0x0018C0B0u, 0x0018C0B4u, 0x0018C2ECu, 0x001A8BB0u, 0x001B54F0u},
};
// clang-format on

// The hero's block: where he stands (three floats) and his facing (the z of his rotation, radians).
inline constexpr std::uint32_t kHeroPosition = 0x80u;
inline constexpr std::uint32_t kHeroYaw = 0x98u;
// The hero's vehicle byte after his state: 2 is Giant Clank.
inline constexpr std::uint32_t kHeroVehicle = 0x20u;
inline constexpr std::uint8_t kGiantClank = 2;

// The follow camera's data (the camera's +0x70): the eye's distance from Ratchet and its height
// above him, and what the camera eases them towards, set again each tick before the stick read
// (rac2.camera.follow.read_stick).
inline constexpr std::uint32_t kCameraData = 0x70u;
inline constexpr std::uint32_t kDistance = 0x18Cu;
inline constexpr std::uint32_t kHeight = 0x190u;
inline constexpr std::uint32_t kDistanceGoal = 0x1A0u;
inline constexpr std::uint32_t kHeightGoal = 0x1ACu;

// The forward row's up part in the view (g_cameraMatrix + 8).
inline constexpr std::uint32_t kForwardZ = 8u;

// Returns the disc of `version`, or nullptr for one the mod does not know.
inline const Disc* disc_of(std::string_view version) {
    // The table's discs in turn: the one of that name.
    for (const Disc& d : kDiscs) {
        // Found.
        if (version == d.version) {
            return &d;
        }
    }

    return nullptr;
}

}  // namespace shoulder_camera::rac2
