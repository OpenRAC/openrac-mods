// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.5.1's weapon tuning rules (tuning_rules.h). The tables below are the mod's balance:
// change them here and in README.md's tables.

#include "tuning_rules.h"

namespace ng_plus_plus {
namespace {

// A boss's vulnerability to the Zodiac by class: the fights' own mobys (tiers.h's kBossClasses).
struct ZodiacBoss {
    int moby_class;
    float vulnerability;
};

// clang-format off: one boss per row.
constexpr ZodiacBoss kZodiacBosses[] = {
    {2644, 4.0f},  // Oozla: the Swamp Monster II
    {2701, 4.0f},  // its arms
    {2702, 4.0f},  // its eye
    {2552, 4.0f},  // Maktar arena: Chainblade
    {2628, 4.0f},  // Maktar arena: the B2 Brawler
    {3210, 4.0f},  // Endako: the boss of the Megacorp building
    {3679, 4.0f},  // Siberius: the boss
    {3973, 4.0f},  // Hrugis Cloud: the boss
    {2874, 4.0f},  // Joba arena
    {3402, 4.0f},  // Joba arena
    {4785, 5.0f},  // Todano: the flying boss (seldom in the pulse's reach)
    {3961, 4.0f},  // Snivelak: the boss
    {3145, 4.0f},  // the Thugs-4-Less leader
    {3806, 4.0f},  // Damosel: the leader's mech
    {3967, 5.0f},  // Damosel orbit: the mothership's parts
    {4623, 5.0f},  // Damosel orbit: the mothership's parts
    {4576, 3.0f},  // Yeedil: the final boss
};
// clang-format on

// A fifth more than the tier's damage factor, for the decoy's health.
constexpr float kDecoyTierGain = 1.2f;

}  // namespace

float tesla_gain(float game_frames) {
    // Steady: kTeslaShare every kTeslaHitTicks; the game: one hit of 1 every game_frames.
    return kTeslaShare * game_frames / static_cast<float>(kTeslaHitTicks);
}

float zodiac_reach(int tier) {
    const float reach = kZodiacReachBase + kZodiacReachTier * static_cast<float>(tier > 0 ? tier : 0);
    return reach < kZodiacReachMost ? reach : kZodiacReachMost;
}

float zodiac_vulnerability(int moby_class) {
    // The table is short.
    for (const ZodiacBoss& boss : kZodiacBosses) {
        if (boss.moby_class == moby_class) {
            return boss.vulnerability;
        }
    }

    return 1.0f;
}

float zodiac_boss_drain(int moby_class, int level, int cycle, bool scaling, float strength) {
    const BossClass* boss = boss_of(moby_class);

    // Not a boss: the game's drain alone.
    if (boss == nullptr) {
        return kZodiacDrain;
    }

    Foe foe;
    foe.boss = true;
    foe.last = boss->last;
    const float divisor = scaling ? hit_divisor(Kind::Single, level, cycle, strength, foe) : 1.0f;
    return kZodiacDrain * zodiac_vulnerability(moby_class) * damage(Kind::Single, level)
           / (divisor > 0.0f ? divisor : 1.0f);
}

float decoy_health(int level) {
    const float tier = level > 0 ? damage(Kind::NoExp, level) * kDecoyTierGain : 1.0f;
    return kDecoyBase * tier;
}

}  // namespace ng_plus_plus
