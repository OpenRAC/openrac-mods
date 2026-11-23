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
#include <utility>

#include "check.h"
#include "discs.h"
#include "extra_enemies.h"
#include "perk_rules.h"
#include "save_book.h"
#include "save_tiers.h"
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
    CHECK(step_count(Kind::Ryno) == 10);  // Heavy, the eight tiers, Ultima
    CHECK(step_count(Kind::Ryno) == kRynoStepCount);
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
    // Alpha of a two-version weapon and the Zodiac comes by experience: a full bar levels up.
    for (const Kind kind : {Kind::Two}) {
        CHECK(step_at(kind, 0)->how == How::Exp);
        CHECK(!can_buy(kind, 0, true, 1'000'000));
        CHECK(!exp_reached(kind, 0, true, kMasteryExp - 1));
        CHECK(exp_reached(kind, 0, true, kMasteryExp));
        CHECK(!exp_reached(kind, 0, false, kMasteryExp));
        CHECK(exp_goal(kind, 0) == kMasteryExp);
        CHECK(std::string(step_at(kind, 0)->name) == "Alpha");
        CHECK(!can_buy(kind, 1, true, 1'000'000));  // Beta by experience, Gamma bought
        CHECK(exp_reached(kind, 1, true, 8'000));
        CHECK(can_buy(kind, 2, true, 0));
        CHECK(price(kind, 0, 0) == 0);
    }
    CHECK(can_buy(Kind::Zapper, 0, true, 0));  // V2 came by experience
}

// 0.6.14: the Zodiac's path: Heavy (XP), Mega (bought), Ultra (XP), then Alpha to Omega on the RYNO
// II's experience (0.6.13: five times the usual) and prices of the same tiers.
void test_zodiac_as_ryno() {
    const Kind zodiac = Kind::Single;
    CHECK(step_count(zodiac) == 11);
    CHECK(step_count(zodiac) == kMaxSteps);
    const char* names[] = {"Heavy", "Mega", "Ultra", "Alpha", "Beta", "Gamma",
                           "Delta", "Epsilon", "Zeta", "Eta", "Omega"};
    for (int n = 0; n < 11; ++n) {
        CHECK(std::string(step_at(zodiac, n)->name) == names[n]);
    }
    CHECK(step_at(zodiac, 11) == nullptr);
    CHECK(display_name("Zodiac", zodiac, 1) == "Heavy Zodiac");
    CHECK(display_name("Zodiac", zodiac, 2) == "Mega Zodiac");
    CHECK(display_name("Zodiac", zodiac, 3) == "Ultra Zodiac");
    CHECK(display_name("Zodiac", zodiac, 4) == "Alpha Zodiac");
    CHECK(display_name("Zodiac", zodiac, 11) == "Omega Zodiac");

    // Heavy by XP, in line with the RYNO II's Heavy; Mega bought; Ultra by XP.
    CHECK(step_at(zodiac, 0)->how == How::Exp);
    CHECK(!exp_reached(zodiac, 0, true, 37'499));
    CHECK(exp_reached(zodiac, 0, true, 37'500));
    CHECK(exp_goal(zodiac, 0) == exp_goal(Kind::Ryno, 0));
    CHECK(step_at(zodiac, 1)->how == How::Buy);
    CHECK(can_buy(zodiac, 1, true, 0));  // for sale as soon as it is Heavy
    CHECK(!can_buy(zodiac, 1, false, 0));
    CHECK(price(zodiac, 1, 1'000'000) == price(Kind::Ryno, 1, 1'000'000));  // 6,000,000
    CHECK(price(zodiac, 1, 1'000'000) == 6'000'000);
    CHECK(step_at(zodiac, 2)->how == How::Exp);
    CHECK(!can_buy(zodiac, 2, true, 1'000'000));
    CHECK(exp_goal(zodiac, 2) == 50'000);
    CHECK(!exp_reached(zodiac, 2, true, 49'999));
    CHECK(exp_reached(zodiac, 2, true, 50'000));
    CHECK(price(zodiac, 0, 1'000'000) == 0);
    CHECK(price(zodiac, 2, 1'000'000) == 0);

    // Alpha onward keeps 0.6.13's goals and prices.
    CHECK(exp_goal(zodiac, 3) == exp_goal(Kind::Ryno, 0));  // Alpha: 37,500
    CHECK(exp_goal(zodiac, 4) == exp_goal(Kind::Ryno, 2));  // Beta: 50,000
    CHECK(exp_goal(zodiac, 6) == exp_goal(Kind::Ryno, 4));  // Delta: 75,000
    CHECK(exp_goal(zodiac, 8) == exp_goal(Kind::Ryno, 6));  // Zeta: 100,000
    CHECK(exp_goal(zodiac, 10) == exp_goal(Kind::Ryno, 8));  // Omega: 150,000
    CHECK(can_buy(zodiac, 5, true, 0));
    CHECK(!can_buy(zodiac, 6, true, 1'000'000));
    CHECK(price(zodiac, 5, 1'000'000) == 10'000'000);
    CHECK(price(zodiac, 7, 1'000'000) == 16'000'000);
    CHECK(price(zodiac, 9, 1'000'000) == 25'000'000);

    // Absolute RYNO-level prices whatever the Zodiac's own row says (none, or a cheap one).
    for (const std::int32_t row : {0, 1'000, 150'000, 1'000'000}) {
        CHECK(price(zodiac, 1, row) == 6'000'000);
        CHECK(price(zodiac, 5, row) == 10'000'000);
        CHECK(price(zodiac, 7, row) == 16'000'000);
        CHECK(price(zodiac, 9, row) == 25'000'000);
    }
    CHECK(price(zodiac, 1, 1'000'000) == price(Kind::Ryno, 1, 1'000'000));

    // The three new steps rise, below Alpha; Alpha and up keep the other weapons' tiers'.
    for (int n = 1; n < 4; ++n) {
        CHECK(step_at(zodiac, n)->damage > step_at(zodiac, n - 1)->damage);
        CHECK(step_at(zodiac, n)->ammo > step_at(zodiac, n - 1)->ammo);
    }
    CHECK(step_at(zodiac, 2)->damage < step_at(zodiac, 3)->damage);
    for (int n = 3; n < 11; ++n) {
        CHECK(step_at(zodiac, n)->damage == step_at(Kind::Two, n - 3)->damage);
    }

    // Heavy, Mega and Ultra are no tier; Alpha is the first.
    CHECK(tier_of(zodiac, 3) == 0);
    CHECK(tier_of(zodiac, 4) == 1);
    CHECK(tier_of(zodiac, 11) == 8);
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
    CHECK(price(Kind::Two, 2, 1'000) == 375'000);          // the floor, Gamma (Alpha is by exp)
    CHECK(price(Kind::Ryno, 0, 1'000'000) == 0);           // Heavy: by experience
    CHECK(price(Kind::Ryno, 1, 1'000'000) == 6'000'000);   // Alpha
    CHECK(price(Kind::Ryno, 3, 1'000'000) == 10'000'000);  // Gamma
    CHECK(price(Kind::Ryno, 5, 1'000'000) == 16'000'000);  // Epsilon
    CHECK(price(Kind::Ryno, 7, 1'000'000) == 25'000'000);  // Eta
    CHECK(price(Kind::Ryno, 9, 1'000'000) == 0);           // Ultima: by experience
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

// Checks the RYNO II's path: Heavy by experience, Alpha to Omega, Ultima by experience last; its
// slower experience and its names.
void test_ryno() {
    constexpr const char* kNames[] = {
        "Heavy", "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Omega", "Ultima"
    };
    constexpr How kHow[] = {How::Exp, How::Buy, How::Exp, How::Buy, How::Exp,
                            How::Buy, How::Exp, How::Buy, How::Exp, How::Exp};

    // Each step's name and how it is reached.
    for (int level = 0; level < step_count(Kind::Ryno); ++level) {
        CHECK(std::string(step_at(Kind::Ryno, level)->name) == kNames[level]);
        CHECK(step_at(Kind::Ryno, level)->how == kHow[level]);
    }

    CHECK(step_at(Kind::Ryno, 10) == nullptr);  // Ultima is the last: no Omega Prime
    CHECK(!exp_reached(Kind::Ryno, 0, true, 37'499));
    CHECK(exp_reached(Kind::Ryno, 0, true, 37'500));
    CHECK(!can_buy(Kind::Ryno, 0, true, 1'000'000));
    CHECK(can_buy(Kind::Ryno, 1, true, 0));   // Alpha: for sale once Heavy is reached
    CHECK(!can_buy(Kind::Ryno, 1, false, 0));
    CHECK(can_buy(Kind::Ryno, 3, true, 0));  // Gamma: for sale once Beta is reached
    // The bar's goal: Heavy's experience, none for Alpha (for sale at once), a step's own by
    // experience, Ultima's after Omega, none past it.
    CHECK(exp_goal(Kind::Ryno, 0) == 37'500);
    CHECK(exp_goal(Kind::Ryno, 1) == 0);
    // Beta, Delta, Zeta, Omega and Ultima: five times the others'.
    CHECK(exp_goal(Kind::Ryno, 2) == 50'000);
    CHECK(exp_goal(Kind::Ryno, 4) == 75'000);
    CHECK(exp_goal(Kind::Ryno, 6) == 100'000);
    CHECK(exp_goal(Kind::Ryno, 8) == 150'000);
    CHECK(exp_goal(Kind::Ryno, 9) == 187'500);
    CHECK(!exp_reached(Kind::Ryno, 2, true, 49'999));
    CHECK(exp_reached(Kind::Ryno, 2, true, 50'000));
    CHECK(!exp_reached(Kind::Ryno, 9, true, 187'499));
    CHECK(exp_reached(Kind::Ryno, 9, true, 187'500));
    CHECK(exp_goal(Kind::Four, 0) == 0);
    CHECK(exp_goal(Kind::Four, 1) == step_at(Kind::Four, 1)->exp);
    CHECK(exp_goal(Kind::Four, 2) == 0);
    CHECK(exp_goal(Kind::Two, 0) == kMasteryExp);
    CHECK(exp_goal(Kind::Ryno, step_count(Kind::Ryno)) == 0);
    CHECK(exp_goal(Kind::None, 0) == 0);
    // Damage and ammo: Heavy's x1.4 and x1.5, rising each step to Ultima's x9.7 and x3.2.
    CHECK(std::fabs(damage(Kind::Ryno, 1) - 1.4f) < 0.001f);
    CHECK(std::fabs(ammo(Kind::Ryno, 1) - 1.5f) < 0.001f);
    CHECK(std::fabs(damage(Kind::Ryno, 10) - 9.7f) < 0.001f);
    CHECK(std::fabs(ammo(Kind::Ryno, 10) - 3.2f) < 0.001f);
    CHECK(ammo_max(Kind::Ryno, 10, 100) == 320);

    // No step weaker than the one before, and the gains between steps never shrink much: a
    // smooth curve.
    for (int level = 1; level < step_count(Kind::Ryno); ++level) {
        CHECK(damage(Kind::Ryno, level + 1) > damage(Kind::Ryno, level));
        CHECK(ammo(Kind::Ryno, level + 1) > ammo(Kind::Ryno, level));
    }

    for (int level = 2; level < step_count(Kind::Ryno); ++level) {
        const float gain = damage(Kind::Ryno, level + 1) - damage(Kind::Ryno, level);
        CHECK(gain >= damage(Kind::Ryno, level) - damage(Kind::Ryno, level - 1) - 0.001f);
    }

    CHECK(display_name("RYNO II", Kind::Ryno, 1) == "Heavy RYNO II");
    CHECK(display_name("RYNO II", Kind::Ryno, 2) == "Alpha RYNO II");
    CHECK(display_name("RYNO II", Kind::Ryno, 9) == "Omega RYNO II");
    CHECK(display_name("RYNO II", Kind::Ryno, 10) == "Ultima RYNO II");
}

// Checks the experience bar for every kind: at the last step it is full (goal 0) with nothing past
// it; at every other step its goal leads to a real next step: the next by experience reached at
// that goal, or the next bought for sale at that goal (at once with none).
void test_bar_goals() {
    for (const Kind kind : {
             Kind::Four, Kind::Two, Kind::NoExp, Kind::Zapper, Kind::Sheep, Kind::Single, Kind::Ryno
         }) {
        const int steps = step_count(kind);

        // At the last step: full, nothing to fill toward and nothing to reach or buy.
        CHECK(exp_goal(kind, steps) == 0);
        CHECK(step_at(kind, steps) == nullptr);
        CHECK(!exp_reached(kind, steps, true, 10'000'000));
        CHECK(!can_buy(kind, steps, true, 10'000'000));

        // Every step before: its goal gives the next step, and no less does.
        for (int level = 0; level < steps; ++level) {
            const std::int32_t goal = exp_goal(kind, level);
            const Step* next = step_at(kind, level);
            CHECK(next != nullptr);

            // The next by experience: reached at the goal, not before.
            if (next->how == How::Exp) {
                CHECK(goal > 0);
                CHECK(exp_reached(kind, level, true, goal));
                CHECK(!exp_reached(kind, level, true, goal - 1));
                continue;
            }

            // The next bought: for sale at the goal, not before; a full bar means for sale now.
            CHECK(can_buy(kind, level, true, goal));
            CHECK(goal == 0 || !can_buy(kind, level, true, goal - 1));
        }
    }
}

// Checks the RYNO II's colours along its path: Heavy orange, the tiers' tints, Ultima black and red.
void test_ryno_colours() {
    CHECK(tint_rgb(Kind::Ryno, 1, 0) == 0x0080FFu);  // Heavy's orange

    // Alpha to Omega: the tiers' own tints.
    for (int tier = 1; tier <= kTiers; ++tier) {
        CHECK(step_at(Kind::Ryno, tier)->tint == step_at(Kind::Four, tier - 1)->tint);
        CHECK(tint_rgb(Kind::Ryno, tier + 1, 77) == tint_rgb(Kind::Four, tier, 77));
    }

    CHECK(step_at(Kind::Ryno, 9)->tint == Tint::Ultima);
    CHECK(tint_rgb(Kind::Ryno, 10, 0) == 0x0000E0u);  // Ultima's red...
    CHECK(tint_rgb(Kind::Ryno, 10, 30) == 0x080808u);  // ...beating to black
    CHECK(tint_moves(Kind::Ryno, 10));
    CHECK(effect_rgb(Kind::Ryno, 10, 0) == 0xFF1A1Au);  // bright red
    CHECK(effect_rgb(Kind::Ryno, 10, 30) == 0x600000u);  // deep red
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
    // The Zodiac: one round more at each of Heavy, Mega and Ultra, then a flat 4 at each tier.
    for (int level = 0; level <= 3; ++level) {
        CHECK(ammo_max(Kind::Single, level, 4) == 4 + level);
    }
    for (int level = 4; level <= 11; ++level) {
        CHECK(ammo_max(Kind::Single, level, 4) == 4 + 4 * (level - 3));
    }
    CHECK(ammo_max(Kind::Single, 11, 4) == 36);
    CHECK(ammo_max(Kind::Single, 3, 4) < ammo_max(Kind::Single, 4, 4));
    CHECK(ammo_max(Kind::Single, 3, 0) == 0);
    CHECK(ammo_max(Kind::Sheep, 3, 0) == 0);  // endless stays endless
    CHECK(ammo_max(Kind::Four, 8, 500) == 999);  // the HUD's three digits
    CHECK(ammo_max(Kind::Four, 8, 60'000) == 60'000);
    CHECK(!tint_rgb(Kind::Four, 0, 0));
    CHECK(tint_rgb(Kind::Four, 1, 0) == 0x20FFB0u);
    CHECK(tint_rgb(Kind::Four, 2, 0) == 0xFFE000u);
    // Omega moves; Alpha does not.
    CHECK(tint_rgb(Kind::Four, 8, 0) != tint_rgb(Kind::Four, 8, 100));
    CHECK(tint_rgb(Kind::Four, 1, 0) == tint_rgb(Kind::Four, 1, 100));
    CHECK(tint_rgb(Kind::Ryno, 10, 0) != tint_rgb(Kind::Ryno, 10, 30));
    // The effects' colours: the tint's hue at full strength, 0xRRGGBB; Ultima's red beats from
    // bright to deep, never to black; Omega's rainbow moves.
    CHECK(!effect_rgb(Kind::Four, 0, 0));
    CHECK(effect_rgb(Kind::Four, 1, 0) == 0xB0FF20u);  // lime
    CHECK(effect_rgb(Kind::Four, 2, 0) == 0x00E0FFu);  // cyan
    CHECK(effect_rgb(Kind::Four, 3, 0) == 0x1AFF80u);  // emerald, raised
    CHECK(effect_rgb(Kind::Four, 4, 0) == 0x00FFFFu);  // teal, raised
    CHECK(effect_rgb(Kind::Four, 8, 0) != effect_rgb(Kind::Four, 8, 100));
    CHECK(effect_rgb(Kind::Ryno, 1, 0) == 0xFF8000u);  // Heavy's orange
    CHECK(effect_rgb(Kind::Ryno, 2, 0) == 0xB0FF20u);  // Alpha's lime
    CHECK(effect_rgb(Kind::Ryno, 10, 0) == 0xFF1A1Au);  // Ultima, bright
    CHECK(effect_rgb(Kind::Ryno, 10, 30) == 0x600000u);  // Ultima, deep
    // Stronger with each tier, the top at the last.
    CHECK(effect_strength(Kind::Four, 0) == 0);
    CHECK(effect_strength(Kind::Four, 1) == kEffectFirst);
    CHECK(effect_strength(Kind::Four, 8) == kEffectTop);
    CHECK(effect_strength(Kind::Ryno, 1) == kEffectFirst);
    CHECK(effect_strength(Kind::Ryno, 9) == 14);
    CHECK(effect_strength(Kind::Ryno, 10) == kEffectTop);  // Ultima
    CHECK(effect_strength(Kind::None, 3) == 0);

    for (int level = 1; level < kTiers; ++level) {
        CHECK(effect_strength(Kind::Four, level + 1) > effect_strength(Kind::Four, level));
    }

    CHECK(tint_moves(Kind::Four, 8) && tint_moves(Kind::Ryno, 10) && !tint_moves(Kind::Ryno, 2));
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

    // Every run and every version: the game's, Heavy, the eight tiers, Ultima.
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
        const float ryno = damage(Kind::Ryno, 9) / hit_divisor(Kind::Ryno, 9, cycle, 1.0f, common);
        const float other = damage(Kind::Four, 8) / hit_divisor(Kind::Four, 8, cycle, 1.0f, common);
        CHECK(ryno >= 8.0f);
        CHECK(ryno >= 4.0f * other);
    }
}

// Returns a clock of the test's own, its first byte `n`.
Clock clock_of(std::uint8_t n) {
    Clock clock{};
    clock[0] = n;
    clock[7] = 0x11;
    return clock;
}

// Checks the book's move to the RYNO II's path now: old Heavy stays, old Ultima becomes Alpha, old
// Alpha to Omega keep their tier, old Omega Prime becomes Ultima; the started bit, the other items
// and the records' order stay; and a book with the marker is never moved again.
void test_ryno_migration() {
    // Every old level, with and without the started bit.
    constexpr int kOldToNew[] = {0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    for (int old = 0; old <= 11; ++old) {
        const auto bare = static_cast<std::uint8_t>(old);
        CHECK(ryno_level_from_old(bare) == kOldToNew[old]);
        CHECK(ryno_level_from_old(static_cast<std::uint8_t>(bare | kStartedBit))
              == (kOldToNew[old] | kStartedBit));
    }

    // The names: old Ultima (2) is Alpha now, old Alpha (3) too, old Omega (10) Omega, old Omega
    // Prime (11) Ultima.
    CHECK(display_name("RYNO II", Kind::Ryno, ryno_level_from_old(2)) == "Alpha RYNO II");
    CHECK(display_name("RYNO II", Kind::Ryno, ryno_level_from_old(3)) == "Alpha RYNO II");
    CHECK(display_name("RYNO II", Kind::Ryno, ryno_level_from_old(10)) == "Omega RYNO II");
    CHECK(display_name("RYNO II", Kind::Ryno, ryno_level_from_old(11)) == "Ultima RYNO II");

    // A book of the layout before: no marker.
    SaveBook book;
    LevelBytes first{};
    first[kRyno] = static_cast<std::uint8_t>(2 | kStartedBit);  // Ultima, counting
    first[kLancer] = 5;
    LevelBytes second{};
    second[kRyno] = 11;  // Omega Prime
    LevelBytes third{};
    third[kRyno] = 1;  // Heavy
    book.put(clock_of(1), first);
    book.put(clock_of(2), second);
    book.put(clock_of(3), third);
    const std::string old_text = book.write();
    CHECK(!book_current(old_text));

    SaveBook read;
    read.read(old_text);
    migrate_book(read);
    CHECK(read.records().size() == 3);
    CHECK(read.records()[0].clock == clock_of(1));  // the order stays
    CHECK(read.find(clock_of(1))->levels[kRyno] == (2 | kStartedBit));  // Alpha
    CHECK(read.find(clock_of(1))->levels[kLancer] == 5);                // others untouched
    CHECK(read.find(clock_of(2))->levels[kRyno] == 10);                 // Ultima
    CHECK(read.find(clock_of(3))->levels[kRyno] == 1);                  // Heavy

    // Written with the marker: read back as it is, never moved again.
    const std::string text = book_text(read);
    CHECK(book_current(text));
    CHECK(text.rfind(std::string(kBookMarker) + "\n", 0) == 0);
    SaveBook again;
    CHECK(again.read(text) == 3);
    CHECK(again.find(clock_of(2))->levels[kRyno] == 10);
    CHECK(again.find(clock_of(1))->levels[kRyno] == (2 | kStartedBit));

    // Layout 1 holds the Zodiac in layout 2's numbering: moved to layout 3's as well.
    CHECK(book_layout(old_text) == 1);

    // The marker wherever the launcher's editor left it, and with a Windows line end.
    CHECK(book_current(old_text + std::string(kBookMarker) + "\r\n"));
    CHECK(!book_current("ngpp-tiers 1\n"));
    CHECK(!book_current(""));
}

// Checks the move of a book of layout 2 to the Zodiac's path now: none stays none (its XP counts
// toward Heavy), a Zodiac at Alpha or above keeps its tier (Alpha 1 to 8 become 4 to 11), the started
// bit, the other items (the RYNO II too) and the order stay, and a book with the marker is not moved.
void test_zodiac_migration() {
    constexpr std::size_t kZodiac = 43;

    for (int old = 0; old <= 8; ++old) {
        const auto bare = static_cast<std::uint8_t>(old);
        const int want = old == 0 ? 0 : old + 3;
        CHECK(zodiac_level_from_old(bare) == want);
        CHECK(zodiac_level_from_old(static_cast<std::uint8_t>(bare | kStartedBit))
              == (want | kStartedBit));

        // The same tier by name: Alpha stays Alpha, Omega Omega.
        if (old > 0) {
            const std::string was = step_at(Kind::Two, old - 1)->name;
            CHECK(display_name("Zodiac", Kind::Single, zodiac_level_from_old(bare))
                  == was + " Zodiac");
        }
    }

    SaveBook book;
    LevelBytes first{};
    first[kZodiac] = static_cast<std::uint8_t>(kStartedBit);  // base, counting its XP
    first[kLancer] = 5;
    first[kRyno] = 7;
    LevelBytes second{};
    second[kZodiac] = 1;  // Alpha
    LevelBytes third{};
    third[kZodiac] = static_cast<std::uint8_t>(8 | kStartedBit);  // Omega
    book.put(clock_of(1), first);
    book.put(clock_of(2), second);
    book.put(clock_of(3), third);
    const std::string old_text = std::string(kBookMarkerV2) + "\n" + book.write();
    CHECK(book_layout(old_text) == 2);
    CHECK(!book_current(old_text));

    SaveBook read;
    read.read(old_text);
    migrate_book(read, 2);
    CHECK(read.records().size() == 3);
    CHECK(read.records()[0].clock == clock_of(1));
    CHECK(read.find(clock_of(1))->levels[kZodiac] == kStartedBit);   // base stays base
    CHECK(read.find(clock_of(1))->levels[kLancer] == 5);
    CHECK(read.find(clock_of(1))->levels[kRyno] == 7);               // layout 2's RYNO II stays
    CHECK(read.find(clock_of(2))->levels[kZodiac] == 4);             // Alpha
    CHECK(read.find(clock_of(3))->levels[kZodiac] == (11 | kStartedBit));  // Omega

    const std::string text = book_text(read);
    CHECK(book_current(text));
    CHECK(book_layout(text) == kBookLayout);
    CHECK(text.find(std::string(kBookMarkerV2)) == std::string::npos);
    SaveBook again;
    CHECK(again.read(text) == 3);
    CHECK(again.find(clock_of(2))->levels[kZodiac] == 4);
}

// Checks a kill's weapon experience: the game's up to cycle 1, then the run's x1.5, x2.5, x3.5 and
// x4.5, times the softened toughness of a bigger enemy, a boss's x5 or the final boss's x10; and
// that levelling the tier of each run by experience takes about as long as Beta did in the first
// challenge mode (its goal over Beta's, times the hits a kill takes, over the experience).
void test_kill_experience() {
    const Foe small = foe_of(false, false, 8);
    const Foe common = foe_of(false, false, 45);
    const Foe big = foe_of(false, false, 320);
    const Foe huge = foe_of(false, false, 5000);
    const Foe boss = foe_of(true, false, 0);
    const Foe last = foe_of(true, true, 0);
    CHECK(kill_experience(0, last) == 1.0f);
    CHECK(kill_experience(1, big) == 1.0f);
    CHECK(kill_experience(2, small) == 1.5f);
    CHECK(kill_experience(3, small) == 2.5f);
    CHECK(kill_experience(4, small) == 3.5f);
    CHECK(kill_experience(5, small) == 4.5f);
    CHECK(kill_experience(20, small) == 4.5f);
    CHECK(std::fabs(kill_experience(2, big) - 3.0f) < 0.001f);  // 320 bolts: x2
    CHECK(std::fabs(kill_experience(2, common) - 1.5f * std::sqrt(1.5f)) < 0.001f);
    CHECK(kill_experience(2, huge) <= 1.5f * 2.45f);
    CHECK(kill_experience(2, boss) == 7.5f);
    CHECK(kill_experience(5, last) == 45.0f);

    // The tier each run levels by experience (cycle 2 Delta, 3 Zeta, then Omega), and its goal.
    const float beta = static_cast<float>(step_at(Kind::Four, 1)->exp);

    // Every run.
    for (int cycle = 2; cycle < 40; ++cycle) {
        const int level = cycle == 2 ? 3 : (cycle == 3 ? 5 : 7);
        const float goal = static_cast<float>(step_at(Kind::Four, level)->exp) / beta;
        const float hits = enemy_health(cycle, 1.0f) / damage(Kind::Four, matching_level(cycle));

        // Small fry and common enemies: within a quarter or so of the first challenge mode's pace.
        for (const Foe& foe : {small, common}) {
            const float pace = goal * hits / kill_experience(cycle, foe);
            CHECK(pace >= 0.75f);
            CHECK(pace <= 1.3f);
        }
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

// Checks the save in play loaded again with its clock unchanged (is_reload): only a tick that takes
// the data back to the save's, with levels no longer the save's, is one.
void test_reload() {
    SavePrint saved;
    saved.bolts = 4'000'000;
    saved.raritanium = 200;
    saved.exp[kLancer] = 77 * kExpUnit;
    saved.row[kLancer] = 80;
    LevelBytes saved_levels{};
    saved_levels[kLancer] = static_cast<std::uint8_t>(2u | kStartedBit);

    // A tier bought: its price gone, its experience started again, a level up.
    SavePrint bought = saved;
    bought.bolts -= 2'250'000;
    bought.exp[kLancer] = 0;
    LevelBytes live = saved_levels;
    live[kLancer] = static_cast<std::uint8_t>(3u | kStartedBit);

    // The save loaded again: its data back in one tick.
    CHECK(is_reload(bought, saved, saved, live, saved_levels));
    // Nothing changed in the tick: not a load.
    CHECK(!is_reload(saved, saved, saved, live, saved_levels));
    // The levels still the save's: nothing to take back.
    CHECK(!is_reload(bought, saved, saved, saved_levels, saved_levels));
    // The data not the save's to the last bolt: not it.
    SavePrint near = saved;
    near.bolts += 1;
    CHECK(!is_reload(bought, near, saved, live, saved_levels));
    // The ship's tier, bought for raritanium alone: the raritanium tells.
    SavePrint ship = saved;
    ship.raritanium -= 80;
    CHECK(ship != saved);
    CHECK(is_reload(ship, saved, saved, live, saved_levels));
    // A started mark alone counts as a change of the levels.
    LevelBytes started = saved_levels;
    started[kRyno] = kStartedBit;
    CHECK(is_reload(bought, saved, saved, started, saved_levels));
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

// Checks the per-planet table: only its classes, each in its own level, at most its entry's copies.
void test_copy_table() {
    CHECK(copy_entry(4, 4022) != nullptr);
    CHECK(copy_entry(1, 4022) == nullptr);
    CHECK(copy_entry(4, 2000) == nullptr);
    CHECK(copies_of(4, 4022, 1) == 0);
    CHECK(copies_of(4, 4022, 2) == 1);
    CHECK(copies_of(4, 4022, 4) == 2);
    CHECK(copies_of(4, 2000, 4) == 0);

    // A big one at most once, whatever the run.
    const CopyEntry* beast = copy_entry(11, 3335);
    CHECK(beast != nullptr && beast->most == 1 && copies_of(11, 3335, 6) == 1);

    // Left out after the test runs: Endako's respawning Sweeper Bots, Siberius's enemies that die
    // twice, Todano's guards with a part of their own, Tabora's Dune Crawlers and Sandflies (never
    // woke where they were tried), the Hover Tank.
    for (const auto& [level, oclass] : {std::pair{3, 4083}, std::pair{7, 3357}, std::pair{12, 3889},
                                        std::pair{8, 2502}, std::pair{8, 4095}, std::pair{20, 3260}}) {
        CHECK(copy_entry(level, oclass) == nullptr);
    }

    // Each entry is a level's class once, and names what it is.
    for (int level = 0; level <= 30; ++level) {
        for (int oclass = 2000; oclass < 5000; ++oclass) {
            const CopyEntry* entry = copy_entry(level, oclass);
            CHECK(entry == nullptr || (entry->most >= 1 && entry->most <= 2 && entry->what != nullptr));
        }
    }

    // Never a boss, a spawner, a crystal or moonstone creature, nor a Y.E.T.I.
    for (const int never : {3098, 3104, 2410, 4098, 3927, 4551, 4304, 2644, 3210, 3679, 4576}) {
        for (int level = 0; level <= 30; ++level) {
            CHECK(copy_entry(level, never) == nullptr);
        }
    }
}

// Each disc's addresses and texts as checked against its own code: PAL's vendor state is v1.01's,
// v2.00's moved; the functions by the port build's names; the texts' ids differ per disc.
void test_discs() {
    using namespace ng_plus_plus;
    const Disc* ntsc = disc_of("ntsc");
    const Disc* pal = disc_of("pal");
    const Disc* ntsc2 = disc_of("ntsc2");
    CHECK(ntsc != nullptr && pal != nullptr && ntsc2 != nullptr && disc_of("jp") == nullptr);
    CHECK(pal->vendor == 0x0021F240u && ntsc2->vendor == 0x0021F1C0u);
    CHECK(std::string(pal->banner) == "func_00289730" && std::string(pal->message) == "func_002B1588");
    CHECK(std::string(ntsc2->banner) == "func_00289840");
    CHECK(std::string(ntsc2->message) == "func_002B1880");
    CHECK(ntsc->upgrade_label == 0x325F && pal->upgrade_label == 0x1B2D);
    CHECK(ntsc2->upgrade_label == 0x30F5);
    CHECK(ntsc->crystal_trade == 10135 && pal->crystal_trade == 4841);
    CHECK(ntsc2->moonstone_trade == 4915);
    CHECK(ntsc->tick_rate == 60 && pal->tick_rate == 50 && ntsc2->tick_rate == 60);
}

// NG++'s times, in sixtieths, in a disc's ticks: PAL's fiftieths.
void test_ticks() {
    using namespace ng_plus_plus;
    CHECK(ticks_at(60, 600) == 600 && ticks_at(50, 600) == 500);
    CHECK(ticks_at(50, 15) == 13 && ticks_at(50, 6) == 5 && ticks_at(50, 1) == 1);
    CHECK(ticks_at(50, 0) == 0);
    CHECK(sixtieths_at(50, 50) == 60u && sixtieths_at(60, 77) == 77u);
    CHECK(std::fabs(per_tick_at(50, 0.6f) - 0.72f) < 1e-6f && per_tick_at(60, 0.6f) == 0.6f);
}

// Checks the carry across a clock the book never saw: the closest earlier record, never a later one.
void test_carry() {
    SaveBook book;
    const Clock early = {0, 10, 5, 3, 0, 20, 11, 0x26};
    const Clock mid = {0, 30, 5, 3, 0, 21, 11, 0x26};
    const Clock late = {0, 40, 5, 3, 0, 22, 11, 0x26};
    const Clock other_slot_later = {0, 59, 9, 3, 0, 25, 11, 0x26};
    const Clock moved = {0, 1, 1, 4, 0, 23, 11, 0x26};
    LevelBytes first{};
    LevelBytes second{};
    SavePrint now;
    now.exp[29] = 5 * kExpUnit;

    first[29] = 0x81;
    second[29] = 0x83;
    book.put(early, first);
    book.put(mid, second);
    book.put(other_slot_later, first);

    // Another slot's save, whose tiered weapon (item 29) has no experience: nothing carried, though
    // the clock is later than the records'.
    SavePrint other = now;
    other.exp = {};
    other.exp[3] = kExpUnit;
    CHECK(carry_record(book, moved, other) == nullptr);

    CHECK(clock_earlier(early, mid) && !clock_earlier(mid, early) && !clock_earlier(mid, mid));

    // The closest earlier record is taken: mid, not early nor the later slot's.
    const Record* from = carry_record(book, moved, now);
    CHECK(from != nullptr && from->clock == mid && from->levels[29] == 0x83);

    // Nothing earlier: a later record is another slot's or a newer save, never taken.
    SaveBook only_later;
    only_later.put(other_slot_later, first);
    CHECK(carry_record(only_later, moved, now) == nullptr);
    CHECK(carry_record(only_later, late, now) == nullptr);

    // A clock the book has, zeros and a new game (no experience): nothing carried.
    CHECK(carry_record(book, mid, now) == nullptr);
    CHECK(carry_record(book, Clock{}, now) == nullptr);
    CHECK(carry_record(book, moved, SavePrint{}) == nullptr);

    // Records with no level are skipped.
    SaveBook empty_between = book;
    empty_between.put(late, LevelBytes{});
    from = carry_record(empty_between, moved, now);
    CHECK(from != nullptr && from->clock == mid);

    // Recording under the new clock leaves every other entry as it was.
    const std::size_t before = book.records().size();
    book.put(moved, from->levels);
    CHECK(book.records().size() == before + 1);
    CHECK(book.find(early) != nullptr && book.find(mid) != nullptr);
    CHECK(book.find(moved) != nullptr && book.find(moved)->levels[29] == 0x83);
}

int main() try {
    test_carry();
    test_discs();
    test_ticks();
    test_kinds();
    test_copy_table();
    test_buy_then_level();
    test_mastery();
    test_zodiac_as_ryno();
    test_zodiac_migration();
    test_no_experience();
    test_naming();
    test_sheepinator();
    test_prices();
    test_prices_rise();
    test_ryno();
    test_bar_goals();
    test_ryno_colours();
    test_ryno_migration();
    test_scaling();
    test_enemies();
    test_bosses();
    test_ryno_fights();
    test_kill_experience();
    test_book();
    test_book_keeps();
    test_saves();
    test_reload();
    test_weapon_looks();
    return ng_plus_plus::test::result("ng_plus_plus_rules");
} catch (...) {
    // A test that throws has failed.
    return 1;
}
