// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The armour tiers (README.md, "Armor tiers"): past the game's best armour (armour 4, which the
// hero's damage reads from g_player_stats +0x18), eight tiers, Alpha Armor to Omega Armor, bought
// at the weapon vendor (an entry of the mod's at the end of the list, under the Shield Charger's
// icon, priced up perk_rules.h's armour ladder). Each takes a share off every hit on the hero on
// top of the game's armour, draws the hero's armour in the tier's colour with a glow (OpenRAC's
// energy material on his moby, 1.25), and from Gamma shocks the enemies near him when he is hit
// (thorns), from Epsilon leaves him 1 point of health once a minute when a hit would end him.
// Kept in NG++'s book (tiers.txt) as item byte kArmourItem.
//
// Only the game's thread uses it.

#pragma once

#include <cstdint>

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

    // Finds moby_material.
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

    // Once a game tick: the hero's glow.
    void tick(std::uint32_t tick);

private:
    openrac::Mod& m_mod;
    const GameView& m_view;
    SaveTiers& m_tiers;
    Perks& m_perks;
    MobyMaterialFn m_material = nullptr;
    bool m_energy = false;
    std::uint32_t m_tick = 0;
    std::uint32_t m_last_stand_ready = 0;  // The tick the next last stand is ready.
    std::uint32_t m_drawn_moby = 0;        // The hero's moby drawn, and its look.
    std::uint32_t m_drawn_rgb = 0;
    int m_drawn_tier = 0;
    int m_level = -1;
};

}  // namespace ng_plus_plus
