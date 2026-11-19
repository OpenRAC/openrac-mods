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

/* Items the game's hints ask for: required except on expert (logic.c's soft). */
static uint64_t soft(int d, uint64_t items) { return d == DIFFICULTY_EXPERT ? 0 : items; }

static uint64_t plan_hash(const rando_plan* p) {
    uint64_t h = 0xCBF29CE484222325ull;

    for (size_t i = 0; i < rando_location_count; ++i) {
        if(rando_locations[i].kind==LOC_GOLD_CASE && !p->shuffled[i])continue;
        if (p->settings.legacy_items && rando_locations[i].kind==LOC_SPECIAL && rando_locations[i].key!=5) continue;
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
        for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
            const uint64_t heli = BIT(ITEM_HELI_PACK), swing = BIT(ITEM_SWINGSHOT);
            if (!strcmp(a->name, "Kalebo III: the Gadgetron CEO's race"))
                CHECK(rando_requirement(d, i) == (BIT(ITEM_HOVERBOARD) | BIT(ITEM_GRINDBOOTS) | heli | swing));
            if (!strcmp(a->name, "Kalebo III: the help desk"))
                CHECK(rando_requirement(d, i) == (BIT(ITEM_GRINDBOOTS) | swing));
            if (!strcmp(a->name, "Drek's Fleet: the infobot"))
                CHECK(rando_requirement(d, i) == (BIT(ITEM_MAGNEBOOTS) | BIT(ITEM_O2_MASK) | swing
                    | soft(d, heli | BIT(ITEM_HYDRO_PACK))));
            if (!strcmp(a->name, "Drek's Fleet: the CodeBot")
                || !strcmp(a->name, "Quartu: the Bolt Grabber's pickup"))
                CHECK(rando_requirement(d, i) == (BIT(ITEM_HYDRO_PACK) | BIT(ITEM_O2_MASK)));
            if (!strcmp(a->name, "Veldin (return): Drek"))
                CHECK(rando_requirement(d, i) == (BIT(ITEM_MAGNEBOOTS) | BIT(ITEM_TRESPASSER)
                    | BIT(ITEM_HYDRODISPLACER) | BIT(ITEM_THRUSTER_PACK) | heli | swing
                    | (d == DIFFICULTY_EXPERT ? 0 : BIT(ITEM_O2_MASK))));
        }

        for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
            if (!strcmp(a->name, "Oltanis: the PDA's seller")) CHECK(rando_requirement(d, i) == (BIT(ITEM_MAGNEBOOTS)));
            if (!strcmp(a->name, "Oltanis: the Morph-o-Ray's pickup")) CHECK(rando_requirement(d, i) == (BIT(ITEM_SWINGSHOT)));
            if (!strcmp(a->name, "Oltanis: Sam, the coordinates of Quartu")) CHECK(rando_requirement(d, i) == (BIT(ITEM_GRINDBOOTS) | BIT(ITEM_SWINGSHOT)));
            if (!strcmp(a->name, "Quartu: the Bolt Grabber's pickup")) CHECK(rando_requirement(d, i) == (BIT(ITEM_HYDRO_PACK) | BIT(ITEM_O2_MASK)));
            if (!strcmp(a->name, "Quartu: the scientist, the coordinates of Kalebo III")) CHECK(rando_requirement(d, i) == (BIT(ITEM_SWINGSHOT)));
            if (!strcmp(a->name, "Quartu: in disguise, the coordinates of Drek's Fleet")) CHECK(rando_requirement(d, i) == (BIT(ITEM_HOLOGUISE) | BIT(ITEM_THRUSTER_PACK) | BIT(ITEM_HELI_PACK) | BIT(ITEM_SWINGSHOT) | soft(d, BIT(ITEM_HYDRO_PACK))));
        }

        for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
            if (!strcmp(a->name, "Hoven: the coordinates of Gemlik")) {
                const uint64_t expected = BIT(ITEM_HELI_PACK)
                    | soft(d, BIT(ITEM_DEVASTATOR) | BIT(ITEM_HYDRO_PACK));
                CHECK(rando_requirement(d, i) == expected);
            }
            if (!strcmp(a->name, "Hoven: the Foreman's Raritanium")) {
                CHECK(rando_requirement(d, i) == (BIT(ITEM_HELI_PACK) | BIT(ITEM_SWINGSHOT)
                    | soft(d, BIT(ITEM_HYDRO_PACK))));
            }
            if (!strcmp(a->name, "Gemlik: the coordinates of Oltanis")) {
                CHECK(((rando_requirement(d, i) & BIT(ITEM_TRESPASSER)) != 0)
                    == (d != DIFFICULTY_EXPERT));
            }
        }

        if (!strcmp(a->name, "Aridia: gold bolt 3")) {
            for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
                CHECK(rando_requirement(d, i) & BIT(ITEM_MAGNEBOOTS));
            }
        }
        if (!strcmp(a->name, "Novalis: gold bolt 2")) {
            for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
                CHECK(rando_requirement(d, i) & BIT(ITEM_HYDRO_PACK));
            }
        }
        if (!strcmp(a->name, "Hoven: gold bolt 2")) {
            for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
                CHECK(rando_requirement(d, i) & BIT(ITEM_HYDRODISPLACER));
            }
        }
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
            /* Currency repeats; every distinct inventory/coordinate reward appears once. */
            if (a->vanilla.kind != REWARD_NONE && a->vanilla.kind != REWARD_GOLD_BOLT) {
                CHECK(a->vanilla.kind != b->vanilla.kind || a->vanilla.value != b->vanilla.value);
            }

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
    int count[REWARD_SPECIAL + 1][64];
    memset(count, 0, sizeof count);

    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        CHECK(p->shuffled[i] == rando_location_shuffled(s, l));

        if (!p->shuffled[i]) {
            CHECK((l->kind==LOC_GOLD_CASE && p->reward[i].kind==REWARD_NONE)
                || (p->reward[i].kind == l->vanilla.kind && p->reward[i].value == l->vanilla.value));
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

    for (int k = 0; k <= REWARD_SPECIAL; ++k) {
        for (int v = 0; v < 64; ++v) {
            CHECK(count[k][v] == 0);
        }
    }
}

