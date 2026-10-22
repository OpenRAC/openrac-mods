// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The weapons' feels and what the game is doing with them: the table, and the
// rule that says whether a weapon is firing and whether a shot just went off.
#include <cstdio>
#include <cstdlib>

#include "weapons.hpp"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

using dualsense::Feel;
using dualsense::ItemId;
using dualsense::kItemBombGlove;
using dualsense::kItemBlaster;
using dualsense::WeaponFeel;
using dualsense::WeaponMoment;
using dualsense::WeaponState;

void test_the_table() {
    // Every weapon in the table has a feel, and every feel is one of them.
    const std::int32_t weapons[] = {
        dualsense::kItemSuckCannon, dualsense::kItemBombGlove,   dualsense::kItemDevastator, dualsense::kItemVisibombGun, dualsense::kItemTaunter,
        dualsense::kItemBlaster,    dualsense::kItemPyrocitor,   dualsense::kItemTeslaClaw,  dualsense::kItemMorphORay,   dualsense::kItemRyno,
        dualsense::kItemDroneDevice, dualsense::kItemDecoyGlove, dualsense::kItemMineGlove,   dualsense::kItemGloveOfDoom,  dualsense::kItemWalloper,
    };
    for (const std::int32_t id : weapons) {
        const WeaponFeel* feel = dualsense::feel_for(id);
        CHECK(feel != nullptr);
        CHECK(feel->feel != Feel::kNone);
        // The steady part or the shot part, at least one of them.
        CHECK(feel->steady != 0 || feel->shot != 0);
    }
    // The weapons that are not in the table have none.
    CHECK(dualsense::feel_for(dualsense::kItemWrench) == nullptr);
    CHECK(dualsense::feel_for(dualsense::kItemSwingshot) == nullptr);
    CHECK(dualsense::feel_for(0) == nullptr);
    CHECK(dualsense::feel_for(-1) == nullptr);

    // The table is the one that was asked for.
    CHECK(dualsense::feel_for(dualsense::kItemWalloper)->feel == Feel::kClick);
    CHECK(dualsense::feel_for(dualsense::kItemDevastator)->feel == Feel::kClick);
    CHECK(dualsense::feel_for(dualsense::kItemBombGlove)->feel == Feel::kClick);
    CHECK(dualsense::feel_for(dualsense::kItemBlaster)->feel == Feel::kKick);
    CHECK(dualsense::feel_for(dualsense::kItemRyno)->feel == Feel::kBuzzRumble);
    CHECK(dualsense::feel_for(dualsense::kItemPyrocitor)->feel == Feel::kBuzz);
    CHECK(dualsense::feel_for(dualsense::kItemTeslaClaw)->feel == Feel::kBuzz);
    CHECK(dualsense::feel_for(dualsense::kItemMorphORay)->feel == Feel::kBuzz);
    CHECK(dualsense::feel_for(dualsense::kItemSuckCannon)->feel == Feel::kSteady);
    CHECK(dualsense::feel_for(dualsense::kItemTaunter)->feel == Feel::kSteady);
    CHECK(dualsense::feel_for(dualsense::kItemDroneDevice)->feel == Feel::kSteady);

    // The heavy click is heavier than the light one.
    CHECK(dualsense::feel_for(dualsense::kItemWalloper)->shot > dualsense::feel_for(dualsense::kItemBombGlove)->shot);
    // The R.Y.N.O. buzzes harder than the hum.
    CHECK(dualsense::feel_for(dualsense::kItemRyno)->steady > dualsense::feel_for(dualsense::kItemPyrocitor)->steady);
}

void test_the_games_own_flag() {
    // The flag is what the game says: the fire animation runs.
    WeaponState now;
    now.item_id = dualsense::kItemBlaster;
    now.flag = true;
    now.counter = 3;
    WeaponState before = now;
    before.flag = false;

    const WeaponMoment moment = dualsense::weapon_moment(now, before);
    CHECK(moment.firing);
    CHECK(moment.began);
    // The Blaster's shot is when its counter is back at zero.
    CHECK(!moment.shot);

    // The next tick: the firing goes on, and the counter is back at zero: a shot.
    before = now;
    now.counter = 0;
    const WeaponMoment shot = dualsense::weapon_moment(now, before);
    CHECK(shot.firing);
    CHECK(shot.shot);
    CHECK(!shot.began);
}

void test_the_walloper() {
    // The Walloper never raises the flag; it has its own hero state.
    WeaponState now;
    now.item_id = dualsense::kItemWalloper;
    now.hero_state = 0x20;
    WeaponState before = now;
    before.hero_state = 0;

    const WeaponMoment moment = dualsense::weapon_moment(now, before);
    CHECK(moment.firing);
    CHECK(moment.began);
    // And it never has a shot of its own.
    CHECK(!moment.shot);

    // In another state, it is not firing.
    now.hero_state = 1;
    CHECK(!dualsense::weapon_moment(now, before).firing);
}

void test_the_two_that_read_the_pad() {
    // The Visibomb Gun and the Taunter read the pad themselves.
    WeaponState now;
    now.item_id = dualsense::kItemVisibombGun;
    now.pressed = true;
    WeaponState before = now;
    before.pressed = false;
    const WeaponMoment moment = dualsense::weapon_moment(now, before);
    CHECK(moment.firing);
    CHECK(moment.began);

    now.item_id = dualsense::kItemTaunter;
    CHECK(dualsense::weapon_moment(now, before).firing);
}

void test_the_ryno() {
    // The R.Y.N.O. counts its missiles: a shot is one more.
    WeaponState now;
    now.item_id = dualsense::kItemRyno;
    now.flag = true;
    now.shots = 3;
    WeaponState before = now;
    before.shots = 2;

    const WeaponMoment moment = dualsense::weapon_moment(now, before);
    CHECK(moment.firing);
    CHECK(moment.shot);

    // The same count: no new shot.
    before.shots = 3;
    CHECK(!dualsense::weapon_moment(now, before).shot);
}

void test_switching_weapons() {
    // Another weapon was out: the firing begins again.
    WeaponState now;
    now.item_id = dualsense::kItemBlaster;
    now.flag = true;
    now.counter = 0;
    WeaponState before;
    before.item_id = dualsense::kItemPyrocitor;
    before.flag = true;

    const WeaponMoment moment = dualsense::weapon_moment(now, before);
    CHECK(moment.firing);
    CHECK(moment.began);
}

void test_nothing_firing() {
    WeaponState now;
    now.item_id = dualsense::kItemWrench;
    WeaponState before = now;
    const WeaponMoment moment = dualsense::weapon_moment(now, before);
    CHECK(!moment.firing);
    CHECK(!moment.shot);
    CHECK(!moment.began);
}

}  // namespace

int main() {
    test_the_table();
    test_the_games_own_flag();
    test_the_walloper();
    test_the_two_that_read_the_pad();
    test_the_ryno();
    test_switching_weapons();
    test_nothing_firing();
    std::puts("dualsense weapons: all passed");
    return 0;
}
