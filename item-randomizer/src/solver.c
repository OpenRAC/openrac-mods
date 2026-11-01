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
 * A location is reached when its planet is unlocked, the player holds all it needs (`hard`, and
 * `soft` but on expert), at least one of its `any`, and its other `planets` are unlocked. A reward
 * that opens the way is never placed behind itself: every shuffled location can be had by the end
 * of a placement, or it is refused too.
 *
 * The difficulties: expert has `hard` alone and puts progression in any place but a gold bolt;
 * standard also counts `soft` and keeps progression out of costly purchases; relaxed has
 * standard's needs, keeps progression out of the pricey purchases too, and puts each progression
 * reward early: the earliest, in the game's own route, of three places drawn.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rando.h"

/* ---- constants ---- */

/* One requirement table per difficulty: DIFFICULTY_RELAXED, _STANDARD and _EXPERT. */
#define DIFFICULTY_COUNT 3

/* How many fills are tried before giving up; the tables never need more than a few thousand. */
#define MAX_ATTEMPTS 20000

/* Relaxed draws this many more places after the first and keeps the earliest of all of them. */
#define RELAXED_EXTRA_DRAWS 2

/* After this many fills, relaxed stops wanting each reward early: with few places shuffled, the
 * early ones run out before what must go late has been placed, and the seed needs the room. */
#define RELAXED_EARLY_ATTEMPTS 64

/* The generator's stream is the code's hash mixed with the seed shifted by this, so that two
 * settings with one seed (or two seeds with one setting) place differently. */
#define STREAM_SEED_SHIFT 13

/* ---- types and state ---- */

/* A playthrough's state: what the player holds and which locations it has had. */
typedef struct sweep {
    uint64_t items;                   /* BIT(item) of every item held */
    uint32_t planets;                 /* 1 << level of every planet unlocked */
    uint8_t had[RANDO_MAX_LOCATIONS]; /* 1 for each location reached */
} sweep;

/* What each location needs, by difficulty (rando_requirement), filled once by prepare. The solver
 * runs while the mod loads (and in the tests), one caller at a time, so these need no locking. */
static uint64_t s_need[DIFFICULTY_COUNT][RANDO_MAX_LOCATIONS];
static int s_need_ready;

/* ---- the rules ---- */

/* Whether the reward opens the way somewhere: a planet, or one of the progression items. */
static int is_progression(rando_reward r) {
    return r.kind == REWARD_PLANET
           || (r.kind == REWARD_ITEM && (rando_progression_items & BIT(r.value)) != 0);
}

/* Whether the settings shuffle the location. */
int rando_location_shuffled(const rando_settings* s, const rando_location* l) {
    // Some places are never shuffled (an NPC's coordinates, the goal).
    if (l->kind == LOC_FIXED) {
        return 0;
    }

    // The starting Bomb Glove is the player's own when the starting gear is kept.
    if (l->kind == LOC_START && s->keep_start) {
        return 0;
    }

    return (s->pools & l->pool) != 0;
}

/* The items the location needs at this difficulty. */
uint64_t rando_requirement(int difficulty, size_t location) {
    const rando_location* l = &rando_locations[location];

    // Expert counts only what a place cannot be done without.
    if (difficulty == DIFFICULTY_EXPERT) {
        return l->hard;
    }

    return l->hard | l->soft;
}

/* Fills the requirement tables the first time. */
static void prepare(void) {
    // Once is enough: the tables are fixed.
    if (!s_need_ready) {
        // Once is enough: the tables are fixed.
        for (int d = 0; d < DIFFICULTY_COUNT; ++d) {
            // One table per difficulty.
            for (size_t i = 0; i < rando_location_count; ++i) {
                s_need[d][i] = rando_requirement(d, i);
            }
        }

        s_need_ready = 1;
    }
}

/* Whether the player in w can have location i now: its planet is unlocked and it holds every item
 * the location needs at this difficulty. */
static int reachable(const sweep* w, int difficulty, size_t i) {
    const rando_location* l = &rando_locations[i];

    // Its planet and any other it takes, what it needs, and one of what it takes one of.
    return (w->planets & (1u << l->level)) != 0 && (l->planets & ~w->planets) == 0
           && (s_need[difficulty][i] & ~w->items) == 0 && (l->any == 0 || (l->any & w->items) != 0);
}

