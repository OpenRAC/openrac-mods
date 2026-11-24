// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The book of saves: the tiers NG++ keeps for each save the game made, by the save's clock, and
// the rules that tell which save is in play when that clock moves (README.md, "Each save gets only
// its own tiers").
//
// The game stamps its save's clock (chunk 4 of the save) when it saves, at an autosave and as a
// loaded save's level starts. The clock alone cannot tell a save made from another save loaded
// whose clock the book has not, so the save's data tells: a save made leaves it as it was, a save
// loaded replaces it.
//
// Nothing here reads game memory or files, so tests/tiers_test.cpp runs it alone.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "tiers.h"

namespace ng_plus_plus {

// A save's clock: the 8 bytes of its chunk 4 (seen in game code: D_001A7360 on NTSC-U).
using Clock = std::array<std::uint8_t, 8>;

// A level byte per item, as the book keeps them: the level in its low 7 bits, kStartedBit set once
// the mod counts the weapon's experience.
using LevelBytes = std::array<std::uint8_t, kItems>;

// The bit of a level byte that marks a weapon whose experience the mod counts: from the first tick
// it is seen at its last version, when the game's own count is set back to 0.
inline constexpr std::uint8_t kStartedBit = 0x80u;

// The bits of a level byte that hold the level.
inline constexpr unsigned kLevelBits = 0x7Fu;

// The most records the book keeps; the oldest goes when a new one does not fit.
inline constexpr std::size_t kRecords = 64;

// A save's clock and the level bytes it had then.
struct Record {
    Clock clock{};        // The save's clock.
    LevelBytes levels{};  // Its level bytes, kStartedBit included.

    // Returns whether both records hold the same clock and levels.
    bool operator==(const Record& other) const {
        return clock == other.clock && levels == other.levels;
    }
};

// The records NG++ keeps, newest last, and their text form (tiers.txt in the mod's data folder).
//
// Copyable; a plain list. Only the game's thread uses it.
class SaveBook {
public:
    // Returns the record of `clock`, or null.
    const Record* find(const Clock& clock) const;

    // Returns the newest record with any level (the one written last), or null. Not a way to tell
    // which save is loaded: a save the book has not seen has no levels (clock_case_of).
    const Record* latest() const;

    // Keeps `levels` under `clock`, replacing its record (never one with levels by all zeros) and
    // dropping the oldest when full.
    void put(const Clock& clock, const LevelBytes& levels);

    // Returns the book as text: a line a record, 16 hex digits of clock, a space and 112 of levels.
    std::string write() const;

    // Replaces the book with the records of `text`, skipping lines it cannot read. Returns how many
    // it read.
    std::size_t read(std::string_view text);

    // Returns the records, oldest first.
    const std::vector<Record>& records() const { return m_records; }

private:
    // The records, oldest first, at most kRecords.
    std::vector<Record> m_records;
};

// What the save's data the mod compares is: the bolts and raritanium, and each item's experience
// and row.
struct SavePrint {
    std::int32_t bolts = 0;                  // The bolts the save holds.
    std::int32_t raritanium = 0;             // Its raritanium (what the ship's tiers cost).
    std::array<std::int32_t, kItems> exp{};  // Each item's experience as the game keeps it, << 5.
    std::array<std::uint8_t, kItems> row{};  // Each item's row in the item table.

    // Returns whether both prints hold the same data.
    bool operator==(const SavePrint& other) const {
        return bolts == other.bolts && raritanium == other.raritanium && exp == other.exp
               && row == other.row;
    }

    bool operator!=(const SavePrint& other) const { return !(*this == other); }
};

// Returns how many of the bolts and the items (experience or row) differ between `a` and `b` (the
// raritanium left out: the clock's cases were weighed without it).
int print_changes(const SavePrint& a, const SavePrint& b);

// From this many changes in the tick the clock moved, the save's data was replaced: another save
// was loaded. A save made changes nothing (the game is paused or between levels), or at most one
// thing in the same tick.
inline constexpr int kLoadChanges = 2;

// Returns whether the tick from `before` to `now` loaded again the save the clock names, made or
// loaded when its data was `saved` and its levels `saved_levels`: the clock stays as it was (the
// game stamps a new one only as the next level starts), but the data, changed in this tick, is the
// save's again to the last bolt, raritanium and point of experience, while the levels in play
// (`live`) are no longer the save's. A purchase or a step never takes the data back to the save's,
// so only a load does.
bool is_reload(
    const SavePrint& before,
    const SavePrint& now,
    const SavePrint& saved,
    const LevelBytes& live,
    const LevelBytes& saved_levels
);

// What the save's clock moving to a new value means.
enum class ClockCase : std::uint8_t {
    Zero,    // all zeros: the title or a new game; no levels until a clock
    Record,  // a clock the book has: that save's levels
    Same,    // the save in play stamped again: its levels go on under the new clock
    Carry,   // a new game of challenge mode (the weapons kept): the levels before the zeros go on
    New,     // a save the book has never seen: no levels, nothing started
};

// What is known of the game when the clock moves, besides the clock itself.
struct ClockMove {
    bool in_play = false;   // A save's levels are in play (a clock was identified since zeros).
    int changes = 0;        // print_changes between the tick before and now.
    bool carried = false;   // A save's levels were in play when the clock went to zeros.
    int carry_changes = 0;  // print_changes between the save's data at the zeros and now.
    int cycle = 0;          // The challenge mode counter now.
};

// What clock_case_of found: the case, and for ClockCase::Record the book's record.
struct ClockVerdict {
    ClockCase what = ClockCase::Zero;  // What the clock's move means.
    const Record* record = nullptr;    // The book's record of the clock; null unless Record.
};

// Returns what the clock moving to `clock` means, given what `move` says of the game. The record
// returned points into `book` and lives until the book changes.
ClockVerdict clock_case_of(const SaveBook& book, const Clock& clock, const ClockMove& move);

// Returns whether clock `a` is earlier than `b`. The clock is the game's wall time in BCD (seen in
// the book: second, minute, hour, a weekday-like byte, day, month, year), so it is compared year
// first.
bool clock_earlier(const Clock& a, const Clock& b);

// Returns the book's record to carry over to the save `clock`, which the book has not seen, when
// the save's data is `now`: the record with levels whose clock is the closest earlier one (the
// same playthrough's last record, as saves only go forward), or null. Null too for a new game, a
// save whose data shows no weapon experience at all, and when `clock` is in the book or zeros. A
// record with a later clock is never taken: it is another slot's, or a newer save of this one. A
// record also needs a fit: the save shows experience on a weapon the record has a tier on, which a
// save of another slot, with those weapons at none, does not.
const Record* carry_record(const SaveBook& book, const Clock& clock, const SavePrint& now);

// Returns whether `clock` is all zeros: the title screen or a new game before its first stamp.
bool is_zero(const Clock& clock);

}  // namespace ng_plus_plus
