/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The item randomizer's placement (rando.h): which reward each shuffled location gives.
 *
 * The fill is an assumed fill. The rewards that open the way somewhere (the progression items
 * and the planets' coordinates) are placed first, one at a time in a random order: for each, the
 * game is played through (a sweep) as if the player had every progression reward not yet placed,
 * and the reward goes to a random empty location that sweep reaches. Placing a reward only where
 * the game can be reached without it, with everything later still to come, keeps every seed
 * beatable; what is left (the other weapons and gadgets, the gold bolts) fills the rest at
 * random. The whole result is played through once more from the start, and a fill whose goal
 * cannot be reached (none of the tables' do: the tests try many) is tried again.
 *
 * The difficulties: expert needs only what a place cannot be done without (`hard`); standard
 * also what play there expects (`soft`), and keeps progression out of costly purchases; relaxed
 * has standard's needs, keeps progression out of gold bolts too, and puts each progression
 * reward early: the earliest, in the game's own route, of three places drawn.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rando.h"

#define NOWHERE 0xFFFFu

static int is_progression(rando_reward r) {
    return r.kind == REWARD_PLANET || (r.kind == REWARD_ITEM && (rando_progression_items & BIT(r.value)) != 0);
}

int rando_location_shuffled(const rando_settings* s, const rando_location* l) {
    if (l->kind == LOC_FIXED) {
        return 0;
    }
    if (l->kind == LOC_START && s->keep_start) {
        return 0;
    }
    return (s->pools & l->pool) != 0;
}

uint64_t rando_requirement(int difficulty, size_t location) {
    const rando_location* l = &rando_locations[location];
    if (difficulty == DIFFICULTY_EXPERT) {
        return l->hard;
    }
    return l->hard | l->soft;
}

/* A playthrough's state. */
typedef struct sweep {
    uint64_t items;
    uint32_t planets;
    uint8_t had[RANDO_MAX_LOCATIONS];
} sweep;

static uint64_t s_need[3][RANDO_MAX_LOCATIONS];
static int s_need_ready;

static void prepare(void) {
    if (!s_need_ready) {
        for (int d = 0; d < 3; ++d) {
            for (size_t i = 0; i < rando_location_count; ++i) {
                s_need[d][i] = rando_requirement(d, i);
            }
        }
        s_need_ready = 1;
    }
}

static int reachable(const sweep* w, int difficulty, size_t i) {
    const rando_location* l = &rando_locations[i];
    return (w->planets & (1u << l->level)) != 0 && (s_need[difficulty][i] & ~w->items) == 0;
}

static void take(sweep* w, rando_reward r) {
    if (r.kind == REWARD_ITEM) {
        w->items |= BIT(r.value);
    } else if (r.kind == REWARD_PLANET) {
        w->planets |= 1u << r.value;
    }
}

/* Plays on from w until nothing more is reached: every location reached gives reward[i] (a
 * location with REWARD_NONE that is shuffled and empty gives nothing). */
static void play(sweep* w, int difficulty, const rando_reward* reward) {
    for (int more = 1; more;) {
        more = 0;
        for (size_t i = 0; i < rando_location_count; ++i) {
            if (!w->had[i] && reachable(w, difficulty, i)) {
                w->had[i] = 1;
                take(w, reward[i]);
                more = 1;
            }
        }
    }
}

static void start(sweep* w) {
    memset(w, 0, sizeof *w);
    w->planets = 1u << LEVEL_VELDIN;
    w->items = BIT(ITEM_WRENCH);
}

static size_t goal(void) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (rando_locations[i].flags & LOCF_GOAL) {
            return i;
        }
    }
    return 0;
}

/* Whether a progression reward may go to location i at this difficulty: relaxed keeps them out of
 * gold bolts and costly purchases, standard out of costly purchases. */
static int fits(int difficulty, size_t i) {
    const rando_location* l = &rando_locations[i];
    if (difficulty == DIFFICULTY_EXPERT) {
        return 1;
    }
    if (l->flags & LOCF_COSTLY) {
        return 0;
    }
    return difficulty != DIFFICULTY_RELAXED || l->kind != LOC_GOLD;
}

