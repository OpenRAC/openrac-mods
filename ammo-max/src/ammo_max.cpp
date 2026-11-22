// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Ratchet & Clank: ammo at the most you can buy, a mod of OpenRAC's native port (README.md). When
// the vendor's buy dialog opens on a weapon's ammo, its count starts at the most the player can
// buy (the bolts they have, the rounds the weapon has room for) instead of the game's 1; Down
// lowers it as before. Each tick (on_frame), the dialog's state is read; the tick it is first in
// state 2, the count is set once, to the most Up would reach (ammo_max.hpp).
//
// The mod patches no code and takes no hook point. Which disc is running decides where the
// dialog's words are (the table in ammo_max.hpp); this file holds the layout of the dialog and of
// the item records, which is the same on both discs.
//
// Settings (openrac-mod.json):
//   trace  (bool, default off)  For bug reports: a line in the game's log each time the count is
//                               set, with the weapon's item id, its ammo, the bolts and the price
//                               of a round.

#include "ammo_max.hpp"

#include <cstdint>
#include <string>

#include "openrac/mod.hpp"

namespace {

// The offsets below are from the vendor's buy dialog code named in ammo_max.hpp (seen in game
// code), the same on the PAL and NTSC-U discs.

// In the vendor menu: the s32 that says which price of a round applies; non-zero selects the item
// record's second price (seen in game code).
constexpr std::uint32_t kDialogPriceSelect = 0x40u;

// In the vendor menu: the s32 index of the row the player has chosen (seen in game code).
constexpr std::uint32_t kDialogRow = 0x58u;

// In the vendor menu: the s32 state of the buy dialog, 1 when it opens and 2 while Up and Down move
// the count (seen in game code).
constexpr std::uint32_t kDialogState = 0x5Cu;

// In the vendor menu: where the rows start, ammo_max::kRowSize bytes each (seen in game code).
constexpr std::uint32_t kDialogRows = 0xD0u;

// In a row: the s32 item id (seen in game code).
constexpr std::uint32_t kRowItem = 0u;

// In a row: the s32 that is 1 when the row is the item's ammo (seen in game code).
constexpr std::uint32_t kRowIsAmmo = 4u;

// In an item record: the u16 price of a round, used while the dialog's price select is clear (seen
// in game code).
constexpr std::uint32_t kItemPrice = 0x8u;

// In an item record: the u16 price of a round, used while the dialog's price select is set (seen
// in game code).
constexpr std::uint32_t kItemPriceSelected = 0xAu;

// In an item record: the u16 most ammo the weapon holds (seen in game code).
constexpr std::uint32_t kItemMaxAmmo = 0xEu;

// The size in bytes of one s32 word, the step of the ammo array (seen in game code).
constexpr std::uint32_t kWordSize = 4u;

// The dialog state the mod acts on: the state in which Up and Down move the count (seen in game
// code).
constexpr std::int32_t kStateAdjusting = 2;

// The dialog state right after it opens, before the count is set (seen in game code).
constexpr std::int32_t kStateOpening = 1;

// The value of the can-raise word (ammo_max::Disc) and of a row's is-ammo word when they are set
// (seen in game code).
constexpr std::int32_t kFlagSet = 1;

// The level whose program holds the vendor's code and words: level 0 (seen in game code).
constexpr int kVendorLevel = 0;

// Returns the s32 at a game address. The address must lie in the game's memory.
std::int32_t read_s32(const openrac::Mod& mod, openrac::gaddr address) {
    return mod.memory().read<std::int32_t>(address);
}

// Returns the u16 at a game address, widened to an s32 for the arithmetic of the count. The address
// must lie in the game's memory.
std::int32_t read_u16(const openrac::Mod& mod, openrac::gaddr address) {
    return mod.memory().read<std::uint16_t>(address);
}

// Finds the table row for the running disc by its version, or null when the disc has none.
const ammo_max::Disc* find_disc(const openrac::Mod& mod) {
    // The first row whose version names the running disc holds its addresses.
    for (const ammo_max::Disc& disc : ammo_max::kDiscs) {
        // A matching version names the disc whose addresses to use.
        if (mod.version() == disc.version) {
            return &disc;
        }
    }

    return nullptr;
}

// Watches the vendor's buy dialog and starts its count at the most. It is the state of the mod's
// frame callback: the callback owns it, so it lives as long as the mod. Only the game thread
// touches it, once a frame.
class DialogWatcher {
public:
    // Watches the dialog of `disc` through `mod`; `trace` logs each count that is set. `mod` and
    // `disc` outlive the watcher: the mod lives until the program ends and the disc is in a table.
    DialogWatcher(const openrac::Mod& mod, const ammo_max::Disc& disc, bool trace)
        : m_mod(&mod),
          m_disc(&disc),
          m_trace(trace) {}

