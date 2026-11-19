// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The hook points NG++ takes that the SDK it builds against (1.14.6) does not name yet, in the form
// of openrac/games/rac2.hpp's types, and the port's text slots. OpenRAC added them for this mod
// after SDK 1.14.6 (a `hook` row in each rac2 disc's libraries.tsv); an OpenRAC without them still
// loads the mod, which then goes without what they give (README.md, "Not yet").

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

// The port's own slots for a mod's texts: the lookup returns the address of slot n for id
// kTextSlot + n, kTextSlotBytes each, kTextSlots of them.
inline constexpr std::int32_t kTextSlot = 0x7F10;
inline constexpr std::uint32_t kTextSlotBytes = 96;
inline constexpr int kTextSlots = 8;

}  // namespace ng_plus_plus::hooks
