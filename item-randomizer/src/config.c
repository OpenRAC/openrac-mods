/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The item randomizer's settings (config.h). One function turns the options the launcher read
 * from openrac-mod.json into the rando_settings the placement is made from: the pools, the
 * difficulty and the seed. A seed code in the "seed" setting overrides the other options, because
 * the code already carries them; a blank seed is the one rolled on an earlier blank start, or a
 * new one.
 */
#include "config.h"

#include <string.h>

/* The starting_weapons choice into the settings; 0 when it names none. */
static int starting_weapons_of(const char* name, rando_settings* s) {
    static const struct {
        const char* name;
        uint8_t keep_start, start_weapons;
    } k_choices[] = {
        {"vanilla", 1, RANDO_START_WEAPONS_NONE},
        {"shuffled", 0, RANDO_START_WEAPONS_NONE},
        {"non_broken", 1, RANDO_START_WEAPONS_NON_BROKEN},
        {"unrestricted", 1, RANDO_START_WEAPONS_UNRESTRICTED},
    };
    for (size_t i = 0; name != NULL && i < sizeof k_choices / sizeof k_choices[0]; ++i) {
        if (strcmp(name, k_choices[i].name) == 0) {
            s->keep_start = k_choices[i].keep_start;
            s->start_weapons = k_choices[i].start_weapons;
            return 1;
        }
    }
    return 0;
}

/* The settings the options give; returns SEED_FROM_* (see config.h). */
int rando_settings_of(
    const rando_options* o, const char* remembered, uint64_t fresh, rando_settings* s
) {
    s->pools = (uint8_t)((o->weapons ? POOL_WEAPONS : 0) | (o->gadgets ? POOL_GADGETS : 0)
                         | (o->gold_bolts ? POOL_GOLD : 0) | (o->planets ? POOL_PLANETS : 0)
                         | (o->vendor ? POOL_VENDOR : 0)
                         | (o->gold_weapons ? POOL_GOLD_WEAPONS : 0)
                         | (o->npc_infobots && o->planets ? POOL_NPC_INFOBOTS : 0)
                         | (o->items ? POOL_ITEMS : 0));
    s->legacy_items = 0;
    s->random_start = (uint8_t)(o->random_start != 0);
    s->keep_start = (uint8_t)(o->keep_start != 0);
    s->start_weapons = RANDO_START_WEAPONS_NONE;
    starting_weapons_of(o->starting_weapons, s);
    s->gold_progression = (uint8_t)(o->gold_progression != 0 && o->gold_bolts);
    const int d = rando_difficulty_of_name(o->difficulty);
    s->difficulty = (uint8_t)(d < 0 ? DIFFICULTY_STANDARD : d);

    // A code or a word in the seed setting decides the seed (a code the settings too); a blank
    // setting falls through to the remembered or fresh seed.
    switch (rando_parse_seed_setting(o->seed, s)) {
        case 2:
            return SEED_FROM_CODE;
        case 1:
            return SEED_FROM_WORD;
        default:
            break;
    }

    // Blank: the seed an earlier blank start rolled (a code's seed only: these settings apply).
    rando_settings r;

    // A remembered seed that is a valid code is used again, so that a blank setting plays the same
    // game on every start; anything else in seed.txt is ignored.
    if (remembered != 0 && rando_decode(remembered, &r)) {
        s->seed = r.seed;
        return SEED_FROM_REMEMBERED;
    }

    s->seed = fresh & RANDO_SEED_MASK;
    return SEED_FROM_NEW;
}
