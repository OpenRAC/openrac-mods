// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++'s rules alone (src/tiers.cpp) and the book of saves (src/save_book.cpp): the kinds, steps,
// prices, experience, damage, ammo, tints, names and the enemies' curve, then the book's records,
// its text form and which save's tiers a clock brings.
//
// Expected values are the README's tables and their arithmetic (a price is the Mega's times the
// tier's factor, rounded to two and a half digits), and the decompiled game's behaviour named in
// src/save_book.h for the clock's cases.

#include <cmath>
#include <cstdint>
#include <string>

#include "check.h"
#include "save_book.h"
#include "tiers.h"
#include "weapon_visuals.h"

namespace {

using namespace ng_plus_plus;

// The items' numbers in the game's tables (seen in game code).
constexpr int kLancer = 30;
constexpr int kRyno = 44;

// The game keeps experience times this (<< 5; seen in game code: g_weaponXp).
constexpr std::int32_t kExpUnit = 32;

// Checks that every item is the kind the README says, and each kind's step count.
void test_kinds() {
    CHECK(kind_of(kLancer) == Kind::Four);
    CHECK(kind_of(12) == Kind::Two);    // Bomb Glove
    CHECK(kind_of(53) == Kind::Two);    // Walloper
    CHECK(kind_of(18) == Kind::Two);    // Tesla Claw: its beam hits
    CHECK(kind_of(17) == Kind::NoExp);  // Decoy Glove: deals no damage
    CHECK(kind_of(31) == Kind::Four);   // Synthenoid: its kills count, as the game's V2 step
    CHECK(kind_of(32) == Kind::Four);   // Spiderbot Glove
    CHECK(step_count(Kind::NoExp) == 8);
    CHECK(kind_of(9) == Kind::Zapper);  // Clank Zapper
    CHECK(kind_of(16) == Kind::Sheep);
    CHECK(kind_of(43) == Kind::Single);  // Zodiac
    CHECK(kind_of(kRyno) == Kind::Ryno);
    CHECK(kind_of(13) == Kind::None);  // Swingshot
    CHECK(kind_of(10) == Kind::None);  // the wrench
    CHECK(step_count(Kind::Four) == 8);
    CHECK(step_count(Kind::Ryno) == 10);
    CHECK(step_count(Kind::None) == 0);
}

// Checks a V4 weapon: Alpha for sale at once, Beta by experience, then Gamma for sale.
void test_buy_then_level() {
    CHECK(!can_buy(Kind::Four, 0, false, 0));  // not at its last version
    CHECK(can_buy(Kind::Four, 0, true, 0));
    CHECK(std::string(step_at(Kind::Four, 0)->name) == "Alpha");
    CHECK(!exp_reached(Kind::Four, 0, true, 1'000'000));  // Alpha is bought
    CHECK(!can_buy(Kind::Four, 1, true, 1'000'000));      // Beta is not for sale
    CHECK(!exp_reached(Kind::Four, 1, true, 7'999));
    CHECK(exp_reached(Kind::Four, 1, true, 8'000));
    CHECK(can_buy(Kind::Four, 2, true, 0));  // Gamma: Beta was reached
    CHECK(std::string(step_at(Kind::Four, 7)->name) == "Omega");
    CHECK(step_at(Kind::Four, 8) == nullptr);
    CHECK(!exp_reached(Kind::Four, 8, true, 1'000'000));
}

// Checks that bought last versions need mastering first.
void test_mastery() {
    CHECK(!can_buy(Kind::Two, 0, true, kMasteryExp - 1));
    CHECK(can_buy(Kind::Two, 0, true, kMasteryExp));
    CHECK(!can_buy(Kind::Single, 0, true, 0));
    CHECK(can_buy(Kind::Zapper, 0, true, 0));  // V2 came by experience
}

// Checks that the Decoy Glove buys every tier, none waiting on experience, the bar full, on the
// others' price ladder with the tiers between in between.
void test_no_experience() {
    std::int32_t last = 150'000;

    // Every tier of the Decoy Glove.
    for (int level = 0; level < 8; ++level) {
        const Step* step = step_at(Kind::NoExp, level);
        CHECK(step != nullptr && step->how == How::Buy);
        CHECK(std::string(step->name) == step_at(Kind::Four, level)->name);
        CHECK(damage(Kind::NoExp, level + 1) == damage(Kind::Four, level + 1));
        CHECK(can_buy(Kind::NoExp, level, true, 0));  // at once, no experience
        CHECK(!can_buy(Kind::NoExp, level, false, 0));
        CHECK(!exp_reached(Kind::NoExp, level, true, 1'000'000));
        CHECK(exp_goal(Kind::NoExp, level) == 0);
        const std::int32_t cost = price(Kind::NoExp, level, 150'000);
        CHECK(cost > last);
        last = cost;
    }

    CHECK(step_at(Kind::NoExp, 8) == nullptr);
    CHECK(mega_version(Kind::NoExp) == 1);

    // Alpha, Gamma, Epsilon and Eta as the others'; Omega at 9 times.
    for (int level = 0; level < 8; level += 2) {
        CHECK(price(Kind::NoExp, level, 150'000) == price(Kind::Four, level, 150'000));
    }

    CHECK(price(Kind::NoExp, 1, 150'000) == 300'000);
    CHECK(price(Kind::NoExp, 3, 150'000) == 490'000);
    CHECK(price(Kind::NoExp, 5, 150'000) == 750'000);
    CHECK(price(Kind::NoExp, 7, 150'000) == 1'350'000);
    CHECK(price(Kind::NoExp, 0, 1'000) == 225'000);  // the floor
}

// Checks that the tiers take the name of the version before the game's bought top.
void test_naming() {
    CHECK(name_version(Kind::Four) == 1);    // Meteor Gun, not Lava Gun nor its Mega
    CHECK(name_version(Kind::Two) == 0);     // Bomb Glove, not its Mega
    CHECK(name_version(Kind::NoExp) == 0);   // Decoy Glove
    CHECK(name_version(Kind::Zapper) == 1);  // Clank Shocker
    CHECK(name_version(Kind::Sheep) == 1);   // Black Sheepinator
    CHECK(name_version(Kind::Single) == 0);
    CHECK(name_version(Kind::Ryno) == 0);
    CHECK(name_version(Kind::None) == -1);

    // Every kind with a Mega is named at or before it.
    for (const Kind kind : {
             Kind::Four, Kind::Two, Kind::NoExp, Kind::Zapper, Kind::Sheep, Kind::Single, Kind::Ryno
         }) {
        const int mega = mega_version(kind);
        CHECK(mega < 0 || name_version(kind) <= mega);
    }
}

// Checks the Sheepinator: no Mega, never sold by the game, bought and levelled from V4 on.
void test_sheepinator() {
    CHECK(can_buy(Kind::Sheep, 0, true, 0));
    CHECK(step_at(Kind::Sheep, 1)->how == How::Exp);
    CHECK(mega_version(Kind::Sheep) < 0);
    CHECK(price(Kind::Sheep, 0, 0) == 2'250'000);
    CHECK(price(Kind::Sheep, 6, 0) == 9'000'000);
}

// Checks that prices follow the Mega, rising, each above the one before and the Mega.
void test_prices() {
    CHECK(price(Kind::Four, 0, 150'000) == 225'000);
    CHECK(price(Kind::Four, 1, 150'000) == 0);  // Beta is by experience
    CHECK(price(Kind::Four, 2, 150'000) == 375'000);
    CHECK(price(Kind::Four, 4, 150'000) == 600'000);
    CHECK(price(Kind::Four, 6, 150'000) == 900'000);
    CHECK(price(Kind::Four, 0, 375'000) == 565'000);
    CHECK(price(Kind::Two, 0, 1'000) == 225'000);         // the floor
    CHECK(price(Kind::Ryno, 1, 1'000'000) == 1'500'000);  // Ultima
    CHECK(price(Kind::Ryno, 8, 1'000'000) == 9'000'000);  // Eta
}

// Returns whether every bought tier of a four-version weapon with Mega `mega` costs more than the
// one before it and the Mega.
bool prices_rise(std::int32_t mega) {
    std::int32_t last = mega;

    // Each of the eight tiers; only the bought ones have a price.
    for (int level = 0; level < 8; ++level) {
        const std::int32_t cost = price(Kind::Four, level, mega);

        // A bought tier at or under the one before.
        if (cost != 0 && cost <= last) {
            return false;
        }

        last = cost != 0 ? cost : last;
    }

    return true;
}

// Checks that every bought tier costs more than the one before, for Megas across the whole range.
void test_prices_rise() {
    // Megas across the whole range, by a prime step.
    for (std::int32_t mega = 1'000; mega <= 2'000'000; mega += 7'919) {
        CHECK(prices_rise(mega));
    }
}

// Checks the RYNO II's own two steps, its slower experience and its names.
void test_ryno() {
    CHECK(std::string(step_at(Kind::Ryno, 0)->name) == "Heavy");
    CHECK(!exp_reached(Kind::Ryno, 0, true, 29'999));
    CHECK(exp_reached(Kind::Ryno, 0, true, 30'000));
    CHECK(!can_buy(Kind::Ryno, 0, true, 1'000'000));
    CHECK(can_buy(Kind::Ryno, 1, true, 0));   // Ultima: Heavy reached
    CHECK(!can_buy(Kind::Ryno, 2, true, 0));  // Alpha: Ultima must be mastered
    CHECK(!can_buy(Kind::Ryno, 2, true, kMasteryExp));
    CHECK(can_buy(Kind::Ryno, 2, true, 30'000));
    // The bar's goal: Heavy's experience, none for Ultima (for sale at once), Ultima's mastery for
    // Alpha, a step's own by experience, none past the last.
    CHECK(exp_goal(Kind::Ryno, 0) == 30'000);
    CHECK(exp_goal(Kind::Ryno, 1) == 0);
    CHECK(exp_goal(Kind::Ryno, 2) == 30'000);
    // Beta, Delta, Zeta and Omega: five times the others'.
    CHECK(exp_goal(Kind::Ryno, 3) == 40'000);
    CHECK(exp_goal(Kind::Ryno, 5) == 60'000);
    CHECK(exp_goal(Kind::Ryno, 7) == 80'000);
    CHECK(exp_goal(Kind::Ryno, 9) == 120'000);
    CHECK(!exp_reached(Kind::Ryno, 3, true, 39'999));
    CHECK(exp_reached(Kind::Ryno, 3, true, 40'000));
    CHECK(exp_goal(Kind::Four, 0) == 0);
    CHECK(exp_goal(Kind::Four, 1) == step_at(Kind::Four, 1)->exp);
    CHECK(exp_goal(Kind::Four, 2) == 0);
    CHECK(exp_goal(Kind::Two, 0) == kMasteryExp);
    CHECK(exp_goal(Kind::Ryno, step_count(Kind::Ryno)) == 0);
    CHECK(exp_goal(Kind::None, 0) == 0);
    CHECK(damage(Kind::Ryno, 2) > damage(Kind::Ryno, 1));
    CHECK(damage(Kind::Ryno, 3) > damage(Kind::Ryno, 2));
    CHECK(display_name("RYNO II", Kind::Ryno, 1) == "Heavy RYNO II");
    CHECK(display_name("RYNO II", Kind::Ryno, 2) == "Ultima RYNO II");
    CHECK(display_name("RYNO II", Kind::Ryno, 10) == "Omega RYNO II");
}

// Checks damage, ammo, tints and names rising with the tiers.
void test_scaling() {
    CHECK(damage(Kind::Four, 0) == 1.0f);

    // Each tier above the one before.
    for (int level = 1; level < 8; ++level) {
        CHECK(damage(Kind::Four, level + 1) > damage(Kind::Four, level));
        CHECK(ammo(Kind::Four, level + 1) > ammo(Kind::Four, level));
    }

    CHECK(ammo_max(Kind::Four, 0, 50) == 50);
    CHECK(ammo_max(Kind::Four, 1, 50) == 55);
    CHECK(ammo_max(Kind::Four, 8, 50) == 100);
    CHECK(ammo_max(Kind::Sheep, 3, 0) == 0);  // endless stays endless
    CHECK(ammo_max(Kind::Four, 8, 60'000) == 0xFFFF);
    CHECK(!tint_rgb(Kind::Four, 0, 0));
    CHECK(tint_rgb(Kind::Four, 1, 0) == 0x20FFB0u);
    CHECK(tint_rgb(Kind::Four, 2, 0) == 0xFFE000u);
    // Omega moves; Alpha does not.
    CHECK(tint_rgb(Kind::Four, 8, 0) != tint_rgb(Kind::Four, 8, 100));
    CHECK(tint_rgb(Kind::Four, 1, 0) == tint_rgb(Kind::Four, 1, 100));
    CHECK(tint_rgb(Kind::Ryno, 2, 0) != tint_rgb(Kind::Ryno, 2, 30));
    // The effects' colours: the tint's hue at full strength, 0xRRGGBB; Ultima's red beats from
    // bright to deep, never to black; Omega's rainbow moves.
    CHECK(!effect_rgb(Kind::Four, 0, 0));
    CHECK(effect_rgb(Kind::Four, 1, 0) == 0xB0FF20u);  // lime
    CHECK(effect_rgb(Kind::Four, 2, 0) == 0x00E0FFu);  // cyan
    CHECK(effect_rgb(Kind::Four, 3, 0) == 0x1AFF80u);  // emerald, raised
    CHECK(effect_rgb(Kind::Four, 4, 0) == 0x00FFFFu);  // teal, raised
    CHECK(effect_rgb(Kind::Four, 8, 0) != effect_rgb(Kind::Four, 8, 100));
    CHECK(effect_rgb(Kind::Ryno, 1, 0) == 0xFF8000u);  // Heavy's orange
    CHECK(effect_rgb(Kind::Ryno, 2, 0) == 0xFF1A1Au);  // Ultima, bright
    CHECK(effect_rgb(Kind::Ryno, 2, 30) == 0x600000u);  // Ultima, deep
    // Stronger with each tier, the top at the last.
    CHECK(effect_strength(Kind::Four, 0) == 0);
    CHECK(effect_strength(Kind::Four, 1) == 2);
    CHECK(effect_strength(Kind::Four, 8) == kEffectTop);
    CHECK(effect_strength(Kind::Ryno, 1) == 2);
    CHECK(effect_strength(Kind::Ryno, 10) == kEffectTop);
    CHECK(effect_strength(Kind::None, 3) == 0);

    for (int level = 1; level < kTiers; ++level) {
        CHECK(effect_strength(Kind::Four, level + 1) > effect_strength(Kind::Four, level));
    }

    CHECK(tint_moves(Kind::Four, 8) && tint_moves(Kind::Ryno, 2));
    CHECK(!tint_moves(Kind::Four, 1) && !tint_moves(Kind::Ryno, 1) && !tint_moves(Kind::Four, 0));
    CHECK(display_name("Lancer", Kind::Four, 1) == "Alpha Lancer");
    CHECK(display_name("Lancer", Kind::Four, 0) == "Lancer");
}

// The enemies' health of 0.4.0 from cycle 2 on (x2.5, x4, x5.5, x6, x6.5, then x6.75).
float health_040(int cycle) {
    constexpr float kHealth040[] = {2.5f, 4.0f, 5.5f, 6.0f, 6.5f, 6.75f};
    return cycle < 2 ? 1.0f : kHealth040[cycle - 2 < 5 ? cycle - 2 : 5];
}

// Returns the tier level (of a four-version weapon) a player can have by cycle `cycle`: Delta at
// 2, Zeta at 3, Omega from 4.
int matching_level(int cycle) {
    return cycle <= 2 ? 4 : (cycle == 3 ? 6 : 8);
}

// Checks the enemies' curve: the README's table, its strength, more hits than 0.4.0's, and the
// tier a player can have by then needing at most half as many hits again as V4 in the first
// challenge mode.
void test_enemies() {
    CHECK(enemy_health(0, 1.0f) == 1.0f);
    CHECK(enemy_health(1, 1.0f) == 1.0f);
    CHECK(enemy_health(2, 1.0f) == 3.75f);
    CHECK(enemy_health(3, 1.0f) == 6.0f);
    CHECK(enemy_health(4, 1.0f) == 9.0f);
    CHECK(enemy_health(5, 1.0f) == 9.75f);
    CHECK(enemy_health(6, 1.0f) == 10.25f);
    CHECK(enemy_health(7, 1.0f) == 10.5f);
    CHECK(enemy_health(20, 1.0f) == 10.5f);
    CHECK(enemy_damage(1, 1.0f) == 1.0f);
    CHECK(enemy_damage(2, 1.0f) == 1.375f);
    CHECK(enemy_damage(5, 1.0f) == 2.5f);
    CHECK(enemy_damage(7, 1.0f) == 3.25f);
    CHECK(enemy_damage(20, 1.0f) == 4.0f);
    CHECK(enemy_health(2, 0.5f) == 2.375f);
    CHECK(enemy_health(9, 0.0f) == 1.0f);

    // Every run from cycle 2: more hits than 0.4.0, and the tier a player can have by then needs a
    // quarter to a half more hits than V4 did in the first challenge mode, never more.
    for (int cycle = 2; cycle < 40; ++cycle) {
        const float hits = enemy_health(cycle, 1.0f) / damage(Kind::Four, matching_level(cycle));
        CHECK(enemy_health(cycle, 1.0f) > health_040(cycle));
        CHECK(hits >= 1.25f);
        CHECK(hits <= 1.5f);
    }

    // The later runs clearly harder than 0.4.0's, even with Omega: half again from cycle 5.
    for (int cycle = 5; cycle < 40; ++cycle) {
        CHECK(enemy_health(cycle, 1.0f) / health_040(cycle) >= 1.4f);
        CHECK(enemy_health(cycle, 1.0f) > damage(Kind::Four, 8));
    }

    // Every tier is a clear step on the game's own V4 (a quarter at the least).
    for (int level = 1; level <= kTiers; ++level) {
        CHECK(damage(Kind::Four, level) >= 1.5f);
    }

    // Each cycle tougher than the one before up to the most, the health ahead of the damage.
    for (int cycle = 2; cycle < 12; ++cycle) {
        CHECK(enemy_health(cycle + 1, 1.0f) > enemy_health(cycle, 1.0f) || cycle >= 7);
        CHECK(enemy_health(cycle + 1, 1.0f) >= enemy_health(cycle, 1.0f));
        CHECK(enemy_health(cycle, 1.0f) > enemy_damage(cycle, 1.0f));
    }
}

// Returns a foe: a boss (`last`: the final one) or an enemy with `bolts` of its own.
Foe foe_of(bool boss, bool last, std::int32_t bolts) {
    Foe foe;
    foe.boss = boss;
    foe.last = last;
    foe.bolts = bolts;
    return foe;
}

// Checks the bosses: x2 more health from cycle 2, a quarter more a run up to x3, the final boss
// half as much again, by their classes; a hit on one divided by both.
void test_bosses() {
    CHECK(boss_health(0, 1.0f) == 1.0f);
    CHECK(boss_health(1, 1.0f) == 1.0f);
    CHECK(boss_health(2, 1.0f) == 2.0f);
    CHECK(boss_health(3, 1.0f) == 2.25f);
    CHECK(boss_health(6, 1.0f) == 3.0f);
    CHECK(boss_health(30, 1.0f) == 3.0f);
    CHECK(boss_health(2, 0.5f) == 1.5f);
    CHECK(is_boss(2644));   // the Swamp Monster
    CHECK(is_boss(4576));   // the mutated Protopet
    CHECK(!is_boss(2921));  // an ordinary Protopet
    CHECK(!is_boss(0));
    CHECK(boss_of(4576)->last);
    CHECK(!boss_of(2644)->last);

    const Foe ordinary = foe_of(false, false, 45);
    const Foe boss = foe_of(true, false, 0);
    const Foe last = foe_of(true, true, 0);
    CHECK(hit_divisor(Kind::Four, 8, 4, 1.0f, ordinary) == 9.0f);
    CHECK(hit_divisor(Kind::Four, 8, 4, 1.0f, boss) == 22.5f);
    CHECK(hit_divisor(Kind::Four, 8, 4, 1.0f, last) == 33.75f);
    CHECK(hit_divisor(Kind::None, 0, 2, 1.0f, ordinary) == 3.75f);
    // The first game and the first challenge mode: the game's own, bosses too.
    CHECK(hit_divisor(Kind::Four, 0, 1, 1.0f, last) == 1.0f);

    // Every run: a boss's health well past an ordinary enemy's, growing, the final boss's past it.
    for (int cycle = 2; cycle < 12; ++cycle) {
        const float on_boss = hit_divisor(Kind::Four, 8, cycle, 1.0f, boss);
        CHECK(on_boss >= 2.0f * enemy_health(cycle, 1.0f));
        CHECK(hit_divisor(Kind::Four, 8, cycle, 1.0f, last) > on_boss);
        CHECK(boss_health(cycle + 1, 1.0f) >= boss_health(cycle, 1.0f));
    }
}

// Returns how many times the game's own RYNO II's hits the RYNO II at `level` needs on `foe` in
// `cycle`: what its hit is divided by, over its tier's damage.
float ryno_times(int level, int cycle, const Foe& foe) {
    return hit_divisor(Kind::Ryno, level, cycle, 1.0f, foe) / damage(Kind::Ryno, level);
}

// Checks the RYNO II's fights: the strongest weapon, none of the curve on an ordinary enemy (which it kills
// as the game's own does, or faster with its tier); a tough one (110 bolts and more, the top
// quarter) needs two to four times the game's RYNO II's hits, more in later runs and for bigger
// ones; a boss three to seven times, the final boss half as many again.
void test_ryno_fights() {
    const Foe small = foe_of(false, false, 8);
    const Foe common = foe_of(false, false, 45);
    const Foe tough = foe_of(false, false, 110);
    const Foe biggest = foe_of(false, false, 984);
    const Foe boss = foe_of(true, false, 0);
    const Foe last = foe_of(true, true, 0);

    // The runs the game balances itself: as the game made it.
    for (int cycle = 0; cycle < 2; ++cycle) {
        CHECK(ryno_hits(cycle, 1.0f, last) == 0.0f);
        CHECK(hit_divisor(Kind::Ryno, 10, cycle, 1.0f, tough) == 1.0f);
    }

    // Every run and every version: the game's, Heavy, Ultima, the eight tiers.
    for (int cycle = 0; cycle < 40; ++cycle) {
        // Each version.
        for (int level = 0; level <= kMaxSteps; ++level) {
            // An ordinary enemy: the hit as its tier makes it, never divided by the curve (one shot
            // for one the game's own kills in one).
            for (const Foe& foe : {small, common}) {
                const float hit =
                    damage(Kind::Ryno, level) / hit_divisor(Kind::Ryno, level, cycle, 1.0f, foe);
                CHECK(hit >= 1.0f);
                CHECK(hit == damage(Kind::Ryno, level));
            }

            // Past cycle 1: tough ones, bosses and the final boss each need more hits.
            if (cycle >= 2) {
                CHECK(ryno_times(level, cycle, tough) >= 1.999f);
                CHECK(ryno_times(level, cycle, biggest) > ryno_times(level, cycle, tough));
                CHECK(ryno_times(level, cycle, boss) >= 2.999f);
                CHECK(ryno_times(level, cycle, last) > ryno_times(level, cycle, boss));
                CHECK(ryno_times(level, cycle, last) >= 4.499f);
            }
        }
    }

    // More hits in later runs, up to the most.
    CHECK(ryno_hits(2, 1.0f, tough) == 2.0f);
    CHECK(ryno_hits(4, 1.0f, tough) == 3.0f);
    CHECK(ryno_hits(9, 1.0f, tough) == 4.0f);
    CHECK(std::fabs(ryno_hits(6, 1.0f, biggest) - 4.0f * std::sqrt(984.0f / 110.0f)) < 0.001f);
    CHECK(ryno_hits(2, 1.0f, boss) == 3.0f);
    CHECK(ryno_hits(6, 1.0f, boss) == 7.0f);
    CHECK(ryno_hits(20, 1.0f, boss) == 7.0f);
    CHECK(ryno_hits(2, 1.0f, last) == 4.5f);
    CHECK(ryno_hits(6, 1.0f, last) == 10.5f);
    CHECK(ryno_hits(6, 0.0f, last) == 0.0f);

    // Still the strongest: on an ordinary enemy its Omega is over eight times the game's RYNO II in
    // every run, four times and more what any other weapon's Omega keeps of its own after the curve.
    for (int cycle = 2; cycle < 40; ++cycle) {
        const float ryno = damage(Kind::Ryno, 10) / hit_divisor(Kind::Ryno, 10, cycle, 1.0f, common);
        const float other = damage(Kind::Four, 8) / hit_divisor(Kind::Four, 8, cycle, 1.0f, common);
        CHECK(ryno >= 8.0f);
        CHECK(ryno >= 4.0f * other);
    }
}

// Checks the book's records: replaced by clock, the oldest dropped when full, and its text form
// read back as written.
void test_book() {
    SaveBook a;
    SaveBook b;
    Clock clock = {1, 2, 3, 4, 5, 6, 7, 8};
    LevelBytes levels{};

    levels[kLancer] = 2;
    levels[kRyno] = 10;
    a.put(clock, levels);
    levels[kLancer] = 3;
    a.put(clock, levels);  // replaces
    CHECK(a.records().size() == 1);
    CHECK(a.find(clock)->levels[kLancer] == 3);
    clock[0] = 9;
    CHECK(a.find(clock) == nullptr);

    // More clocks than the book keeps.
    for (std::size_t n = 0; n < kRecords + 5; ++n) {
        clock[1] = static_cast<std::uint8_t>(n);
        a.put(clock, levels);
    }

    CHECK(a.records().size() == kRecords);
    const std::string text = a.write();
    CHECK(text.size() == kRecords * (16 + 1 + 2 * kItems + 1));
    CHECK(b.read(text) == kRecords);
    CHECK(a.records() == b.records());
    CHECK(b.read("nonsense\n0102\n") == 0);
}

// Checks that zeros never go over a record with levels, and that the latest is the last written
// with any level.
void test_book_keeps() {
    SaveBook a;
    const Clock one = {0, 1, 2, 3, 0, 12, 11, 0x26};
    const Clock two = {0, 9, 2, 3, 0, 12, 11, 0x26};
    LevelBytes levels{};
    const LevelBytes none{};

    CHECK(a.latest() == nullptr);
    levels[28] = 0x81;
    a.put(one, levels);
    a.put(one, none);
    CHECK(a.find(one)->levels[28] == 0x81);
    a.put(two, none);  // a new clock with nothing
    CHECK(a.latest() == a.find(one));
}

// Returns a ClockMove of its five values, in ClockMove's order.
ClockMove move(bool in_play, int changes, bool carried, int carry_changes, int cycle) {
    ClockMove out;
    out.in_play = in_play;
    out.changes = changes;
    out.carried = carried;
    out.carry_changes = carry_changes;
    out.cycle = cycle;
    return out;
}

// Checks which levels a save gets when its clock moves: only its own.
void test_saves() {
    SaveBook book;
    const Clock a = {0, 1, 2, 3, 0, 12, 11, 0x26};   // save A, tiered
    const Clock a2 = {0, 5, 2, 3, 0, 12, 11, 0x26};  // A stamped again
    const Clock b = {0, 7, 9, 3, 0, 12, 11, 0x26};   // save B, never tiered
    const Clock zero{};
    LevelBytes levels{};
    SavePrint one;

    one.bolts = 1000;
    one.exp[29] = 100 * kExpUnit;
    one.row[29] = 96;
    SavePrint two = one;
    CHECK(print_changes(one, two) == 0);
    two.exp[29] += 32;  // a kill in the tick
    CHECK(print_changes(one, two) == 1);
    two.bolts = 5;
    two.row[kLancer] = 80;
    CHECK(print_changes(one, two) == 3);

    levels[29] = static_cast<std::uint8_t>(2u | kStartedBit);
    book.put(a, levels);
    // A loaded (the title shows its clock): its record, from nothing or from another save.
    ClockVerdict verdict = clock_case_of(book, a, move(false, 99, false, 99, 0));
    CHECK(verdict.what == ClockCase::Record);
    CHECK(verdict.record != nullptr && verdict.record->levels[29] == 0x82);
    CHECK(clock_case_of(book, a, move(true, 99, false, 99, 0)).what == ClockCase::Record);
    // A stamped again as its level starts (or at a save): nothing else changed, its levels go on.
    verdict = clock_case_of(book, a2, move(true, 0, false, 99, 0));
    CHECK(verdict.what == ClockCase::Same && verdict.record == nullptr);
    CHECK(clock_case_of(book, a2, move(true, 1, false, 99, 0)).what == ClockCase::Same);
    // B loaded while A was in play (the save's data replaced): none of A's, nothing started.
    CHECK(clock_case_of(book, b, move(true, kLoadChanges, false, 99, 3)).what == ClockCase::New);
    // B loaded from nothing (the title after zeros), even in challenge mode: none.
    CHECK(clock_case_of(book, b, move(false, 99, true, 40, 3)).what == ClockCase::New);
    CHECK(clock_case_of(book, b, move(false, 99, false, 99, 3)).what == ClockCase::New);
    // Challenge mode's next game: zeros, then a new clock with the weapons as they were.
    CHECK(clock_case_of(book, zero, move(true, 0, false, 99, 1)).what == ClockCase::Zero);
    CHECK(clock_case_of(book, b, move(false, 99, true, 0, 2)).what == ClockCase::Carry);
    // A new game (counter 0, weapons gone): none.
    CHECK(clock_case_of(book, b, move(false, 99, true, 0, 0)).what == ClockCase::New);
    // A, then B (never tiered: no record kept), then A again: A's.
    CHECK(book.find(b) == nullptr);
    verdict = clock_case_of(book, a, move(true, 50, false, 99, 0));
    CHECK(verdict.what == ClockCase::Record);
    CHECK(verdict.record != nullptr && verdict.record->levels[29] == 0x82);
}

}  // namespace

// Checks the weapons' looks (weapon_visuals.h): every class names a weapon the tiers know, the
// Shield Charger's shield takes each tier's own colour and stronger energy, Omega's moving, and
// without energy only a shot's or bot's body is tinted.
void test_weapon_looks() {
    // Each class a weapon of the tiers', listed once.
    for (const VisualClass& visual : kVisualClasses) {
        CHECK(kind_of(visual.item) != Kind::None);
        CHECK(visual_class(visual.moby_class) == &visual);
    }

    CHECK(visual_class(9999) == nullptr);
    const VisualClass* shield = visual_class(2652);
    const VisualClass* barrier = visual_class(4498);
    CHECK(shield != nullptr && shield->item == 45 && barrier != nullptr && barrier->item == 45);
    const Kind kind = kind_of(45);
    CHECK(!look_for(*shield, kind, 0, true, 0));
    std::uint32_t last_rgb = 0;
    std::uint32_t last_material = 0;

    // Alpha to Eta: each its own colour, each stronger than the one before.
    for (int level = 1; level < kTiers; ++level) {
        const std::optional<MobyLook> look = look_for(*shield, kind, level, true, 0);
        CHECK(look && (look->material & 0xFFu) == kMaterialEnergy);
        CHECK(look && look->rgb == effect_rgb(kind, level, 0) && look->rgb != last_rgb);
        CHECK(look && look->material > last_material);
        last_rgb = look ? look->rgb : 0;
        last_material = look ? look->material : 0;
    }

    // Omega: the strongest, its colour cycling.
    const std::optional<MobyLook> omega = look_for(*shield, kind, kTiers, true, 0);
    CHECK(omega && omega->material == energy_at(kEffectTop));
    CHECK(omega && omega != look_for(*shield, kind, kTiers, true, 100));
    // No energy: the shield (no body) left as the game draws it; a bot's body tinted.
    CHECK(!look_for(*shield, kind, 3, false, 0));
    const VisualClass* bot = visual_class(2904);
    const std::optional<MobyLook> tinted = look_for(*bot, kind_of(bot->item), 3, false, 0);
    CHECK(tinted && tinted->material == kMaterialTint && tinted->rgb == effect_rgb(Kind::Four, 3, 0));
}

int main() try {
    test_kinds();
    test_buy_then_level();
    test_mastery();
    test_no_experience();
    test_naming();
    test_sheepinator();
    test_prices();
    test_prices_rise();
    test_ryno();
    test_scaling();
    test_enemies();
    test_bosses();
    test_ryno_fights();
    test_book();
    test_book_keeps();
    test_saves();
    test_weapon_looks();
    return ng_plus_plus::test::result("ng_plus_plus_rules");
} catch (...) {
    // A test that throws has failed.
    return 1;
}
