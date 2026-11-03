/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank in Archipelago: the ids the multiworld knows this game's items and locations by.
 * The mod and the APWorld agree on them: the APWorld reads them from the table tools/dump_table.c
 * prints, and that tool uses these functions.
 *
 * An id is built from what the game itself calls the thing, never from a row's place in logic.c's
 * table, so a new row there does not move the others:
 *   item      AP_BASE + the game's item id           (GiveItem's ids, 0x02..0x23)
 *   planet    AP_BASE + 0x100 + the planet           (UnlockPlanet's, the levels 0..18)
 *   gold bolt AP_BASE + 0x200
 *   location  AP_BASE + 0x10000 + kind << 12 | level << 8 | key   (rando.h's LOC_*)
 */
#ifndef AP_IDS_H
#define AP_IDS_H

#include <stdint.h>

#include "rando.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's name in the multiworld: the APWorld's `game`. */
#define AP_GAME "Ratchet & Clank"

/* The first id of this game ("RC" in ASCII, then zeroes). */
#define AP_BASE INT64_C(0x52430000)
#define AP_PLANET_OFFSET 0x100
#define AP_GOLD_BOLT_OFFSET 0x200
#define AP_LOCATION_OFFSET 0x10000

/* The id of a reward as an item of the multiworld; -1 for none. */
static inline int64_t ap_item_id(rando_reward r) {
    switch (r.kind) {
        case REWARD_ITEM:
            return AP_BASE + r.value;
        case REWARD_PLANET:
            return AP_BASE + AP_PLANET_OFFSET + r.value;
        case REWARD_GOLD_BOLT:
            return AP_BASE + AP_GOLD_BOLT_OFFSET;
        default:
            return -1;
    }
}

/* The reward an item id of the multiworld stands for; kind REWARD_NONE for one not this game's. */
static inline rando_reward ap_reward_of(int64_t id) {
    rando_reward r = {REWARD_NONE, 0};
    const int64_t v = id - AP_BASE;

    // Each range of ids is one kind of reward.
    if (v >= 0 && v < RANDO_ITEM_COUNT) {
        r.kind = REWARD_ITEM;
        r.value = (uint8_t)v;
    } else if (v >= AP_PLANET_OFFSET && v < AP_PLANET_OFFSET + LEVEL_COUNT) {
        r.kind = REWARD_PLANET;
        r.value = (uint8_t)(v - AP_PLANET_OFFSET);
    } else if (v == AP_GOLD_BOLT_OFFSET) {
        r.kind = REWARD_GOLD_BOLT;
    }

    return r;
}

/* The id of a location of logic.c's table as a location of the multiworld: its kind, level and
 * key each in their own bits (the levels reach 0x12, past four bits, so the kind is above 16). */
static inline int64_t ap_location_id(const rando_location* l) {
    return AP_BASE + AP_LOCATION_OFFSET + ((int64_t)l->kind << 16) + ((int64_t)l->level << 8)
           + l->key;
}

/* Whether a row of the table is a location of the multiworld: every row but the fixed ones (an
 * NPC's coordinates, the Persuader, Drek), which the APWorld has as events. */
static inline int ap_is_location(const rando_location* l) {
    return l->kind != LOC_FIXED;
}

#ifdef __cplusplus
}
#endif

#endif /* AP_IDS_H */