/* Adds a reward to what the player in w holds (a gold bolt adds nothing that opens a way). */
static void take(sweep* w, rando_reward r) {
    // An item goes into the item set, a planet into the planet set.
    if (r.kind == REWARD_ITEM) {
        w->items |= BIT(r.value);
    } else if (r.kind == REWARD_PLANET) {
        w->planets |= 1u << r.value;
    }
}

/* ---- the sweep ---- */

/* Plays on from w until nothing more is reached: every location reached gives reward[i] (a
 * location with REWARD_NONE that is shuffled and empty gives nothing). */
static void play(sweep* w, int difficulty, const rando_reward* reward) {
    // Each round may reach places that open more, so go on until a round reaches nothing.
    for (int more = 1; more;) {
        more = 0;

        // One round: reach every location now reachable.
        for (size_t i = 0; i < rando_location_count; ++i) {
            // A place not had yet and reachable now is had, and gives its reward.
            if (!w->had[i] && reachable(w, difficulty, i)) {
                w->had[i] = 1;
                take(w, reward[i]);
                more = 1;
            }
        }
    }
}

/* A new game's state: Veldin unlocked and the OmniWrench in hand. */
static void start(sweep* w) {
    memset(w, 0, sizeof *w);
    w->planets = 1u << LEVEL_VELDIN;
    w->items = BIT(ITEM_WRENCH);
}

/* The index of the goal location (beating the game); 0 if the table has none. */
static size_t goal(void) {
    // The row marked as the goal.
    for (size_t i = 0; i < rando_location_count; ++i) {
        // The row marked as the goal.
        if (rando_locations[i].flags & LOCF_GOAL) {
            return i;
        }
    }

    return 0;
}

/* Whether a progression reward may go to location i at this difficulty: relaxed keeps them out of
 * costly and pricey purchases, standard out of costly purchases. */
static int fits(int difficulty, size_t i) {
    const rando_location* l = &rando_locations[i];

    // A gold bolt is a hidden corner: what the way on needs is never in one.
    if (l->kind == LOC_GOLD) {
        return 0;
    }

    // Expert puts progression anywhere else.
    if (difficulty == DIFFICULTY_EXPERT) {
        return 1;
    }

    // Standard and relaxed keep it out of purchases a player cannot afford early.
    if (l->flags & LOCF_COSTLY) {
        return 0;
    }

    return difficulty != DIFFICULTY_RELAXED || (l->flags & LOCF_PRICEY) == 0;
}

/* Shuffles the first n rewards of a in place (Fisher-Yates, one draw per step). */
static void shuffle(rando_rng* r, rando_reward* a, size_t n) {
    // From the end, swap each position with a random one at or before it.
    for (size_t i = n; i > 1; --i) {
        const size_t j = rando_below(r, (uint32_t)i);
        const rando_reward t = a[i - 1];
        a[i - 1] = a[j];
        a[j] = t;
    }
}

/* ---- the fill ---- */

/* One try at a placement: the assumed fill described at the top. 0 when every reward found a
 * place; -1 when a progression reward had nowhere to go (the caller tries again). */
