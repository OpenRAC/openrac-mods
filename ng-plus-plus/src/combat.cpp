// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The fights (combat.h).

#include "combat.h"

namespace ng_plus_plus {

void Combat::post_hit(
    openrac::Original<void(std::uint32_t, std::uint32_t)> original,
    std::uint32_t victim,
    std::uint32_t request
) {
    const GameView& view = m_weapons.view();
    float scale = 1.0f;

    // Only hits on others: the hero's own hits are the enemies' business (take_damage).
    if (victim != view.hero_moby() && request != 0) {
        const int held = view.held();

        // The weapon in hand's tier, whatever fired the shot (README.md, "Not yet").
        if (held >= 0 && held < kItems) {
            scale *= damage(kind_of(held), m_weapons.tiers().level(held));
        }

        // Tougher enemies: their health times the cycle's, as the damage they take divided by it.
        if (m_enemies.scaling) {
            scale /= enemy_health(view.cycle(), m_enemies.strength);
        }
    }

    // Nothing to scale: the game's hit as it is.
    if (scale == 1.0f) {
        original(victim, request);
        return;
    }

    const openrac::Memory memory = view.memory();
    const auto damage_now = memory.read<float>(request + kHitDamage);
    memory.write(request + kHitDamage, damage_now * scale);
    original(victim, request);

    // The caller may post the same request to the next victim.
    memory.write(request + kHitDamage, damage_now);
}

void Combat::take_damage(openrac::Original<void(std::int32_t)> original, std::int32_t amount) {
    const GameView& view = m_weapons.view();
    const float scale = m_enemies.scaling ? enemy_damage(view.cycle(), m_enemies.strength) : 1.0f;

    // A heal or nothing to scale: the game's own.
    if (amount <= 0 || scale == 1.0f) {
        original(amount);
        return;
    }

    // The hero's health is whole points: the fraction waits for the next hit.
    const float exact = static_cast<float>(amount) * scale + m_damage_carry;
    const auto points = static_cast<std::int32_t>(exact);
    m_damage_carry = exact - static_cast<float>(points);
    original(points);
}

}  // namespace ng_plus_plus
