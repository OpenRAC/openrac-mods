// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.5.0's rules past damage, ammo and colour (src/perk_rules.cpp): tiers and stages, the hit
// perks, the RYNO II's split rockets, the Ice Gun, the armour, the modifiers and elites, and the
// RYNO II's Omega Prime step (src/tiers.cpp), then the book keeping the new level bytes.
//
// Expected values are the README's tables ("What each tier does") and the rules' own doc comments
// in src/perk_rules.h.

#include <cstdint>
#include <cstring>
#include <string>

#include "check.h"
#include "perk_rules.h"
#include "save_book.h"
#include "tiers.h"

namespace {

using namespace ng_plus_plus;

// The items' numbers in the game's tables (seen in game code).
constexpr int kLancer = 30;
constexpr int kPlasmaCoil = 28;
constexpr int kRyno = 44;

// Checks the tier of a weapon at each level, and the stages the tiers start.
void test_tiers_and_stages() {
    for (int level = 1; level <= 8; ++level) {
        CHECK(tier_of(Kind::Four, level) == level);
    }

    CHECK(tier_of(Kind::Ryno, 1) == 0);
    CHECK(tier_of(Kind::Ryno, 2) == 0);

    for (int level = 3; level <= 10; ++level) {
        CHECK(tier_of(Kind::Ryno, level) == level - 2);
    }

    CHECK(tier_of(Kind::Ryno, 11) == 9);

    CHECK(stage_of_tier(0) == Stage::None);
    CHECK(stage_of_tier(2) == Stage::None);
    CHECK(stage_of_tier(3) == Stage::Gamma);
    CHECK(stage_of_tier(4) == Stage::Gamma);
    CHECK(stage_of_tier(5) == Stage::Epsilon);
    CHECK(stage_of_tier(7) == Stage::Epsilon);
    CHECK(stage_of_tier(8) == Stage::Omega);
    CHECK(stage_of_tier(9) == Stage::Omega);

    CHECK(stage_of(Kind::Four, 3) == Stage::Gamma);
    CHECK(stage_of(Kind::Four, 8) == Stage::Omega);
    CHECK(stage_of(Kind::Ryno, 5) == Stage::Gamma);
    CHECK(stage_of(Kind::Ryno, 10) == Stage::Omega);

    CHECK(raised(Stage::None) == Stage::Gamma);
    CHECK(raised(Stage::Gamma) == Stage::Epsilon);
    CHECK(raised(Stage::Epsilon) == Stage::Omega);
    CHECK(raised(Stage::Omega) == Stage::Omega);
}

// Checks that the perks of the listed items only grow from Gamma to Omega and none at no stage.
void test_hit_perks_grow() {
    const int items[] = {30, 28, 18, 9, 53, 26, 23, 12, 37, 43};

    for (int item : items) {
        const HitPerk none = hit_perk(item, Stage::None);
        const HitPerk gamma = hit_perk(item, Stage::Gamma);
        const HitPerk epsilon = hit_perk(item, Stage::Epsilon);
        const HitPerk omega = hit_perk(item, Stage::Omega);

        CHECK(!none.any());
        CHECK(omega.any());

        CHECK(gamma.hops <= epsilon.hops && epsilon.hops <= omega.hops);
        CHECK(gamma.fan <= epsilon.fan && epsilon.fan <= omega.fan);
        CHECK(gamma.burst_radius <= epsilon.burst_radius);
        CHECK(epsilon.burst_radius <= omega.burst_radius);
        CHECK(gamma.pierce <= epsilon.pierce && epsilon.pierce <= omega.pierce);
    }
}

// Checks the Lancer's and the Plasma Coil's signature perks at Omega.
void test_hit_perks_signature() {
    const HitPerk lancer = hit_perk(kLancer, Stage::Omega);

    CHECK(lancer.nth == 5);
    CHECK(lancer.nth_hops == 4);

    CHECK(hit_perk(kPlasmaCoil, Stage::Gamma).hops == 1);
    CHECK(hit_perk(kPlasmaCoil, Stage::Epsilon).hops == 3);
    CHECK(hit_perk(kPlasmaCoil, Stage::Omega).hops == 6);
    CHECK(hit_perk(kPlasmaCoil, Stage::Omega).end_radius > 0.0f);
    CHECK(hit_perk(kPlasmaCoil, Stage::Omega).end_share > 0.0f);
}

// Checks that weapons whose perks need hook points the port lacks have none even at Omega.
void test_hit_perks_none() {
    const int items[] = {27, 24, 22, 32, 25, 41, 31};

    for (int item : items) {
        CHECK(!hit_perk(item, Stage::Omega).any());
    }
}

// Checks the RYNO II's split rockets by tier.
void test_ryno_perk() {
    for (int tier = 0; tier <= 2; ++tier) {
        CHECK(!ryno_perk(tier).any());
    }

    CHECK(ryno_perk(3).fan == 2);
    CHECK(ryno_perk(3).fan_again == 0);
    CHECK(ryno_perk(5).fan == 3);
    CHECK(ryno_perk(8).fan_again == 1);
    CHECK(ryno_perk(9).fan_again == 2);
}

// Checks the Ice Gun's effect by tier.
void test_ice_effect() {
    const IceEffect none = ice_effect(0);

    CHECK(none.chill_ticks == 0);
    CHECK(none.freeze_ticks == 0);
    CHECK(none.shatter_radius == 0.0f);
    CHECK(none.energy == 0);

    for (int tier = 1; tier <= 8; ++tier) {
        const IceEffect effect = ice_effect(tier);

        CHECK(effect.chill_ticks > 0);
        CHECK(effect.energy >= 0 && effect.energy <= 15);

        if (tier < 5) {
            CHECK(effect.freeze_ticks == 0);
        } else {
            CHECK(effect.freeze_ticks > 0);
            CHECK(effect.shatter_radius > 0.0f);
            CHECK(effect.shatter_share > 0.0f);
        }

        CHECK(effect.shatter_freezes == (tier == 8));
    }
}

// Checks the Ice Gun's colours and the weapon colour dispatch.
void test_ice_tint() {
    CHECK(!ice_tint_rgb(0, 0).has_value());

    for (int tier = 1; tier <= 8; ++tier) {
        CHECK(ice_tint_rgb(tier, 0).has_value());
    }

    // Omega's aurora cycles with the tick.
    bool moved = false;

    for (std::uint32_t tick = 1; tick < 600 && !moved; ++tick) {
        moved = ice_tint_rgb(8, tick) != ice_tint_rgb(8, 0);
    }

    CHECK(moved);

    for (int level = 0; level <= 8; ++level) {
        CHECK(weapon_tint_rgb(kLavaGun, level, 7) == ice_tint_rgb(level, 7));
        CHECK(weapon_tint_rgb(kLancer, level, 7) == tint_rgb(Kind::Four, level, 7));
        CHECK(weapon_effect_rgb(kLancer, level, 7) == effect_rgb(Kind::Four, level, 7));
    }

    CHECK(max_level(55) == 8);
    CHECK(max_level(44) == 11);
    CHECK(max_level(30) == 8);
    CHECK(max_level(13) == 0);
}

// Checks the armour's damage, price and name by tier.
void test_armour_numbers() {
    CHECK(armour_damage(0) == 1.0f);

    float before = 1.0f;

    for (int tier = 1; tier <= 8; ++tier) {
        const float damage = armour_damage(tier);

        CHECK(damage < before);
        before = damage;
    }

    CHECK(armour_damage(8) > 0.449f && armour_damage(8) < 0.451f);

    CHECK(armour_price(0) == 0);
    CHECK(armour_price(9) == 0);

    std::int32_t last = 0;

    for (int tier = 1; tier <= 8; ++tier) {
        CHECK(armour_price(tier) > last);
        CHECK(armour_price(tier) == price(Kind::NoExp, tier - 1, kArmourBase));
        last = armour_price(tier);
    }

    CHECK(armour_name(1) == "Alpha Armor");
    CHECK(armour_name(8) == "Omega Armor");
}

// Checks the armour's colours, glow and perks.
void test_armour_looks() {
    CHECK(!armour_rgb(0, 0).has_value());
    CHECK(!armour_tint(0, 0).has_value());

    for (int tier = 1; tier <= 8; ++tier) {
        CHECK(armour_rgb(tier, 0).has_value());

        // The tint is the glow with red and blue swapped.
        const std::uint32_t rgb = *armour_rgb(tier, 5);
        const std::uint32_t swapped =
            ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu);

        CHECK(armour_tint(tier, 5) == swapped);
    }

