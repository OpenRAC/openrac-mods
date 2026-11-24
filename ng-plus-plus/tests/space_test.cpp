// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++'s space rules alone (space_missions.h, ship_upgrades.h, ship_looks.h): the waves and the
// elites across new games, the ship's tiers (names, prices, damage, fire rate, the timers), the
// Ship Shack's entry bit, the looks and what each tier unlocks.

#include <cstdint>
#include <string>

#include "check.h"
#include "ship_looks.h"
#include "ship_upgrades.h"
#include "space_missions.h"
#include "tiers.h"

namespace {

using namespace ng_plus_plus;

// The waves: none before cycle 2, then one, two from 4, three from 6; Swarm one more; never
// fewer as the cycles go up.
void test_waves() {
    CHECK(extra_waves(0, Modifier::None) == 0);
    CHECK(extra_waves(1, Modifier::None) == 0);
    CHECK(extra_waves(1, Modifier::Swarm) == 0);
    CHECK(extra_waves(2, Modifier::None) == 1);
    CHECK(extra_waves(2, Modifier::Swarm) == 2);
    CHECK(extra_waves(3, Modifier::None) == 2);
    CHECK(extra_waves(4, Modifier::None) == 3);
    CHECK(extra_waves(6, Modifier::None) == 4);
    CHECK(extra_waves(20, Modifier::Bounty) == 4);

    // Mayhem only grows.
    for (int cycle = 0; cycle < 12; ++cycle) {
        CHECK(extra_waves(cycle + 1, Modifier::None) >= extra_waves(cycle, Modifier::None));
        CHECK(target_spawn_rate(cycle + 1) >= target_spawn_rate(cycle));
    }

    CHECK(target_spawn_rate(1) == 1.0f);
    CHECK(target_spawn_rate(2) == 1.5f);
    CHECK(target_spawn_rate(4) == 2.0f);
}

// A wave is due at a quarter of the peak, before the last ship dies, while waves are left, and
// never for a fight of fewer than three.
void test_wave_due() {
    CHECK(wave_due(8, 2, 0, 1));
    CHECK(!wave_due(8, 3, 0, 1));
    CHECK(!wave_due(8, 0, 0, 1));
    CHECK(!wave_due(8, 2, 1, 1));
    CHECK(!wave_due(2, 0, 0, 3));
    CHECK(!wave_due(2, 1, 0, 3));
    CHECK(wave_due(3, 0 + 1, 0, 1) == false);
    CHECK(wave_due(4, 1, 0, 1));
    CHECK(!wave_due(8, 2, 0, 0));
}

// The elites: none before cycle 2, 20 % then 10 more a run up to 60, Elite guard doubled up to
// 80, a wave NG++ brought back 15 more.
void test_space_elites() {
    CHECK(space_elite_percent(1, Modifier::None, 0) == 0);
    CHECK(space_elite_percent(1, Modifier::None, 1) == 0);
    CHECK(space_elite_percent(2, Modifier::None, 0) == 25);
    CHECK(space_elite_percent(3, Modifier::None, 0) == 35);
    CHECK(space_elite_percent(9, Modifier::None, 0) == 70);
    CHECK(space_elite_percent(2, Modifier::Elite, 0) == 50);
    CHECK(space_elite_percent(9, Modifier::Elite, 0) == 90);
    CHECK(space_elite_percent(2, Modifier::None, 1) == 40);
    CHECK(space_elite_percent(9, Modifier::Elite, 2) == 100);
}

// The levels and the classes.
void test_classes() {
    CHECK(is_space_level(5) && is_space_level(10) && is_space_level(15) && is_space_level(25));
    CHECK(!is_space_level(0) && !is_space_level(24) && !is_space_level(-1));
    CHECK(ship_class_of(5, 3955) != nullptr && ship_class_of(5, 3955)->fighter);
    CHECK(ship_class_of(10, 3973) != nullptr && !ship_class_of(10, 3973)->fighter);
    CHECK(ship_class_of(15, 4625) != nullptr && !ship_class_of(15, 4625)->fighter);
    CHECK(ship_class_of(15, 3987) != nullptr && ship_class_of(10, 3987) != nullptr);
    CHECK(ship_class_of(5, 3987) == nullptr);
    CHECK(is_player_side(kShipClass) && is_player_side(3262) && is_player_side(4310));
    CHECK(!is_player_side(3955));
    CHECK(is_ship_moby(kShipClass) && is_ship_moby(3412) && is_ship_moby(kShipShotClass));
    CHECK(is_ship_moby(4742) && !is_ship_moby(3262) && !is_ship_moby(3987));
}

// The game's ship init on each disc and level.
void test_revive_addresses() {
    CHECK(revive_address("ntsc", 5) == 0x0037AEC0u);
    CHECK(revive_address("ntsc", 25) == 0x00374DF8u);
    CHECK(revive_address("pal", 10) == 0x0037F400u);
    CHECK(revive_address("ntsc2", 15) == 0x00382980u);
    CHECK(revive_address("ntsc", 24) == 0u);
    CHECK(revive_address("jp", 5) == 0u);
    CHECK(equipment_word("ntsc") == 0x001A7AF0u);
    CHECK(equipment_word("pal") == 0x001A7B78u);
    CHECK(equipment_word("ntsc2") == 0x001A7AF8u);
    CHECK(equipment_word("jp") == 0u);
}

// The ship's tiers: names, prices above the game's dearest part (60) and rising, damage and fire
// rate rising, none past Omega.
void test_ship_tiers() {
    CHECK(std::string(ship_tier_name(1)) == "Alpha");
    CHECK(std::string(ship_tier_name(8)) == "Omega");
    CHECK(std::string(ship_tier_name(0)).empty() && std::string(ship_tier_name(9)).empty());
    CHECK(ship_tier_title(3) == "Gamma Ship Weapons");
    CHECK(ship_tier_price(1) == 80 && ship_tier_price(8) == 360 && ship_tier_price(0) == 0);
    CHECK(ship_damage(0) == 1.0f && ship_fire_rate(0) == 1.0f);
    CHECK(ship_damage(9) == 1.0f);

    // Each tier dearer, harder and faster than the one before.
    for (int tier = 1; tier <= kShipTiers; ++tier) {
        CHECK(ship_tier_price(tier) > 60);
        CHECK(ship_damage(tier) > ship_damage(tier - 1));
        CHECK(ship_fire_rate(tier) > ship_fire_rate(tier - 1));
        CHECK(tier == 1 || ship_tier_price(tier) > ship_tier_price(tier - 1));
        CHECK(ship_tier_text(tier).find("faster") != std::string::npos);
    }

    CHECK(ship_tier_text(kShipOverchargeTier).find("overcharged") != std::string::npos);
    CHECK(ship_tier_text(kShipArcTier).find("arc") != std::string::npos);

    // The entry's bit is none of the game's: hull 0-1, parts 2-15, paint 16-20.
    CHECK((kShipTierBit & 0x001FFFFFu) == 0);
    CHECK(kShipItem != 55 && kind_of(kShipItem) == Kind::None);
}

// The timers: a faster rate takes whole ticks with the fraction carried, never past 1 left.
void test_timers() {
    float carry = 0.0f;
    int taken = 0;

    // Ten ticks at x1.5 take five more.
    for (int tick = 0; tick < 10; ++tick) {
        taken += extra_timer_ticks(1.5f, 100, carry);
    }

    CHECK(taken == 5);
    carry = 0.0f;
    CHECK(extra_timer_ticks(2.0f, 1, carry) == 0);
    CHECK(extra_timer_ticks(2.0f, 0, carry) == 0);
    CHECK(extra_timer_ticks(1.0f, 50, carry) == 0);
    carry = 0.0f;
    CHECK(extra_timer_ticks(3.0f, 2, carry) == 1);
    CHECK(extra_timer_ticks(2.0f, 50, carry) == 1);
}

// The looks: their settings' values, what each needs, the fall back to the tier's colour.
void test_looks() {
    CHECK(ship_look_of("off") == ShipLook::Off);
    CHECK(ship_look_of("prismatic") == ShipLook::Prismatic);
    CHECK(ship_look_of("nonsense") == ShipLook::Tier);
    CHECK(ship_look_tier(ShipLook::Ion) == 1);
    CHECK(ship_look_tier(ShipLook::Prismatic) == 8);
    CHECK(!ship_paint(ShipLook::Tier, 0, 0));
    CHECK(!ship_paint(ShipLook::Off, 8, 0));
    CHECK(!ship_paint(ShipLook::Ion, 0, 0));

    const auto ion = ship_paint(ShipLook::Ion, 1, 0);
    CHECK(ion && ion->rgb == 0x20E0FFu);

    // Prismatic before Omega: the tier's colour.
    const auto early = ship_paint(ShipLook::Prismatic, 2, 0);
    const auto tier = ship_paint(ShipLook::Tier, 2, 0);
    CHECK(early && tier && early->rgb == tier->rgb && early->material == tier->material);

    // The rainbow moves.
    const auto a = ship_paint(ShipLook::Prismatic, 8, 0);
    const auto b = ship_paint(ShipLook::Prismatic, 8, 60);
    CHECK(a && b && a->rgb != b->rgb);

    const auto obsidian = ship_paint(ShipLook::Obsidian, 7, 0);
    CHECK(obsidian && obsidian->material == 3u);
}

}  // namespace

int main() try {
    test_waves();
    test_wave_due();
    test_space_elites();
    test_classes();
    test_revive_addresses();
    test_ship_tiers();
    test_timers();
    test_looks();
    return ng_plus_plus::test::result("ng_plus_plus_space");
} catch (...) {
    // A test that throws has failed.
    return 1;
}
