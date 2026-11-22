// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The tiers in play and their book (save_tiers.h): the clock's watch, the file, the party's levels.

#include "save_tiers.h"

#include "perk_rules.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace ng_plus_plus {
namespace {

namespace fs = std::filesystem;

// The book's file in the mod's data folder.
constexpr const char* kBookFile = "tiers.txt";

// The longest book NG++ reads: kRecords lines and room to spare.
constexpr std::size_t kBookLimit = kRecords * 160;

// The names of the clock's cases, for the log, in ClockCase's order.
constexpr const char* kCaseNames[] =
    {"zeros", "the book's", "the save in play's", "carried on", "none: a save never seen"};

}  // namespace

bool book_current(std::string_view text) {
    // The marker's line, wherever it is (the launcher's editor keeps it in its place).
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);

        // A line written on Windows ends in a carriage return.
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        // Found.
        if (line == kBookMarker) {
            return true;
        }

        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
    }

    return false;
}

std::uint8_t ryno_level_from_old(std::uint8_t old) {
    // The old layout: 0 none, 1 Heavy, 2 Ultima, 3 Alpha to 10 Omega, 11 Omega Prime.
    constexpr int kOldUltima = 2;
    const auto started = static_cast<std::uint8_t>(old & kStartedBit);
    const int level = static_cast<int>(old & kLevelBits);

    // None and Heavy stay; the old Ultima is Alpha now; the tiers and Omega Prime move down one.
    const int now = level < kOldUltima ? level : (level == kOldUltima ? kOldUltima : level - 1);
    return static_cast<std::uint8_t>(now | started);
}

void migrate_book(SaveBook& book) {
    constexpr std::size_t kRyno = 44;
    SaveBook moved;

    // Oldest first, so the order stays.
    for (const Record& record : book.records()) {
        LevelBytes levels = record.levels;
        levels[kRyno] = ryno_level_from_old(levels[kRyno]);
        moved.put(record.clock, levels);
    }

    book = moved;
}

std::string book_text(const SaveBook& book) {
    std::string text(kBookMarker);
    text += '\n';
    text += book.write();
    return text;
}

void SaveTiers::load_book() {
    const std::string path = book_path();
    std::ifstream file(path, std::ios::binary);

    // No folder or no file yet: an empty book.
    if (path.empty() || !file) {
        m_book.read({});
        return;
    }

    std::string text;
    text.resize(kBookLimit);
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    m_book.read(text);

    // Closed before any rewrite: Windows cannot replace a file still open.
    file.close();

    // A book of the layout before: moved over once, and written at once with the marker, so it is
    // never moved again.
    if (!book_current(text)) {
        migrate_book(m_book);
        m_mod.info("tiers.txt: the RYNO II's levels moved to its path Heavy, Alpha to Omega, Ultima");
        save_book();
    }
}

int SaveTiers::level(int item) const {
    const auto n = static_cast<std::size_t>(item);
    return m_party_on ? m_party[n] : m_level[n];
}

void SaveTiers::step(int item) {
    ++m_level[static_cast<std::size_t>(item)];

    // Kept at once, not only at the next save: a restart (a new version of the mod) loses nothing.
    // A guest's levels are the host's, never kept.
    if (clock_known() && !m_party_on) {
        keep(m_clock);
    }
}

LevelBytes SaveTiers::level_bytes() const {
    LevelBytes bytes{};

    // Each item's level with its started bit.
    for (std::size_t n = 0; n < kItems; ++n) {
        const std::uint8_t started = m_started[n] ? kStartedBit : 0;
        bytes[n] = static_cast<std::uint8_t>(m_level[n] | started);
    }

    return bytes;
}

void SaveTiers::keep(const Clock& clock) {
    const LevelBytes kept = level_bytes();
    const Record* record = m_book.find(clock);
    const bool any = kept != LevelBytes{};

    // Nothing yet for a clock the book has not: no record (a save never seen has none either).
    // Nothing changed for one it has: no write.
    if (record == nullptr ? !any : record->levels == kept) {
        return;
    }

    m_book.put(clock, kept);
    save_book();
}

std::string SaveTiers::book_path() const {
    const char* dir = m_mod.api().data_dir(m_mod.handle());

    // A port that gives no folder keeps nothing.
    if (dir == nullptr) {
        return {};
    }

    return (fs::path(dir) / kBookFile).string();
}