    bool moved = false;

    for (std::uint32_t tick = 1; tick < 600 && !moved; ++tick) {
        moved = armour_rgb(8, tick) != armour_rgb(8, 0);
    }

    CHECK(moved);

    CHECK(armour_energy(8) == kEffectTop);

    for (int tier = 1; tier <= 8; ++tier) {
        CHECK(armour_energy(tier) >= armour_energy(tier - 1));
    }

    for (int tier = 0; tier < 3; ++tier) {
        const ArmourPerk perk = armour_perk(tier);

        CHECK(perk.thorns_radius == 0.0f && perk.thorns_share == 0.0f);
        CHECK(perk.last_stand_ticks == 0);
    }

    for (int tier = 3; tier <= 8; ++tier) {
        CHECK(armour_perk(tier).thorns_radius > 0.0f);
        CHECK(armour_perk(tier).thorns_share > 0.0f);
    }

    CHECK(armour_perk(4).last_stand_ticks == 0);

    for (int tier = 5; tier <= 8; ++tier) {
        CHECK(armour_perk(tier).last_stand_ticks > 0);
    }

    CHECK(armour_perk(8).thorns_share > armour_perk(3).thorns_share);
    CHECK(armour_perk(8).last_stand_ticks < armour_perk(5).last_stand_ticks);
}

