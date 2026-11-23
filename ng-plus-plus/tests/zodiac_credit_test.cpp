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

// 0.6.11: a Zodiac field's kill. The field's hits are posted later from a copy of the last request,
// which names the weapon in hand (the Pulse Rifle here); each pulse's post marks the victim for the
// Zodiac, so the kill on any pulse, long after the gun's own pulse window, is the Zodiac's.
void test_field_kill() {
    ZodiacCredit credit;
    constexpr std::uint32_t kPulse = 15;  // kFieldPulseTicks.

    // A field pulsing for 3 s (180 ticks): the mark follows each pulse.
    for (std::uint32_t tick = 1000; tick < 1180; tick += kPulse) {
        credit.mark(kEnemy, tick, kZodiac, 0);
        credit.prune(tick);
    }

    std::uint8_t weapon = kPulseRifle;
    CHECK(credit_zodiac(credit, kEnemy, 1180 - kPulse + 2, weapon));
    CHECK((weapon) == (kZodiac));
}

// An explosion's kill: another weapon's blast posted later is credited to that weapon, not the one
// in hand, and the Zodiac's mark on the same enemy is replaced by it.
void test_explosion_kill() {
    ZodiacCredit credit;
    constexpr int kDecoy = 17;
    std::uint8_t weapon = kZodiac;  // The weapon in hand when the blast went off.
    credit.mark(kEnemy, 300, kDecoy, 0);
    CHECK(credit_zodiac(credit, kEnemy, 301, weapon));
    CHECK((weapon) == (kDecoy));

    // A Zodiac mark, then a blast's: the later names the blast's weapon.
    weapon = kPulseRifle;
    credit.mark(kEnemy, 400, kZodiac, 0);
    credit.mark(kEnemy, 402, kDecoy, 0);
    CHECK(credit_zodiac(credit, kEnemy, 403, weapon));
    CHECK((weapon) == (kDecoy));
}

// An expired mark credits nothing, whatever the item, and a longer stated mark stands longer.
void test_expired_mark() {
    ZodiacCredit credit;
    std::uint8_t weapon = kPulseRifle;
    credit.mark(kEnemy, 10, kZodiac, 0);
    CHECK(!credit_zodiac(credit, kEnemy, 10 + kZodiacCreditTicks + 1, weapon));
    CHECK((weapon) == (kPulseRifle));

    credit.mark(kEnemy, 10, 17, 0);
    CHECK(!credit_zodiac(credit, kEnemy, 10 + kZodiacCreditTicks + 1, weapon));
    CHECK((weapon) == (kPulseRifle));

    credit.mark(kEnemy, 10, kZodiac, 100);
    CHECK(credit.marked(kEnemy, 110));
    CHECK(!credit.marked(kEnemy, 111));
    CHECK(credit_zodiac(credit, kEnemy, 60, weapon));
    CHECK((weapon) == (kZodiac));
}

}  // namespace

int main() {
    test_field_kill();
    test_explosion_kill();
    test_expired_mark();
    test_pulse_kill();
    test_reach_kill();
    test_other_weapon_unchanged();
    test_once_and_prune();
    return ng_plus_plus::test::result("ng_plus_plus_zodiac_credit");
}
