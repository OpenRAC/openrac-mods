// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The armour tiers (README.md, "Armor tiers"): past the game's best armour (Carbonox, armour 4,
// which the hero's damage reads from g_player_stats +0x18), eight tiers, Alpha Armor to Omega
// Armor, sold at the armour vendor after Carbonox (armour_vendor.h). Each takes a share off every
// hit on the hero on top of the game's armour, and from Gamma shocks the enemies near him when he
// is hit (thorns), from Epsilon leaves him 1 point of health once a minute when a hit would end him.
// The look is Carbonox, the model the hero wears, repainted in the tier's colour with a rim that
// glows brighter each tier (OpenRAC's paint material on his class, 1.34), with the visor and lamps
// in an accent of the tier's own (the paint's accent, 1.38, armour_accent): in the world, and on the
// armour vendor's picture of him, which shows the tier for sale. An OpenRAC before 1.34 draws the
// tier as a glow over Carbonox instead (energy, 1.25).
// Kept in NG++'s book (tiers.txt) as item byte kArmourItem.
//
// Only the game's thread uses it.

#pragma once

#include <cstdint>
#include <optional>

#include "armour_api.h"
#include "bolt_api.h"
#include "game_view.h"
#include "openrac/mod.hpp"
#include "perk_rules.h"
#include "perks.h"
#include "save_tiers.h"

namespace ng_plus_plus {

// The armour tiers.
class Armour {
public:
    // The armour of `view`'s game, kept in `tiers`, its thorns through `perks`.
    Armour(openrac::Mod& mod, const GameView& view, SaveTiers& tiers, Perks& perks)
        : m_mod(mod),
          m_view(view),
          m_tiers(tiers),
          m_perks(perks) {}

    // Finds moby_material and moby_material_class.
    void start();

    // Returns the tier in play (the party host's in a party).
    int tier() const { return m_tiers.level(kArmourItem); }

    // Returns this save's own tier.
    int own_tier() const { return m_tiers.own_level(kArmourItem); }

    // Returns whether the next tier is for sale: the game's best armour worn, a tier left, not a
    // party's guest.
    bool for_sale() const;

    // Returns the next tier's price, 0 with none for sale.
    std::int32_t price() const { return for_sale() ? armour_price(own_tier() + 1) : 0; }

    // The vendor's purchase of the next tier, paid.
    void buy();

    // Returns what a hit on the hero is multiplied by at the tier in play.
    float damage_scale() const { return armour_damage(tier()); }

    // After a hit of `amount` points on the hero: the thorns and the last stand.
    void hurt(std::int32_t amount);

    // Before a hit of `points` on the hero: the last stand, when it is ready and the hit would end
    // him, takes it down to leave him 1 point (so the game never starts his death). Returns the
    // points to deal.
    std::int32_t spare(std::int32_t points);

    // Once a game tick: the hero's look, and the armour vendor's picture of him in `previewed`, the
    // tier it shows (none: the tier worn).
    void tick(std::uint32_t tick, std::optional<int> previewed);

private:
    openrac::Mod& m_mod;
    const GameView& m_view;
    SaveTiers& m_tiers;
    Perks& m_perks;

    // The paint of tier `tier` at `tick`: its material and colour, or none for no tier.
    struct Look {
        std::uint32_t material = 0;
        std::uint32_t rgb = 0;

        bool operator==(const Look& other) const {
            return material == other.material && rgb == other.rgb;
        }
    };

    std::optional<Look> look_of(int tier, std::uint32_t tick) const;

    // Puts `look` on the hero's class in `scope` when it is not what was sent there.
    void paint(std::uint32_t scope, const std::optional<Look>& look, std::optional<Look>& sent);

    // An OpenRAC before 1.34: the tier's glow on the hero's moby (energy).
    void glow(std::uint32_t tick);

    MobyMaterialFn m_material = nullptr;
    MaterialClassFn m_material_class = nullptr;
    bool m_energy = false;
    bool m_accent = false;  // The port's paint takes an accent (1.38).
    std::uint32_t m_tick = 0;
    std::uint32_t m_last_stand_ready = 0;  // The tick the next last stand is ready.
    std::optional<Look> m_world;           // The look sent for the world, and for the menus.
    std::optional<Look> m_menu;
    std::uint32_t m_drawn_moby = 0;  // An older port's: the hero's moby drawn, and its look.
    std::uint32_t m_drawn_rgb = 0;
    int m_drawn_tier = 0;
    int m_level = -1;
};

}  // namespace ng_plus_plus
