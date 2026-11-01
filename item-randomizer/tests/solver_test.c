/* SPDX-License-Identifier: GPL-3.0-or-later
 * The item randomizer's solver (src/solver.c) and its tables (src/logic.c):
 *
 *   - the tables hold together: each key once, the goal there, the game's own placement beatable
 *     at every difficulty (the requirements describe a game that can be finished);
 *   - every seed of every setting is solved and beatable, the same seed gives the same placement,
 *     a placement only moves the rewards of the shuffled locations among them, and progression
 *     rewards keep out of what a difficulty keeps them out of;
 *   - one fixed seed's placement is a fixed hash: the same on every system (CI runs it on macOS,
 *     Linux and Windows); its hash is pinned from this implementation, so a change to the tables
 *     or the fill shows up here.
 *
 *   solver_test [COUNT [FIRST]]   COUNT seeds from FIRST for each setting (default 200 from 0)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rando.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static int is_progression(rando_reward r) {
    return r.kind == REWARD_PLANET
           || (r.kind == REWARD_ITEM && (rando_progression_items & BIT(r.value)) != 0);
}

static uint64_t plan_hash(const rando_plan* p) {
    uint64_t h = 0xCBF29CE484222325ull;

    for (size_t i = 0; i < rando_location_count; ++i) {
        h = (h ^ (uint64_t)(p->reward[i].kind * 256u + p->reward[i].value)) * 0x100000001B3ull;
    }

    return h;
}

static void check_tables(void) {
    CHECK(rando_location_count <= RANDO_MAX_LOCATIONS);
    int goals = 0;

    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* a = &rando_locations[i];
        CHECK(a->name != NULL && a->level < LEVEL_COUNT);
        goals += (a->flags & LOCF_GOAL) != 0;
        CHECK((a->kind == LOC_FIXED) == (a->pool == 0));

        if (a->kind == LOC_GIVE || a->kind == LOC_VENDOR || a->kind == LOC_START) {
            CHECK(
                a->vanilla.kind == REWARD_ITEM && a->vanilla.value == a->key
                && a->key < RANDO_ITEM_COUNT
            );
        }

        if (a->kind == LOC_INFOBOT) {
            CHECK(a->vanilla.kind == REWARD_PLANET && a->key < 16);
        }

        if (a->kind == LOC_GOLD) {
            CHECK(a->vanilla.kind == REWARD_GOLD_BOLT && a->key < 4);
        }
        /* Each key once: one location per item id, per infobot slot, per gold bolt. */

        for (size_t j = i + 1; j < rando_location_count; ++j) {
            const rando_location* b = &rando_locations[j];

            if (a->kind == LOC_FIXED || b->kind == LOC_FIXED) {
                continue;
            }

            const int items = a->kind <= LOC_START && b->kind <= LOC_START;
            CHECK(!(items && a->key == b->key));
            CHECK(
                !(a->kind == b->kind && a->kind >= LOC_INFOBOT && a->level == b->level
                  && a->key == b->key)
            );
        }
        /* No location needs what it gives. */

        if (a->vanilla.kind == REWARD_ITEM) {
            CHECK((a->hard & BIT(a->vanilla.value)) == 0);
        }
    }

    CHECK(goals == 1);
    int gold = 0;

    for (size_t i = 0; i < rando_location_count; ++i) {
        gold += rando_locations[i].kind == LOC_GOLD;
    }

    CHECK(gold == 40);

    /* The game's own placement, at every difficulty, can be finished. */

    for (int d = 0; d <= DIFFICULTY_EXPERT; ++d) {
        rando_plan p;
        memset(&p, 0, sizeof p);
        p.settings.difficulty = (uint8_t)d;

        for (size_t i = 0; i < rando_location_count; ++i) {
            p.reward[i] = rando_locations[i].vanilla;
        }

        int spheres[RANDO_MAX_LOCATIONS];
        const int beatable = rando_beatable(&p, spheres);

        for (size_t i = 0; i < rando_location_count; ++i) {
            if (spheres[i] < 0) {
                fprintf(
                    stderr,
                    "%s: never reached in the game's own placement (%s)\n",
                    rando_locations[i].name,
                    rando_difficulty_name(d)
                );
            }
        }

        for (size_t i = 0; i < rando_location_count; ++i) {
            CHECK(spheres[i] >= 0);
        }

        CHECK(beatable);
    }
}

