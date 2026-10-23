// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The weapons of Ratchet & Clank, and how each one feels on the controller's
// right trigger.
//
// The game's own signal that a weapon is firing is the hero's own flag: the
// fire animation running, set and cleared by nothing but the weapon code
// (Hero_StartModuleAnim and Hero_ClearPendingModuleAnim,
// games/rac1/src/overlays/shared/help_0020CDF0.c:718 and :757). The weapons
// that do not set it have their own way of being seen, and the per-shot
// moments come from the weapon's own counters.

#pragma once

#include <cstdint>

namespace dualsense {

// The weapons' item ids (games/rac1/docs/kb/datamining.md, and the code that
// pins each id to a weapon).
enum ItemId : std::int32_t {
    kItemWrench = 0x08,
    kItemSuckCannon = 0x09,
    kItemBombGlove = 0x0A,
    kItemDevastator = 0x0B,
    kItemSwingshot = 0x0C,
    kItemVisibombGun = 0x0D,
    kItemTaunter = 0x0E,
    kItemBlaster = 0x0F,
    kItemPyrocitor = 0x10,
    kItemMineGlove = 0x11,
    kItemWalloper = 0x12,
    kItemTeslaClaw = 0x13,
    kItemGloveOfDoom = 0x14,
    kItemMorphORay = 0x15,
    kItemRyno = 0x17,
    kItemDroneDevice = 0x18,
    kItemDecoyGlove = 0x19,
};

// How a weapon feels through the controller's motors.
//
// The table is the one the weapons were given: a wall that snaps and then the
// shot, a lighter click, a kick with every shot, a buzz through a salvo, a hum
// while firing, a steady resistance. The controller's right trigger is not
// used by the game, so the feel is given through the motors, which is what
// the PlayStation 2 had.
enum class Feel {
    kNone,        // nothing: the weapon is not in the table
    kSteady,      // a steady low rumble while it fires
    kClick,       // a sharp click at the shot
    kBuzz,        // a hum while it fires
    kKick,        // a light rumble while it fires, and a kick with every shot
    kBuzzRumble,  // a buzz while it fires, and a full rumble with every shot
};

// A weapon's feel.
struct WeaponFeel {
    Feel feel = Feel::kNone;
    std::uint8_t steady = 0;  // the strength of the part while it fires, 0..8
    std::uint8_t shot = 0;    // the strength of the part at the shot, 0..8
};

// The feel of a weapon, by its item id, or null when it has none.
const WeaponFeel* feel_for(std::int32_t item_id);

// What the mod reads of the game about the weapon, this tick.
struct WeaponState {
    std::int32_t item_id = 0;     // the equipped weapon
    bool flag = false;            // the game's own flag: the fire animation runs
    std::int32_t age = 0;         // frames since the fire animation started
    std::int32_t hero_state = 0;  // the hero's state
    bool pressed = false;         // the fire button went down this tick
    std::int32_t counter = 0;     // the weapon's own counter (the shot's cooldown)
    std::int32_t shots = 0;       // the R.Y.N.O.'s missiles fired
};

// What happened with the weapon this tick.
struct WeaponMoment {
    bool firing = false;  // the weapon is firing now
    bool shot = false;    // a shot went off this tick
    bool began = false;   // the firing just began
};

// What the weapon is doing, against what it was doing before.
WeaponMoment weapon_moment(const WeaponState& now, const WeaponState& before);

}  // namespace dualsense
