// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The fights (combat.h).

#include "combat.h"

#include <array>
#include <cmath>
#include <optional>

#include "armour.h"
#include "elites.h"
#include "perks.h"
#include "tuning.h"
#include "space_missions.h"

namespace ng_plus_plus {

void Combat::post_hit(
    openrac::Original<void(std::uint32_t, std::uint32_t)> original,
    std::uint32_t victim,
    std::uint32_t request
) {
    post_hit_with(original.get(), victim, request);
}

void Combat::post_hit_with(PostHitFn original, std::uint32_t victim, std::uint32_t request) {
    // A space level's hits: the ship's and its enemies' (space_missions.h).
    if (m_space != nullptr && m_space->owns_hits()) {
        m_space->post_hit(original, victim, request);
        return;
    }

    const GameView& view = m_weapons.view();
    float scale = 1.0f;
    int item = -1;
    const bool on_other = victim != view.hero_moby() && request != 0;

    // Only hits on others: the hero's own hits are the enemies' business (take_damage).
    // A decoy: the enemies' hits on it as hard as on the hero (tuning.h), nothing of the tiers.
    const bool decoy = on_other && m_tuning != nullptr && m_tuning->is_decoy(victim);

    if (decoy) {
        scale = m_tuning->decoy_hit_scale();
    } else if (on_other) {
        item = fired_item(request);
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

        // An elite has more health.
        if (m_elites != nullptr) {
            scale /= m_elites->health(victim);
        }

        // The Tesla Claw's own once-a-second hit, as small as its steady ones (tuning.h).
        if (m_tuning != nullptr) {
            scale *= m_tuning->claw_hit(original, victim, request);
        }
    }

    const openrac::Memory memory = view.memory();
    const auto damage_now = memory.read<float>(request + kHitDamage);

    // Nothing to scale: the game's hit as it is.
    if (scale == 1.0f) {
        original(victim, request);
    } else {
        memory.write(request + kHitDamage, damage_now * scale);
        original(victim, request);

        // The caller may post the same request to the next victim.
        memory.write(request + kHitDamage, damage_now);
    }

    // The weapon's perks, from the damage as it landed.
    if (on_other && !decoy && m_perks != nullptr) {
        const auto source = memory.read<std::uint32_t>(request + kHitSource);
        m_perks->hit(original, victim, request, item, source, damage_now * scale);
    }
}

void Combat::record_death(
    openrac::Original<void(std::uint32_t, std::int32_t, std::int32_t)> original,
    std::uint32_t moby,
    std::int32_t flags,
    std::int32_t arg
) {
    const GameView& view = m_weapons.view();
    const std::optional<int> moby_class = view.moby_class(moby);
    const BossClass* boss = moby_class ? boss_of(*moby_class) : nullptr;
    Foe foe;
    foe.boss = boss != nullptr;
    foe.last = boss != nullptr && boss->last;
    foe.bolts = view.enemy_bolts(moby).value_or(0);
    const float elite = m_elites != nullptr ? m_elites->experience(moby) : 1.0f;
    const float scale = (m_enemies.scaling ? kill_experience(view.cycle(), foe) : 1.0f) * elite;

    // The perks see the death first (the Ice Gun's shatter, overdrive, Volatile).
    if (m_perks != nullptr) {
        m_perks->death(moby);
    }

    // What the Zodiac's pulse or reach took names the Zodiac, so its kill's experience is its own.
    if (m_tuning != nullptr) {
        m_tuning->credit_kill(moby);
    }

    // Nothing to scale: the game's.
    if (scale == 1.0f) {
        original(moby, flags, arg);
        return;
    }

    std::array<std::int32_t, kItems> before{};

    // Each weapon's experience before the game's award.
    for (int item = 0; item < kItems; ++item) {
        before[static_cast<std::size_t>(item)] = view.raw_experience(item);
    }

    original(moby, flags, arg);

    // What each gained, scaled.
    for (int item = 0; item < kItems; ++item) {
        const std::int32_t gained =
            view.raw_experience(item) - before[static_cast<std::size_t>(item)];

        // Only a gain.
        if (gained > 0) {
            const double more = std::floor(static_cast<double>(gained) * (scale - 1.0f) + 0.5);
            view.add_raw_experience(item, static_cast<std::int32_t>(more));
        }
    }
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
    float scale = m_enemies.scaling ? enemy_damage(view.cycle(), m_enemies.strength) : 1.0f;

    // The run's Bounty hits harder; the armour tiers take their share off.
    if (m_elites != nullptr && m_elites->modifier() == Modifier::Bounty) {
        scale *= kBountyDamage;
    }

    if (m_armour != nullptr) {
        scale *= m_armour->damage_scale();
    }

    // Clank as the hero: at most a point a hit, after the scaling and the modifiers.
    const bool clank =
        m_enemies.gentle_clank && view.moby_class(view.hero_moby()) == kClankHeroClass;

    // A heal or nothing to scale: the game's own (the armour's last stand first).
    if (amount <= 0 || scale == 1.0f) {
        if (m_armour != nullptr) {
            amount = m_armour->spare(amount);
        }

        amount = hero_hit(amount, clank);
        original(amount);

        if (m_armour != nullptr) {
            m_armour->hurt(amount);
        }

        return;
    }

    // The hero's health is whole points: the fraction waits for the next hit.
    const float exact = static_cast<float>(amount) * scale + m_damage_carry;
    const auto points = static_cast<std::int32_t>(exact);
    m_damage_carry = exact - static_cast<float>(points);
    const std::int32_t spared = m_armour != nullptr ? m_armour->spare(points) : points;
    const std::int32_t dealt = hero_hit(spared, clank);
    original(dealt);

    // The armour's thorns and last stand.
    if (m_armour != nullptr) {
        m_armour->hurt(dealt);
    }
}

}  // namespace ng_plus_plus
