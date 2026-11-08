/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Going Commando: the placement (rando2.h), an assumed fill as Ratchet & Clank's (solver.c): the
 * items that open the way (r2_progression_items) are placed first, one at a time in a random
 * order, each in a random empty location a sweep reaches with every progression item not yet
 * placed; the others fill what is left at random. The result is played through from the start, and
 * a fill whose goal is not reached is tried again.
 *
 * The difficulties: expert and standard have the same needs (logic2.c's, with no tricks in them);
 * relaxed also keeps progression out of the pricey purchase (the Hypnomatic maker's).
 *
 * The vendor: with its pool shuffled, the first level of each weapon the vendor stocks is another
 * weapon's (a permutation of the game's own levels, so as many weapons come on sale at each
 * planet), the two the game starts with left as they are.
 */
#include <stdio.h>
#include <string.h>

#include "rando2.h"

#define MAX_ATTEMPTS 20000

/* What the player holds and which locations it has had. */
typedef struct r2_sweep {
    uint64_t items;
    uint8_t had[R2_MAX_LOCATIONS];
} r2_sweep;

/* "RAC2", so that a seed places differently here than in Ratchet & Clank. */
#define STREAM_GAME 0x5241433200000000ull

int r2_location_shuffled(const rando_settings* s, size_t i) {
    return (s->pools & r2_locations[i].pool) != 0;
}

uint64_t r2_requirement(size_t i) {
    return r2_locations[i].need;
}

static int reachable(const r2_sweep* w, size_t i) {
    const r2_location* l = &r2_locations[i];
    const uint64_t jump = R2_BIT(R2_HELI_PACK) | R2_BIT(R2_THRUSTER_PACK);

    return (l->need & ~w->items) == 0 && (!(l->flags & R2F_JUMP) || (w->items & jump) != 0);
}

/* Plays on from w until nothing more is reached: each location reached gives its reward (0 for
 * an empty one). Returns whether the goal was reached. */
static int play(r2_sweep* w, const uint8_t* reward) {
    int goal = 0;
    int more = 1;

    while (more) {
        more = 0;

        for (size_t i = 0; i < r2_location_count; ++i) {
            if (w->had[i] || !reachable(w, i)) {
                continue;
            }

            w->had[i] = 1;
            more = 1;

            if (r2_locations[i].flags & R2F_GOAL) {
                goal = 1;
            } else if (reward[i] != 0) {
                w->items |= R2_BIT(reward[i]);
            }
        }
    }

    return goal;
}

int r2_beatable(const r2_plan* plan, int* spheres) {
    r2_sweep w;
    memset(&w, 0, sizeof w);
    int sphere = 0;
    int goal = 0;

    for (size_t i = 0; spheres != NULL && i < r2_location_count; ++i) {
        spheres[i] = -1;
    }

    // Sphere by sphere: what the start reaches, then what that opens, and so on.
    for (;;) {
        uint64_t gained = 0;
        int any = 0;

        for (size_t i = 0; i < r2_location_count; ++i) {
            if (w.had[i] || !reachable(&w, i)) {
                continue;
            }

            w.had[i] = 1;
            any = 1;

            if (spheres != NULL) {
                spheres[i] = sphere;
            }

            if (r2_locations[i].flags & R2F_GOAL) {
                goal = 1;
            } else if (plan->reward[i] != 0) {
                gained |= R2_BIT(plan->reward[i]);
            }
        }

        if (!any) {
            break;
        }

        w.items |= gained;
        ++sphere;
    }

    // Every shuffled location must be had by the end too: nothing is behind itself.
    for (size_t i = 0; i < r2_location_count; ++i) {
        if (plan->shuffled[i] && !w.had[i]) {
            return 0;
        }
    }

    return goal;
}

/* One fill; 0 when it holds. */
static int fill(const rando_settings* s, r2_plan* plan, rando_rng* rng) {
    uint8_t pool[R2_MAX_LOCATIONS];
    size_t count = 0;

    for (size_t i = 0; i < r2_location_count; ++i) {
        plan->shuffled[i] = (uint8_t)r2_location_shuffled(s, i);
        plan->reward[i] = plan->shuffled[i] ? 0 : r2_locations[i].item;

        if (plan->shuffled[i]) {
            pool[count++] = r2_locations[i].item;
        }
    }

    // The progression items first, in a random order, then the rest.
    for (size_t i = count; i > 1; --i) {
        const size_t j = rando_below(rng, (uint32_t)i);
        const uint8_t t = pool[i - 1];
        pool[i - 1] = pool[j];
        pool[j] = t;
    }

    uint8_t first[R2_MAX_LOCATIONS], later[R2_MAX_LOCATIONS];
    size_t firsts = 0, laters = 0;

    for (size_t i = 0; i < count; ++i) {
        if (r2_progression_items & R2_BIT(pool[i])) {
            first[firsts++] = pool[i];
        } else {
            later[laters++] = pool[i];
        }
    }

    for (size_t p = 0; p < firsts; ++p) {
        // Assume every progression item still to place is held.
        r2_sweep w;
        memset(&w, 0, sizeof w);

        for (size_t q = p + 1; q < firsts; ++q) {
            w.items |= R2_BIT(first[q]);
        }

        play(&w, plan->reward);

        size_t open[R2_MAX_LOCATIONS];
        size_t opens = 0;

        for (size_t i = 0; i < r2_location_count; ++i) {
            const int pricey = s->difficulty == DIFFICULTY_RELAXED
                               && (r2_locations[i].flags & R2F_PRICEY) != 0;

            if (plan->shuffled[i] && plan->reward[i] == 0 && w.had[i] && !pricey) {
                open[opens++] = i;
            }
        }

        if (opens == 0) {
            return -1;
        }

        plan->reward[open[rando_below(rng, (uint32_t)opens)]] = first[p];
    }

    // The rest wherever is left.
    for (size_t p = 0; p < laters; ++p) {
        size_t open[R2_MAX_LOCATIONS];
        size_t opens = 0;

        for (size_t i = 0; i < r2_location_count; ++i) {
            if (plan->shuffled[i] && plan->reward[i] == 0) {
                open[opens++] = i;
            }
        }

        if (opens == 0) {
            return -1;
        }

        plan->reward[open[rando_below(rng, (uint32_t)opens)]] = later[p];
    }

    return r2_beatable(plan, NULL) ? 0 : -1;
}

/* The vendor's stock: the weapons' first levels shuffled among them, the start's two kept. */
static void fill_stock(const rando_settings* s, r2_plan* plan, rando_rng* rng) {
    uint8_t levels[R2_VENDOR_COUNT];
    size_t moved[R2_VENDOR_COUNT];
    size_t count = 0;

    for (size_t i = 0; i < R2_VENDOR_COUNT; ++i) {
        plan->stock[i] = r2_vendor_stock[i].level;

        if ((s->pools & POOL_VENDOR) && r2_vendor_stock[i].level != 0) {
            moved[count] = i;
            levels[count++] = r2_vendor_stock[i].level;
        }
    }

    for (size_t i = count; i > 1; --i) {
        const size_t j = rando_below(rng, (uint32_t)i);
        const uint8_t t = levels[i - 1];
        levels[i - 1] = levels[j];
        levels[j] = t;
    }

    for (size_t i = 0; i < count; ++i) {
        plan->stock[moved[i]] = levels[i];
    }
}

int r2_solve(const rando_settings* s, r2_plan* plan) {
    memset(plan, 0, sizeof *plan);
    plan->settings = *s;
    rando_rng rng = {
        s->seed ^ STREAM_GAME ^ ((uint64_t)s->pools << 40) ^ ((uint64_t)s->difficulty << 48)
    };

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        if (fill(s, plan, &rng) == 0) {
            plan->attempts = attempt;
            fill_stock(s, plan, &rng);
            return 0;
        }
    }

    return -1;
}

