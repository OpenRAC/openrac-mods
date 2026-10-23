/* SPDX-License-Identifier: GPL-3.0-or-later
 * The item randomizer's settings (src/config.c) and its manifest: each pool switch is its pool, a
 * code in the seed setting brings its own settings, a word is hashed with the switches, a blank
 * seed takes the remembered one or a new one, an unknown difficulty is standard; and the manifest
 * declares every key the mod reads, with the defaults the README gives.
 *
 *   settings_test [openrac-mod.json]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

int main(int argc, char** argv) {
    rando_options o = {"", 1, 1, 1, 1, 1, 1, "standard"};
    rando_settings s;

    /* The switches. */
    CHECK(rando_settings_of(&o, NULL, 0x1234567890FFull, &s) == SEED_FROM_NEW);
    CHECK(
        s.pools == POOL_ALL && s.keep_start == 1 && s.difficulty == DIFFICULTY_STANDARD
        && s.seed == (0x1234567890FFull & RANDO_SEED_MASK)
    );
    const int bits[5] = {POOL_WEAPONS, POOL_GADGETS, POOL_GOLD, POOL_PLANETS, POOL_VENDOR};

    for (int i = 0; i < 5; ++i) {
        rando_options one = {"x", i == 0, i == 1, i == 2, i == 3, i == 4, 0, "expert"};
        CHECK(
            rando_settings_of(&one, NULL, 0, &s) == SEED_FROM_WORD && s.pools == bits[i]
            && s.keep_start == 0 && s.difficulty == DIFFICULTY_EXPERT
        );
    }

    o.difficulty = "relaxed";
    CHECK(rando_settings_of(&o, NULL, 0, &s) && s.difficulty == DIFFICULTY_RELAXED);
    o.difficulty = "nonsense";
    CHECK(rando_settings_of(&o, NULL, 0, &s) && s.difficulty == DIFFICULTY_STANDARD);
    o.difficulty = NULL;
    CHECK(rando_settings_of(&o, NULL, 0, &s) && s.difficulty == DIFFICULTY_STANDARD);

    /* A word: its hash, the switches as they are. */
    o.seed = "Qwark";
    o.gold_bolts = 0;
    CHECK(
        rando_settings_of(&o, NULL, 99, &s) == SEED_FROM_WORD
        && s.seed == rando_seed_of_text("Qwark") && s.pools == (POOL_ALL & ~POOL_GOLD)
    );

    /* A code: its own settings, whatever the switches say. */
    const rando_settings shared = {0xABCDEull, POOL_GADGETS | POOL_PLANETS, 0, DIFFICULTY_RELAXED};
    char code[RANDO_CODE_SIZE];
    rando_encode(&shared, code);
    o.seed = code;
    CHECK(rando_settings_of(&o, NULL, 99, &s) == SEED_FROM_CODE);
    CHECK(
        s.seed == shared.seed && s.pools == shared.pools && s.keep_start == 0
        && s.difficulty == DIFFICULTY_RELAXED
    );

    /* Blank: the remembered seed (only its seed: these switches), else the fresh one. */
    o.seed = "";
    o.gold_bolts = 1;
    o.difficulty = "standard";
    CHECK(
        rando_settings_of(&o, code, 99, &s) == SEED_FROM_REMEMBERED && s.seed == shared.seed
        && s.pools == POOL_ALL && s.difficulty == DIFFICULTY_STANDARD
    );
    CHECK(rando_settings_of(&o, "not a code", 99, &s) == SEED_FROM_NEW && s.seed == 99);
    o.seed = NULL;
    CHECK(rando_settings_of(&o, NULL, 7, &s) == SEED_FROM_NEW && s.seed == 7);

    /* The manifest declares what the mod reads. */

    if (argc > 1) {
        FILE* f = fopen(argv[1], "rb");
        CHECK(f != NULL);
        static char text[16384];
        const size_t n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[n] = 0;
        static const char* const keys[] =
            {"\"seed\"",
             "\"difficulty\"",
             "\"shuffle_gadgets\"",
             "\"shuffle_weapons\"",
             "\"shuffle_vendor\"",
             "\"shuffle_gold_bolts\"",
             "\"shuffle_infobots\"",
             "\"keep_starting_gear\"",
             "\"spoiler_log\"",
             "\"Item_Give\"",
             "\"Moby_Edwina_Update\""};

        for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
            if (strstr(text, keys[i]) == NULL) {
                fprintf(stderr, "the manifest has no %s\n", keys[i]);
                exit(1);
            }
        }

        CHECK(strstr(text, "\"id\": \"item-randomizer\"") != NULL);
        CHECK(strstr(text, "\"default\": \"standard\"") != NULL);
    }

    printf("settings_test: ok\n");
    return 0;
}
