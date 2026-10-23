// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The hero's fields the mod reads, and where they are.
//
// The hero's structure is the game's own (games/rac1/include/hero.h): the
// offsets are from there, and the base address is the one the port uses
// (port/game/rac1/host/front_end.c). The NTSC-U copy of the game has its
// data 0x100 lower.

#pragma once

#include <cstdint>

namespace dualsense {

// From the hero's base.
inline constexpr std::uint32_t kHeroHealthOffset = 0x22A8;      // health.hp
inline constexpr std::uint32_t kHeroItemIdOffset = 0x10B8;      // items[0].item_id
inline constexpr std::uint32_t kHeroWeaponMobyOffset = 0x1090;  // items[0].moby
inline constexpr std::uint32_t kHeroFireFlagOffset = 0x20A8;    // unk20A8
inline constexpr std::uint32_t kHeroFireAgeOffset = 0x1BC;      // unk1BC
inline constexpr std::uint32_t kHeroStateOffset = 0x2084;       // state.current

// The hero's base, per disc.
inline constexpr std::uint32_t kHeroBasePal = 0x0013F450u;
inline constexpr std::uint32_t kHeroBaseNtsc = 0x0013F350u;

// The maximum hit points, the global gMaxNanotech (games/rac1/include/names.h).
inline constexpr std::uint32_t kMaxHealthPal = 0x0015EEA0u;
inline constexpr std::uint32_t kMaxHealthNtsc = 0x0015EDA0u;

// The weapon's moby holds its own counters, in its pvars (moby+0x78).
inline constexpr std::uint32_t kMobyPvarsOffset = 0x78;
inline constexpr std::uint32_t kPvarCounter = 0x00;  // the shot's cooldown
inline constexpr std::uint32_t kPvarShots = 0x04;    // the R.Y.N.O.'s missiles

// The game's RAM: the guest's 32 MB. A pointer that is not in it is not a
// pointer the game meant, and following it would fault the game.
inline constexpr std::uint32_t kGameRamEnd = 0x02000000u;

// Whether an address, and the bytes after it, are in the game's RAM.
inline bool in_ram(std::uint32_t address, std::uint32_t size) {
    return address <= kGameRamEnd && size <= kGameRamEnd - address;
}

}  // namespace dualsense
