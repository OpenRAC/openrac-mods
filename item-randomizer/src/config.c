/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The item randomizer's settings (config.h).
 */
#include "config.h"

int rando_settings_of(
    const rando_options* o, const char* remembered, uint64_t fresh, rando_settings* s
) {
    s->pools = (uint8_t)((o->weapons ? POOL_WEAPONS : 0) | (o->gadgets ? POOL_GADGETS : 0)
                         | (o->gold_bolts ? POOL_GOLD : 0) | (o->planets ? POOL_PLANETS : 0)
                         | (o->vendor ? POOL_VENDOR : 0));
    s->keep_start = (uint8_t)(o->keep_start != 0);
    const int d = rando_difficulty_of_name(o->difficulty);
    s->difficulty = (uint8_t)(d < 0 ? DIFFICULTY_STANDARD : d);

    switch (rando_parse_seed_setting(o->seed, s)) {
        case 2:
            return SEED_FROM_CODE;
        case 1:
            return SEED_FROM_WORD;
        default:
            break;
    }
    /* Blank: the seed an earlier blank start rolled (a code's seed only: these settings apply). */
    rando_settings r;

    if (remembered != 0 && rando_decode(remembered, &r)) {
        s->seed = r.seed;
        return SEED_FROM_REMEMBERED;
    }

    s->seed = fresh & RANDO_SEED_MASK;
    return SEED_FROM_NEW;
}