/* Hoven is already unlocked: the water obstacle still blocks Edwina.
 * Giving the Hydrodisplacer from an independent location must then open her check. */
static void check_edwina_water_route(void) {
    rando_plan p = {0};
    int edwina = -1, start = -1, provider = -1;
    int spheres[RANDO_MAX_LOCATIONS];
    for (size_t i = 0; i < rando_location_count; ++i) {
        p.reward[i] = rando_locations[i].vanilla;
        if (!strcmp(rando_locations[i].name, "Hoven: Edwina")) edwina = (int)i;
        if (rando_locations[i].kind == LOC_START) start = (int)i;
        if (!strcmp(rando_locations[i].name, "Novalis: the vendor's Pyrocitor")) provider = (int)i;
        if (p.reward[i].kind == REWARD_ITEM && (p.reward[i].value == ITEM_HYDRODISPLACER || p.reward[i].value == ITEM_SWINGSHOT))
            p.reward[i] = (rando_reward){REWARD_NONE, 0};
    }
    CHECK(edwina >= 0 && start >= 0 && provider >= 0);
    p.reward[start] = (rando_reward){REWARD_PLANET, LEVEL_HOVEN};
    for (int difficulty = DIFFICULTY_RELAXED; difficulty <= DIFFICULTY_EXPERT; ++difficulty) {
        p.settings.difficulty = (uint8_t)difficulty;
        p.reward[provider] = (rando_reward){REWARD_NONE, 0};
        rando_beatable(&p, spheres);
        CHECK(spheres[edwina] == -1);
        p.reward[provider] = (rando_reward){REWARD_ITEM, ITEM_HYDRODISPLACER};
        rando_beatable(&p, spheres);
        CHECK(spheres[edwina] >= 0);
    }
}

/* Unlocking Kerwan alone is sufficient to reach Helga. No Heli-Pack reward
 * exists anywhere in this plan, so inventory elsewhere cannot mask the rule. */
static void check_helga_without_heli_pack(void) {
    rando_plan p = {0};
    int helga = -1, start = -1;
    int spheres[RANDO_MAX_LOCATIONS];
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (!strcmp(rando_locations[i].name, "Kerwan: Helga")) helga = (int)i;
        if (rando_locations[i].kind == LOC_START) start = (int)i;
    }
    CHECK(helga >= 0 && start >= 0);
    p.reward[start] = (rando_reward){REWARD_PLANET, LEVEL_KERWAN};
    for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
        p.settings.difficulty = (uint8_t)d;
        rando_beatable(&p, spheres);
        CHECK(spheres[helga] >= 0);
    }
}