static int fill(const rando_settings* s, rando_rng* rng, rando_plan* plan, int early) {
    rando_reward prog[RANDO_MAX_LOCATIONS], rest[RANDO_MAX_LOCATIONS];
    size_t np = 0, nr = 0;

    // Every location: decide whether it is shuffled, and pool its vanilla reward if so.
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        plan->shuffled[i] = (uint8_t)rando_location_shuffled(s, l);
        plan->reward[i] = l->vanilla;

        // A shuffled location starts empty, and its vanilla reward joins the pool to hand out.
        if (plan->shuffled[i]) {
            plan->reward[i].kind = REWARD_NONE;
            plan->reward[i].value = 0;

            // Rewards that open the way are placed first; the rest fill what is left.
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

    // One progression reward at a time, the last drawn first.
    while (np > 0) {
        const rando_reward r = prog[--np];
        sweep w;
        start(&w);

        // Assume the player holds every progression reward not yet placed, so this one goes only
        // where the game can be reached without it.
        for (size_t k = 0; k < np; ++k) {
            take(&w, prog[k]); /* assumed: the rewards still to place */
        }

        play(&w, d, plan->reward);
        size_t nc = 0;

        // The places this reward may take: shuffled, empty, reached by the sweep, and allowed at
        // this difficulty.
        for (size_t i = 0; i < rando_location_count; ++i) {
            // Every location that can take this reward.
            if (plan->shuffled[i] && plan->reward[i].kind == REWARD_NONE && w.had[i]
                && fits(d, i)) {
                candidates[nc++] = i;
            }
        }

        // No place left to put it: this fill has failed.
        if (nc == 0) {
            return -1;
        }

        size_t at = candidates[rando_below(rng, (uint32_t)nc)];

        // Relaxed wants each progression reward early: the earliest in the game's route of three
        // places drawn.
        if (d == DIFFICULTY_RELAXED && early) {
            // Draw more places and keep the earliest.
            for (int k = 0; k < RELAXED_EXTRA_DRAWS; ++k) {
                const size_t other = candidates[rando_below(rng, (uint32_t)nc)];
                at = rando_locations[other].step < rando_locations[at].step ? other : at;
            }
        }

        plan->reward[at] = r;
    }

    size_t k = 0;

    // The other rewards go to the places still empty, in the order they were shuffled.
    for (size_t i = 0; i < rando_location_count; ++i) {
        // Every empty shuffled location takes the next of the other rewards.
        if (plan->shuffled[i] && plan->reward[i].kind == REWARD_NONE) {
            plan->reward[i] = rest[k++];
        }
    }

    return k == nr ? 0 : -1;
}

/* Fills plan for the settings, trying again until a fill holds; 0, or -1 when none did. */
int rando_solve(const rando_settings* s, rando_plan* plan) {
    prepare();
    memset(plan, 0, sizeof *plan);
    plan->settings = *s;
    plan->settings.seed &= RANDO_SEED_MASK;
    plan->settings.pools &= POOL_ALL;

    // A difficulty out of range is read as standard.
    if (plan->settings.difficulty > DIFFICULTY_EXPERT) {
        plan->settings.difficulty = DIFFICULTY_STANDARD;
    }

    // The stream: the code's payload, so that every setting changes the whole placement.
    char code[RANDO_CODE_SIZE];
    rando_encode(&plan->settings, code);
    rando_rng rng = {rando_seed_of_text(code) ^ (plan->settings.seed << STREAM_SEED_SHIFT)};

    // A new fill each attempt, from the stream's next numbers, until one holds.
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        plan->attempts = attempt;

        // A fill that placed everything is played through once more from the start, to be sure.
        if (fill(&plan->settings, &rng, plan, attempt <= RELAXED_EARLY_ATTEMPTS) == 0
            && rando_beatable(plan, 0) && rando_stranded(plan) == 0) {
            return 0;
        }
    }

    return -1;
}

/* ---- the playthrough ---- */

/* Plays plan through from the start, sphere by sphere; 1 when the goal is reached. spheres, when
 * not null, gets each location's sphere (0 for the start, -1 for never). */
int rando_beatable(const rando_plan* plan, int* spheres) {
    prepare();
    const int d = plan->settings.difficulty;
    sweep w;
    start(&w);

    // Locations never reached stay at -1.
    if (spheres) {
        // Each location's sphere, which is never until the playthrough reaches it.
        for (size_t i = 0; i < rando_location_count; ++i) {
            spheres[i] = -1;
        }
    }

    // Sphere by sphere: what is reachable with what was had before it.
    for (int sphere = 0;; ++sphere) {
        uint8_t now[RANDO_MAX_LOCATIONS];
        size_t found = 0;

        // Which locations this sphere reaches, from what the earlier spheres gave.
        for (size_t i = 0; i < rando_location_count; ++i) {
            now[i] = !w.had[i] && reachable(&w, d, i);
            found += now[i];
        }

        // Nothing new reached: the playthrough is over.
        if (found == 0) {
            break;
        }

        // This sphere's places are had together, after the sphere is found, so each needs only
        // what earlier spheres gave.
        for (size_t i = 0; i < rando_location_count; ++i) {
            // The sphere's locations are had together.
            if (now[i]) {
                w.had[i] = 1;
                take(&w, plan->reward[i]);

                // Note the sphere the location was had in.
                if (spheres) {
                    spheres[i] = sphere;
                }
            }
        }
    }

    return w.had[goal()];
}

/* How many shuffled locations the playthrough never reaches. */
int rando_stranded(const rando_plan* plan) {
    prepare();
    sweep w;
    start(&w);
    play(&w, plan->settings.difficulty, plan->reward);
    int n = 0;

    // A shuffled location the whole game does not reach holds a reward nobody can have.
    for (size_t i = 0; i < rando_location_count; ++i) {
        n += plan->shuffled[i] && !w.had[i];
    }

    return n;
}

/* ---- the spoiler log ---- */

