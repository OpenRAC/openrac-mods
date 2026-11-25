// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The armour tiers at the armour vendor (README.md, "Armor tiers"). The armour vendor is the weapon
// vendor's menu in mode 3: its list (rac2.armor_vendor.build_list) is rows {name, price, armour}, the game's armours above the one
// worn; with Carbonox worn it has none, and the next tier is its row. The row's armour is 4, so the
// game's own preview (rac2.vendor.preview) shows Ratchet in Carbonox, which the armour tiers repaint
// (armour.h, through moby_material_class on the menu's picture); its icon is Carbonox's, and
// Carbonox's name and description are retold while the row is under the cursor. The game's
// purchase dialog refuses every armour while Carbonox is worn, so the row's purchase is the mod's
// (rac2.armor_vendor.buy): at the dialog's decision, a yes with the bolts to pay steps the tier and
// pays, then the dialog closes as a refusal does and the row shows the next tier.
//
// Only the game's thread uses it.

#pragma once

#include <cstdint>
#include <optional>

#include "armour.h"
#include "game_view.h"
#include "openrac/mod.hpp"

namespace ng_plus_plus {

// The armour tiers' row at the armour vendor.
class ArmourVendor {
public:
    // The vendor of `view`'s game, selling `armour`'s tiers.
    ArmourVendor(openrac::Mod& mod, GameView& view, Armour& armour)
        : m_mod(mod),
          m_view(view),
          m_armour(armour) {}

    // The game's list, then the next tier's row after it when one is for sale.
    void build_list(openrac::Original<void()> original);

    // The purchase dialog: the mod's row bought at its decision, then the game's dialog.
    void buy(
        openrac::Original<void(std::uint32_t, std::uint32_t)> original,
        std::uint32_t answer,
        std::uint32_t unused
    );

    // Returns the game address of text `id` while the armour vendor shows the mod's row (its name,
    // its description), or none for the game's own.
    std::optional<std::uint32_t> text(std::int32_t id);

    // Returns the tier the armour vendor's preview shows now (the mod's row under its cursor), or
    // none.
    std::optional<int> previewed_tier() const;

private:
    // Returns whether the armour vendor's menu is up with the mod's row in its list.
    bool showing() const;

    // Writes the row for the next tier at the mod's place, or takes it off when none is left.
    void refresh();

    openrac::Mod& m_mod;
    GameView& m_view;
    Armour& m_armour;
    int m_place = -1;  // The mod's row in the list, -1 for none.
    int m_tier = 0;    // The tier it sells.
};

}  // namespace ng_plus_plus
