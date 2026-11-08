/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The item randomizer's entry: one library for every game it plays (docs/MULTI_GAME.md), handing
 * the load to the game's own part by openrac_api.game.
 */
#include <stdio.h>
#include <string.h>

#include "mod.h"

OPENRAC_MOD_DEFINE(ITEM_RANDOMIZER_ID, ITEM_RANDOMIZER_VERSION)

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    if (strcmp(api->game, "rac1") == 0) {
        return rac1_mod_load(api, mod);
    }

    if (strcmp(api->game, "rac2") == 0) {
        return rac2_mod_load(api, mod);
    }

    char reason[96];
    snprintf(reason, sizeof reason, "the item randomizer does not play %s", api->game);
    api->fail(mod, reason);
    return 1;
}
