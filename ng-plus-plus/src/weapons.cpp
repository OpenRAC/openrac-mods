// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The weapons (weapons.h): items, their versions' rows, names and the next tier for sale.

#include "weapons.h"

#include "perk_rules.h"

namespace ng_plus_plus {
namespace {

// How far version_row follows the links before it gives up on a broken chain.
constexpr int kMostVersions = 16;

// The name a weapon goes by when the game's text cannot be read.
constexpr const char* kFallbackName = "Weapon";

}  // namespace

WeaponView Weapons::weapon(int item) const {
    const Row row = m_view.current_row(item);
    WeaponView out{item, kind_of(item), row};
    out.owned = m_view.owned(item);
    out.at_last = row.is_last();
    out.level = m_tiers.level(item);
    return out;
}

std::optional<Row> Weapons::version_row(int item, int version) const {
    Row row = m_view.row(item);

    // Up to the item's first version.
    for (int n = 0; n < kMostVersions && row.parent() > 0; ++n) {
        row = m_view.row(row.parent());
    }

    // Down the links to the version asked for, as far as they go.
    for (int n = 0; n < version && row.next() > 0; ++n) {
        row = m_view.row(row.next());
    }

    // A chain shorter than asked: the row reached is another version.
    if (row.version() != version) {
        return std::nullopt;
    }

    return row;
}

std::int32_t Weapons::mega_price(int item) const {
    const int version = mega_version(kind_of(item));
    const std::optional<Row> row = version < 0 ? std::nullopt : version_row(item, version);

    // No Mega row: the tiers' prices start from the ladder.
    if (!row) {
        return 0;
    }

    return row->price() > 0 ? row->price() : 0;
}

bool Weapons::for_sale(int item) const {
    const WeaponView weapon = this->weapon(item);
    const bool sellable = !m_tiers.party_on() && weapon.kind != Kind::None && weapon.owned;
    const int level = m_tiers.own_level(item);
    return sellable && weapon.at_last && can_buy(weapon.kind, level, true, m_view.experience(item));
}

std::string Weapons::name_at(int item, int level) const {
    const Kind kind = kind_of(item);
    const int version = name_version(kind);
    std::optional<Row> named = version >= 0 ? version_row(item, version) : std::nullopt;

    // A version the chain does not reach: the first, else the row now.
    if (!named) {
        named = version_row(item, 0);
    }

    const Row row = named ? *named : m_view.current_row(item);

    // The Lava Gun's tiers are the Ice Gun's (rac3's Lava Gun V5 is its Liquid Nitrogen Gun).
    if (item == kLavaGun && level > 0) {
        return display_name(kIceGunName, kind, level);
    }

    const std::string base = m_view.text(row.name_id());
    return display_name(base.empty() ? kFallbackName : base, kind, level);
}

Row Weapons::upgrade_row(int item) const {
    // The vendor's Mega step goes two versions on, one for the two-version weapons.
    const int version = mega_version(kind_of(item)) == 1 ? 1 : 2;
    const std::optional<Row> row = version_row(item, version);
    return row ? *row : m_view.current_row(item);
}

std::optional<std::uint32_t> Weapons::tint(int item, int level, std::uint32_t tick) const {
    return weapon_tint_rgb(item, level, tick);
}

}  // namespace ng_plus_plus
