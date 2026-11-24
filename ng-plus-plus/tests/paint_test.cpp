// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.6.6's armour paint (src/armour.cpp): none in a cutscene or scene, none without Carbonox,
// the tier's in normal play with Carbonox and a tier; the vendor's menu picture stays painted.

#include <algorithm>

#include "armour_api.h"
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

// OpenRAC's OPENRAC_MOBY_MATERIAL_PAINT_ACCENT_AT of interface 1.38, as the SDK macro packs it:
// the paint with its glow level, the accent's flag, and the top nibble of each channel in bits 12
// to 23 (red highest).
void test_accent_packs_as_the_sdk() {
    CHECK(paint_accent_at(9, 0xFF2010) == (5u | 9u << 8 | 0x1000000u | 0xFu << 20 | 0x2u << 16 | 0x1u << 12));
    CHECK(paint_accent_at(0, 0) == (kMaterialPaint | kPaintAccentBit));
    // The level and the flag stay put, whatever the accent's channels.
    CHECK((paint_accent_at(15, 0xFFFFFF) & 0xFFFu) == (kMaterialPaint | 15u << 8));
    CHECK((paint_accent_at(3, 0x20C0FF) & kPaintAccentBit) != 0);
    CHECK((paint_at(3) & kPaintAccentBit) == 0);
    CHECK(paint_accent_at(3, 0x20C0FF) > paint_at(3));
}

// The accents: one a tier, each far from its body's colour (a visor that stands out), lit
// (a channel high), and Omega's follows its cycle as the body does.
void test_accents_stand_against_the_body() {
    CHECK(!armour_accent(0, 0).has_value());

    // The distance in the colour channels (0 to 765).
    const auto apart = [](std::uint32_t a, std::uint32_t b) {
        int sum = 0;

        for (int shift = 0; shift < 24; shift += 8) {
            const int x = static_cast<int>(a >> shift & 0xFFu);
            const int y = static_cast<int>(b >> shift & 0xFFu);
            sum += x > y ? x - y : y - x;
        }

        return sum;
    };

    for (int tier = 1; tier <= kArmourTiers; ++tier) {
        for (std::uint32_t tick : {0u, 40u, 100u, 200u}) {
            const auto accent = armour_accent(tier, tick);
            const auto body = armour_rgb(tier, tick);
            CHECK(accent.has_value() && body.has_value());
            // Away from the body, bright, and of a channel the port keeps whole.
            CHECK(apart(*accent, *body) > 200 || tier == kArmourTiers);
            const std::uint32_t top =
                std::max({*accent >> 16 & 0xFFu, *accent >> 8 & 0xFFu, *accent & 0xFFu});
            CHECK(top >= 0xC0u);
        }
    }

    // The accents differ tier by tier, and Alpha's stays red (the game's own visor).
    for (int tier = 2; tier <= 7; ++tier) {
        CHECK(*armour_accent(tier, 0) != *armour_accent(tier - 1, 0));
    }

    CHECK((*armour_accent(1, 0) >> 16 & 0xFFu) == 0xFFu);
    // Omega moves with its cycle.
    CHECK(*armour_accent(kArmourTiers, 0) != *armour_accent(kArmourTiers, 60));
}

}  // namespace

int main() {
    test_accent_packs_as_the_sdk();
    test_accents_stand_against_the_body();
    test_scene_no_paint();
    test_not_carbonox_no_paint();
    test_play_carbonox_tier_paints();
    return ng_plus_plus::test::result("ng_plus_plus_paint");
}
