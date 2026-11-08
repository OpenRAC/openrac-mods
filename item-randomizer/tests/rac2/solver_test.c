/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Going Commando's placement (src/rac2): every seed of every setting is beatable, each item of a
 * pool is placed once, fixed places keep their item, a seed places the same way twice, the vendor's
 * stock is a permutation of the game's levels, and the tables hold together. Arguments: how many
 * seeds per setting (default 300) and the first seed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rando2.h"

static int s_failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            ++s_failures;                                                                          \
        }                                                                                          \
    } while (0)

static void tables(void) {
    CHECK(r2_location_count <= R2_MAX_LOCATIONS);
    int goals = 0;

    for (size_t i = 0; i < r2_location_count; ++i) {
        const r2_location* l = &r2_locations[i];
        goals += (l->flags & R2F_GOAL) != 0;

        // Each item is one place's, at most.
        for (size_t j = i + 1; j < r2_location_count; ++j) {
            CHECK(l->item == 0 || r2_locations[j].item != l->item);
        }

        CHECK(l->item < R2_ITEM_COUNT);
        CHECK(l->level < R2_LEVEL_COUNT);
        // A place that needs its own item is never reached.
        CHECK(l->item == 0 || (l->need & R2_BIT(l->item)) == 0);
    }

    CHECK(goals == 1);

    // The vanilla game is beatable by these rules.
    r2_plan plan;
    memset(&plan, 0, sizeof plan);
    for (size_t i = 0; i < r2_location_count; ++i) {
        plan.reward[i] = r2_locations[i].item;
    }
    CHECK(r2_beatable(&plan, NULL));
}

static void one(const rando_settings* s) {
    r2_plan plan, again;
    CHECK(r2_solve(s, &plan) == 0);
    CHECK(r2_beatable(&plan, NULL));
    CHECK(r2_solve(s, &again) == 0);
    CHECK(memcmp(plan.reward, again.reward, sizeof plan.reward) == 0);
    CHECK(memcmp(plan.stock, again.stock, sizeof plan.stock) == 0);

    // The pool's items, each once; the fixed places their own.
    int seen[R2_ITEM_COUNT] = {0};
    for (size_t i = 0; i < r2_location_count; ++i) {
        if (!plan.shuffled[i]) {
            CHECK(plan.reward[i] == r2_locations[i].item);
            continue;
        }
        CHECK(plan.reward[i] != 0);
        ++seen[plan.reward[i]];
        if (s->difficulty == DIFFICULTY_RELAXED && (r2_locations[i].flags & R2F_PRICEY)) {
            CHECK((r2_progression_items & R2_BIT(plan.reward[i])) == 0);
        }
    }
    for (size_t i = 0; i < r2_location_count; ++i) {
        if (plan.shuffled[i]) {
            CHECK(seen[r2_locations[i].item] == 1);
        }
    }

    // The stock: the same levels, the start's two where they were.
    int levels[R2_LEVEL_COUNT] = {0};
    for (int i = 0; i < R2_VENDOR_COUNT; ++i) {
        ++levels[r2_vendor_stock[i].level];
        --levels[plan.stock[i]];
        if (r2_vendor_stock[i].level == 0 || !(s->pools & POOL_VENDOR)) {
            CHECK(plan.stock[i] == r2_vendor_stock[i].level);
        }
    }
    for (int l = 0; l < R2_LEVEL_COUNT; ++l) {
        CHECK(levels[l] == 0);
    }
}

int main(int argc, char** argv) {
    const int count = argc > 1 ? atoi(argv[1]) : 300;
    const uint64_t first = argc > 2 ? strtoull(argv[2], NULL, 10) : 1;

    tables();

    static const uint8_t pools[] = {POOL_GADGETS, POOL_WEAPONS, POOL_GADGETS | POOL_WEAPONS,
                                    POOL_GADGETS | POOL_WEAPONS | POOL_VENDOR, POOL_VENDOR, 0};
    int moved = 0;

    for (size_t p = 0; p < sizeof pools; ++p) {
        for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
            for (int n = 0; n < count; ++n) {
                rando_settings s = {(first + (uint64_t)n) & RANDO_SEED_MASK, pools[p], 1, (uint8_t)d};
                one(&s);

                // Shuffling gadgets moves something.
                if (pools[p] & POOL_GADGETS) {
                    r2_plan plan;
                    r2_solve(&s, &plan);
                    for (size_t i = 0; i < r2_location_count; ++i) {
                        moved += plan.shuffled[i] && plan.reward[i] != r2_locations[i].item;
                    }
                }
            }
        }
    }

    CHECK(moved > 0);

    // The spoiler names the code and every shuffled place.
    rando_settings s = {12345, POOL_GADGETS | POOL_VENDOR, 1, DIFFICULTY_STANDARD};
    r2_plan plan;
    CHECK(r2_solve(&s, &plan) == 0);
    char text[8192];
    const size_t n = r2_spoiler(&plan, "test", text, sizeof text);
    CHECK(n > 0 && n < sizeof text);
    CHECK(strstr(text, "Seed code: ") != NULL && strstr(text, "the end") != NULL);
    CHECK(strstr(text, "Oozla: Megacorp scientist") != NULL);

    if (s_failures == 0) {
        printf("rac2 solver: all passed\n");
    }
    return s_failures != 0;
}