void SaveTiers::save_book() const {
    const std::string path = book_path();

    // Nowhere to keep it.
    if (path.empty()) {
        return;
    }

    const std::string temp = path + ".new";
    const std::string text = book_text(m_book);
    std::error_code error;

    // Written whole to the new file first, so a crash never leaves half a book.
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();

        // A file that could not be written is dropped; the old book stays.
        if (!file) {
            fs::remove(temp, error);
            m_mod.warn("cannot write " + temp);
            return;
        }
    }

    fs::rename(temp, path, error);

    // The old book stays when the new one cannot take its place.
    if (error) {
        m_mod.warn("cannot replace " + path + ": " + error.message());
    }
}

void SaveTiers::watch(const GameView& view) {
    const SavePrint now = view.print();
    const Clock clock = view.clock();

    // A load, a save or a new game.
    if (!m_clock_seen || clock != m_clock) {
        m_clock = clock;
        m_clock_seen = true;
        clock_moved(clock, now, view);
        m_published = false;
    }

    m_print = now;
    m_print_seen = true;
}

void SaveTiers::clock_moved(const Clock& clock, const SavePrint& now, const GameView& view) {
    // With nothing to compare against, as many changes as a load makes.
    constexpr int kUnknown = kItems + 1;

    ClockMove move;
    move.in_play = !m_from_nothing;
    move.changes = m_print_seen ? print_changes(m_print, now) : kUnknown;
    move.carried = m_carried;
    move.carry_changes = m_carried ? print_changes(m_carry_print, now) : kUnknown;
    move.cycle = view.cycle();
    const ClockVerdict verdict = clock_case_of(m_book, clock, move);

    m_mod.info(
        std::string("save's clock moved: levels ") + kCaseNames[static_cast<int>(verdict.what)]
        + " (" + std::to_string(move.changes) + " changes, " + std::to_string(move.carry_changes)
        + " since zeros, title " + std::to_string(view.loaded_level() < 0 ? 1 : 0) + ")"
    );

    // Each case brings its own levels.
    switch (verdict.what) {
        // The title or a new game: nothing yet. The levels in play are kept aside for challenge
        // mode's next game, which keeps the weapons.
        case ClockCase::Zero:
            // A save's levels were in play: kept aside.
            if (!m_from_nothing) {
                m_carried = true;
                m_carry_level = m_level;
                m_carry_started = m_started;
                m_carry_print = now;
            }

            m_level = {};
            m_started = {};
            m_from_nothing = true;
            return;
        // That save's levels.
        case ClockCase::Record:
            take_record(*verdict.record);
            break;
        // The save in play stamped again: its levels go on.
        case ClockCase::Same:
            break;
        case ClockCase::Carry:
            m_level = m_carry_level;
            m_started = m_carry_started;
            break;
        // Its own levels only: none, and its weapons' experience counted from the first tick the
        // mod sees them at their last version.
        case ClockCase::New:
            m_level = {};
            m_started = {};
            break;
    }

    m_from_nothing = false;
    m_carried = false;
    keep(clock);
}

void SaveTiers::take_record(const Record& record) {
    // Each item's level, cut to what the weapon has; a level means its count had started.
    for (int item = 0; item < kItems; ++item) {
        const auto n = static_cast<std::size_t>(item);
        const int most = max_level(item);
        const int kept = static_cast<int>(record.levels[n] & kLevelBits);
        m_level[n] = static_cast<std::uint8_t>(kept > most ? most : kept);
        m_started[n] = (record.levels[n] & kStartedBit) != 0 || kept != 0;
    }
}

void SaveTiers::take_party_rules(const void* data, std::uint32_t size) {
    // No rules, or not a level per item: the party is over, this save's levels again.
    if (data == nullptr || size < kItems) {
        m_party_on = false;
        return;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);

    // Each level cut to what the weapon has: a host's newer mod may know more.
    for (int item = 0; item < kItems; ++item) {
        const auto n = static_cast<std::size_t>(item);
        const int most = max_level(item);
        m_party[n] = static_cast<std::uint8_t>(bytes[n] > most ? most : bytes[n]);
    }

    m_party_on = true;
}

void SaveTiers::publish() {
    // A guest publishes nothing; the host only after a change.
    if (m_party_on || m_published) {
        return;
    }

    int tiered = 0;

    // Count the weapons with a tier, for the party's line.
    for (int item = 0; item < kItems; ++item) {
        tiered += item != kArmourItem && m_level[static_cast<std::size_t>(item)] != 0 ? 1 : 0;
    }

    const std::string text = "NG++: " + std::to_string(tiered) + " weapons";
    m_mod.api().party_rules_publish(m_mod.handle(), m_level.data(), kItems, text.c_str());
    m_published = true;
}

}  // namespace ng_plus_plus
