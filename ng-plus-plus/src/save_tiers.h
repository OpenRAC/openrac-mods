// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The tiers in play and the save they belong to: each weapon's level and whether the mod counts its
// experience yet, the party host's levels when a party gives them, and the book of saves kept in
// the mod's data folder (tiers.txt), with the watch on the save's clock that tells which save's
// tiers are in play (save_book.h has the rules).
//
// The tiers follow the saves as the game's own progress does: a step taken since the last save is
// the game in progress's only, as the bolts it cost are; the book takes the tiers in play when the
// game stamps the save's clock (a save, an autosave, a level's start), and a save loaded gets back
// the tiers of its own clock, never later ones.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "game_view.h"
#include "save_book.h"
#include "tiers.h"

namespace ng_plus_plus {

// A level per item, 0 for none.
using Levels = std::array<std::uint8_t, kItems>;

// ---- The book's layout ----

// The first line of tiers.txt in the book's layout now: the RYNO II's path Heavy, Alpha to Omega,
// Ultima (README.md, "RYNO II") and the Zodiac's Heavy, Mega, Ultra, Alpha to Omega (0.6.14). A book
// without a marker is layout 1, whose RYNO II went Heavy, Ultima, Alpha to Omega, Omega Prime; one
// marked "ngpp-tiers 2" is layout 2, whose Zodiac went Alpha to Omega. Each is moved over once, as it
// is read. Lines that are no record are skipped by SaveBook::read and kept by the launcher's save
// editor.
inline constexpr std::string_view kBookMarker = "ngpp-tiers 3";
inline constexpr std::string_view kBookMarkerV2 = "ngpp-tiers 2";

// The layout now.
inline constexpr int kBookLayout = 3;

// Returns whether the book `text` has kBookMarker: the layout now.
bool book_current(std::string_view text);

// Returns the layout of the book `text`: 1 with no marker, 2 or kBookLayout by its marker.
int book_layout(std::string_view text);

// Returns the Zodiac's level now for its level `old` in layout 2: none stays none, each step moves
// up by kZodiacOwn (Alpha, the first, is the fourth now), so a Zodiac at Alpha or above keeps its
// tier. The started bit stays as it is.
std::uint8_t zodiac_level_from_old(std::uint8_t old);

// Returns the RYNO II's level now for its level `old` in the layout before: Heavy stays Heavy, the
// old Ultima (2) becomes Alpha, Alpha to Omega keep their tier (3 to 10 become 2 to 9), Omega Prime
// (11) becomes Ultima (10). The started bit stays as it is.
std::uint8_t ryno_level_from_old(std::uint8_t old);

// Moves every record of `book`, read from layout `from` (1 or 2), to the layout now, in their order:
// the RYNO II's levels from layout 1, the Zodiac's from layouts 1 and 2.
void migrate_book(SaveBook& book, int from = 1);

// Returns `book` as tiers.txt keeps it: kBookMarker, then a line a record.
std::string book_text(const SaveBook& book);

// The tiers in play and the book they are kept in.
//
// Holds the mod by reference (it outlives this; the mod's state owns both). Only the game's thread
// uses it.
class SaveTiers {
public:
    // Starts with no tiers; the book is read by load_book.
    explicit SaveTiers(const openrac::Mod& mod) : m_mod(mod) {}

    // Reads the book from the mod's data folder; a missing or unreadable file is an empty book.
    void load_book();

    // ---- The levels ----

    // Returns the level the game plays item `item` at: the party host's in a party, else this
    // save's.
    int level(int item) const;

    // Returns this save's own level of item `item`, the one that steps and is kept.
    int own_level(int item) const { return m_level[static_cast<std::size_t>(item)]; }

    // Returns whether a party host's levels are in play (a guest never steps its own).
    bool party_on() const { return m_party_on; }

    // Takes item `item`'s next step: the game in progress's, kept in the book at the next save.
    void step(int item);

#ifdef NGPP_TEST_SETUP
    // Test runs only (test_setup.h): item `item`'s level in play, kept nowhere.
    void set_level_for_test(int item, int level) {
        m_level[static_cast<std::size_t>(item)] = static_cast<std::uint8_t>(level);
        m_started[static_cast<std::size_t>(item)] = true;
    }
#endif

    // Returns whether the mod counts item `item`'s experience yet, and marks it counted.
    bool started(int item) const { return m_started[static_cast<std::size_t>(item)]; }

    void start(int item) { m_started[static_cast<std::size_t>(item)] = true; }

    // ---- The save's clock ----

    // Looks at the save's clock and data each tick, at the title too: when the clock moved, the
    // levels of the save it names come into play (clock_case_of).
    void watch(const GameView& view);

    // Returns whether the clock seen last is a save's (not all zeros, nor none yet).
    bool clock_known() const { return m_clock_seen && !is_zero(m_clock); }

    // ---- The party ----

    // Takes a party host's rules: `size` bytes at `data`, a level per item; null or short when
    // there are none (the party left).
    void take_party_rules(const void* data, std::uint32_t size);

    // Publishes this game's levels for a party it hosts, once after each change.
    void publish();

    // Marks the levels changed, so publish sends them again.
    void mark_changed() { m_published = false; }

private:
    // The level bytes of the levels and started marks now, as the book keeps them.
    LevelBytes level_bytes() const;

    // Keeps the levels now under `clock` in the book and writes the book.
    void keep(const Clock& clock);

    // Brings in the levels of the save `clock` names; `now` is the save's data now.
    void clock_moved(const Clock& clock, const SavePrint& now, const GameView& view);

    // Brings in the levels and started marks of `levels`, as the book keeps them.
    void take_levels(const LevelBytes& levels);

    // Remembers the save the clock now names as it is now: its data `now` and the levels in play.
    void mark_saved(const SavePrint& now);

    // Writes the book to the mod's data folder, through a new file put in the old one's place.
    void save_book() const;

    // Returns the book's path, "" with no data folder.
    std::string book_path() const;

    const openrac::Mod& m_mod;  // The mod; outlives this.

    Levels m_level{};                      // This save's level per item.
    std::array<bool, kItems> m_started{};  // Whether the mod counts each one's experience.
    Levels m_party{};                      // The party host's levels.
    bool m_party_on = false;               // The host's levels are in play.
    bool m_published = false;              // The levels now have been published.

    SaveBook m_book;             // The book of saves, as tiers.txt has it.
    Clock m_clock{};             // The clock seen last.
    bool m_clock_seen = false;   // A clock has been seen since load.
    bool m_from_nothing = true;  // No save's levels since load or the last zeros.
    SavePrint m_print;           // The save's data at the last tick.
    bool m_print_seen = false;   // m_print holds a tick's data.

    // The save the clock names, as it was when the clock moved to it: its data and levels. The
    // same save loaded again leaves the clock as it is and brings back this data (is_reload).
    bool m_saved_known = false;
    SavePrint m_saved_print;
    LevelBytes m_saved_levels{};

    // The levels in play when the clock went to zeros, and the save's data then: challenge mode's
    // next game carries them on.
    bool m_carried = false;
    Levels m_carry_level{};
    std::array<bool, kItems> m_carry_started{};
    SavePrint m_carry_print;
};

}  // namespace ng_plus_plus
