/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Boldan Nanotech at any hour, a mod of OpenRAC's native port for Going Commando (README.md): the
 * Nanotech Upgrade on Boldan, which the game shows only on an even hour of the console's clock,
 * can be collected at any hour. The mod takes the clock's update (the hook point
 * rac2.moby.boldan_clock.update, Moby_BoldanClock_Update), runs the game's own and then, while the
 * clock's hour is odd, shows the upgrade again as the clock itself does on an even hour; so it is
 * never drawn hidden. A port without that hook point finds the clock in the moby array (boldan.h)
 * and does the same right after its update (the moby gate, on_moby_update), or, without the gate
 * or with another mod holding it, on the tick (on_frame), a frame later. The clock, its hands and
 * its chimes are the game's own throughout.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "openrac/openrac_mod.h"

#include "boldan.h"

OPENRAC_MOD_DEFINE("boldan-nanotech", "1.0.0")

static const openrac_api* s_api;
static openrac_mod* s_mod;
static const bn_disc* s_disc;
static uint32_t s_clock;   /* the clock's moby, 0 until found on this Boldan load */
static int s_clock_slot;   /* its slot, as the gate names it; -1 for none */
static uint32_t s_first;   /* the moby array it was found in */
static int s_said;         /* the clock's finding said once a load */
/* The game's Moby_BoldanClock_Update, the hook point's original. */
static void (*s_clock_update)(uint32_t);

static void forget(void) {
    s_clock = 0;
    s_clock_slot = -1;
    s_first = 0;
    s_said = 0;
}

/* The clock of the level loaded now, found again when the moby array moved; 0 off Boldan. */
static uint32_t clock_now(uint32_t* first, uint32_t* count) {
    if (s_api->loaded_level() != BN_LEVEL_BOLDAN
        || !bn_mobys(s_api->memory, s_api->relocate_data(s_disc->moby_table), first, count)) {
        forget();
        return 0;
    }
    if (s_clock != 0 && *first == s_first && s_clock < *first + *count * BN_MOBY_SIZE
        && bn_upgrade_of(s_api->memory, *first, *count, s_clock) != 0) {
        return s_clock;
    }
    const int index = bn_find_clock(s_api->memory, *first, *count);
    if (index < 0) {
        forget();
        return 0;
    }
    s_first = *first;
    s_clock = *first + (uint32_t)index * BN_MOBY_SIZE;
    s_clock_slot = s_api->size >= offsetof(openrac_api, moby_slot_of) + sizeof(void*)
                           && s_api->moby_slot_of != NULL
                       ? s_api->moby_slot_of(s_clock)
                       : -1;
    if (s_clock_slot < 0) {
        s_clock_slot = index;
    }
    if (!s_said) {
        s_said = 1;
        char line[96];
        snprintf(line, sizeof line, "Boldan's clock found (moby %d): its Nanotech Upgrade stays out",
                 index);
        s_api->log(s_mod, OPENRAC_LOG_INFO, line);
    }
    return s_clock;
}

/* Shows the upgrade if the clock's hour hides it. */
static void keep_shown(uint32_t clock, uint32_t first, uint32_t count) {
    if (clock != 0 && bn_hour_hides(s_api->memory, clock)) {
        const uint32_t upgrade = bn_upgrade_of(s_api->memory, first, count, clock);
        if (upgrade != 0) {
            bn_show(s_api->memory, upgrade);
        }
    }
}

static void tick(void* user) {
    (void)user;
    uint32_t first = 0, count = 0;
    const uint32_t clock = clock_now(&first, &count); /* before first and count are read */
    keep_shown(clock, first, count);
}

/* The clock's update, the hook point's replacement: the game's own, then the upgrade shown. */
static void clock_hook(uint32_t clock) {
    s_clock_update(clock);
    uint32_t first = 0, count = 0;
    if (bn_mobys(s_api->memory, s_api->relocate_data(s_disc->moby_table), &first, &count)
        && clock >= first && clock < first + count * BN_MOBY_SIZE) {
        keep_shown(clock, first, count);
    }
}

/* After the clock's own update: before the frame is drawn. */
static int gate(void* user, int slot, int phase) {
    (void)user;
    if (phase == OPENRAC_MOBY_AFTER && slot == s_clock_slot && s_clock != 0) {
        uint32_t first = 0, count = 0;
        const uint32_t clock = clock_now(&first, &count); /* before first and count are read */
        keep_shown(clock, first, count);
    }
    return 0;
}

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    forget();
    if (strcmp(api->game, "rac2") != 0) {
        api->fail(mod, "boldan-nanotech is for Going Commando (rac2)");
        return 1;
    }
    s_disc = bn_disc_of(api->version);
    if (s_disc == NULL) {
        char reason[128];
        snprintf(reason, sizeof reason,
                 "boldan-nanotech knows NTSC-U v1.01 (ntsc), PAL (pal) and NTSC-U v2.00 (ntsc2), not %s",
                 api->version);
        api->fail(mod, reason);
        return 1;
    }
    const int hooked = api->hook(mod, "rac2.moby.boldan_clock.update", "void(u32)",
                                 (void*)clock_hook, (void**)&s_clock_update);
    if (hooked == OPENRAC_HOOK_OK) {
        return 0;
    }
    char line[112];
    snprintf(line, sizeof line,
             "Boldan's clock is no hook point of this OpenRAC's (%d): the mod finds the clock itself",
             hooked);
    api->log(mod, OPENRAC_LOG_INFO, line);
    if (api->on_frame(mod, tick, NULL) != 0) {
        api->fail(mod, "no tick callback in this OpenRAC");
        return 1;
    }
    const int has_gate = api->size >= offsetof(openrac_api, on_moby_update) + sizeof(void*)
                         && api->on_moby_update != NULL;
    if (!has_gate || api->on_moby_update(mod, gate, NULL) != OPENRAC_HOOK_OK) {
        api->log(mod, OPENRAC_LOG_INFO,
                 "the moby gate is not this mod's: the upgrade is shown again each tick instead");
    }
    return 0;
}