    // Reads the dialog's state and, the tick it first reaches state 2, starts the count at the
    // most.
    void tick() {
        const int level = m_mod->loaded_level();

        // With no level loaded there is no vendor to read; forget the last level so the next one
        // is read afresh.
        if (level < 0) {
            m_level = -1;

            return;
        }

        const openrac::gaddr dialog = level0(m_disc->dialog);
        const std::int32_t state = read_s32(*m_mod, dialog + kDialogState);

        // A new level (or the first tick) has no earlier state to compare with: take the current
        // one, and stop unless the dialog is opening, so a dialog already open mid-adjust is
        // not counted as newly opened.
        if (level != m_level) {
            m_level = level;
            m_state = state;

            // Only a dialog still in its opening state can be started at the most this tick.
            if (state != kStateOpening) {
                return;
            }
        }

        // The dialog has moved into the state where the count is adjusted: this is the one
        // tick to set it.
        if (state == kStateAdjusting && m_state != kStateAdjusting) {
            start_at_most(dialog);
        }

        m_state = state;
    }

private:
    // Returns the game address of a word in level 0's program, as loaded now (the program moves
    // with the level).
    openrac::gaddr level0(std::uint32_t address) const {
        return m_mod->relocate_level(kVendorLevel, address);
    }

    // Sets the count of the dialog at `dialog` to the most, for a weapon's ammo the game lets Up
    // raise. Where the game would not let Up raise the count (nothing to buy, or a row that is not
    // ammo), it leaves the game's 1 alone. Call it from the game thread, in tick.
    void start_at_most(openrac::gaddr dialog) const {
        // The game sets this flag only when the player has bolts for a round and room for it;
        // without it Up could not raise the count, so the mod must not either.
        if (read_s32(*m_mod, level0(m_disc->can_raise)) != kFlagSet) {
            return;
        }

        const std::int32_t row = read_s32(*m_mod, dialog + kDialogRow);

        // A row outside the table would read memory that is not a row.
        if (row < 0 || row >= ammo_max::kRows) {
            return;
        }

        const openrac::gaddr entry =
            dialog + kDialogRows + static_cast<std::uint32_t>(row) * ammo_max::kRowSize;
        const std::int32_t item = read_s32(*m_mod, entry + kRowItem);

        // Only an ammo row with a known item id is bought by count; anything else is left as the
        // game has it.
        if (read_s32(*m_mod, entry + kRowIsAmmo) != kFlagSet || item < 0
            || item >= ammo_max::kItems) {
            return;
        }

        const std::uint32_t item_index = static_cast<std::uint32_t>(item);
        const openrac::gaddr record = level0(m_disc->item_table) + item_index * ammo_max::kItemSize;

        // The dialog's price select picks which of the record's two prices of a round applies.
        const std::uint32_t price_offset =
            read_s32(*m_mod, dialog + kDialogPriceSelect) != 0 ? kItemPriceSelected : kItemPrice;
        const std::int32_t price = read_u16(*m_mod, record + price_offset);
        const std::int32_t max_ammo = read_u16(*m_mod, record + kItemMaxAmmo);
        const std::int32_t bolts = read_s32(*m_mod, m_mod->relocate(m_disc->bolts));
        const std::int32_t ammo =
            read_s32(*m_mod, m_mod->relocate(m_disc->ammo) + kWordSize * item_index);
        const openrac::gaddr count = level0(m_disc->count);
        const std::int32_t start = ammo_max::start_count(bolts, price, ammo, max_ammo);

        // Raise the count only; when nothing can be bought start is 0 and the game's 1 stays, and
        // a count the game already has higher is not lowered.
        if (start > read_s32(*m_mod, count)) {
            m_mod->memory().write<std::int32_t>(count, start);
        }

        // The trace setting asks for one log line each time the count is set, for bug reports.
        if (m_trace) {
            m_mod->info(
                "item " + std::to_string(item) + ": " + std::to_string(ammo) + "/"
                + std::to_string(max_ammo) + " rounds, " + std::to_string(bolts) + " bolts at "
                + std::to_string(price) + " a round: the count starts at "
                + std::to_string(read_s32(*m_mod, count))
            );
        }
    }

    // The host's view of the game and its calls. Not owned; the mod lives until the program ends.
    const openrac::Mod* m_mod;

    // The addresses for the running disc. Not owned; it is a row of ammo_max::kDiscs.
    const ammo_max::Disc* m_disc;

    // The trace setting: true when each count set is logged.
    bool m_trace;

    // The level whose dialog was read last, -1 when no level is loaded.
    int m_level = -1;

    // The dialog's state at that last read.
    std::int32_t m_state = 0;
};

// Checks the game and the disc, reads the setting and registers the tick callback. The port calls
// it when it loads the library. Throws openrac::Error to refuse to run, with the reason the
// launcher shows.
void load(openrac::Mod& mod) {
    // The addresses are those of Ratchet & Clank's executable; another game would be read at the
    // wrong places.
    if (mod.game() != "rac1") {
        throw openrac::Error("ammo-max is for Ratchet & Clank (rac1)");
    }

    const ammo_max::Disc* disc = find_disc(mod);

    // A disc with no table row has no known addresses; refuse to run rather than guess.
    if (disc == nullptr) {
        throw openrac::Error(
            "ammo-max knows the PAL and NTSC-U discs, not " + std::string(mod.version())
        );
    }

    const bool trace = mod.setting<bool>("trace");

    // The mod works only through the per-frame callback, which owns the watcher and so its state;
    // on_frame throws when the host refuses it.
    mod.on_frame([watcher = DialogWatcher(mod, *disc, trace)]() mutable { watcher.tick(); });
}

}  // namespace

// The integration entry point registers the original dialog watcher for RAC1.
void load_ammo_max(openrac::Mod& mod) { load(mod); }
