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

#include <string>

#include "check.h"
#include "save_book.h"
#include "tiers.h"

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
    CHECK(display_name("Lancer", Kind::Four, 1) == "Alpha Lancer");
    CHECK(display_name("Lancer", Kind::Four, 0) == "Lancer");
}

// Checks the enemies' curve: the README's table, its strength and that it runs ahead of the tiers.
void test_enemies() {
    CHECK(enemy_health(0, 1.0f) == 1.0f);
    CHECK(enemy_health(1, 1.0f) == 1.0f);
    CHECK(enemy_health(2, 1.0f) == 2.5f);
    CHECK(enemy_health(3, 1.0f) == 4.0f);
    CHECK(enemy_health(4, 1.0f) == 5.5f);
    CHECK(enemy_health(5, 1.0f) == 6.0f);
    CHECK(enemy_health(6, 1.0f) == 6.5f);
    CHECK(enemy_health(7, 1.0f) == 6.75f);
    CHECK(enemy_health(20, 1.0f) == 6.75f);
    CHECK(enemy_damage(1, 1.0f) == 1.0f);
    CHECK(enemy_damage(2, 1.0f) == 1.375f);
    CHECK(enemy_damage(5, 1.0f) == 2.5f);
    CHECK(enemy_damage(7, 1.0f) == 3.25f);
    CHECK(enemy_damage(20, 1.0f) == 4.0f);
    CHECK(enemy_health(2, 0.5f) == 1.75f);
    CHECK(enemy_health(9, 0.0f) == 1.0f);
    // Half as fast again as 0.3's curve (x2, x3, x4, x5 from cycle 2).
    CHECK(enemy_health(2, 1.0f) - 1.0f == 1.5f * (2.0f - 1.0f));
    CHECK(enemy_health(3, 1.0f) - 1.0f == 1.5f * (3.0f - 1.0f));
    CHECK(enemy_health(4, 1.0f) - 1.0f == 1.5f * (4.0f - 1.0f));
    CHECK(enemy_damage(2, 1.0f) - 1.0f == 1.5f * (1.25f - 1.0f));
    CHECK(enemy_damage(5, 1.0f) - 1.0f == 1.5f * (2.0f - 1.0f));
    // Just behind the tiers: from cycle 2, a weapon at the tier a player can have by then (cycle 2
    // Delta, 3 Zeta, 4 Omega) still kills faster than V4 at cycle 1, by an eighth at the least.
    CHECK(damage(Kind::Four, 4) / enemy_health(2, 1.0f) >= 1.125f);
    CHECK(damage(Kind::Four, 6) / enemy_health(3, 1.0f) >= 1.125f);
    CHECK(damage(Kind::Four, 8) / enemy_health(4, 1.0f) >= 1.125f);
    // A fully upgraded weapon (Omega) out-damages the enemies' health in every run, and so does
    // every weapon kind's Omega.
    for (int cycle = 0; cycle < 40; ++cycle) {
        CHECK(damage(Kind::Four, 8) > enemy_health(cycle, 1.0f));
        CHECK(damage(Kind::Two, 8) > enemy_health(cycle, 1.0f));
        CHECK(damage(Kind::Single, 8) > enemy_health(cycle, 1.0f));
        CHECK(damage(Kind::Ryno, 10) > enemy_health(cycle, 1.0f));
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
    test_book();
    test_book_keeps();
    test_saves();
    return ng_plus_plus::test::result("ng_plus_plus_rules");
} catch (...) {
    // A test that throws has failed.
    return 1;
}
