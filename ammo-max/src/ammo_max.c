/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: ammo at the most you can buy, a mod of OpenRAC's native port (README.md). When
 * the vendor's buy dialog opens on a weapon's ammo, its count starts at the most the player can
 * buy (the bolts they have, the rounds the weapon has room for) instead of the game's 1; Down
 * lowers it as before. Each tick (on_frame), the dialog's state is read; the tick it is first in
 * state 2, the count is set once, to the most Up would reach (ammo_max.h).
 *
 * The mod patches no code and takes no hook point. Which disc is running decides where the
 * dialog's words are (the table in ammo_max.h); this file holds the layout of the dialog and of
 * the item records, which is the same on both discs.
 *
 * Settings (openrac-mod.json):
 *   trace  (bool, default off)  For bug reports: a line in the game's log each time the count is
 *                               set, with the weapon's item id, its ammo, the bolts and the price
 *                               of a round.
 */
#include "ammo_max.h"

#include <stdio.h>
#include <string.h>

#include "openrac/openrac_mod.h"

OPENRAC_MOD_DEFINE("ammo-max", "1.0.3")

/* ---- Constants ---- */

/* The offsets below are from the vendor's buy dialog code named in ammo_max.h (seen in game code),
 * the same on the PAL and NTSC-U discs. */

/* In the vendor menu: the s32 that says which price of a round applies; non-zero selects the item
 * record's second price (seen in game code). */
#define AMMO_MAX_DIALOG_PRICE_SELECT 0x40u

/* In the vendor menu: the s32 index of the row the player has chosen (seen in game code). */
#define AMMO_MAX_DIALOG_ROW 0x58u

/* In the vendor menu: the s32 state of the buy dialog, 1 when it opens and 2 while Up and Down
 * move the count (seen in game code). */
#define AMMO_MAX_DIALOG_STATE 0x5Cu

/* In the vendor menu: where the rows start, AMMO_MAX_ROW_SIZE bytes each (seen in game code). */
#define AMMO_MAX_DIALOG_ROWS 0xD0u

/* In a row: the s32 item id (seen in game code). */
#define AMMO_MAX_ROW_ITEM 0u

/* In a row: the s32 that is 1 when the row is the item's ammo (seen in game code). */
#define AMMO_MAX_ROW_IS_AMMO 4u

/* In an item record: the u16 price of a round, used while the dialog's price select is clear (seen
 * in game code). */
#define AMMO_MAX_ITEM_PRICE 0x8u

/* In an item record: the u16 price of a round, used while the dialog's price select is set (seen
 * in game code). */
#define AMMO_MAX_ITEM_PRICE_SELECTED 0xAu

/* In an item record: the u16 most ammo the weapon holds (seen in game code). */
#define AMMO_MAX_ITEM_MAX_AMMO 0xEu

/* The size in bytes of one s32 word, the step of the ammo array (seen in game code). */
#define AMMO_MAX_WORD 4u

/* The dialog state the mod acts on: the state in which Up and Down move the count (seen in game
 * code). */
#define AMMO_MAX_STATE_ADJUSTING 2

/* The dialog state right after it opens, before the count is set (seen in game code). */
#define AMMO_MAX_STATE_OPENING 1

/* The value of can_raise (ammo_max.h) and of a row's is-ammo word when they are set (seen in game
 * code). */
#define AMMO_MAX_FLAG_SET 1

/* The level whose program holds the vendor's code and words: level 0 (seen in game code). */
#define AMMO_MAX_VENDOR_LEVEL 0

/* The size of the log line the trace setting writes, in bytes (assumed). */
#define AMMO_MAX_TRACE_LINE_SIZE 160

/* The size of the reason shown when the disc is not known, in bytes (assumed). */
#define AMMO_MAX_REASON_SIZE 96

/* ---- State ---- */

/* The host's table of calls, from openrac_mod_load. Set on the load thread, read on the game thread
 * in tick. */
static const openrac_api* s_api;

/* This mod's handle for the host's calls (log, fail). Same threads as s_api. */
static openrac_mod* s_mod;

/* The addresses for the running disc, or NULL before a disc is matched. Set at load, read in tick. */
static const ammo_max_disc* s_disc;

/* The trace setting: non-zero when each count set is logged. Set at load, read in tick. */
static int s_trace;

/* The level whose dialog was read last, -1 when no level is loaded. Touched only by tick. */
static int s_level = -1;

/* The dialog's state at that last read. Touched only by tick. */
static int32_t s_state;

/* ---- Helpers ---- */

/* The s32 at a game address. The address must lie in the game's memory. */
static int32_t get_s32(openrac_gaddr a) {
    int32_t v;
    memcpy(&v, s_api->memory + a, 4);
    return v;
}

/* The u16 at a game address. The address must lie in the game's memory. */
static uint16_t get_u16(openrac_gaddr a) {
    uint16_t v;
    memcpy(&v, s_api->memory + a, 2);
    return v;
}

/* Writes an s32 at a game address. The address must lie in the game's memory; call it from the game
 * thread, in tick. */
static void put_s32(openrac_gaddr a, int32_t v) {
    memcpy(s_api->memory + a, &v, 4);
}

/* The game address of a word in level 0's program, as loaded now (the program moves with the
 * level). */
static openrac_gaddr level0(uint32_t a) {
    return s_api->relocate_level_data(AMMO_MAX_VENDOR_LEVEL, a);
}

/* ---- Main logic ---- */

/* The dialog has just opened: its count to the most, for a weapon's ammo the game lets Up raise.
 * Where the game would not let Up raise the count (nothing to buy, or a row that is not ammo), it
 * leaves the game's 1 alone. Call it from the game thread, in tick. */
