// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.6.6's armour paint (src/armour.cpp): none in a cutscene or scene, none without Carbonox,
// the tier's in normal play with Carbonox and a tier; the vendor's menu picture stays painted.

#include "check.h"
#include "perk_rules.h"

namespace {

using namespace ng_plus_plus;

constexpr std::int32_t kScene = 3;

void test_scene_no_paint() {
    CHECK(!armour_paints_world(kScene, kBestArmour, 3));
}

void test_not_carbonox_no_paint() {
    CHECK(!armour_paints_world(kPlayGameMode, 0, 3));
    CHECK(!armour_paints_world(kPlayGameMode, kBestArmour - 1, 3));
    CHECK(!armour_paints_menu(0, 3));
}

void test_play_carbonox_tier_paints() {
    CHECK(armour_paints_world(kPlayGameMode, kBestArmour, 1));
    CHECK(!armour_paints_world(kPlayGameMode, kBestArmour, 0));
    // The vendor's preview: a menu, in a vendor's game mode, Carbonox worn.
    CHECK(armour_paints_menu(kBestArmour, 2));
    CHECK(!armour_paints_menu(kBestArmour, 0));
}

}  // namespace

int main() {
    test_scene_no_paint();
    test_not_carbonox_no_paint();
    test_play_carbonox_tier_paints();
    return ng_plus_plus::test::result("ng_plus_plus_paint");
}
