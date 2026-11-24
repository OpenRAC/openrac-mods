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
    m_material_class = material_class_of(m_mod.api());
    m_energy = m_material != nullptr && m_mod.api().api_minor >= kEnergyMinor;
    m_accent = m_material_class != nullptr && m_mod.api().api_minor >= kPaintAccentMinor;

    // What this port lacks, for a player's log.
    if (m_material_class == nullptr) {
        m_mod.info(
            "the armour tiers are drawn as a glow, not repainted, and the armour vendor cannot "
            "sell "
            "them in this build: both need OpenRAC with mod interface 1.34; their protection does "
            "not"
        );
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
        m_last_stand_ready =
            m_tick + static_cast<std::uint32_t>(m_view.ticks(perk.last_stand_ticks));
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

std::optional<Armour::Look> Armour::look_of(int tier, std::uint32_t tick) const {
    const std::optional<std::uint32_t> rgb = armour_rgb(tier, m_view.sixtieths(tick));

    // No tier: the game's Carbonox.
    if (!rgb) {
        return std::nullopt;
    }

    const int level = armour_paint_level(tier);
    const std::optional<std::uint32_t> accent = armour_accent(tier, m_view.sixtieths(tick));

    // A port with the accent: the visor and lamps in the tier's own accent, the suit in its colour.
    if (m_accent && accent) {
        return Look{paint_accent_at(level, *accent), *rgb};
    }

    return Look{paint_at(level), *rgb};
}

void Armour::paint(
    std::uint32_t scope, const std::optional<Look>& look, std::optional<Look>& sent
) {
    // Sent already.
    if (look == sent) {
        return;
    }

    // None: the mod's paint off the class; else the tier's.
    const std::uint32_t material = look ? look->material : kMaterialNone;
    const std::uint32_t rgb = look ? look->rgb : kNoTint;

    if (m_material_class(m_mod.handle(), kHeroClass, scope, material, rgb) == OPENRAC_HOOK_OK) {
        sent = look;
    }
}

void Armour::tick(std::uint32_t tick, std::optional<int> previewed) {
    m_tick = tick;
    const int level = m_view.loaded_level();

    // Another level: the port dropped the looks with the old one.
    if (level != m_level) {
        m_level = level;
        m_drawn_moby = 0;
        m_world.reset();
        m_menu.reset();
    }

    // No level yet.
    if (level < 0) {
        return;
    }

    // An older port: a glow on the hero's moby.
    if (m_material_class == nullptr) {
        glow(tick);
        return;
    }

    // Omega's cycle moves every other tick only.
    if (tier() >= kArmourTiers && tick % 2u != 0) {
        return;
    }

    // The hero in the world in the tier he wears; the vendor's picture of him in the tier it shows.
    // Stock in a scene, or without Carbonox or a tier.
    const int worn = m_view.armour();
    const int shown = previewed.value_or(tier());
    paint(
        kScopeWorld,
        armour_paints_world(m_view.game_mode(), worn, tier()) ? look_of(tier(), tick)
                                                              : std::nullopt,
        m_world
    );
    paint(
        kScopeMenu,
        armour_paints_menu(worn, shown) ? look_of(shown, tick) : std::nullopt,
        m_menu
    );
}

void Armour::glow(std::uint32_t tick) {
    // A port without energy: the game's armour.
    if (!m_energy) {
        return;
    }

    const int now = armour_paints_world(m_view.game_mode(), m_view.armour(), tier()) ? tier() : 0;
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