static void shuffle(rando_rng* r, rando_reward* a, size_t n) {
    for (size_t i = n; i > 1; --i) {
        const size_t j = rando_below(r, (uint32_t)i);
        const rando_reward t = a[i - 1];
        a[i - 1] = a[j];
        a[j] = t;
    }
}

static int fill(const rando_settings* s, rando_rng* rng, rando_plan* plan) {
    rando_reward prog[RANDO_MAX_LOCATIONS], rest[RANDO_MAX_LOCATIONS];
    size_t np = 0, nr = 0;
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        plan->shuffled[i] = (uint8_t)rando_location_shuffled(s, l);
        plan->reward[i] = l->vanilla;
        if (plan->shuffled[i]) {
            plan->reward[i].kind = REWARD_NONE;
            plan->reward[i].value = 0;
            if (is_progression(l->vanilla)) {
                prog[np++] = l->vanilla;
            } else {
                rest[nr++] = l->vanilla;
            }
        }
    }
    shuffle(rng, prog, np);
    shuffle(rng, rest, nr);
    const int d = s->difficulty;
    size_t candidates[RANDO_MAX_LOCATIONS];
    while (np > 0) {
        const rando_reward r = prog[--np];
        sweep w;
        start(&w);
        for (size_t k = 0; k < np; ++k) {
            take(&w, prog[k]); /* assumed: the rewards still to place */
        }
        play(&w, d, plan->reward);
        size_t nc = 0;
        for (size_t i = 0; i < rando_location_count; ++i) {
            if (plan->shuffled[i] && plan->reward[i].kind == REWARD_NONE && w.had[i] && fits(d, i)) {
                candidates[nc++] = i;
            }
        }
        if (nc == 0) {
            return -1;
        }
        size_t at = candidates[rando_below(rng, (uint32_t)nc)];
        if (d == DIFFICULTY_RELAXED) {
            /* Early: the earliest in the game's route of three drawn. */
            for (int k = 0; k < 2; ++k) {
                const size_t other = candidates[rando_below(rng, (uint32_t)nc)];
                at = rando_locations[other].step < rando_locations[at].step ? other : at;
            }
        }
        plan->reward[at] = r;
    }
    size_t k = 0;
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (plan->shuffled[i] && plan->reward[i].kind == REWARD_NONE) {
            plan->reward[i] = rest[k++];
        }
    }
    return k == nr ? 0 : -1;
}

int rando_solve(const rando_settings* s, rando_plan* plan) {
    prepare();
    memset(plan, 0, sizeof *plan);
    plan->settings = *s;
    plan->settings.seed &= RANDO_SEED_MASK;
    plan->settings.pools &= POOL_ALL;
    if (plan->settings.difficulty > DIFFICULTY_EXPERT) {
        plan->settings.difficulty = DIFFICULTY_STANDARD;
    }
    /* The stream: the code's payload, so that every setting changes the whole placement. */
    char code[RANDO_CODE_SIZE];
    rando_encode(&plan->settings, code);
    rando_rng rng = {rando_seed_of_text(code) ^ (plan->settings.seed << 13)};
    for (int attempt = 1; attempt <= 500; ++attempt) {
        plan->attempts = attempt;
        if (fill(&plan->settings, &rng, plan) == 0 && rando_beatable(plan, 0)) {
            return 0;
        }
    }
    return -1;
}

int rando_beatable(const rando_plan* plan, int* spheres) {
    prepare();
    const int d = plan->settings.difficulty;
    sweep w;
    start(&w);
    if (spheres) {
        for (size_t i = 0; i < rando_location_count; ++i) {
            spheres[i] = -1;
        }
    }
    /* Sphere by sphere: what is reachable with what was had before it. */
    for (int sphere = 0;; ++sphere) {
        uint8_t now[RANDO_MAX_LOCATIONS];
        size_t found = 0;
        for (size_t i = 0; i < rando_location_count; ++i) {
            now[i] = !w.had[i] && reachable(&w, d, i);
            found += now[i];
        }
        if (found == 0) {
            break;
        }
        for (size_t i = 0; i < rando_location_count; ++i) {
            if (now[i]) {
                w.had[i] = 1;
                take(&w, plan->reward[i]);
                if (spheres) {
                    spheres[i] = sphere;
                }
            }
        }
    }
    return w.had[goal()];
}

