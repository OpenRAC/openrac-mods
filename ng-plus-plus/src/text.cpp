// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The weapons' texts at their tiers (text.h).

#include "text.h"

#include <algorithm>
#include <vector>

#include "hooks.h"

namespace ng_plus_plus {
namespace {

// The port's text slots each kind of text goes into. The game prints a text as soon as it looks it
// up (drawn, or copied), so a slot is free again after; the names of the menus come several in a
// row, so they take five slots in turn.
constexpr int kLabelSlot = 0;        // The vendor's label under the mod's entry.
constexpr int kVendorSlot = 1;       // The vendor's dialog and preview of the mod's entry.
constexpr int kSharedSlotFirst = 2;  // The first of the slots owned_text takes in turn.
constexpr int kSharedSlots = 5;      // How many it takes.
constexpr int kLevelUpSlot = 7;      // The level-up line.

static_assert(kLevelUpSlot < hooks::kTextSlots, "the port has eight slots");

}  // namespace

std::optional<std::string> substitute(
    std::string_view message, std::string_view game_name, std::string_view tier_name
) {
    // An empty name is in every text; it names nothing.
    if (game_name.empty()) {
        return std::nullopt;
    }

    const std::size_t at = message.find(game_name);

    // The text does not name the version as its row does.
    if (at == std::string_view::npos) {
        return std::nullopt;
    }

    std::string out(message.substr(0, at));
    out += tier_name;
    out += message.substr(at + game_name.size());
    return out;
}

std::optional<std::string> retell(
    std::string_view message,
    std::initializer_list<std::string_view> game_names,
    std::string_view tier_name,
    std::size_t room
) {
    std::vector<std::string_view> names(game_names);

    // The longest first: a name inside another ("Lancer" in "Heavy Lancer") must not split it.
    std::stable_sort(names.begin(), names.end(), [](std::string_view a, std::string_view b) {
        return a.size() > b.size();
    });

    // The first name found in the message is the one replaced.
    for (const std::string_view name : names) {
        std::optional<std::string> out = substitute(message, name, tier_name);

        // Not this name; the next may be in it.
        if (!out) {
            continue;
        }

        // A text the slot cannot hold whole stays the game's.
        if (out->size() >= room) {
            return std::nullopt;
        }

        return out;
    }

    return std::nullopt;
}

std::optional<std::uint32_t> TextResolver::resolve(std::int32_t id) {
    // 1. The vendor's list drawing the mod's entry: its label.
    if (const std::optional<std::uint32_t> label = vendor_label(id)) {
        return label;
    }

    // 2. The vendor's dialog and preview of the mod's entry, or of the item bought last.
    if (const std::optional<NamedTier> entry = m_vendor.named_entry()) {
        const Row row = m_weapons.upgrade_row(entry->item);
        const std::optional<std::uint32_t> text =
            row_text(row, id, entry->item, entry->level, kVendorSlot);

        // Any other text the vendor shows meanwhile goes on to the weapons' own.
        if (text) {
            return text;
        }
    }

    // 3. The level-up line the mod shows for a tier earned.
    if (const std::optional<int> item = m_experience.announcing()) {
        const WeaponView weapon = m_weapons.weapon(*item);
        const std::optional<std::uint32_t> text =
            row_text(weapon.row, id, *item, weapon.level, kLevelUpSlot);

        // The line shows the weapon's name; any other text goes on as usual.
        if (text) {
            return text;
        }
    }

    // 4. Every other screen: the weapon whose row holds the id.
    return owned_text(id);
}

std::optional<std::uint32_t> TextResolver::vendor_label(std::int32_t id) {
    // Only the label under an upgrade entry's icon.
    if (id != kUpgradeLabelText) {
        return std::nullopt;
    }

    const std::optional<NamedTier> drawn = m_vendor.drawn_upgrade();
    const Step* step = drawn ? step_at(kind_of(drawn->item), drawn->level - 1) : nullptr;

    // Not the mod's entry, or no tier left to sell.
    if (step == nullptr) {
        return std::nullopt;
    }

    // The label names the game's version for sale ("Mega"): the tier's own name takes its place.
    const std::string label = m_view.text(id);
    const std::optional<std::string> told =
        retell(label, {label}, step->name, hooks::kTextSlotBytes);

    // The game's label could not be read: left as it is.
    if (!told) {
        return std::nullopt;
    }

    return m_view.write_slot(kLabelSlot, *told);
}

std::optional<std::uint32_t> TextResolver::row_text(
    const Row& row, std::int32_t id, int item, int level, int slot
) {
    // No tier to name, or not one of the row's own texts (kRowTextFields).
    if (level <= 0 || !row.has_text(id)) {
        return std::nullopt;
    }

    // The version's names as the row has them: the label (+0x08) and the name (+0x48).
    const std::string label = row.label_id() != 0 ? m_view.text(row.label_id()) : std::string();
    const std::string name = row.name_id() != 0 ? m_view.text(row.name_id()) : std::string();
    const std::string message = m_view.text(id);
    const std::string tier_name = m_weapons.name_at(item, level);
    const std::optional<std::string> told =
        retell(message, {label, name}, tier_name, hooks::kTextSlotBytes);

    // The text does not name the version: the game's.
    if (!told) {
        return std::nullopt;
    }

    return m_view.write_slot(slot, *told);
}

std::optional<std::uint32_t> TextResolver::owned_text(std::int32_t id) {
    int found = -1;
    int found_slot = -1;

    // The one item whose row now holds the id; two items on the same row count once.
    for (int item = 0; item < kItems; ++item) {
        const int slot = m_view.slot(item);

        // The same row as the one found, or a row that does not hold the id.
        if (slot == found_slot || !m_view.row(slot).has_text(id)) {
            continue;
        }

        // Shared by two rows: no one weapon to name, left as it is.
        if (found >= 0) {
            return std::nullopt;
        }

        found = item;
        found_slot = slot;
    }

    // No weapon's text.
    if (found < 0) {
        return std::nullopt;
    }

    const WeaponView weapon = m_weapons.weapon(found);

    // No tier to name.
    if (!weapon.has_tier()) {
        return std::nullopt;
    }

    const int slot = kSharedSlotFirst + m_next_shared;
    const std::optional<std::uint32_t> text = row_text(weapon.row, id, found, weapon.level, slot);

    // A slot used: the next text takes the next one.
    if (text) {
        m_next_shared = (m_next_shared + 1) % kSharedSlots;
    }

    return text;
}

}  // namespace ng_plus_plus