static void start_at_most(openrac_gaddr dialog) {
    // The game sets this flag only when the player has bolts for a round and room for it; without
    // it Up could not raise the count, so the mod must not either.
    if (get_s32(level0(s_disc->can_raise)) != AMMO_MAX_FLAG_SET) {
        return;
    }

    const int32_t row = get_s32(dialog + AMMO_MAX_DIALOG_ROW);

    // A row outside the table would read memory that is not a row.
    if (row < 0 || row >= AMMO_MAX_ROWS) {
        return;
    }

    const openrac_gaddr entry = dialog + AMMO_MAX_DIALOG_ROWS + (uint32_t)row * AMMO_MAX_ROW_SIZE;
    const int32_t item = get_s32(entry + AMMO_MAX_ROW_ITEM);

    // Only an ammo row with a known item id is bought by count; anything else is left as the game
    // has it.
    if (get_s32(entry + AMMO_MAX_ROW_IS_AMMO) != AMMO_MAX_FLAG_SET || item < 0
        || item >= AMMO_MAX_ITEMS) {
        return;
    }

    const openrac_gaddr record = level0(s_disc->item_table) + (uint32_t)item * AMMO_MAX_ITEM_SIZE;
    const uint32_t price_offset = get_s32(dialog + AMMO_MAX_DIALOG_PRICE_SELECT) != 0
                                      ? AMMO_MAX_ITEM_PRICE_SELECTED
                                      : AMMO_MAX_ITEM_PRICE;
    const int32_t price = get_u16(record + price_offset);
    const int32_t max = get_u16(record + AMMO_MAX_ITEM_MAX_AMMO);
    const int32_t bolts = get_s32(s_api->relocate_data(s_disc->bolts));
    const int32_t ammo =
        get_s32(s_api->relocate_data(s_disc->ammo) + AMMO_MAX_WORD * (uint32_t)item);
    const openrac_gaddr count = level0(s_disc->count);
    const int32_t n = ammo_max_count(bolts, price, ammo, max);

    // Raise the count only; when nothing can be bought n is 0 and the game's 1 stays, and a count
    // the game already has higher is not lowered.
    if (n > get_s32(count)) {
        put_s32(count, n);
    }

    // The trace setting asks for one log line each time the count is set, for bug reports.
    if (s_trace) {
        char line[AMMO_MAX_TRACE_LINE_SIZE];
        snprintf(
            line,
            sizeof line,
            "item %d: %d/%d rounds, %d bolts at %d a round: the count starts at %d",
            (int)item,
            (int)ammo,
            (int)max,
            (int)bolts,
            (int)price,
            (int)get_s32(count)
        );
        s_api->log(s_mod, OPENRAC_LOG_INFO, line);
    }
}

/* The per-frame callback: reads the dialog's state and, the tick it first reaches state 2, starts
 * the count at the most. Runs on the game thread once a frame. */
static void tick(void* user) {
    (void)user;
    const int level = s_api->loaded_level();

    // With no level loaded there is no vendor to read; forget the last level so the next one is
    // read afresh.
    if (level < 0) {
        s_level = -1;
        return;
    }

    const openrac_gaddr dialog = level0(s_disc->dialog);
    const int32_t state = get_s32(dialog + AMMO_MAX_DIALOG_STATE);

    // A new level (or the first tick) has no earlier state to compare with: take the current one,
    // and stop unless the dialog is just opening, so a dialog already open mid-adjust is not
    // counted as newly opened.
    if (level != s_level) {
        s_level = level;
        s_state = state;

        // Only a dialog still in its opening state can be started at the most this tick.
        if (state != AMMO_MAX_STATE_OPENING) {
            return;
        }
    }

    // The dialog has just moved into the state where the count is adjusted: this is the one tick
    // to set it.
    if (state == AMMO_MAX_STATE_ADJUSTING && s_state != AMMO_MAX_STATE_ADJUSTING) {
        start_at_most(dialog);
    }

    s_state = state;
}

/* ---- Entry point ---- */

/* Called by the host when it loads the library: checks the game and the disc, reads the setting and
 * registers the tick callback. Returns 0 on success; on failure it tells the host why through fail
 * and returns 1. */
OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    s_disc = NULL;
    s_level = -1;
    s_state = 0;

    // The addresses are those of Ratchet & Clank's executable; another game would be read at the
    // wrong places.
    if (strcmp(api->game, "rac1") != 0) {
        api->fail(mod, "ammo-max is for Ratchet & Clank (rac1)");
        return 1;
    }

    // Find the table row for the running disc by its version.
    for (size_t i = 0; i < sizeof k_ammo_max_discs / sizeof k_ammo_max_discs[0]; ++i) {
        // A matching version names the disc whose addresses to use.
        if (strcmp(api->version, k_ammo_max_discs[i].version) == 0) {
            s_disc = &k_ammo_max_discs[i];
        }
    }

    // A disc with no table row has no known addresses; refuse to run rather than guess.
    if (s_disc == NULL) {
        char reason[AMMO_MAX_REASON_SIZE];
        snprintf(
            reason, sizeof reason, "ammo-max knows the PAL and NTSC-U discs, not %s", api->version
        );
        api->fail(mod, reason);
        return 1;
    }

    s_trace = api->setting_bool(mod, "trace");

    // The mod works only through the per-frame callback, so a host without one cannot run it.
    if (api->on_frame(mod, tick, NULL) != 0) {
        api->fail(mod, "no tick callback in this OpenRAC");
        return 1;
    }

    return 0;
}