/* ---- the spoiler log ---- */

typedef struct out {
    char* p;
    size_t size, used;
} out;

#if defined(__GNUC__)
static void put(out* o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
static void put(out* o, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char* at = o->used < o->size ? o->p + o->used : 0;
    const size_t room = o->used < o->size ? o->size - o->used : 0;
    const int n = vsnprintf(at, room, fmt, ap);
    va_end(ap);
    if (n > 0) {
        o->used += (size_t)n;
    }
}

static void reward_text(rando_reward r, char* buf, size_t n) {
    switch (r.kind) {
        case REWARD_ITEM:
            snprintf(buf, n, "%s", rando_item_name(r.value));
            break;
        case REWARD_PLANET:
            snprintf(buf, n, "the coordinates of %s", rando_planet_name(r.value));
            break;
        case REWARD_GOLD_BOLT:
            snprintf(buf, n, "a gold bolt");
            break;
        default:
            snprintf(buf, n, "nothing");
    }
}

size_t rando_spoiler(const rando_plan* plan, const char* mod_version, char* text, size_t size) {
    out o = {text, size, 0};
    if (size > 0) {
        text[0] = 0;
    }
    const rando_settings* s = &plan->settings;
    char code[RANDO_CODE_SIZE];
    rando_encode(s, code);
    put(&o, "Ratchet & Clank item randomizer %s: spoiler log\n\n", mod_version);
    put(&o, "Seed code   %s\n", code);
    put(&o, "Difficulty  %s\n", rando_difficulty_name(s->difficulty));
    put(&o, "Shuffled    %s%s%s%s%s\n", (s->pools & POOL_WEAPONS) ? "weapons " : "", (s->pools & POOL_GADGETS) ? "gadgets " : "",
        (s->pools & POOL_GOLD) ? "gold-bolts " : "", (s->pools & POOL_PLANETS) ? "infobots " : "",
        (s->pools & POOL_VENDOR) ? "vendor " : "");
    put(&o, "Start gear  %s\n\n", s->keep_start ? "kept" : "shuffled");

    int spheres[RANDO_MAX_LOCATIONS];
    const int ok = rando_beatable(plan, spheres);
    put(&o, "Where everything is (* marks a reward that opens the way)\n");
    int level = -1;
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        if (!plan->shuffled[i]) {
            continue;
        }
        if (l->level != level) {
            level = l->level;
            put(&o, "\n%s\n", rando_planet_name(level));
        }
        char r[64];
        reward_text(plan->reward[i], r, sizeof r);
        put(&o, "  %-52s %s%s\n", l->name, is_progression(plan->reward[i]) ? "* " : "  ", r);
    }
    put(&o, "\nA playthrough (each sphere needs only what the spheres before it give)\n");
    for (int sphere = 0; sphere < RANDO_MAX_LOCATIONS; ++sphere) {
        int any = 0;
        for (size_t i = 0; i < rando_location_count; ++i) {
            if (spheres[i] != sphere || !(is_progression(plan->reward[i]) || (rando_locations[i].flags & LOCF_GOAL))) {
                continue;
            }
            if (!any) {
                put(&o, "\nSphere %d\n", sphere + 1);
                any = 1;
            }
            char r[64];
            reward_text(plan->reward[i], r, sizeof r);
            put(&o, "  %-52s %s\n", rando_locations[i].name, (rando_locations[i].flags & LOCF_GOAL) ? "the end" : r);
        }
    }
    put(&o, "\n%s\n", ok ? "Beatable." : "NOT BEATABLE: please report this seed code.");
    return o.used + 1;
}