// Checks the modifiers' rotation and names.
void test_modifiers() {
    CHECK(modifier_of(0) == Modifier::None);
    CHECK(modifier_of(1) == Modifier::None);
    CHECK(modifier_of(2) == Modifier::Swarm);
    CHECK(modifier_of(3) == Modifier::Elite);
    CHECK(modifier_of(4) == Modifier::Volatile);
    CHECK(modifier_of(5) == Modifier::Bounty);
    CHECK(modifier_of(6) == Modifier::Swarm);

    for (Modifier modifier :
         {Modifier::None, Modifier::Swarm, Modifier::Elite, Modifier::Volatile, Modifier::Bounty}) {
        CHECK(modifier_name(modifier) != nullptr);
    }
}

// Checks the share of elites by cycle and modifier.
void test_elite_percent() {
    CHECK(elite_percent(2, Modifier::Swarm) == 0);
    CHECK(elite_percent(2, Modifier::Elite) == 15);
    CHECK(elite_percent(3, Modifier::None) == 15);
    CHECK(elite_percent(4, Modifier::None) == 20);
    CHECK(elite_percent(40, Modifier::None) == 35);
    CHECK(elite_percent(40, Modifier::Elite) == 70);
}

// Checks that the elite roll is fixed, covers 0 and 100 percent and follows the percent.
void test_is_elite() {
    CHECK(is_elite(3, 4, 1, 25) == is_elite(3, 4, 1, 25));

    CHECK(!is_elite(3, 4, 1, 0));
    CHECK(is_elite(3, 4, 1, 100));

    int elites = 0;

    for (int i = 0; i < 10000; ++i) {
        if (is_elite(i % 17, i % 101, i / 7, 25)) {
            ++elites;
        }
    }

    CHECK(elites >= 2200 && elites <= 2800);
}

