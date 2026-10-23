/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The item randomizer's settings (openrac-mod.json's "settings") into its rando_settings, apart
 * from the port so that a test can check them (tests/settings_test.c).
 */
#ifndef ITEM_RANDOMIZER_CONFIG_H
#define ITEM_RANDOMIZER_CONFIG_H

#include "rando.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The manifest's settings, as the launcher wrote them. */
typedef struct rando_options {
    const char* seed;       /* "seed": a code, a word, or blank */
    int weapons;            /* "shuffle_weapons" */
    int gadgets;            /* "shuffle_gadgets" */
    int gold_bolts;         /* "shuffle_gold_bolts" */
    int planets;            /* "shuffle_infobots" */
    int vendor;             /* "shuffle_vendor" */
    int keep_start;         /* "keep_starting_gear" */
    const char* difficulty; /* "difficulty": relaxed, standard, expert */
} rando_options;

/* Where the seed came from. */
enum {
    SEED_FROM_CODE = 1,       /* a code: its settings, the others ignored */
    SEED_FROM_WORD = 2,       /* a word: its hash, with the settings */
    SEED_FROM_REMEMBERED = 3, /* blank: the seed rolled for an earlier blank start (seed.txt) */
    SEED_FROM_NEW = 4,        /* blank and none remembered: `fresh`, to be remembered */
};

/* The settings the options give. remembered: seed.txt's text, or null; fresh: a new random seed
 * for a blank seed with none remembered. Returns SEED_FROM_*. */
int rando_settings_of(
    const rando_options* o, const char* remembered, uint64_t fresh, rando_settings* s
);

#ifdef __cplusplus
}
#endif

#endif /* ITEM_RANDOMIZER_CONFIG_H */
