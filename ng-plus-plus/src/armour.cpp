// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The armour tiers (armour.h).

#include "armour.h"

#include <string>

#include "weapon_visuals.h"

namespace ng_plus_plus {

void Armour::start() {
    m_material = moby_material_of(m_mod.api());
    m_energy = m_material != nullptr && m_mod.api().api_minor >= kEnergyMinor;

    // What this port lacks, for a player's log.
    if (!m_energy) {
        m_mod.info("the armour tiers' glow needs OpenRAC 1.25; their protection does not");
    }
}

bool Armour::for_sale() const {
    return !m_tiers.party_on() && m_view.armour() >= kBestArmour && own_tier() < kArmourTiers;
}

void Armour::buy() {
    const int before = own_tier();
    m_tiers.step(kArmourItem);
    m_tiers.mark_changed();
    m_mod.info(
        "purchase: armour tier " + std::to_string(before) + " -> " + std::to_string(own_tier())
    );
    const std::string text = armour_name(own_tier());
    m_mod.api().play_notice(m_mod.handle(), text.c_str());
}

void Armour::hurt(std::int32_t amount) {
    const ArmourPerk perk = armour_perk(tier());

    // A heal, or nothing at this tier.
    if (amount <= 0) {
        return;
    }

    // Thorns: the enemies near him are shocked.
    if (perk.thorns_radius > 0.0f) {
        m_perks.burst_later(m_perks.hero_position(), perk.thorns_radius, perk.thorns_share);
    }

    // The last stand: a hit that ended him leaves 1 point, once in a while.
    if (perk.last_stand_ticks > 0 && m_view.hero_health() <= 0 && m_tick >= m_last_stand_ready) {
        m_view.set_hero_health(1);
        m_last_stand_ready = m_tick + static_cast<std::uint32_t>(m_view.ticks(perk.last_stand_ticks));
        m_mod.info("last stand: the armour kept Ratchet at 1 point");
        m_mod.api().play_notice(m_mod.handle(), "Last stand!");
    }
}

std::int32_t Armour::spare(std::int32_t points) {
    const ArmourPerk perk = armour_perk(tier());
    const std::int32_t health = m_view.hero_health();

    // No last stand at this tier, not ready, a heal, or a hit he lives through.
    if (perk.last_stand_ticks <= 0 || m_tick < m_last_stand_ready || points <= 0 || health <= 1
        || points < health) {
        return points;
    }

    m_last_stand_ready = m_tick + static_cast<std::uint32_t>(m_view.ticks(perk.last_stand_ticks));
    m_mod.info("last stand: the armour kept Ratchet at 1 point");
    m_mod.api().play_notice(m_mod.handle(), "Last stand!");
    return health - 1;
}

void Armour::tick(std::uint32_t tick) {
    m_tick = tick;
    const int level = m_view.loaded_level();

    // Another level: the port dropped the hero's look with the old one.
    if (level != m_level) {
        m_level = level;
        m_drawn_moby = 0;
    }

    // A port without energy, no level, or no tier: the game's armour.
    if (!m_energy || level < 0) {
        return;
    }

    const int now = tier();
    const std::uint32_t hero = m_view.hero_moby();

    // No tier now: taken off where the mod drew one.
    if (now <= 0) {
        if (m_drawn_moby != 0 && m_drawn_moby == hero) {
            m_material(m_mod.handle(), hero, kMaterialNone, kNoTint);
        }

        m_drawn_moby = 0;
        return;
    }

    // No hero yet.
    if (!GameView::is_memory(hero, 0x100u)) {
        return;
    }

    const std::optional<std::uint32_t> rgb = armour_rgb(now, m_view.sixtieths(tick));
    const bool same = m_drawn_moby == hero && m_drawn_tier == now && rgb && m_drawn_rgb == *rgb;

    // The same look as sent (Omega's cycle moves every other tick only).
    if (same || !rgb || (m_drawn_moby == hero && now >= kArmourTiers && tick % 2u != 0)) {
        return;
    }

    // Sent.
    if (m_material(m_mod.handle(), hero, energy_at(armour_energy(now)), *rgb) == OPENRAC_HOOK_OK) {
        m_drawn_moby = hero;
        m_drawn_rgb = *rgb;
        m_drawn_tier = now;
    }
}

}  // namespace ng_plus_plus