// Checks the arenas and the hero's health from Omega weapons.
void test_arenas_and_health() {
    CHECK(is_arena_level(2));
    CHECK(is_arena_level(11));
    CHECK(!is_arena_level(4));

    CHECK(omega_health(0) == 0);
    CHECK(omega_health(1) == 0);
    CHECK(omega_health(2) == 1);
    CHECK(omega_health(25) == 12);
}

// Checks the RYNO II's Omega Prime step.
void test_omega_prime() {
    const Step* step = step_at(Kind::Ryno, 10);

    CHECK(step != nullptr);

    if (step != nullptr) {
        CHECK(std::strcmp(step->name, "Omega Prime") == 0);
        CHECK(step->how == How::Exp);
        CHECK(step->tint == Tint::Prime);
    }

    CHECK(exp_goal(Kind::Ryno, 10) == 30000 * kRynoExpScale);
    CHECK(damage(Kind::Ryno, 11) > damage(Kind::Ryno, 10));
    CHECK(price(Kind::Ryno, 10, 1'000'000) == 0);
    CHECK(price(Kind::Ryno, 10, 0) == 0);
    CHECK(display_name("RYNO II", Kind::Ryno, 11) == "Omega Prime RYNO II");
}

// Checks that the book keeps the RYNO II's 11th level and the armour's tier.
void test_book_new_levels() {
    LevelBytes levels{};

    levels[kRyno] = 11;
    levels[55] = 8;
    levels[3] = static_cast<std::uint8_t>(kStartedBit | 2);

    const Clock clock{1, 2, 3, 4, 5, 6, 7, 8};
    SaveBook book;

    book.put(clock, levels);

    SaveBook again;

    CHECK(again.read(book.write()) == 1);

    const Record* record = again.find(clock);

    CHECK(record != nullptr);

    if (record != nullptr) {
        CHECK(record->levels[kRyno] == 11);
        CHECK(record->levels[55] == 8);
        CHECK((record->levels[3] & kStartedBit) != 0);
        CHECK(record->levels == levels);
    }
}

// Checks the shots' perks (OpenRAC 1.29): more shots of the shot weapons, never fewer at a higher
// stage, the RYNO II's split rockets by tier, the Gravity Bomb's and the Bomb Glove's mini bombs.
void test_shot_perks() {
    constexpr int kShotWeapons[] = {27, 24, 25, 12, 42, 37, 17, 41};
    constexpr Stage kStages[] = {Stage::None, Stage::Gamma, Stage::Epsilon, Stage::Omega};

    for (const int item : kShotWeapons) {
        CHECK(shot_perk(item, Stage::None, 0).refires == 0);
        int last = 0;

        for (const Stage stage : kStages) {
            const ShotPerk perk = shot_perk(item, stage, 0);
            CHECK(perk.refires >= last);
            CHECK(perk.refires == 0 || perk.spread > 0.0f);
            last = perk.refires;
        }

        CHECK(shot_perk(item, Stage::Omega, 8).refires > 0);
    }

    // The weapons with no shot moby of their own get none.
    CHECK(shot_perk(30, Stage::Omega, 8).refires == 0);    // Lancer
    CHECK(shot_perk(22, Stage::Omega, 8).end_shots == 0);  // Chopper

    // The Minirocket Tube splits from Epsilon and blasts at Omega.
    CHECK(shot_perk(27, Stage::Gamma, 3).split_after == 0);
    CHECK(shot_perk(27, Stage::Epsilon, 5).split_after > 0);
    CHECK(shot_perk(27, Stage::Omega, 8).end_blasts == 2);

    // The RYNO II: 2, then 3 homing RYNO rockets, splitting once at Omega and twice at Omega Prime.
    CHECK(shot_perk(44, Stage::None, 2).end_shots == 0);
    CHECK(shot_perk(44, Stage::Gamma, 3).end_shots == 2);
    CHECK(shot_perk(44, Stage::Epsilon, 5).end_shots == 3);
    CHECK(shot_perk(44, Stage::Gamma, 3).end_class == kRynoRocket);
    CHECK(shot_perk(44, Stage::Gamma, 3).end_homing);
    CHECK(shot_perk(44, Stage::Epsilon, 5).end_generations == 0);
    CHECK(shot_perk(44, Stage::Omega, 8).end_generations == 1);
    CHECK(shot_perk(44, Stage::Omega, 9).end_generations == 2);
    CHECK(shot_perk(44, Stage::Omega, 9).end_most > shot_perk(44, Stage::Omega, 8).end_most);

    // The Gravity Bomb's mini bombs from Epsilon, eight and a second bomb at Omega.
    CHECK(shot_perk(42, Stage::Gamma, 3).end_shots == 0);
    CHECK(shot_perk(42, Stage::Epsilon, 5).end_shots == 4);
    CHECK(shot_perk(42, Stage::Omega, 8).end_shots == 8);
    CHECK(shot_perk(42, Stage::Omega, 8).end_class == kMiniBomb);
    CHECK(shot_perk(42, Stage::Omega, 8).end_again_ticks > 0);

    // The Bomb Glove bursts into four at Omega; the Seeker Gun seeks again from Epsilon.
    CHECK(shot_perk(12, Stage::Omega, 8).end_shots == 4);
    CHECK(shot_perk(24, Stage::Epsilon, 5).end_class == kSeeker);
}

