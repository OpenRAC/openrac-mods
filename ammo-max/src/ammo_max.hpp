// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The ammo-max mod (README.md): where the vendor's buy dialog keeps what it asks, on each disc, and
// the count the dialog starts at.
//
// The dialog (VendorBuyDialogUpdate: PAL func_L00_0029CDE0, games/rac1
// src/overlays/shared/update_0029B6A0.c; NTSC-U FUN_L00_0029ba78, the NTSC decomp's
// src/overlays/shared/gameplay/entities/00295100.c) runs a state at a time: 1 when it opens, which
// sets the count to 1 and, for ammo the player has room for and bolts to pay a round of, the flag
// that lets Up raise it; then 2, where Up and Down move the count between 1 and the most the bolts
// pay for and the weapon has room for, and Square buys. The mod sets the count to that most once,
// the tick the dialog is first seen in state 2.
//
// The addresses and offsets are those of the functions named above (seen in game code). The count
// function is plain integer arithmetic with no game memory, so the tests can run it alone
// (tests/count_test.cpp).

#pragma once

#include <cstdint>

namespace ammo_max {

// Where one disc keeps what the dialog reads and writes.
struct Disc {
    // The value of openrac_api.version that names the disc.
    const char* version;

    // The executable's, through relocate: the bolts the player holds, an s32 (gBolts).
    std::uint32_t bolts;

    // The executable's, through relocate: the ammo of each item id, s32[37].
    std::uint32_t ammo;

    // Level 0's program's, through relocate_level(0, ...): the items' records, 0x18 bytes per item
    // id. +8 and +0xA hold the round's price (u16; +0xA while the dialog's +0x40 is set), +0xE the
    // most ammo.
    std::uint32_t item_table;

    // Level 0's program's: the vendor menu. +0x40 holds which price, +0x58 the row chosen, +0x5C the
    // buy dialog's state, +0xD0 the rows, 0x14 bytes each (+0 the item id, +4 1 when the row is its
    // ammo).
    std::uint32_t dialog;

    // Level 0's program's: the count the dialog asks for, an s32.
    std::uint32_t count;

    // Level 0's program's: 1 when Up may raise the count, an s32.
    std::uint32_t can_raise;
};

// The addresses of each disc the mod knows, in the field order of Disc (seen in game code); matched
// by openrac_api.version at load.
inline constexpr Disc kDiscs[] = {
    {"pal", 0x0015EE98u, 0x0013D530u, 0x001C43B0u, 0x001CA7C0u, 0x00161F30u, 0x00161F10u},
    {"ntsc", 0x0015ED98u, 0x0013D428u, 0x001C40B0u, 0x001CA4C0u, 0x00161E34u, 0x00161E10u},
};

// How many item ids the game has, 0..0x24, and so the length of the ammo array (seen in game
// code).
inline constexpr std::int32_t kItems = 37;

// The size in bytes of one item record in the item table (seen in game code).
inline constexpr std::uint32_t kItemSize = 0x18u;

// How many rows the vendor menu holds (seen in game code).
inline constexpr std::int32_t kRows = 16;

// The size in bytes of one row of the vendor menu (seen in game code).
inline constexpr std::uint32_t kRowSize = 0x14u;

// Returns the count the dialog starts at: as many rounds as the bolts pay for and the weapon has
// room for, the most Up would reach. Returns 0 when that is none (a round costs nothing, or no
// room, or too few bolts: the game's 1 stays).
inline std::int32_t start_count(
    std::int32_t bolts, std::int32_t price, std::int32_t ammo, std::int32_t max_ammo
) {
    // With no price, too few bolts for one round or a full weapon, nothing can be bought.
    if (price <= 0 || bolts < price || ammo >= max_ammo) {
        return 0;
    }

    const std::int32_t affordable = bolts / price;
    const std::int32_t room = max_ammo - ammo;

    // The count is capped by whichever runs out first: the bolts or the room.
    return affordable < room ? affordable : room;
}

}  // namespace ammo_max