/* A bounded text buffer: used counts what was wanted, even past size. */
typedef struct out {
    char* p;
    size_t size, used;
} out;

#if defined(__GNUC__)
static void put(out* o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#endif

/* Appends printf-style text to o; what does not fit is counted but not stored. */
static void put(out* o, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char* at = o->used < o->size ? o->p + o->used : 0;
    const size_t room = o->used < o->size ? o->size - o->used : 0;
    const int n = vsnprintf(at, room, fmt, ap);
    va_end(ap);

    // A formatting error adds nothing.
    if (n > 0) {
        o->used += (size_t)n;
    }
}

/* The reward in words, into buf. */
void rando_reward_name(rando_reward r, char* buf, size_t n) {
    // The reward's kind picks its wording.
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

/* The log's title, the seed code, the difficulty and the pools. */
static void put_header(out* o, const rando_plan* plan, const char* mod_version) {
    const rando_settings* s = &plan->settings;
    char code[RANDO_CODE_SIZE];
    rando_encode(s, code);
    put(o, "Ratchet & Clank item randomizer %s: spoiler log\n\n", mod_version);
    put(o, "Seed code   %s\n", code);
    put(o, "Difficulty  %s\n", rando_difficulty_name(s->difficulty));
    put(o,
        "Shuffled    %s%s%s%s%s\n",
        (s->pools & POOL_WEAPONS) ? "weapons " : "",
        (s->pools & POOL_GADGETS) ? "gadgets " : "",
        (s->pools & POOL_GOLD) ? "gold-bolts " : "",
        (s->pools & POOL_PLANETS) ? "infobots " : "",
        (s->pools & POOL_VENDOR) ? "vendor " : "");
    put(o, "Start gear  %s\n\n", s->keep_start ? "kept" : "shuffled");
}

/* Every shuffled location's reward, grouped by planet; * marks a reward that opens the way. */
static void put_locations(out* o, const rando_plan* plan) {
    put(o, "Where everything is (* marks a reward that opens the way)\n");
    int level = -1;

    // Every shuffled location, grouped by planet.
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];

        // Locations that keep the game's own reward are not listed.
        if (!plan->shuffled[i]) {
            continue;
        }

        // A new planet gets its own heading.
        if (l->level != level) {
            level = l->level;
            put(o, "\n%s\n", rando_planet_name(level));
        }

        char r[64];
        rando_reward_name(plan->reward[i], r, sizeof r);
        put(o, "  %-52s %s%s\n", l->name, is_progression(plan->reward[i]) ? "* " : "  ", r);
    }
}

/* The playthrough: for each sphere, the progression rewards and the goal reached in it. */
static void put_playthrough(out* o, const rando_plan* plan, const int* spheres) {
    put(o, "\nA playthrough (each sphere needs only what the spheres before it give)\n");

    // One heading and its lines for each sphere that has any.
    for (int sphere = 0; sphere < RANDO_MAX_LOCATIONS; ++sphere) {
        int any = 0;

        // The sphere's locations that matter to the route.
        for (size_t i = 0; i < rando_location_count; ++i) {
            // Only this sphere's places, and of those only the ones that matter to the route.
            if (spheres[i] != sphere
                || !(is_progression(plan->reward[i]) || (rando_locations[i].flags & LOCF_GOAL))) {
                continue;
            }

            // The sphere's heading comes with its first line.
            if (!any) {
                put(o, "\nSphere %d\n", sphere + 1);
                any = 1;
            }

            char r[64];
            rando_reward_name(plan->reward[i], r, sizeof r);
            put(o,
                "  %-52s %s\n",
                rando_locations[i].name,
                (rando_locations[i].flags & LOCF_GOAL) ? "the end" : r);
        }
    }
}

/* Writes the spoiler log into text (at most size bytes, terminated); returns the length it needed
 * with the terminator, which is more than size when the text was cut. */
size_t rando_spoiler(const rando_plan* plan, const char* mod_version, char* text, size_t size) {
    out o = {text, size, 0};

    // An empty log is a valid one if nothing else fits.
    if (size > 0) {
        text[0] = 0;
    }

    put_header(&o, plan, mod_version);

    int spheres[RANDO_MAX_LOCATIONS];
    const int ok = rando_beatable(plan, spheres);
    put_locations(&o, plan);
    put_playthrough(&o, plan, spheres);
    put(&o, "\n%s\n", ok ? "Beatable." : "NOT BEATABLE: please report this seed code.");
    return o.used + 1;
}