/* ---- the spoiler log ---- */

typedef struct r2_out {
    char* out;
    size_t size;
    size_t length;
} r2_out;

static void put(r2_out* o, const char* text) {
    const size_t n = strlen(text);

    if (o->length < o->size) {
        const size_t room = o->size - o->length - 1;
        memcpy(o->out + o->length, text, n < room ? n : room);
        o->out[o->length + (n < room ? n : room)] = '\0';
    }

    o->length += n;
}

size_t r2_spoiler(const r2_plan* plan, const char* mod_version, char* out, size_t size) {
    r2_out o = {out, size, 0};
    char line[192], code[RANDO_CODE_SIZE];

    if (size > 0) {
        out[0] = '\0';
    }

    rando_encode(&plan->settings, code);
    snprintf(line, sizeof line,
             "Going Commando item randomizer %s\nSeed code: %s\nLogic: %s\nShuffled: %s%s%s\n\n",
             mod_version, code, rando_difficulty_name(plan->settings.difficulty),
             (plan->settings.pools & POOL_GADGETS) ? "gadgets " : "",
             (plan->settings.pools & POOL_WEAPONS) ? "weapons " : "",
             (plan->settings.pools & POOL_VENDOR) ? "vendor" : "");
    put(&o, line);
    put(&o, "Where everything is:\n");

    for (size_t i = 0; i < r2_location_count; ++i) {
        if (!plan->shuffled[i]) {
            continue;
        }

        snprintf(line, sizeof line, "  %-46s %s\n", r2_locations[i].name,
                 r2_item_name(plan->reward[i]));
        put(&o, line);
    }

    put(&o, "\nThe vendor stocks:\n");

    for (size_t i = 0; i < R2_VENDOR_COUNT; ++i) {
        snprintf(line, sizeof line, "  %-18s from %s\n", r2_item_name(r2_vendor_stock[i].item),
                 r2_level_name(plan->stock[i]));
        put(&o, line);
    }

    // A way through: each sphere's progression.
    int spheres[R2_MAX_LOCATIONS];
    r2_beatable(plan, spheres);
    put(&o, "\nA way through (each step needs only what came before):\n");

    for (int sphere = 0; sphere < R2_MAX_LOCATIONS; ++sphere) {
        int any = 0;

        for (size_t i = 0; i < r2_location_count; ++i) {
            const int goal = (r2_locations[i].flags & R2F_GOAL) != 0;
            const int useful = (r2_progression_items & R2_BIT(plan->reward[i])) != 0;

            if (spheres[i] != sphere || !(goal || (plan->shuffled[i] && useful))) {
                continue;
            }

            snprintf(line, sizeof line, "  %d. %s: %s\n", sphere + 1, r2_locations[i].name,
                     goal ? "the end" : r2_item_name(plan->reward[i]));
            put(&o, line);
            any = 1;
        }

        (void)any;
    }

    return o.length;
}