/* On expert, Eudora's warp-pad pickup requires Heli-Pack alone: no Swingshot or
 * Trespasser exists in this isolated playthrough (the others add the hinted Trespasser). */
static void check_eudora_warp_pad(void) {
    rando_plan p = {0};
    int pickup = -1, start = -1, provider = -1;
    int spheres[RANDO_MAX_LOCATIONS];
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        if (!strcmp(l->name, "Eudora: the warp pad")) pickup = (int)i;
        if (l->kind == LOC_START) start = (int)i;
        if (l->level == LEVEL_VELDIN && l->vanilla.kind == REWARD_PLANET
            && l->vanilla.value == LEVEL_NOVALIS) provider = (int)i;
    }
    CHECK(pickup >= 0 && start >= 0 && provider >= 0);
    p.reward[start] = (rando_reward){REWARD_PLANET, LEVEL_EUDORA};
    for (int d = DIFFICULTY_EXPERT; d <= DIFFICULTY_EXPERT; ++d) {
        p.settings.difficulty = (uint8_t)d;
        p.reward[provider] = (rando_reward){REWARD_NONE, 0};
        rando_beatable(&p, spheres);
        CHECK(spheres[pickup] == -1);
        p.reward[provider] = (rando_reward){REWARD_ITEM, ITEM_HELI_PACK};
        rando_beatable(&p, spheres);
        CHECK(spheres[pickup] >= 0);
    }
}

/* Isolate the three Rilgar routes, supplying only each route's required
 * inventory. Omitted gadgets have no reward anywhere in these plans. */
static void check_rilgar_routes(void) {
    const char* names[] = {"Rilgar: the shady salesman",
        "Rilgar: the bouncer, the coordinates of Umbris", "Rilgar: the Hoverboard race"};
    const int extra[] = {0, ITEM_SWINGSHOT, ITEM_HOVERBOARD};
    for (int route = 0; route < 3; ++route) {
        rando_plan p = {0};
        int target = -1, start = -1, heli = -1, supplier = -1;
        int spheres[RANDO_MAX_LOCATIONS];
        for (size_t i = 0; i < rando_location_count; ++i) {
            const rando_location* l = &rando_locations[i];
            if (!strcmp(l->name, names[route])) target = (int)i;
            if (l->kind == LOC_START) start = (int)i;
            if (l->level == LEVEL_VELDIN && l->vanilla.kind == REWARD_PLANET
                && l->vanilla.value == LEVEL_NOVALIS) heli = (int)i;
            if (l->kind == LOC_VENDOR && l->key == ITEM_MINE_GLOVE) supplier = (int)i;
        }
        CHECK(target >= 0 && start >= 0 && heli >= 0 && supplier >= 0);
        p.reward[start] = (rando_reward){REWARD_PLANET, LEVEL_RILGAR};
        if (extra[route]) p.reward[supplier] = (rando_reward){REWARD_ITEM, (uint8_t)extra[route]};
        for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
            p.settings.difficulty = (uint8_t)d;
            p.reward[heli] = (rando_reward){REWARD_NONE, 0};
            rando_beatable(&p, spheres);
            CHECK(spheres[target] == -1);
            p.reward[heli] = (rando_reward){REWARD_ITEM, ITEM_HELI_PACK};
            rando_beatable(&p, spheres);
            CHECK(spheres[target] >= 0);
        }
    }
}

/* The Blarg Hydrodisplacer pickup needs Trespasser alone. */
static void check_blarg_hydrodisplacer_pickup(void) {
    rando_plan p = {0};
    int target = -1, start = -1, provider = -1;
    int spheres[RANDO_MAX_LOCATIONS];
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        if (!strcmp(l->name, "Blarg: the Hydrodisplacer's pickup")) target = (int)i;
        if (l->kind == LOC_START) start = (int)i;
        if (l->level == LEVEL_VELDIN && l->vanilla.kind == REWARD_PLANET
            && l->vanilla.value == LEVEL_NOVALIS) provider = (int)i;
    }
    CHECK(target >= 0 && start >= 0 && provider >= 0);
    p.reward[start] = (rando_reward){REWARD_PLANET, LEVEL_BLARG};
    for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
        p.settings.difficulty = (uint8_t)d;
        p.reward[provider] = (rando_reward){REWARD_NONE, 0};
        rando_beatable(&p, spheres);
        CHECK(spheres[target] == -1);
        p.reward[provider] = (rando_reward){REWARD_ITEM, ITEM_TRESPASSER};
        rando_beatable(&p, spheres);
        CHECK(spheres[target] >= 0);
    }
}