// Checks the copies' fan: right, left, further right, and the Pulse Rifle's fire rate by stage.
void test_refire_and_rate() {
    CHECK(refire_yaw(0, 0.2f) > 0.0f);
    CHECK(refire_yaw(1, 0.2f) < 0.0f);
    CHECK(refire_yaw(2, 0.2f) > refire_yaw(0, 0.2f));
    CHECK(refire_yaw(0, 0.2f) == -refire_yaw(1, 0.2f));
    CHECK(fire_rate(23, Stage::None) == 1.0f);
    CHECK(fire_rate(23, Stage::Gamma) > 1.0f);
    CHECK(fire_rate(23, Stage::Omega) > fire_rate(23, Stage::Epsilon));
    CHECK(fire_rate(30, Stage::Omega) == 1.0f);
}

// Checks the status perks: the Plasma Coil slows, the Tesla Claw and the Walloper stun, from
// Epsilon only.
void test_status_perks() {
    CHECK(hit_perk(28, Stage::Gamma).status == Status::None);
    CHECK(hit_perk(28, Stage::Epsilon).status == Status::SlowHalf);
    CHECK(hit_perk(28, Stage::Omega).status_ticks > hit_perk(28, Stage::Epsilon).status_ticks);
    CHECK(hit_perk(18, Stage::Gamma).status == Status::None);
    CHECK(hit_perk(18, Stage::Epsilon).status == Status::Freeze);
    CHECK(hit_perk(53, Stage::Epsilon).status == Status::Freeze);
    CHECK(hit_perk(53, Stage::Epsilon).status_ticks > 0);
}

}  // namespace

int main() try {
    test_tiers_and_stages();
    test_hit_perks_grow();
    test_hit_perks_signature();
    test_hit_perks_none();
    test_ryno_perk();
    test_ice_effect();
    test_ice_tint();
    test_armour_numbers();
    test_armour_looks();
    test_modifiers();
    test_elite_percent();
    test_is_elite();
    test_arenas_and_health();
    test_omega_prime();
    test_book_new_levels();
    test_shot_perks();
    test_refire_and_rate();
    test_status_perks();
    return ng_plus_plus::test::result("ng_plus_plus_perks");
} catch (...) {
    // A test that throws has failed.
    return 1;
}
