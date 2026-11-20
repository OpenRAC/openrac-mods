// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The weapons' texts at their tiers: one resolver behind rac2.text.by_id for every text the game
// prints of a weapon.
//
// The game keeps several finished, translated texts per version row, and each screen reads its own
// text id from the row (discs.h, kRowTextFields: the names, the ammo pickup's messages, the
// descriptions). For a looked-up id that is a text field of a row the mod names at a tier, the
// resolver takes the game's own string and puts the tier's name where the version's name is
// ("Ultra Meteor Gun: %d rounds" becomes "Beta Meteor Gun: %d rounds"). That keeps every language
// and every placeholder; a text that does not hold the version's name is left as the game has it.
//
// Which row and tier, in this order:
//   1. the vendor's list drawing the mod's entry: its label ("Mega") becomes the tier for sale's;
//   2. the vendor's dialog and preview of the mod's entry (or the item bought last): the row the
//      vendor names an upgrade by, at the tier for sale (or bought);
//   3. the game's level-up line the mod shows for a tier earned: the weapon's row now;
//   4. the game's popup at the end of an in-hand level-up the mod started: its text with the tier's
//      name for the version's, or the tier's name alone for the game's empty text (id 0);
//   5. any other lookup: the row now of the one owned weapon whose row holds the id, at its tier.

#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "experience.h"
#include "game_view.h"
#include "vendor.h"
#include "weapons.h"

namespace ng_plus_plus {

// Returns `message` with its first `game_name` replaced by `tier_name`, or none when `game_name`
// is empty or not in it. The name is matched without its own leading and trailing spaces and
// through the game's in-text codes (bytes below a space: colours, button glyphs) in the message,
// which the game puts around and inside a name ("Got %d Ultra \x0cHeavy \x0cLancer\x08\x08
// ammo"); the codes met inside it go before the tier's name. Pure: the resolver's one substitution.
std::optional<std::string> substitute(
    std::string_view message, std::string_view game_name, std::string_view tier_name
);

// Returns `message` with the tier's name in place of the first of `game_names` it holds, the
// longest tried first (so "Heavy Lancer" wins over "Lancer"), or none when it holds none or the
// result is `room` characters or more (it would not fit the port's slot with its terminator).
std::optional<std::string> retell(
    std::string_view message,
    std::initializer_list<std::string_view> game_names,
    std::string_view tier_name,
    std::size_t room
);

// The resolver behind rac2.text.by_id.
//
// Holds the view, the weapons, the vendor and the experience by reference; the mod's state owns
// them all. Writes into the port's text slots. Only the game's thread uses it.
class TextResolver {
public:
    // Resolves texts of `weapons`' game, writing through `view`; asks `vendor` and `experience`
    // which entry or level-up the game is naming.
    TextResolver(
        GameView& view, const Weapons& weapons, const Vendor& vendor, Experience& experience
    )
        : m_view(view),
          m_weapons(weapons),
          m_vendor(vendor),
          m_experience(experience) {}

    // Returns the game address of text `id` at a tier (in one of the port's slots), or none when
    // the game's own text stands.
    std::optional<std::uint32_t> resolve(std::int32_t id);

private:
    // The vendor's label for the mod's entry being drawn, or none.
    std::optional<std::uint32_t> vendor_label(std::int32_t id);

    // Text `id` of `row`, retold with item `item`'s name at `level`, into slot `slot`; none when
    // `id` is not one of the row's texts or does not hold its name.
    std::optional<std::uint32_t> row_text(
        const Row& row, std::int32_t id, int item, int level, int slot
    );

    // The popup of the in-hand level-up waiting for one (Experience::level_up_pending), or none.
    std::optional<std::uint32_t> level_up_popup(std::int32_t id);

    // Text `id` of the one owned weapon whose row now holds it, at its tier; none when no weapon's
    // or two rows' do.
    std::optional<std::uint32_t> owned_text(std::int32_t id);

    GameView& m_view;                // The game and its slots; outlive this.
    const Weapons& m_weapons;        // The weapons; outlive this.
    const Vendor& m_vendor;          // The vendor's entries; outlives this.
    Experience& m_experience;        // The level-up being shown; outlives this.
    int m_next_shared = 0;           // The next of the slots owned_text takes in turn.
};

}  // namespace ng_plus_plus