/* Blarg's coordinate record (the NPC and its infobot are one check) is reachable with planet
 * access alone. No item rewards exist in this plan, including Heli-Pack and Swingshot. */
static void check_blarg_infobot_routes(void) {
    rando_plan p = {0};
    int start = -1, shuttle = -1;
    int spheres[RANDO_MAX_LOCATIONS];
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        if (l->kind == LOC_START) start = (int)i;
        if (!strcmp(l->name, "Blarg: the shuttle, the coordinates of Rilgar")) shuttle = (int)i;
    }
    CHECK(start >= 0 && shuttle >= 0);
    p.reward[start] = (rando_reward){REWARD_PLANET, LEVEL_BLARG};
    for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
        p.settings.difficulty = (uint8_t)d;
        rando_beatable(&p, spheres);
        CHECK(spheres[shuttle] >= 0);
    }
}

/* On expert, isolated Batalia routes have only their one required gadget, or none
 * (the others add the hinted Grindboots). */
static void check_batalia_routes(void) {
    const char* names[] = {"Batalia: the turret's commando",
        "Batalia: the commando, the coordinates of Orxon",
        "Batalia: the deserter, the coordinates of Gaspar"};
    const int required[] = {ITEM_MAGNEBOOTS, 0, ITEM_GRINDBOOTS};
    for (int route = 0; route < 3; ++route) {
        rando_plan p = {0};
        int target = -1, start = -1, provider = -1;
        int spheres[RANDO_MAX_LOCATIONS];
        for (size_t i = 0; i < rando_location_count; ++i) {
            const rando_location* l = &rando_locations[i];
            if (!strcmp(l->name, names[route])) target = (int)i;
            if (l->kind == LOC_START) start = (int)i;
            if (l->level == LEVEL_VELDIN && l->vanilla.kind == REWARD_PLANET
                && l->vanilla.value == LEVEL_NOVALIS) provider = (int)i;
        }
        CHECK(target >= 0 && start >= 0 && provider >= 0);
        p.reward[start] = (rando_reward){REWARD_PLANET, LEVEL_BATALIA};
        for (int d = DIFFICULTY_EXPERT; d <= DIFFICULTY_EXPERT; ++d) {
            p.settings.difficulty = (uint8_t)d;
            p.reward[provider] = (rando_reward){REWARD_NONE, 0};
            rando_beatable(&p, spheres);
            CHECK((spheres[target] >= 0) == (required[route] == 0));
            if (required[route]) {
                p.reward[provider] = (rando_reward){REWARD_ITEM, (uint8_t)required[route]};
                rando_beatable(&p, spheres);
                CHECK(spheres[target] >= 0);
            }
        }
    }
}

/* Gaspar's helmet route needs Heli-Pack but no Swingshot. */
static void check_gaspar_helmet_route(void) {
    rando_plan p = {0};
    int target = -1, start = -1, provider = -1;
    int spheres[RANDO_MAX_LOCATIONS];
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        if (!strcmp(l->name, "Gaspar: the Pilot's Helmet's pickup")) target = (int)i;
        if (l->kind == LOC_START) start = (int)i;
        if (l->level == LEVEL_VELDIN && l->vanilla.kind == REWARD_PLANET
            && l->vanilla.value == LEVEL_NOVALIS) provider = (int)i;
    }
    CHECK(target >= 0 && start >= 0 && provider >= 0);
    p.reward[start] = (rando_reward){REWARD_PLANET, LEVEL_GASPAR};
    for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
        p.settings.difficulty = (uint8_t)d;
        p.reward[provider] = (rando_reward){REWARD_NONE, 0};
        rando_beatable(&p, spheres);
        CHECK(spheres[target] == -1);
        p.reward[provider] = (rando_reward){REWARD_ITEM, ITEM_HELI_PACK};
        rando_beatable(&p, spheres);
        CHECK(spheres[target] >= 0);
    }
}

