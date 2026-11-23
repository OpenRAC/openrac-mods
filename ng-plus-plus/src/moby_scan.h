// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The level's live mobys as the perks see them (perks.h): read straight from the game's moby
// table, as weapon_visuals.cpp does, once a tick at most. OpenRAC's moby_list2 would do as well,
// but its flags are the game's own on rac2 (no OPENRAC_MOBY_CREATURE), and the perks need the
// enemy record (+0x24's kind 5) the table read gives anyway.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game_view.h"
#include "homing.h"
#include "openrac/mod.hpp"

namespace ng_plus_plus {

// A moby's fields the perks read and write (OpenRAC's port/game/rac2/host/play.c's MOBY_*).
inline constexpr std::uint32_t kMobyPosition = 0x10u;   // f32[3]: where it is.
inline constexpr std::uint32_t kMobyStateByte = 0x20u;  // u8: 0x80 and over dead or free.
inline constexpr std::uint32_t kMobyRecordBytes = 0x100u;

// One live moby.
struct ScannedMoby {
    std::uint32_t address = 0;  // Its game address.
    int slot = 0;               // Its slot in the table.
    int moby_class = 0;         // Its class (+0xAA).
    std::array<float, 3> position{};
    bool enemy = false;    // Its class record is an enemy's (kind 5).
    bool spawned = false;  // The game made it (past the level's placed mobys).
};

// Returns the squared distance of `a` and `b`.
inline float distance2(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    const float x = a[0] - b[0];
    const float y = a[1] - b[1];
    const float z = a[2] - b[2];
    return x * x + y * y + z * z;
}

// The level's live mobys, read when asked.
class MobyScan {
public:
    explicit MobyScan(const GameView& view) : m_view(view) {}

    // Reads the table again unless it was read in tick `tick` already. False outside a level or
    // with a table not laid out.
    bool read(std::uint32_t tick);

    // The mobys read last, in slot order.
    const std::vector<ScannedMoby>& mobys() const { return m_mobys; }

    // Returns the one at `address` in the last read, or null.
    const ScannedMoby* find(std::uint32_t address) const;

    // Returns the slot of the moby at `address` in `view`'s level now, -1 for none.
    static int slot_of(const GameView& view, std::uint32_t address);

    // Returns the address of the moby in `slot`, 0 for none read.
    std::uint32_t address_of(int slot) const;

    // Returns the game address of the moby in `slot` of `view`'s level now, 0 for none.
    static std::uint32_t slot_address(const GameView& view, int slot);

    // Returns whether the moby at `address` is alive now and of class `moby_class` (read now).
    bool alive(std::uint32_t address, int moby_class) const;

    // Returns the position of the moby at `address`, read now.
    std::array<float, 3> position(std::uint32_t address) const;

    // Returns what decides whether a shot may fly at the moby at `address` (homing.h), read now.
    TargetFacts facts(std::uint32_t address) const;

    // Returns whether a shot may fly at the moby at `address` now (homing.h's targetable).
    bool targetable(std::uint32_t address) const { return ng_plus_plus::targetable(facts(address)); }

    // Returns the height above its position a shot aims at on the moby at `address` (its health
    // record's +0x10, as the game's pick aims), 0 for none.
    float aim_height(std::uint32_t address) const;

private:
    const GameView& m_view;
    std::vector<ScannedMoby> m_mobys;
    std::uint32_t m_base = 0;
    std::uint32_t m_tick = 0;
    bool m_read = false;
};

}  // namespace ng_plus_plus
