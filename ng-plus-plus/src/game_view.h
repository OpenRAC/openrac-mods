// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Typed access to what NG++ reads and writes of the game: the item table's rows, the items' slots,
// experience and ammo, the hero, the vendor's list, the save's clock and data, and the game's texts
// with the port's text slots. Every address comes from the disc's row of discs.h.
//
// Nothing here knows the tiers; weapons.h joins the two.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "discs.h"
#include "openrac/mod.hpp"
#include "save_book.h"

namespace ng_plus_plus {

// One row of the item table: one version of one item.
//
// A view: it owns nothing, and the game's memory lasts for the program. Copyable.
class Row {
public:
    // Views row `index`, which starts at game address `address` in `memory`.
    Row(openrac::Memory memory, openrac::gaddr address, int index)
        : m_memory(memory),
          m_address(address),
          m_index(index) {}

    // Returns the row's index in the item table.
    int index() const { return m_index; }

    // Returns the row's version, from 0.
    int version() const { return m_memory.read<std::uint8_t>(m_address + kRowVersion); }

    // Returns the next version's row index, 0 at the last.
    int next() const { return m_memory.read<std::int16_t>(m_address + kRowNext); }

    // Returns the version before's row index, 0 at the first.
    int parent() const { return m_memory.read<std::int16_t>(m_address + kRowParent); }

    // Returns whether no version follows this one.
    bool is_last() const { return m_memory.read<std::uint16_t>(m_address + kRowNext) == 0; }

    // Returns the class the vendor shows for the row.
    std::int32_t vendor_class() const { return m_memory.read<std::int32_t>(m_address + kRowClass); }

    // Returns the row's price in bolts.
    std::int32_t price() const { return m_memory.read<std::int32_t>(m_address + kRowPrice); }

    // Returns the most ammo, and writes it.
    std::uint16_t ammo_max() const { return m_memory.read<std::uint16_t>(m_address + kRowAmmoMax); }

    void set_ammo_max(std::uint16_t most) const { m_memory.write(m_address + kRowAmmoMax, most); }

    // Returns the experience (>> 5) to the next version (<= 0: a full bar), and writes it.
    std::int32_t exp_goal() const { return m_memory.read<std::int32_t>(m_address + kRowExpGoal); }

    void set_exp_goal(std::int32_t goal) const { m_memory.write(m_address + kRowExpGoal, goal); }

    // Returns the text id the row holds in `field`.
    std::int32_t text_id(const RowTextField& field) const;

    // Returns the text id of the row's name (+0x48), and of its label (+0x08).
    std::int32_t name_id() const { return m_memory.read<std::int16_t>(m_address + kRowName); }

    std::int32_t label_id() const { return m_memory.read<std::int32_t>(m_address + kRowLabel); }

    // Returns whether one of the row's text fields (kRowTextFields) holds `id`. Never for id 0,
    // which the game's lookups use for "no text".
    bool has_text(std::int32_t id) const;

private:
    openrac::Memory m_memory;  // The game's memory; not owned, lasts for the program.
    openrac::gaddr m_address;  // The row's first byte.
    int m_index;               // Its index in the item table.
};

// A vendor list entry's fields.
struct VendorEntry {
    std::int32_t item = -1;  // The item it sells.
    bool upgrade = false;    // An upgrade (its +0x14 is 1), not the item or its ammo.
};

// The game as NG++ sees it on one disc: typed reads and writes of its memory, its text lookup and
// the port's text slots.
//
// Holds the mod and the disc by reference; both outlive it (the mod's state, ng_plus_plus.cpp,
// owns it). Only the game's thread uses it.
class GameView {
public:
    // The game's functions NG++ calls, by their signatures.
    using TextFn = std::int32_t (*)(std::int32_t);
    using BannerFn = std::int32_t (*)(std::int32_t, std::int32_t);
    using MessageFn = void (*)(std::int32_t, std::int32_t);
    using UpgradeFxFn = void (*)(std::int32_t, std::int32_t);

    // Views the game of `mod` on `disc`, finding the game's functions by the disc's names.
    GameView(const openrac::Mod& mod, const Disc& disc);

    // Returns the mod, for logging and the port's calls the view does not wrap.
    const openrac::Mod& mod() const { return m_mod; }

    // Returns the game's memory.
    openrac::Memory memory() const { return m_mod.memory(); }

    // Returns the level loaded now, -1 at the title screen and in the galaxy map.
    int loaded_level() const { return m_mod.api().loaded_level(); }

    // ---- The items ----

    // Returns row `index` of the item table.
    Row row(int index) const;

    // Returns item `item`'s row now, and its index (its slot).
    Row current_row(int item) const { return row(slot(item)); }

