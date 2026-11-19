// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The hook points NG++ takes that the SDK it builds against does not name yet, in the form of
// openrac/games/rac2.hpp's types, and the port's text slots. OpenRAC added the vendor's and the
// text's after SDK 1.14.6 and the bolts' in 1.23 (a `hook` row in each rac2 disc's libraries.tsv);
// an OpenRAC without them still loads the mod, which then goes without what they give (README.md,
// "Not yet").

#pragma once

#include <cstdint>

namespace ng_plus_plus::hooks {

// The vendor's list drawn (func_002F9C80): each row -5 to +6 around the cursor, in order, asks
// Item_VersionColor for its icon's colour, then an upgrade row asks for its label's text.
struct Vendor_DrawList {
    static constexpr const char* id = "rac2.vendor.draw_list";
    static constexpr const char* name = "Vendor_DrawList";
    static constexpr const char* signature = "void()";
    using fn = void (*)();
};

// A vendor row's icon colour by its kind (func_00336EF8; an upgrade entry: kind 2, the Mega gold).
struct Item_VersionColor {
    static constexpr const char* id = "rac2.item.version_color";
    static constexpr const char* name = "Item_VersionColor";
    static constexpr const char* signature = "u32(i32)";
    using fn = std::uint32_t (*)(std::int32_t);
};

// The game's text of an id (func_00288FB8): its string's game address.
struct Text_ById {
    static constexpr const char* id = "rac2.text.by_id";
    static constexpr const char* name = "Text_ById";
    static constexpr const char* signature = "i32(i32)";
    using fn = std::int32_t (*)(std::int32_t);
};

// A burst of bolt coins from `moby` (moby_spawn_bolt_pickups, and its copy in every level's code):
// an amount from low to high, doubled in challenge mode and paced, split into the biggest coins
// that fit (with flags 8 at most `maximum`), each made by Bolt_SpawnPickup. OpenRAC 1.23.
struct Bolt_SpawnPickups {
    static constexpr const char* id = "rac2.bolt.spawn_pickups";
    static constexpr const char* name = "Bolt_SpawnPickups";
    static constexpr const char* signature = "void(u32,i32,i32,i32,i32,i32,f32)";
    using fn = void (*)(
        std::uint32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t, float
    );
};

// One coin worth `value` at `position` with `velocity` (game addresses of four floats each), from
// `source` (moby_spawn_bolt_pickup): its moby, or 0. OpenRAC 1.23.
struct Bolt_SpawnPickup {
    static constexpr const char* id = "rac2.bolt.spawn_pickup";
    static constexpr const char* name = "Bolt_SpawnPickup";
    static constexpr const char* signature = "u32(u32,u32,u32,u32,i32,i32)";
    using fn = std::uint32_t (*)(
        std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::int32_t
    );
};

// A coin collected (CollectBolts): its class's worth added to the bolts. OpenRAC 1.23.
struct Bolt_Collect {
    static constexpr const char* id = "rac2.bolt.collect";
    static constexpr const char* name = "Bolt_Collect";
    static constexpr const char* signature = "void(u32)";
    using fn = void (*)(std::uint32_t);
};

// The port's own slots for a mod's texts: the lookup returns the address of slot n for id
// kTextSlot + n, kTextSlotBytes each, kTextSlots of them.
inline constexpr std::int32_t kTextSlot = 0x7F10;
inline constexpr std::uint32_t kTextSlotBytes = 96;
inline constexpr int kTextSlots = 8;

}  // namespace ng_plus_plus::hooks
