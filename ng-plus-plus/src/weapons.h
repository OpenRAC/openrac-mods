// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Which weapon, which row, which tier: the one place that joins the game's item table (game_view.h)
// with the tiers in play (save_tiers.h). Text, tints, damage, ammo, the experience bar and the
// vendor all ask here.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "game_view.h"
#include "save_tiers.h"
#include "tiers.h"

namespace ng_plus_plus {

// One item as the mod sees it now: its kind, its row now and the tier in play.
struct WeaponView {
    int item = 0;            // The item number.
    Kind kind = Kind::None;  // How the mod treats it.
    Row row;                 // Its row now.
    bool owned = false;      // The player has it.
    bool at_last = false;    // Its row now is its last real version.
    int level = 0;           // The tier in play (the party host's in a party), 0 for none.

    // Returns whether the weapon wears a tier now: one the mod knows, owned, at its last version,
    // with a level.
    bool has_tier() const { return kind != Kind::None && level != 0 && owned && at_last; }
};

// The weapons: each item's view, its versions' rows, its names and its next tier for sale.
//
// Holds the game view and the tiers by reference; the mod's state owns all three. Only the game's
// thread uses it.
class Weapons {
public:
    // Joins `view` and `tiers`.
    Weapons(const GameView& view, const SaveTiers& tiers) : m_view(view), m_tiers(tiers) {}

    // Returns the game view the weapons read, and the tiers in play.
    const GameView& view() const { return m_view; }

    const SaveTiers& tiers() const { return m_tiers; }

    // Returns item `item` as the mod sees it now.
    WeaponView weapon(int item) const;

    // Returns the row of item `item`'s version `version`, as the game finds it (func_00288870:
    // from the item's own row up to its first, then down the links), or none.
    std::optional<Row> version_row(int item, int version) const;

    // Returns item `item`'s Mega price, 0 for none.
    std::int32_t mega_price(int item) const;

    // Returns whether item `item`'s next tier is for sale now (never for a party's guest).
    bool for_sale(int item) const;

    // Returns item `item`'s full name at `level`: "<tier> <the game's name of the version before
    // its bought top>" ("Beta Meteor Gun"; name_version), the game's text in the disc's language.
    std::string name_at(int item, int level) const;

    // Returns the row the vendor names an upgrade entry for item `item` by: its version 2's row (1
    // for the two-version weapons), or, for the RYNO II and the Zodiac with no such row, its own
    // row now (what the game names it by when that lookup fails).
    Row upgrade_row(int item) const;

    // Returns item `item`'s tint at `level` at game tick `tick`, none at level 0.
    std::optional<std::uint32_t> tint(int item, int level, std::uint32_t tick) const;

private:
    const GameView& m_view;    // The game; outlives this.
    const SaveTiers& m_tiers;  // The tiers in play; outlive this.
};

}  // namespace ng_plus_plus
