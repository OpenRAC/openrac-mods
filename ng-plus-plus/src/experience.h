// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The steps and what each tick keeps of them in the game: a weapon at its last version whose
// experience reaches its next tier takes it, with the game's own level-up; a step bought or earned
// fills the ammo; the last version's row carries the tier's most ammo and the experience bar's goal;
// a line at the top of the screen names a tier taken or a tiered weapon taken in hand.
//
// The game's experience keeps adding up at a weapon's last version, and its own level check skips
// every last version (func_L00_002D5EB8 skips a V4 row, a two-version weapon's second and a
// one-version weapon; Weapon_LevelUp, func_00287EB0, stops at a row with no next), so the mod's
// steps and the row's goal never meet the game's. The row is the game's static table, never saved,
// and gets back what was there when no step is left.

#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "save_tiers.h"
#include "weapons.h"

namespace ng_plus_plus {

// How a weapon reached a step.
enum class StepCause : std::uint8_t {
    Bought,  // at the vendor: the vendor's own purchase feedback shows it
    Earned,  // by experience: the game's own level-up shows it
};

// The steps by experience and the rows of the last versions.
//
// Holds the tiers and the weapons by reference; the mod's state owns them. Only the game's thread
// uses it.
class Experience {
public:
    // Steps `tiers`' weapons, seen through `weapons`. `notices`: the "notices" setting, the line at
    // the top of the screen.
    Experience(SaveTiers& tiers, const Weapons& weapons, bool notices)
        : m_tiers(tiers),
          m_weapons(weapons),
          m_notices(notices) {}

    // Tells whether the mod's text lookup is in place, which the game's own level-up line needs
    // to name the weapon at its tier.
    void set_text_hooked(bool hooked) { m_text_hooked = hooked; }

    // Item `item` takes its next step: its experience starts again, its ammo is filled to the new
    // most, the step is shown as the game shows one of `cause`, and the book keeps it.
    void take_step(int item, StepCause cause);

    // Each tick in a level: every owned weapon at its last version whose experience reaches its
    // next tier takes it. Not for a party's guest, nor while the save's clock is unknown.
    void step_by_experience();

    // Each tick in a level: the last versions' rows carry the tiers' most ammo and bar goals.
    void write_rows();

    // Each tick: clears the line at the top of the screen when its time is up.
    void count_down_notice();

    // Each tick in a level: names a tiered weapon taken in hand.
    void name_weapon_in_hand();

    // Returns the item whose level-up line the game is showing now, so the text resolver names it
    // at its tier; none outside that call.
    std::optional<int> announcing() const;

    // Returns the weapon in hand whose level-up animation the mod started and whose popup the game
    // has not shown yet, or none. At the animation's end the game shows the popup of its own
    // level-up table (help_show_message_for_id, func_00288508): a text naming the last version
    // ("Ultra"), or text 0, an empty box, for a weapon the table has no line for (the RYNO II, the
    // Zodiac); the text resolver names the weapon at its tier there instead.
    std::optional<int> level_up_pending() const;

    // The pending level-up's popup was named: the next lookups are the game's again.
    void level_up_shown() { m_pending = -1; }

private:
    // What the mod found and wrote in one last version's row.
    struct RowWrite {
        std::int32_t ammo_base = 0;     // The most ammo as found: the game's or another mod's.
        std::int32_t ammo_written = 0;  // The most ammo as the mod wrote it (0: not written).
        std::int32_t goal_base = 0;     // The bar's goal as found.
        std::int32_t goal_written = 0;  // The bar's goal as the mod wrote it.
        bool goal_seen = false;         // goal_base holds what was found.
    };

    // Shows a step by experience as the game shows a level-up: the in-hand animation for the
    // weapon in hand, else the popup and line; the mod's line when this build has neither.
    void show_earned(int item);

    // Puts item `item`'s full name at the top of the screen, when the setting is on.
    void notice(int item);

    SaveTiers& m_tiers;          // The tiers in play; outlive this.
    const Weapons& m_weapons;    // The weapons; outlive this.
    bool m_notices;              // The "notices" setting.
    bool m_text_hooked = false;  // The mod's text lookup is in place.

    std::array<RowWrite, kItems> m_rows{};  // Per item, its last version's row.
    int m_notice_ticks = 0;                 // Ticks the line at the top stays up.
    int m_held = -1;                        // The item in hand at the last tick.
    int m_announcing = -1;                  // The item of the level-up line being shown; -1 none.
    int m_pending = -1;                     // The in-hand level-up waiting for its popup; -1 none.
    int m_pending_ticks = 0;                // Ticks it still waits.
};

}  // namespace ng_plus_plus