static void check_plan(const rando_settings* s, const rando_plan* p) {
    CHECK(rando_beatable(p, NULL));
    /* The shuffled locations' rewards are their vanilla rewards, moved; the rest are as the game has them. */
    int count[4][64];
    memset(count, 0, sizeof count);

    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        CHECK(p->shuffled[i] == rando_location_shuffled(s, l));

        if (!p->shuffled[i]) {
            CHECK(p->reward[i].kind == l->vanilla.kind && p->reward[i].value == l->vanilla.value);
            continue;
        }

        CHECK(p->reward[i].kind != REWARD_NONE);
        ++count[l->vanilla.kind][l->vanilla.value];
        --count[p->reward[i].kind][p->reward[i].value];
        /* Where each difficulty keeps progression out of. */

        if (is_progression(p->reward[i])) {
            CHECK(l->kind != LOC_GOLD);
            CHECK(
                s->difficulty == DIFFICULTY_EXPERT || !(l->flags & LOCF_COSTLY) || !p->shuffled[i]
            );
            CHECK(
                s->difficulty != DIFFICULTY_RELAXED || !(l->flags & LOCF_PRICEY) || !p->shuffled[i]
            );
        }
    }

    CHECK(rando_stranded(p) == 0);

    for (int k = 0; k < 4; ++k) {
        for (int v = 0; v < 64; ++v) {
            CHECK(count[k][v] == 0);
        }
    }
}

int main(int argc, char** argv) {
    const long n = argc > 1 ? atol(argv[1]) : 200;
    const long first = argc > 2 ? atol(argv[2]) : 0;
    check_tables();

    /* One fixed seed: its placement on every system. */
    static rando_plan p, q;
    const rando_settings fixed = {0x123456789Aull, POOL_ALL, 1, DIFFICULTY_STANDARD};
    CHECK(rando_solve(&fixed, &p) == 0);
    printf(
        "fixed placement hash %016llx after %d attempt(s)\n",
        (unsigned long long)plan_hash(&p),
        p.attempts
    );
    CHECK(plan_hash(&p) == 0x9f06cdf858c65046ull);

    /* Every setting, n seeds each. */
    long solved = 0;
    int worst = 0;

    for (int pools = 0; pools <= (int)POOL_ALL; ++pools) {
        for (int keep = 0; keep <= 1; ++keep) {
            for (int d = 0; d <= DIFFICULTY_EXPERT; ++d) {
                rando_rng r = {
                    (uint64_t)first * 0x9E3779B97F4A7C15ull + (uint64_t)(pools * 6 + keep * 3 + d)
                };

                for (long k = 0; k < n; ++k) {
                    const rando_settings s = {
                        rando_next(&r) & RANDO_SEED_MASK, (uint8_t)pools, (uint8_t)keep, (uint8_t)d
                    };

                    if (rando_solve(&s, &p) != 0) {
                        char code[RANDO_CODE_SIZE];
                        rando_encode(&s, code);
                        fprintf(stderr, "no placement for %s\n", code);
                        exit(1);
                    }

                    check_plan(&s, &p);

                    if (k < 3) { /* the same seed, the same placement */
                        CHECK(
                            rando_solve(&s, &q) == 0
                            && memcmp(p.reward, q.reward, sizeof p.reward) == 0
                        );
                    }

                    worst = p.attempts > worst ? p.attempts : worst;
                    ++solved;
                }
            }
        }
    }
    /* A different seed, a different placement (almost always: here, for these two). */
    CHECK(rando_solve(&fixed, &p) == 0);
    const rando_settings other = {fixed.seed + 1, POOL_ALL, 1, DIFFICULTY_STANDARD};
    CHECK(rando_solve(&other, &q) == 0 && memcmp(p.reward, q.reward, sizeof p.reward) != 0);

    /* The spoiler log names the code and ends beatable. */
    static char text[64 * 1024];
    const size_t need = rando_spoiler(&p, "test", text, sizeof text);
    CHECK(
        need <= sizeof text && strstr(text, "4ZRJ-6HB7-H6Q5") != NULL
        && strstr(text, "Beatable.") != NULL
    );
    CHECK(rando_spoiler(&p, "test", text, 16) == need && strlen(text) == 15);

    printf(
        "solver_test: ok, %ld placements beatable (seeds %ld..%ld of each setting), at most %d "
        "attempts\n",
        solved,
        first,
        first + n - 1,
        worst
    );
    return 0;
}