    int slot(int item) const;

    // Returns whether item `item` is owned.
    bool owned(int item) const;

    // Returns item `item`'s experience as the game compares it (>> 5).
    std::int32_t experience(int item) const { return raw_experience(item) >> 5; }

    // Returns item `item`'s experience as the game keeps it (<< 5).
    std::int32_t raw_experience(int item) const;

    // Sets item `item`'s experience back to 0.
    void clear_experience(int item) const;

    // Sets item `item`'s ammo to `rounds`.
    void set_ammo(int item, std::int32_t rounds) const;

    // ---- The hero ----

    // Returns the item in the hero's hand (-1 for none).
    int held() const;

    // Returns whether the hero can play a weapon's level-up now.
    bool hero_ready() const;

    // Returns the hero's moby.
    std::uint32_t hero_moby() const;

    // Returns the class of the moby at game address `moby`, or none when it is no game memory.
    std::optional<int> moby_class(std::uint32_t moby) const;

    // ---- The save ----

    // Returns the save's clock.
    Clock clock() const;

    // Returns the save's data the book compares: the bolts, each item's experience and row.
    SavePrint print() const;

    // Returns the challenge mode counter of the game in progress.
    int cycle() const;

    // Return the player's bolts, Tabora's crystals and Grelbin's moonstones held.
    std::int32_t bolts() const;

    std::int32_t crystals() const;

    std::int32_t moonstones() const;

    // ---- Bolts ----

    // Returns the kill streak's multiplier (1 to 20; 0 before the first kill counts).
    int streak() const;

    // Adds `bolts` to the game's word of bolts a burst could not make into coins (where the game's
    // burst puts a coin limit's rest, and its coin maker a coin it could not make).
    void add_bolt_credit(std::int32_t bolts) const;

    // Returns the own bolts of the moby at game address `moby` when it is an enemy (its class
    // record's kind), else none: a crate, a scripted burst's moby, no moby.
    std::optional<std::int32_t> enemy_bolts(std::uint32_t moby) const;

    // ---- The vendor ----

    // Returns the vendor's cursor, and its list's count.
    int vendor_cursor() const;

    int vendor_count() const;

    void set_vendor_count(int count) const;

    // Returns the vendor's entry at `place` of its list.
    VendorEntry vendor_entry(int place) const;

    // Writes an upgrade entry for `item` at `place`, as the game makes its Mega entry
    // (func_002F6608): the preview's class from `row`.
    void put_upgrade_entry(int place, int item, const Row& row) const;

    // Moves the entries from `place` on (`count` in the list) one place down, making room at it.
    void open_vendor_place(int place, int count) const;

    // ---- Texts ----

    // Returns the game's string of text id `id`, "" for none. The mod's own text_by_id leaves the
    // lookup to the game while it runs (looking_up_own).
    std::string text(std::int32_t id) const;

    // Returns whether the view is inside a lookup of its own (text, slot_address), so the mod's
    // replacement of the lookup goes straight on to the game's.
    bool looking_up_own() const { return m_own_lookups > 0; }

    // Writes `text` into the port's text slot `slot` (0 to hooks::kTextSlots - 1), cut to fit.
    // Returns the slot's game address, what the lookup answers with, or none when the port has no
    // slots.
    std::optional<std::uint32_t> write_slot(int slot, std::string_view text);

    // Returns whether the port has its text slots (a port with the text hook point).
    bool has_slots() const;

    // ---- The game's functions ----

    // Return the game's level-up popup, message line and in-hand level-up; null when this build
    // has none.
    BannerFn banner() const { return m_banner; }

    MessageFn message() const { return m_message; }

    UpgradeFxFn upgrade_fx() const { return m_upgrade_fx; }

private:
    // Returns `address` relocated for the program loaded now.
    openrac::gaddr at(std::uint32_t address) const;

    // Returns the address of vendor entry `place`.
    openrac::gaddr entry_address(int place) const;

    // Returns the game address of the port's text slot `slot`, or none.
    std::optional<std::uint32_t> slot_address(int slot) const;

    const openrac::Mod& m_mod;      // The mod; owns the port's table, outlives the view.
    const Disc& m_disc;             // The disc's addresses; a row of kDiscs.
    TextFn m_text;                  // The port's text lookup (null: none in this build).
    BannerFn m_banner;              // The level-up popup (null: none).
    MessageFn m_message;            // The message line (null: none).
    UpgradeFxFn m_upgrade_fx;       // The in-hand level-up (null: none).
    mutable int m_own_lookups = 0;  // The view's own text lookups under way.
    bool m_slot_logged = false;     // The first slot write has been logged.
};

}  // namespace ng_plus_plus
