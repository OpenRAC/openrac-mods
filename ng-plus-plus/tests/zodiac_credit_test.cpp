// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.6.4's Zodiac kill credit (src/zodiac_credit.h): the pulse's and the reach's kills name the
// Zodiac in the health record's last-weapon byte, so the game's own award gives it the experience;
// another weapon's kill keeps its weapon.

#include <cstdint>
#include <string>

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

// 0.6.12: NG++'s reach takes an enemy the gun's way, by its death path byte (+0x0E), and posts no
// hit: 100 health or less dies, vaporized for 0 (or any byte but 1 to 3), left to its owner for 1
// or 2, never for 3; more loses a pulse's drain, and one the drain takes to nothing dies the same
// way instead of by its own death, which an earlier weapon's copied hit used to set off.
void test_reach_action() {
    constexpr float kDrain = 600.0f;
    CHECK(zodiac_action(100.0f, 0, kDrain) == ZodiacAction::Vaporize);
    CHECK(zodiac_action(1.0f, 0, kDrain) == ZodiacAction::Vaporize);
    CHECK(zodiac_action(50.0f, 7, kDrain) == ZodiacAction::Vaporize);
    CHECK(zodiac_action(50.0f, 1, kDrain) == ZodiacAction::Owner);
    CHECK(zodiac_action(50.0f, 2, kDrain) == ZodiacAction::Owner);
    CHECK(zodiac_action(50.0f, 3, kDrain) == ZodiacAction::Skip);

    // Over the line: drained, alive.
    CHECK(zodiac_action(700.5f, 0, kDrain) == ZodiacAction::Drain);
    CHECK(zodiac_action(5000.0f, 3, kDrain) == ZodiacAction::Drain);

    // Drained to nothing: the gun's kill, by the same byte.
    CHECK(zodiac_action(600.0f, 0, kDrain) == ZodiacAction::Vaporize);
    CHECK(zodiac_action(400.0f, 1, kDrain) == ZodiacAction::Owner);
    CHECK(zodiac_action(400.0f, 3, kDrain) == ZodiacAction::Skip);
}

// A reach kill is credited as its death is recorded: the mark is made right before the reach
// records the death itself, whatever the byte named before (here an earlier weapon).
void test_reach_kill_recorded_at_once() {
    ZodiacCredit credit;
    std::uint8_t weapon = kPulseRifle;
    credit.mark(kEnemy, 900, kZodiac, 0);
    CHECK(credit.marked_for(kEnemy, 900, kZodiac));
    CHECK(!credit.marked_for(kEnemy, 900, 17));
    CHECK(credit_zodiac(credit, kEnemy, 900, weapon));
    CHECK((weapon) == (kZodiac));
}

// A death near a pulse, as WeaponTuning tells it apart: the gun records its kill with health left
// (path 0 vaporized, 1 or 2 its owner's explosion); an enemy with none died its own death.
void test_death_paths() {
    CHECK(zodiac_death(80.0f, 0) == ZodiacDeath::Vaporized);
    CHECK(zodiac_death(80.0f, 9) == ZodiacDeath::Vaporized);
    CHECK(zodiac_death(80.0f, 1) == ZodiacDeath::Owner);
    CHECK(zodiac_death(80.0f, 2) == ZodiacDeath::Owner);
    CHECK(zodiac_death(0.0f, 0) == ZodiacDeath::Own);
    CHECK(zodiac_death(-3.0f, 2) == ZodiacDeath::Own);
}

// An enemy the pulse drained to nothing dies its own death later: its longer mark still credits
// the Zodiac seconds on, which the 20 ticks of a kill's did not.
void test_drained_own_death() {
    ZodiacCredit credit;
    std::uint8_t weapon = 255;
    credit.mark(kEnemy, 2000, kZodiac, kZodiacDrainCreditTicks);
    CHECK(!credit_zodiac(credit, kEnemy, 2000 + kZodiacDrainCreditTicks + 1, weapon));

    credit.mark(kEnemy, 2000, kZodiac, kZodiacDrainCreditTicks);
    CHECK(credit_zodiac(credit, kEnemy, 2000 + 4 * kZodiacCreditTicks, weapon));
    CHECK((weapon) == (kZodiac));
}

// The summary line: every count, the other weapon named, and the step's note.
void test_summary() {
    ZodiacStats stats;
    CHECK(!stats.any());
    stats.pulses = 3;
    stats.vaporized = 4;
    stats.owner = 2;
    stats.reach_vaporized = 5;
    stats.credited = 11;
    stats.not_credited = 1;
    stats.last_other = 23;
    stats.experience = 32 * 70;
    stats.awards = 11;
    CHECK(stats.any());

    const std::string line = zodiac_summary(stats, "level 2, experience 10 of 6000");
    CHECK(line.find("3 pulses") != std::string::npos);
    CHECK(line.find("4 vaporized") != std::string::npos);
    CHECK(line.find("2 by their owner's explosion") != std::string::npos);
    CHECK(line.find("reach 5 vaporized") != std::string::npos);
    CHECK(line.find("11 deaths credited to item 43") != std::string::npos);
    CHECK(line.find("last named 23") != std::string::npos);
    CHECK(line.find("experience +70 in 11 awards") != std::string::npos);
    CHECK(line.find("next step: level 2, experience 10 of 6000") != std::string::npos);
}

}  // namespace

int main() {
    test_reach_action();
    test_reach_kill_recorded_at_once();
    test_death_paths();
    test_drained_own_death();
    test_summary();
    test_field_kill();
    test_explosion_kill();
    test_expired_mark();
    test_pulse_kill();
    test_reach_kill();
    test_other_weapon_unchanged();
    test_once_and_prune();
    return ng_plus_plus::test::result("ng_plus_plus_zodiac_credit");
}
