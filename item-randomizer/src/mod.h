/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The item randomizer's entry (mod.c) and each game's load: Ratchet & Clank's (item_randomizer.c
 * and the files beside it) and Going Commando's (rac2/). docs/MULTI_GAME.md.
 */
#ifndef ITEM_RANDOMIZER_MOD_H
#define ITEM_RANDOMIZER_MOD_H

#include "openrac/openrac_mod.h"

#define ITEM_RANDOMIZER_ID "item-randomizer"
#define ITEM_RANDOMIZER_VERSION "2.2.0"

int rac1_mod_load(const openrac_api* api, openrac_mod* mod);
int rac2_mod_load(const openrac_api* api, openrac_mod* mod);

#endif /* ITEM_RANDOMIZER_MOD_H */
