// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The vendor: an upgrade entry for each weapon whose next tier is for sale, built as the game
// builds its Mega entry, its price, its purchase, and the tints of the list's icons. Which texts
// the vendor's entries name is the text resolver's (text.h); the vendor tells it which entry and
// tier the game is drawing or naming.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "experience.h"
#include "openrac/games/rac2.hpp"
#include "openrac/mod.hpp"
#include "weapons.h"

namespace ng_plus_plus {

// An item and a tier the vendor names it at.
struct NamedTier {
    int item = 0;         // The item (the armour entry's: the item whose icon it shows).
    int level = 0;        // The tier, as a level (1: the first step).
    bool armour = false;  // An armour tier (armour.h), not the item's.
};

class Armour;

// The vendor's side of NG++.
//
// Holds the weapons and the experience by reference; the mod's state owns all of them. Only the
// game's thread uses it.
class Vendor {
public:
    // Sells the tiers of `weapons`; a purchase steps through `experience`.
    Vendor(const Weapons& weapons, Experience& experience)
        : m_weapons(weapons),
          m_experience(experience) {}

    // Sells the armour tiers of `armour` too (an entry at the end of the list, its room kept).
    void set_armour(Armour* armour) { m_armour = armour; }

    // ---- Hooks ----

    // The vendor's list built (Vendor_BuildList): the game's own, then an upgrade entry for each
    // weapon whose next tier is for sale, right after that weapon's own entry.
    void build_list(openrac::Original<void(std::int32_t)> original, std::int32_t hide_unowned);

    // The price of the entry under the cursor (Vendor_Price): the tier's for an entry of the mod's.
    std::int32_t price(openrac::Original<std::int32_t()> original);

    // The vendor's upgrade purchase, paid (Item_Upgrade): the tier for an entry of the mod's, the
    // game's Mega step for any other. Returns 1 when the item moved on.
    std::int32_t upgrade(openrac::Original<std::int32_t(std::int32_t)> original, std::int32_t id);

    // The vendor's list drawn (Vendor_DrawList): what version_color sees is counted row by row.
    void draw_list(openrac::Original<void()> original);

    // A drawn row's icon colour (Item_VersionColor): the tier for sale's tint on the mod's entry,
    // the tier in play's on the weapon's own entry. `tick` moves the animated tints.
    std::uint32_t version_color(
        openrac::Original<std::uint32_t(std::int32_t)> original,
        std::int32_t kind,
        std::uint32_t tick
    );

    // ---- For the text resolver ----

    // Returns the item and tier for sale of the mod's entry the list is drawing now (its label
    // comes next), or none.
    std::optional<NamedTier> drawn_upgrade() const;

    // Returns the item and tier the vendor's dialog and preview name now: the mod's entry under the
    // cursor at the tier for sale, or for a while after a purchase the item bought at its new tier.
    std::optional<NamedTier> named_entry() const;

    // ---- Each tick ----

    // Counts down the time the purchase feedback names the item bought.
    void tick();

private:
    // An entry the mod added to the list.
    struct Added {
        int place = 0;  // Its place in the list.
        int item = 0;   // The item whose tier it sells.
    };

    // Returns the list place of the `drawn`-th row the game draws (from 0), or -1 past the last.
    int drawn_place(int drawn) const;

    // Returns the item of the mod's entry at `place` of the list, or -1.
    int added_item_at(int place) const;

    // Logs the armour tiers' offer when it changed since the last build: the armour worn, the
    // tier, and whether the next is `offered`.
    void log_armour(bool offered);

    // Returns whether `place` holds the mod's armour entry.
    bool armour_at(int place) const;

    // Returns the item whose icon the armour entry shows: the Shield Charger when owned, else the
    // first weapon owned; -1 for none.
    int armour_carrier() const;

    const Weapons& m_weapons;    // The weapons; outlive this.
    Experience& m_experience;    // Steps a purchase; outlives this.
    std::vector<Added> m_added;  // The entries the mod added at the last build.
    int m_drawn = -1;        // Icons asked for so far while the list draws; -1 when not drawing.
    int m_drawn_place = -1;  // The list place the last icon was for; -1 for none.
    int m_bought = -1;       // The item a purchase stepped last; -1 for none.
    bool m_bought_armour = false;  // That purchase was an armour tier.
    Armour* m_armour = nullptr;    // The armour tiers, or none.
    int m_armour_place = -1;       // The armour entry's place at the last build, -1 for none.
    int m_armour_item = -1;        // The item its icon shows.
    int m_bought_ticks = 0;  // Ticks the purchase feedback keeps naming it.
    int m_logged_worn = -1;         // The armour, tier and offer the log named last.
    int m_logged_tier = -1;
    bool m_logged_offered = false;
};

}  // namespace ng_plus_plus
