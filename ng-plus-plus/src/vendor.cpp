// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The vendor's side of NG++ (vendor.h).

#include "vendor.h"

#include <string>

#include "armour.h"

namespace ng_plus_plus {
namespace {

// Ticks the vendor's purchase feedback names the item bought at its new tier: ten seconds.
constexpr int kBoughtTicks = 600;

}  // namespace

void Vendor::build_list(openrac::Original<void(std::int32_t)> original, std::int32_t hide_unowned) {
    original(hide_unowned);

    const GameView& view = m_weapons.view();
    int count = view.vendor_count();
    m_added.clear();

    // A list the game left in a state the mod does not know: left as it is.
    if (count < 0 || count > kVendorMaxEntries) {
        return;
    }

    m_armour_place = -1;
    m_armour_item = armour_carrier();
    const bool armour = m_armour != nullptr && m_armour->for_sale() && m_armour_item >= 0;
    log_armour(armour);

    // The armour entry's room kept: the weapons' entries never crowd it out.
    const int room = armour ? kVendorMaxEntries - 1 : kVendorMaxEntries;

    // Each weapon whose next tier is for sale, while the list has room.
    for (int item = 0; item < kItems && count < room; ++item) {
        // Nothing for sale: no entry.
        if (!m_weapons.for_sale(item)) {
            continue;
        }

        int place = count;

        // Right after the weapon's own entry (the last of its item), else at the end.
        for (int n = 0; n < count; ++n) {
            // An entry of the weapon: the new one goes after it.
            if (view.vendor_entry(n).item == item) {
                place = n + 1;
            }
        }

        view.open_vendor_place(place, count);

        // The mod's entries after it moved down with the rest.
        for (Added& added : m_added) {
            // At or after the new place: one down.
            if (added.place >= place) {
                ++added.place;
            }
        }

        view.put_upgrade_entry(place, item, view.current_row(item));
        m_added.push_back({place, item});
        ++count;
    }

    // The next armour tier, at the end of the list.
    if (armour && count < kVendorMaxEntries) {
        view.put_upgrade_entry(count, m_armour_item, m_weapons.upgrade_row(m_armour_item));
        m_armour_place = count;
        ++count;
    }

    view.set_vendor_count(count);
}

void Vendor::log_armour(bool offered) {
    // No armour tiers.
    if (m_armour == nullptr) {
        return;
    }

    const int worn = m_weapons.view().armour();
    const int tier = m_armour->own_tier();

    // Said once for each change: the vendor builds its list after every purchase.
    if (worn == m_logged_worn && tier == m_logged_tier && offered == m_logged_offered) {
        return;
    }

    m_logged_worn = worn;
    m_logged_tier = tier;
    m_logged_offered = offered;
    m_weapons.view().mod().info(
        "armour tiers: armour " + std::to_string(worn) + " worn (" + std::to_string(kBestArmour)
        + " is Carbonox), tier " + std::to_string(tier) + ", "
        + (offered ? armour_name(tier + 1) + " for sale at the end of the list"
                   : std::string("none for sale"))
    );
}

bool Vendor::armour_at(int place) const {
    // No armour entry at the last build, or another place.
    if (m_armour == nullptr || place < 0 || place != m_armour_place) {
        return false;
    }

    // The place must still hold the mod's entry: the game may have rebuilt the list.
    const VendorEntry entry = m_weapons.view().vendor_entry(place);
    return entry.item == m_armour_item && entry.upgrade;
}

int Vendor::armour_carrier() const {
    constexpr int kShieldCharger = 45;

    // The Shield Charger, the closest to armour.
    if (m_weapons.view().owned(kShieldCharger)) {
        return kShieldCharger;
    }

    // Else the first weapon owned.
    for (int item = 0; item < kItems; ++item) {
        if (kind_of(item) != Kind::None && m_weapons.view().owned(item)) {
            return item;
        }
    }

    return -1;
}

int Vendor::added_item_at(int place) const {
    const GameView& view = m_weapons.view();

    // Each entry the mod added at the last build.
    for (const Added& added : m_added) {
        // The place must still hold the mod's upgrade entry: the game may have rebuilt the list.
        if (added.place != place) {
            continue;
        }

        const VendorEntry entry = view.vendor_entry(place);
        return entry.item == added.item && entry.upgrade ? added.item : -1;
    }

    return -1;
}

std::int32_t Vendor::price(openrac::Original<std::int32_t()> original) {
    const int cursor = m_weapons.view().vendor_cursor();

    // The armour entry: the next tier's price.
    if (armour_at(cursor) && m_armour->for_sale()) {
        return m_armour->price();
    }

    const int item = added_item_at(cursor);

    // The mod's entry: the tier's price, from the weapon's own Mega.
    if (item >= 0 && m_weapons.for_sale(item)) {
        const int level = m_weapons.tiers().own_level(item);
        return ng_plus_plus::price(kind_of(item), level, m_weapons.mega_price(item));
    }

    return original();
}

std::int32_t Vendor::upgrade(
    openrac::Original<std::int32_t(std::int32_t)> original, std::int32_t id
) {
    const GameView& view = m_weapons.view();
    const int under = added_item_at(view.vendor_cursor());
    const bool is_item = id >= 0 && id < kItems;

    // The armour entry, paid: the next armour tier.
    if (armour_at(view.vendor_cursor()) && id == m_armour_item && m_armour->for_sale()) {
        m_armour->buy();
        m_bought = id;
        m_bought_armour = true;
        m_bought_ticks = kBoughtTicks;
        return 1;
    }

    // The vendor's purchase of the mod's entry, paid: the tier.
    if (is_item && under == id && m_weapons.for_sale(id)) {
        const int before = m_weapons.tiers().own_level(id);
        m_experience.take_step(id, StepCause::Bought);
        m_bought = id;
        m_bought_armour = false;
        m_bought_ticks = kBoughtTicks;
        view.mod().info(
            "purchase: item " + std::to_string(id) + ", tier " + std::to_string(before) + " -> "
            + std::to_string(m_weapons.tiers().own_level(id))
        );
        return 1;
    }

    // A weapon's purchase the mod leaves to the game, logged for bug reports.
    if (under >= 0 || (is_item && kind_of(id) != Kind::None)) {
        const bool for_sale = is_item && m_weapons.for_sale(id);
        view.mod().info(
            "purchase: item " + std::to_string(id) + " left to the game (entry under the cursor "
            + std::to_string(under) + ", for sale " + std::to_string(for_sale ? 1 : 0) + ")"
        );
    }

    return original(id);
}

void Vendor::draw_list(openrac::Original<void()> original) {
    // While the game draws, version_color counts the icons asked for.
    m_drawn = 0;
    m_drawn_place = -1;
    original();
    m_drawn = -1;
    m_drawn_place = -1;
}

int Vendor::drawn_place(int drawn) const {
    const GameView& view = m_weapons.view();
    const int cursor = view.vendor_cursor();
    const int count = view.vendor_count();
    int seen = 0;

    // The rows around the cursor that are in the list, in the order the game draws them.
    for (int n = kVendorDrawFirst; n < kVendorDrawEnd; ++n) {
        const int place = cursor + n;

        // Rows past either end of the list are not drawn.
        if (place < 0 || place >= count) {
            continue;
        }

        // The row this call is for.
        if (seen == drawn) {
            return place;
        }

        ++seen;
    }

    return -1;
}

std::uint32_t Vendor::version_color(
    openrac::Original<std::uint32_t(std::int32_t)> original, std::int32_t kind, std::uint32_t tick
) {
    const std::uint32_t color = original(kind);

    // Asked outside the list's drawing: the game's own.
    if (m_drawn < 0) {
        return color;
    }

    const GameView& view = m_weapons.view();
    const int place = drawn_place(m_drawn);
    ++m_drawn;
    m_drawn_place = place;

    // The armour entry: the tier for sale's colour.
    if (armour_at(place)) {
        const std::optional<std::uint32_t> rgb = armour_tint(m_armour->own_tier() + 1, tick);
        return rgb ? (color & kColorAlpha) | *rgb : color;
    }

    const int added = added_item_at(place);

    // The mod's entry: the tier for sale, the one after the weapon's level now.
    if (added >= 0) {
        const std::optional<std::uint32_t> rgb =
            m_weapons.tint(added, m_weapons.tiers().level(added) + 1, tick);
        return rgb ? (color & kColorAlpha) | *rgb : color;
    }

    // Past the list's drawn rows.
    if (place < 0) {
        return color;
    }

    const int item = view.vendor_entry(place).item;

    // Not an item, or one with no tier: the game's own colour.
    if (item < 0 || item >= kItems || !m_weapons.weapon(item).has_tier()) {
        return color;
    }

    // The weapon's own entry (its ammo): the tier it has now.
    const std::optional<std::uint32_t> rgb =
        m_weapons.tint(item, m_weapons.tiers().level(item), tick);
    return rgb ? (color & kColorAlpha) | *rgb : color;
}

std::optional<NamedTier> Vendor::drawn_upgrade() const {
    // Only while the list draws, after the icon of the row whose label comes next.
    if (m_drawn <= 0) {
        return std::nullopt;
    }

    // The armour entry: the tier for sale.
    if (armour_at(m_drawn_place)) {
        return NamedTier{m_armour_item, m_armour->own_tier() + 1, true};
    }

    const int item = added_item_at(m_drawn_place);

    // That row is not the mod's.
    if (item < 0) {
        return std::nullopt;
    }

    return NamedTier{item, m_weapons.tiers().level(item) + 1};
}

std::optional<NamedTier> Vendor::named_entry() const {
    const int cursor = m_weapons.view().vendor_cursor();

    // The armour entry under the cursor: the tier for sale.
    if (armour_at(cursor) && m_armour->for_sale()) {
        return NamedTier{m_armour_item, m_armour->own_tier() + 1, true};
    }

    // An armour tier bought moments ago.
    if (m_bought_armour && m_bought >= 0 && m_bought_ticks > 0 && m_armour != nullptr) {
        return NamedTier{m_bought, m_armour->own_tier(), true};
    }

    const int item = added_item_at(cursor);

    // The mod's entry under the cursor: the tier for sale.
    if (item >= 0 && m_weapons.for_sale(item)) {
        return NamedTier{item, m_weapons.tiers().own_level(item) + 1};
    }

    // Bought moments ago: the vendor's purchase feedback names the entry it sold, at the tier it has now.
    if (m_bought >= 0 && m_bought_ticks > 0) {
        return NamedTier{m_bought, m_weapons.tiers().own_level(m_bought)};
    }

    return std::nullopt;
}

void Vendor::tick() {
    // The purchase feedback's time runs out.
    if (m_bought_ticks > 0) {
        --m_bought_ticks;
    }
}

}  // namespace ng_plus_plus
