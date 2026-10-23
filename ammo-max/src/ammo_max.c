/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: ammo at the most you can buy, a mod of OpenRAC's native port (README.md). When
 * the vendor's buy dialog opens on a weapon's ammo, its count starts at the most the player can
 * buy (the bolts they have, the rounds the weapon has room for) instead of the game's 1; Down
 * lowers it as before. Each tick (on_frame), the dialog's state is read; the tick it is first in
 * state 2, the count is set once, to the most Up would reach (ammo_max.h).
 */
#include "ammo_max.h"

#include <stdio.h>
#include <string.h>

#include "openrac/openrac_mod.h"

OPENRAC_MOD_DEFINE("ammo-max", "1.0.0")

static const openrac_api* s_api;
static openrac_mod* s_mod;
static const ammo_max_disc* s_disc;
static int s_trace;
static int s_level = -1; /* the level whose dialog was read last */
static int32_t s_state;  /* its state then */

static int32_t get_s32(openrac_gaddr a) {
    int32_t v;
    memcpy(&v, s_api->memory + a, 4);
    return v;
}

static uint16_t get_u16(openrac_gaddr a) {
    uint16_t v;
    memcpy(&v, s_api->memory + a, 2);
    return v;
}

static void put_s32(openrac_gaddr a, int32_t v) {
    memcpy(s_api->memory + a, &v, 4);
}

static openrac_gaddr level0(uint32_t a) {
    return s_api->relocate_level_data(0, a);
}

/* The dialog has just opened: its count to the most, for a weapon's ammo the game lets Up raise. */
static void start_at_most(openrac_gaddr dialog) {
    if (get_s32(level0(s_disc->can_raise)) != 1) {
        return;
    }

    const int32_t row = get_s32(dialog + 0x58u);

    if (row < 0 || row >= AMMO_MAX_ROWS) {
        return;
    }

    const openrac_gaddr entry = dialog + 0xD0u + (uint32_t)row * AMMO_MAX_ROW_SIZE;
    const int32_t item = get_s32(entry);

    if (get_s32(entry + 4u) != 1 || item < 0 || item >= AMMO_MAX_ITEMS) {
        return;
    }

    const openrac_gaddr record = level0(s_disc->item_table) + (uint32_t)item * AMMO_MAX_ITEM_SIZE;
    const int32_t price = get_u16(record + (get_s32(dialog + 0x40u) != 0 ? 0xAu : 0x8u));
    const int32_t max = get_u16(record + 0xEu);
    const int32_t bolts = get_s32(s_api->relocate_data(s_disc->bolts));
    const int32_t ammo = get_s32(s_api->relocate_data(s_disc->ammo) + 4u * (uint32_t)item);
    const openrac_gaddr count = level0(s_disc->count);
    const int32_t n = ammo_max_count(bolts, price, ammo, max);

    if (n > get_s32(count)) {
        put_s32(count, n);
    }

    if (s_trace) {
        char line[160];
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

static void tick(void* user) {
    (void)user;
    const int level = s_api->loaded_level();

    if (level < 0) {
        s_level = -1;
        return;
    }

    const openrac_gaddr dialog = level0(s_disc->dialog);
    const int32_t state = get_s32(dialog + 0x5Cu);

    if (level != s_level) {
        s_level = level;
        s_state = state;

        if (state != 1) {
            return;
        }
    }

    if (state == 2 && s_state != 2) {
        start_at_most(dialog);
    }

    s_state = state;
}

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    s_disc = NULL;
    s_level = -1;
    s_state = 0;

    if (strcmp(api->game, "rac1") != 0) {
        api->fail(mod, "ammo-max is for Ratchet & Clank (rac1)");
        return 1;
    }

    for (size_t i = 0; i < sizeof k_ammo_max_discs / sizeof k_ammo_max_discs[0]; ++i) {
        if (strcmp(api->version, k_ammo_max_discs[i].version) == 0) {
            s_disc = &k_ammo_max_discs[i];
        }
    }

    if (s_disc == NULL) {
        char reason[96];
        snprintf(
            reason, sizeof reason, "ammo-max knows the PAL and NTSC-U discs, not %s", api->version
        );
        api->fail(mod, reason);
        return 1;
    }

    s_trace = api->setting_bool(mod, "trace");

    if (api->on_frame(mod, tick, NULL) != 0) {
        api->fail(mod, "no tick callback in this OpenRAC");
        return 1;
    }

    return 0;
}
