// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The fights (combat.h).

#include "combat.h"

#include <optional>

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
        const int item = fired_item(request);
        const Kind kind = item >= 0 && item < kItems ? kind_of(item) : Kind::None;

        // The tier of the weapon that dealt the hit.
        if (item >= 0 && item < kItems) {
            scale *= damage(kind, m_weapons.tiers().level(item));
        }

        // Tougher enemies: their health times the cycle's (a boss's more, the final boss's more
        // again; the RYNO II's hits by its own rule), as the damage they take divided by it.
        if (m_enemies.scaling) {
            const std::optional<int> victim_class = view.moby_class(victim);
            const BossClass* boss = victim_class ? boss_of(*victim_class) : nullptr;
            Foe foe;
            foe.boss = boss != nullptr;
            foe.last = boss != nullptr && boss->last;
            foe.bolts = view.enemy_bolts(victim).value_or(0);
            const int level = item >= 0 && item < kItems ? m_weapons.tiers().level(item) : 0;
            scale /= hit_divisor(kind, level, view.cycle(), m_enemies.strength, foe);
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

int Combat::fired_item(std::uint32_t request) const {
    const GameView& view = m_weapons.view();
    const auto source = view.memory().read<std::uint32_t>(request + kHitSource);
    const std::optional<int> source_class = view.moby_class(source);

    // A weapon's own moby: its weapon, whatever is in hand now.
    if (source_class) {
        for (const HitSource& known : kHitSources) {
            // The class's weapon.
            if (known.moby_class == *source_class) {
                return known.item;
            }
        }
    }

    // The hero's own hits and every source not known: the weapon in hand.
    return view.held();
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
