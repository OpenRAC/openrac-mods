// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The armour tiers at the armour vendor (armour_vendor.h).

#include "armour_vendor.h"

#include <string>

#include "perk_rules.h"

namespace ng_plus_plus {
namespace {

// The port's text slots the row's texts go into (text.cpp's: the weapon vendor's label and
// dialog, which are not up while the armour vendor is).
constexpr int kNameSlot = 0;
constexpr int kAboutSlot = 1;

// The dialog's answer the game reads: yes.
constexpr std::int32_t kYes = 1;

}  // namespace

void ArmourVendor::build_list(openrac::Original<void()> original) {
    original();
    m_place = -1;

    // Nothing to sell (no Carbonox, every tier, a party's guest), another menu, or no slots.
    if (!m_armour.for_sale() || m_view.vendor_mode() != kArmourMode || !m_view.has_slots()) {
        return;
    }

    const int count = m_view.vendor_count();

    // A list the game left in a state the mod does not know, or full.
    if (count < 0 || count >= kArmourRowsMax) {
        return;
    }

    m_place = count;
    m_view.set_vendor_count(count + 1);
    refresh();
    m_mod.info(
        "armour vendor: " + armour_name(m_tier) + " for sale at row " + std::to_string(m_place)
        + " (armour " + std::to_string(m_view.armour()) + " worn, the game's rows "
        + std::to_string(count) + ")"
    );
}

void ArmourVendor::refresh() {
    // No row of the mod's.
    if (m_place < 0) {
        return;
    }

    // Every tier bought: the row goes (it is the last of the list).
    if (!m_armour.for_sale()) {
        m_view.set_vendor_count(m_place);
        m_place = -1;
        m_tier = 0;
        return;
    }

    m_tier = m_armour.own_tier() + 1;

    // Carbonox's icon and pack: the preview shows the model the tiers repaint.
    m_view.put_armour_row(m_place, {kArmourIcon + kBestArmour, armour_price(m_tier), kBestArmour});
}

bool ArmourVendor::showing() const {
    // The row must still be the mod's: the game may have built the list again.
    if (m_place < 0 || m_view.vendor_mode() != kArmourMode || !m_view.vendor_open()
        || m_view.vendor_count() <= m_place) {
        return false;
    }

    const ArmourRow row = m_view.armour_row(m_place);
    return row.text == kArmourIcon + kBestArmour && row.armour == kBestArmour
           && row.price == armour_price(m_tier);
}

void ArmourVendor::buy(
    openrac::Original<void(std::uint32_t, std::uint32_t)> original,
    std::uint32_t answer,
    std::uint32_t unused
) {
    const bool ours = showing() && m_view.vendor_cursor() == m_place;
    const auto at = static_cast<openrac::gaddr>(answer);
    bool bought = false;

    // The dialog decides on the mod's row: a yes pays and steps the tier here, then the game closes
    // the dialog as for a no (its own purchase refuses every armour while Carbonox is worn).
    if (ours && m_view.vendor_dialog() == kDialogDecide && GameView::is_memory(at, 4)
        && m_view.memory().read<std::int32_t>(at) == kYes) {
        const std::int32_t price = m_view.armour_row(m_place).price;
        const std::int32_t bolts = m_view.bolts();

        // The dialog's first state refused a row the bolts cannot pay; checked again here.
        if (m_armour.for_sale() && m_armour.own_tier() + 1 == m_tier && bolts >= price) {
            m_view.set_bolts(bolts - price);
            m_armour.buy();
            bought = true;
        } else {
            m_mod.info("armour vendor: " + armour_name(m_tier) + " not bought (bolts or tier)");
        }

        m_view.memory().write(at, std::int32_t{0});
    }

    original(answer, unused);

    // The row shows the next tier, or goes.
    if (bought) {
        refresh();
    }
}

std::optional<std::uint32_t> ArmourVendor::text(std::int32_t id) {
    const bool about = id == m_view.armour_about_text();
    const bool name = id != 0 && id == m_view.armour_name_text();

    // Only Carbonox's name and description, while the armour vendor shows the mod's row under its
    // cursor.
    if ((!about && !name) || !showing() || m_view.vendor_cursor() != m_place) {
        return std::nullopt;
    }

    return about ? m_view.write_slot(kAboutSlot, armour_about(m_tier))
                 : m_view.write_slot(kNameSlot, armour_name(m_tier));
}

std::optional<int> ArmourVendor::previewed_tier() const {
    // The mod's row under the cursor: its tier.
    if (showing() && m_view.vendor_cursor() == m_place) {
        return m_tier;
    }

    return std::nullopt;
}

}  // namespace ng_plus_plus
