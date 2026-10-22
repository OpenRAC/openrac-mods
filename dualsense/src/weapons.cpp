// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The weapons' feels and what the game is doing with them (weapons.hpp).

#include "weapons.hpp"

namespace dualsense {
namespace {

// The feel of each weapon, in the order of the item ids.
//
// The strengths are the controller's own scale, 1 to 8: the part while the
// weapon fires, and the part at the shot.

const WeaponFeel kFeels[] = {
    // kItemSuckCannon: a steady resistance
    {Feel::kSteady, 3, 0},
    // kItemBombGlove: a lighter click
    {Feel::kClick, 0, 4},
    // kItemDevastator: a heavy click, a stiff wall that snaps and then the shot
    {Feel::kClick, 0, 8},
    // kItemVisibombGun: a lighter click
    {Feel::kClick, 0, 4},
    // kItemTaunter: a steady resistance
    {Feel::kSteady, 3, 0},
    // kItemBlaster: a light resistance and a kick with every shot
    {Feel::kKick, 2, 5},
    // kItemPyrocitor: a hum while firing
    {Feel::kBuzz, 3, 0},
    // kItemMineGlove: a lighter click
    {Feel::kClick, 0, 4},
    // kItemWalloper: a heavy click, a stiff wall that snaps and then the shot
    {Feel::kClick, 0, 8},
    // kItemTeslaClaw: a hum while firing
    {Feel::kBuzz, 3, 0},
    // kItemGloveOfDoom: a lighter click
    {Feel::kClick, 0, 4},
    // kItemMorphORay: a hum while firing
    {Feel::kBuzz, 3, 0},
    // kItemRyno: a heavy buzz through the salvo, and a full rumble per shot
    {Feel::kBuzzRumble, 6, 8},
    // kItemDroneDevice: a steady resistance
    {Feel::kSteady, 3, 0},
    // kItemDecoyGlove: a lighter click
    {Feel::kClick, 0, 4},
};

// The hero's state the Walloper fires from (games/rac1/src/overlays/shared/
// help_00221A98.c:3267).
constexpr std::int32_t kHeroStateWalloper = 0x20;

}  // namespace

const WeaponFeel* feel_for(std::int32_t item_id) {
    switch (item_id) {
        case kItemSuckCannon: return &kFeels[0];
        case kItemBombGlove: return &kFeels[1];
        case kItemDevastator: return &kFeels[2];
        case kItemVisibombGun: return &kFeels[3];
        case kItemTaunter: return &kFeels[4];
        case kItemBlaster: return &kFeels[5];
        case kItemPyrocitor: return &kFeels[6];
        case kItemMineGlove: return &kFeels[7];
        case kItemWalloper: return &kFeels[8];
        case kItemTeslaClaw: return &kFeels[9];
        case kItemGloveOfDoom: return &kFeels[10];
        case kItemMorphORay: return &kFeels[11];
        case kItemRyno: return &kFeels[12];
        case kItemDroneDevice: return &kFeels[13];
        case kItemDecoyGlove: return &kFeels[14];
        default: return nullptr;
    }
}

WeaponMoment weapon_moment(const WeaponState& now, const WeaponState& before) {
    WeaponMoment moment;
    switch (now.item_id) {
        case kItemWalloper:
            // The Walloper plays a sound and never raises the flag; it has its
            // own hero state.
            moment.firing = now.hero_state == kHeroStateWalloper;
            break;
        case kItemVisibombGun:
        case kItemTaunter:
            // These two read the pad themselves; their bodies are still
            // assembly on the PAL disc.
            moment.firing = now.pressed;
            break;
        default:
            moment.firing = now.flag;
            break;
    }

    // A shot. The weapons that fire on a counter reach it when the counter is
    // back at zero; the R.Y.N.O. counts its missiles.
    switch (now.item_id) {
        case kItemBlaster:
        case kItemDevastator:
            moment.shot = moment.firing && now.counter == 0;
            break;
        case kItemRyno:
            moment.shot = moment.firing && now.shots != before.shots;
            break;
        default:
            break;
    }

    // The firing just began: the flag was down and it is up now, or the weapon
    // was another one and now is this one.
    moment.began = moment.firing &&
                   (!before.flag || before.item_id != now.item_id);
    return moment;
}

}  // namespace dualsense
