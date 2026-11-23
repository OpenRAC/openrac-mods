// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.6.4's Zodiac kill credit (src/zodiac_credit.h): the pulse's and the reach's kills name the
// Zodiac in the health record's last-weapon byte, so the game's own award gives it the experience;
// another weapon's kill keeps its weapon.

#include <cstdint>

#include "check.h"
#include "tuning_rules.h"
#include "zodiac_credit.h"

namespace {

using namespace ng_plus_plus;

constexpr std::uint32_t kEnemy = 0x01234500u;
constexpr std::uint8_t kPulseRifle = 23;

// A pulse kill: the pulse posts no hit, so the byte still names the weapon that hit the enemy
// before (or none, 255); the pulse's mark makes it the Zodiac's.
void test_pulse_kill() {
    ZodiacCredit credit;
    std::uint8_t weapon = kPulseRifle;
    credit.mark(kEnemy, 100);
    CHECK(credit_zodiac(credit, kEnemy, 101, weapon));
    CHECK((weapon) == (kZodiac));

    std::uint8_t none = 255;
    credit.mark(kEnemy, 200);
    CHECK(credit_zodiac(credit, kEnemy, 200, none));
    CHECK((none) == (kZodiac));
}

// A reach kill: the copied hit named an earlier weapon, the mark made before it posts it wins.
void test_reach_kill() {
    ZodiacCredit credit;
    std::uint8_t weapon = 7;  // The weapon the copied hit was of.
    credit.mark(kEnemy, 50);
    weapon = 7;  // The post sets it again.
    CHECK(credit_zodiac(credit, kEnemy, 50 + kZodiacCreditTicks, weapon));
    CHECK((weapon) == (kZodiac));
}

// Another weapon's kill: not marked, or marked too long ago, keeps its weapon.
void test_other_weapon_unchanged() {
    ZodiacCredit credit;
    std::uint8_t weapon = kPulseRifle;
    CHECK(!credit_zodiac(credit, kEnemy, 10, weapon));
    CHECK((weapon) == (kPulseRifle));

    // Another enemy's mark is not this one's.
    credit.mark(kEnemy + 0x100, 10);
    CHECK(!credit_zodiac(credit, kEnemy, 10, weapon));
    CHECK((weapon) == (kPulseRifle));

    // A mark that ran out.
    credit.mark(kEnemy, 10);
    CHECK(!credit_zodiac(credit, kEnemy, 10 + kZodiacCreditTicks + 1, weapon));
    CHECK((weapon) == (kPulseRifle));
}

// Credited once: the mark is spent, a second death record of the same enemy is not credited, and
// pruning forgets the stale marks.
void test_once_and_prune() {
    ZodiacCredit credit;
    std::uint8_t weapon = kPulseRifle;
    credit.mark(kEnemy, 5);
    CHECK(credit_zodiac(credit, kEnemy, 6, weapon));
    weapon = kPulseRifle;
    CHECK(!credit_zodiac(credit, kEnemy, 7, weapon));
    CHECK((weapon) == (kPulseRifle));

    credit.mark(kEnemy, 5);
    credit.mark(kEnemy + 4, 500);
    credit.prune(500);
    CHECK(credit.size() == 1u);
}

}  // namespace

int main() {
    test_pulse_kill();
    test_reach_kill();
    test_other_weapon_unchanged();
    test_once_and_prune();
    return ng_plus_plus::test::result("ng_plus_plus_zodiac_credit");
}
