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

// The text the game's level-up popup shows for a weapon its table has no line for: none, an empty
// box (help_show_message_for_id, func_00288508, passes 0 to the message line).
constexpr std::int32_t kEmptyText = 0;

// Returns whether `c` is one of the game's in-text codes rather than a letter: the colours (0x08
// the default, 0x0C a highlight, ...) and the buttons' glyphs (0x10 to 0x13), all below a space.
// The game's messages wrap a weapon's name in them, between its words too ("Got %d Ultra
// \x0cHeavy \x0cLancer\x08\x08 ammo"), where the row's name has none ("Ultra Heavy Lancer").
bool is_text_code(char c) {
    return static_cast<unsigned char>(c) < 0x20u;
}

}  // namespace

std::optional<std::string> substitute(
    std::string_view message, std::string_view game_name, std::string_view tier_name
) {
    // The name's own padding is no part of it: the game's names can end in a space ("Ultra
    // Vaporizer ") that its messages do not repeat.
    const std::size_t first = game_name.find_first_not_of(' ');
    const std::size_t last = game_name.find_last_not_of(' ');

    // An empty name is in every text; it names nothing.
    if (first == std::string_view::npos) {
        return std::nullopt;
    }

    const std::string_view name = game_name.substr(first, last - first + 1);

    // Each place the name can start, in order: the first mention wins.
    for (std::size_t at = 0; at < message.size(); ++at) {
        // A name starts on its own first letter, never on a control byte.
        if (message[at] != name[0]) {
            continue;
        }

        std::size_t in_message = at;
        std::size_t in_name = 0;
        std::string codes;  // The control bytes met inside the name, kept in order.

        // Letter by letter, stepping over the game's control bytes between them.
        while (in_message < message.size() && in_name < name.size()) {
            const char c = message[in_message];

            // A colour or glyph code inside the name: not a letter of it, kept.
            if (is_text_code(c) && !is_text_code(name[in_name])) {
                codes += c;
                ++in_message;
                continue;
            }

            // A letter that differs: not the name here.
            if (c != name[in_name]) {
                break;
            }

            ++in_message;
            ++in_name;
        }

        // Not the whole name from here.
        if (in_name < name.size()) {
            continue;
        }

        // The codes first, so a colour opened inside the name now opens before the tier's name
        // and every code the game closes later still has its opener.
        std::string out(message.substr(0, at));
        out += codes;
        out += tier_name;
        out += message.substr(in_message);
        return out;
    }

    // The text does not name the version as its row does.
    return std::nullopt;
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

    // 4. The popup at the end of an in-hand level-up.
    if (const std::optional<std::uint32_t> popup = level_up_popup(id)) {
        return popup;
    }

    // 5. Every other screen: the weapon whose row holds the id.
    return owned_text(id);
}

std::optional<std::uint32_t> TextResolver::level_up_popup(std::int32_t id) {
    const std::optional<int> item = m_experience.level_up_pending();

    // No in-hand level-up waiting.
    if (!item) {
        return std::nullopt;
    }

    const WeaponView weapon = m_weapons.weapon(*item);
    const std::string tier_name = m_weapons.name_at(*item, weapon.level);

    // The game's table has no line for the weapon (the RYNO II, the Zodiac): it shows text 0, an
    // empty box. The tier's name fills it.
    if (id == kEmptyText) {
        m_experience.level_up_shown();
        return m_view.write_slot(kLevelUpSlot, tier_name);
    }

    // The table's line names the last version ("Ultra"): the tier's name in its place.
    const Row& row = weapon.row;
    const std::string label = row.label_id() != 0 ? m_view.text(row.label_id()) : std::string();
    const std::string name = row.name_id() != 0 ? m_view.text(row.name_id()) : std::string();
    const std::optional<std::string> told =
        retell(m_view.text(id), {label, name}, tier_name, hooks::kTextSlotBytes);

    // A text that does not name the weapon: the game's.
    if (!told) {
        return std::nullopt;
    }

    // Not one of the row's own texts (the HUD's name, its ammo): the popup itself, shown once.
    if (!row.has_text(id)) {
        m_experience.level_up_shown();
    }

    return m_view.write_slot(kLevelUpSlot, *told);
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