/* Pokitaru routes omit Swingshot in both modern and legacy generation. */
static void check_pokitaru_routes(void) {
    const char* names[] = {"Pokitaru: Bob", "Pokitaru: the resort owner", "Pokitaru: the Persuader"};
    for (int legacy = 0; legacy <= 1; ++legacy) {
        for (int route = 0; route < 3; ++route) {
            rando_plan p = {0};
            int target = -1, start = -1, heli = -1, vendor = -1, bob = -1;
            int spheres[RANDO_MAX_LOCATIONS];
            for (size_t i = 0; i < rando_location_count; ++i) {
                const rando_location* l = &rando_locations[i];
                if (!strcmp(l->name, names[route])) target = (int)i;
                if (l->kind == LOC_START) start = (int)i;
                if (l->level == LEVEL_VELDIN && l->vanilla.kind == REWARD_PLANET
                    && l->vanilla.value == LEVEL_NOVALIS) heli = (int)i;
                if (l->kind == LOC_VENDOR && l->key == ITEM_DECOY_GLOVE) vendor = (int)i;
                if (!strcmp(l->name, "Pokitaru: Bob")) bob = (int)i;
            }
            CHECK(target >= 0 && start >= 0 && heli >= 0 && vendor >= 0 && bob >= 0);
            p.settings.legacy_items = (uint8_t)legacy;
            p.reward[start] = (rando_reward){REWARD_PLANET, LEVEL_POKITARU};
            p.reward[heli] = (rando_reward){REWARD_ITEM, ITEM_HELI_PACK};
            if (route == 1) {
                p.reward[vendor] = (rando_reward){REWARD_ITEM, ITEM_THRUSTER_PACK};
            } else if (route == 2) {
                p.reward[bob] = legacy ? (rando_reward){REWARD_PLANET, LEVEL_HOVEN}
                    : (rando_reward){REWARD_SPECIAL, SPECIAL_RARITANIUM};
            }
            for (int d = DIFFICULTY_RELAXED; d <= DIFFICULTY_EXPERT; ++d) {
                p.settings.difficulty = (uint8_t)d;
                if (route == 1) p.reward[bob] = (rando_reward){REWARD_NONE, 0};
                if (route == 2) p.reward[vendor] = (rando_reward){REWARD_NONE, 0};
                rando_beatable(&p, spheres);
                CHECK((spheres[target] >= 0) == (route == 0));
                if (route == 1) p.reward[bob] = (rando_reward){REWARD_ITEM, ITEM_PILOTS_HELMET};
                if (route == 2) p.reward[vendor] = (rando_reward){REWARD_ITEM, ITEM_TRESPASSER};
                rando_beatable(&p, spheres);
                CHECK(spheres[target] >= 0);
            }
        }
    }
}

int main(int argc, char** argv) {
    const long n = argc > 1 ? atol(argv[1]) : 200;
    const long first = argc > 2 ? atol(argv[2]) : 0;
    check_tables();
    check_edwina_water_route();
    check_helga_without_heli_pack();
    check_eudora_warp_pad();
    check_rilgar_routes();
    check_blarg_hydrodisplacer_pickup();
    check_blarg_infobot_routes();
    check_batalia_routes();
    check_gaspar_helmet_route();
    check_pokitaru_routes();

    /* One fixed seed: its placement on every system. */
    static rando_plan p, q;
    const rando_settings fixed = {0x123456789Aull, POOL_ALL, 1, DIFFICULTY_STANDARD, 1};
    CHECK(rando_solve(&fixed, &p) == 0);
    printf(
        "fixed placement hash %016llx after %d attempt(s)\n",
        (unsigned long long)plan_hash(&p),
        p.attempts
    );
    CHECK(plan_hash(&p) == 0x90fce36b5c777277ull);

    /* Every setting, n seeds each. */
    long solved = 0;
    int worst = 0;

    /* The legacy pools, each with and without the Items-menu pool (NPC coordinates and gold
     * cases have their own tests). */
    for (int set = 0; set < 2 * ((int)POOL_ALL + 1); ++set) {
        const int pools = (set & (int)POOL_ALL) | (set > (int)POOL_ALL ? (int)POOL_ITEMS : 0);
        for (int keep = 0; keep <= 1; ++keep) {
            for (int d = 0; d <= DIFFICULTY_EXPERT; ++d) {
                rando_rng r = {
                    (uint64_t)first * 0x9E3779B97F4A7C15ull + (uint64_t)(set * 6 + keep * 3 + d)
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
    const rando_settings other = {fixed.seed + 1, POOL_ALL, 1, DIFFICULTY_STANDARD, 1};
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
