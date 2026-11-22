// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The book of saves (save_book.h): its records, their text form and the clock's cases.

#include "save_book.h"

#include <algorithm>

namespace ng_plus_plus {
namespace {

// The hex digits a record is written in.
constexpr char kHexDigits[] = "0123456789abcdef";

// The characters of a record's line: the clock's 16 hex digits, a space, the levels' and a newline.
constexpr std::size_t kClockDigits = 2 * std::tuple_size<Clock>::value;
constexpr std::size_t kLineChars = kClockDigits + 1 + 2 * static_cast<std::size_t>(kItems) + 1;

// Returns whether every level of `levels` is 0, the started bits included.
bool all_zero(const LevelBytes& levels) {
    return std::all_of(levels.begin(), levels.end(), [](std::uint8_t level) { return level == 0; });
}

// Returns the value of hex digit `c`, or -1 for none.
int hex_digit(char c) {
    // Digits, then either case of letter.
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    // Lower-case letters, as the book writes them.
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    // Upper-case letters, as a hand-edited book may have them.
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

// Reads `out.size()` bytes of hex from the start of `text` into `out`. False when one is not hex.
template <std::size_t Bytes>
bool read_hex(std::string_view text, std::array<std::uint8_t, Bytes>& out) {
    // Two digits a byte.
    for (std::size_t n = 0; n < Bytes; ++n) {
        const int high = hex_digit(text[2 * n]);
        const int low = high < 0 ? -1 : hex_digit(text[2 * n + 1]);

        // Not a digit: not a record.
        if (low < 0) {
            return false;
        }

        out[n] = static_cast<std::uint8_t>(high * 16 + low);
    }

    return true;
}

// Appends the bytes of `bytes` to `out` as hex.
template <std::size_t Bytes>
void write_hex(const std::array<std::uint8_t, Bytes>& bytes, std::string& out) {
    // Two digits a byte, high first.
    for (const std::uint8_t byte : bytes) {
        out += kHexDigits[static_cast<unsigned>(byte) >> 4u];
        out += kHexDigits[static_cast<unsigned>(byte) & 15u];
    }
}

}  // namespace

const Record* SaveBook::find(const Clock& clock) const {
    // The newest first: a clock is kept once, but look where it was written last.
    for (auto it = m_records.rbegin(); it != m_records.rend(); ++it) {
        // This record's clock.
        if (it->clock == clock) {
            return &*it;
        }
    }

    return nullptr;
}

const Record* SaveBook::latest() const {
    // The newest first, skipping the records with no level.
    for (auto it = m_records.rbegin(); it != m_records.rend(); ++it) {
        // A record with a level.
        if (!all_zero(it->levels)) {
            return &*it;
        }
    }

    return nullptr;
}

void SaveBook::put(const Clock& clock, const LevelBytes& levels) {
    auto it = std::find_if(m_records.begin(), m_records.end(), [&](const Record& record) {
        return record.clock == clock;
    });

    // Never zeros over a record with levels.
    if (it != m_records.end() && all_zero(levels) && !all_zero(it->levels)) {
        return;
    }

    // A record of that clock already: drop it, the new one goes last.
    if (it != m_records.end()) {
        m_records.erase(it);
    }

    // Full: the oldest goes.
    if (m_records.size() == kRecords) {
        m_records.erase(m_records.begin());
    }

    m_records.push_back(Record{clock, levels});
}

std::string SaveBook::write() const {
    std::string text;
    text.reserve(m_records.size() * kLineChars);

    // One line a record, oldest first.
    for (const Record& record : m_records) {
        write_hex(record.clock, text);
        text += ' ';
        write_hex(record.levels, text);
        text += '\n';
    }

    return text;
}

std::size_t SaveBook::read(std::string_view text) {
    m_records.clear();

    // A line at a time; the last may have no newline.
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        Clock clock{};
        LevelBytes levels{};

        // A line of the right length and digits is a record; anything else is skipped.
        const bool long_enough = line.size() >= kLineChars - 1;

        // Both hex runs read: a record.
        if (long_enough && line[kClockDigits] == ' ' && read_hex(line, clock)
            && read_hex(line.substr(kClockDigits + 1), levels)) {
            put(clock, levels);
        }

        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
    }

    return m_records.size();
}

int print_changes(const SavePrint& a, const SavePrint& b) {
    int changes = a.bolts != b.bolts ? 1 : 0;

    // An item differs in its experience or its row.
    for (std::size_t n = 0; n < kItems; ++n) {
        changes += a.exp[n] != b.exp[n] || a.row[n] != b.row[n] ? 1 : 0;
    }

    return changes;
}

bool is_reload(
    const SavePrint& before,
    const SavePrint& now,
    const SavePrint& saved,
    const LevelBytes& live,
    const LevelBytes& saved_levels
) {
    return now != before && now == saved && live != saved_levels;
}

bool is_zero(const Clock& clock) {
    return std::all_of(clock.begin(), clock.end(), [](std::uint8_t byte) { return byte == 0; });
}

ClockVerdict clock_case_of(const SaveBook& book, const Clock& clock, const ClockMove& move) {
    // Zeros: the title or a new game, nothing to look up.
    if (is_zero(clock)) {
        return {ClockCase::Zero, nullptr};
    }

    const Record* record = book.find(clock);

    // A clock the book has: that save.
    if (record != nullptr) {
        return {ClockCase::Record, record};
    }

    // A clock the book has not: the save in play stamped again, if its data is as it was.
    if (move.in_play) {
        return {move.changes < kLoadChanges ? ClockCase::Same : ClockCase::New, nullptr};
    }

    // After zeros: challenge mode's next game keeps the weapons (seen in game code:
    // level_restart_keep_progress, func_002DE990: their rows and experience, the counter +1); a
    // new game or another save does not.
    if (move.carried && move.cycle > 0 && move.carry_changes < kLoadChanges) {
        return {ClockCase::Carry, nullptr};
    }

    return {ClockCase::New, nullptr};
}

}  // namespace ng_plus_plus
