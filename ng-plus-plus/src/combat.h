// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The fights: a tiered weapon's damage, and the enemies' health and damage across challenge mode's
// new games (tiers.h's curve). The game posts every hit through Moby_PostHit and takes every hit on
// the hero through Hero_TakeDamage; the mod scales each for the call.

#pragma once

#include <cstdint>

#include "openrac/mod.hpp"
#include "weapons.h"

namespace ng_plus_plus {

// The enemies' curve, from the mod's settings.
struct EnemySettings {
    bool scaling = true;    // The "enemy_scaling" setting: the curve is on.
    float strength = 1.0f;  // The "enemy_strength" setting: times what the curve adds.
};

// The fights.
//
// Holds the weapons by reference; the mod's state owns them. Only the game's thread uses it.
class Combat {
public:
    // Scales the fights of `weapons`' game by the tiers and `enemies`.
    Combat(const Weapons& weapons, EnemySettings enemies)
        : m_weapons(weapons),
          m_enemies(enemies) {}

    // A hit posted to `victim` from the hit request at game address `request` (Moby_PostHit): on
    // anyone but the hero, its damage times the tier of the weapon that dealt it (fired_item) and
    // divided by the enemies' health, for the call. The request is put back after, since the
    // caller may post it to the next victim.
    void post_hit(
        openrac::Original<void(std::uint32_t, std::uint32_t)> original,
        std::uint32_t victim,
        std::uint32_t request
    );

    // A moby's death recorded (Moby_RecordDeath): the game's, then from cycle 2 the weapon
    // experience it gave (to whichever weapons: the killing one and the game's shares) times
    // kill_experience for the moby (an enemy's own bolts, a boss by its class).
    void record_death(
        openrac::Original<void(std::uint32_t, std::int32_t, std::int32_t)> original,
        std::uint32_t moby,
        std::int32_t flags,
        std::int32_t arg
    );

    // A hit of `amount` points on the hero (Hero_TakeDamage): times the enemies' damage, the
    // fraction of a point carried to the next hit.
    void take_damage(openrac::Original<void(std::int32_t)> original, std::int32_t amount);

    // Returns the item whose hit the request at game address `request` is: the weapon whose own
    // moby is its source (kHitSources: a shot, blast or pool still hurting after a switch), else
    // the weapon in the hero's hand; -1 for none.
    int fired_item(std::uint32_t request) const;

private:
    const Weapons& m_weapons;     // The weapons; outlive this.
    EnemySettings m_enemies;      // The curve's settings.
    float m_damage_carry = 0.0f;  // The fraction of a point carried to the hero's next hit.
};

}  // namespace ng_plus_plus
