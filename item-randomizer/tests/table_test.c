/* SPDX-License-Identifier: GPL-3.0-or-later
 * The item randomizer's table (src/logic.c) against what is known of the game:
 *
 *   - both discs' levels (tests/data/discs.tsv, tools/disc_facts.py): every infobot and gold bolt
 *     of a disc has its row, at its level and slot, with its planet, and every row its moby;
 *   - what the game's own words say a place takes: each pinned here, a wrong row shows up;
 *   - a seed sweep proving the solver sees a broken placement: in solved placements of every kind
 *     of setting, each gadget moved to a place that needs it is reported, and a hand-made placement
 *     of each softlock the old table allowed is refused.
 *
 *   table_test DISCS.TSV [SEEDS]
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

/* The row of a location by its kind, level and key, or -1. */
static int row_of(int kind, int level, int key) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];

        if (l->kind == kind && l->level == level && l->key == key) {
            return (int)i;
        }
    }

    return -1;
}

/* The row whose name is this. */
static int named(const char* name) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (strcmp(rando_locations[i].name, name) == 0) {
            return (int)i;
        }
    }

    fprintf(stderr, "no row %s\n", name);
    exit(1);
}

static void check_disc(const char* path, const char* want) {
    FILE* f = fopen(path, "r");
    CHECK(f != NULL);
    char line[128];
    int bots = 0, golds = 0;
    uint8_t seen[RANDO_MAX_LOCATIONS] = {0};

    while (fgets(line, sizeof line, f) != NULL) {
        char disc[16], kind[16];
        int level, key, value;

        if (line[0] == '#'
            || sscanf(line, "%15s %15s %d %d %d", disc, kind, &level, &key, &value) != 5
            || strcmp(disc, want) != 0) {
            continue;
        }

        if (strcmp(kind, "gold") == 0) {
            const int r = row_of(LOC_GOLD, level, key);
            CHECK(r >= 0);
            seen[r] = 1;
            ++golds;
        } else if (key != 0xFF) {
            /* A placed infobot with no slot (255, Novalis's) never runs as one: its planet is 0. */
            const int r = row_of(LOC_INFOBOT, level, key);
            CHECK(r >= 0 && rando_locations[r].vanilla.value == value);
            seen[r] = 1;
            ++bots;
        }
    }

    fclose(f);
    CHECK(golds == 40 && bots >= 6);

    for (size_t i = 0; i < rando_location_count; ++i) {
        const int kind = rando_locations[i].kind;
        CHECK((kind != LOC_GOLD && kind != LOC_INFOBOT) || seen[i]);
    }

    printf("%s: %d gold bolts and %d infobots agree with the table\n", want, golds, bots);
}

/* The game's words, row by row. */
static void check_words(void) {
#define NEEDS(name, item) CHECK(rando_locations[named(name)].hard& BIT(item))
    NEEDS("Aridia: Skid's agent, after the race", ITEM_HOVERBOARD);
    CHECK(
        rando_locations[named("Aridia: Skid's agent, after the race")].planets
        & (1u << LEVEL_RILGAR)
    );
    NEEDS("Aridia: the Trespasser's pickup", ITEM_SWINGSHOT);
    NEEDS("Kerwan: Helga", ITEM_HELI_PACK);
    NEEDS("Kerwan: the infobot", ITEM_SWINGSHOT);
    NEEDS("Rilgar: the bouncer, the coordinates of Umbris", ITEM_HOVERBOARD);
    NEEDS("Umbris: the infobot", ITEM_HYDRODISPLACER);
    NEEDS("Batalia: the commando, the coordinates of Orxon", ITEM_GRINDBOOTS);
    NEEDS("Pokitaru: the resort owner", ITEM_PILOTS_HELMET);
    NEEDS("Pokitaru: the resort owner", ITEM_THRUSTER_PACK);
    NEEDS("Orxon: Ratchet's infobot", ITEM_O2_MASK);
    NEEDS("Hoven: the coordinates of Gemlik", ITEM_HYDRO_PACK);
    NEEDS("Gemlik: the coordinates of Oltanis", ITEM_MAGNEBOOTS);
    NEEDS("Gemlik: the coordinates of Oltanis", ITEM_PILOTS_HELMET);
    NEEDS("Quartu: in disguise, the coordinates of Drek's Fleet", ITEM_HOLOGUISE);
    NEEDS("Quartu: in disguise, the coordinates of Drek's Fleet", ITEM_HYDRO_PACK);
    NEEDS("Kalebo III: the Gadgetron CEO's race", ITEM_HOVERBOARD);
    NEEDS("Drek's Fleet: the infobot", ITEM_HYDRO_PACK);
#undef NEEDS
    CHECK(
        rando_locations[named("Gemlik: the coordinates of Oltanis")].any
        == (BIT(ITEM_DEVASTATOR) | BIT(ITEM_VISIBOMB))
    );
    /* Clank's part of Orxon asks for nothing. */
    CHECK(rando_locations[named("Orxon: Clank's infobot")].hard == 0);
    CHECK(rando_locations[named("Orxon: Clank finds the Magneboots")].hard == 0);

    /* No gold bolt is ever reachable before all that opens a way. */

    for (size_t i = 0; i < rando_location_count; ++i) {
        if (rando_locations[i].kind == LOC_GOLD) {
            CHECK(rando_locations[i].hard & BIT(ITEM_MAGNEBOOTS));
        }
    }
}

/* The location of reward r in p, or -1. */
static int where(const rando_plan* p, int kind, int value) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (p->reward[i].kind == kind && p->reward[i].value == value) {
            return (int)i;
        }
    }

    return -1;
}

static void swap_rewards(rando_plan* p, int a, int b) {
    const rando_reward t = p->reward[a];
    p->reward[a] = p->reward[b];
    p->reward[b] = t;
}

/* Every shuffled location that needs a gadget, given that gadget in p: the solver must see it. */
static long mutate(const rando_plan* solved) {
    static rando_plan p;
    long caught = 0;

    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];

        if (!solved->shuffled[i] || l->hard == 0) {
            continue;
        }

        for (int item = 0; item < RANDO_ITEM_COUNT; ++item) {
            const int from = (l->hard & BIT(item)) ? where(solved, REWARD_ITEM, item) : -1;

            if (from < 0 || !solved->shuffled[from] || from == (int)i) {
                continue;
            }

            p = *solved;
            swap_rewards(&p, from, (int)i);
            CHECK(!rando_beatable(&p, NULL) || rando_stranded(&p) > 0);
            ++caught;
        }
    }

    return caught;
}

/* The softlocks the table before this one allowed, each built by hand from a solved placement: the
 * O2 Mask's place behind the Thruster-Pack it takes, Skid's agent holding what the race takes... */
static void check_old_softlocks(void) {
    static rando_plan p;
    const rando_settings s = {12345, POOL_ALL, 1, DIFFICULTY_EXPERT};
    CHECK(rando_solve(&s, &p) == 0);

    /* The Thruster-Pack behind Orxon's Ratchet side, with the O2 Mask at the resort owner. */
    const int mask = where(&p, REWARD_ITEM, ITEM_O2_MASK);
    const int owner = named("Pokitaru: the resort owner");
    swap_rewards(&p, mask, owner);
    const int thrust = where(&p, REWARD_ITEM, ITEM_THRUSTER_PACK);
    const int ratchet = named("Orxon: Ratchet's infobot");
    swap_rewards(&p, thrust, ratchet);
    CHECK(!rando_beatable(&p, NULL) || rando_stranded(&p) > 0);

    /* The Hoverboard at Skid's agent, who wants the race won on it. */
    CHECK(rando_solve(&s, &p) == 0);
    swap_rewards(
        &p, where(&p, REWARD_ITEM, ITEM_HOVERBOARD), named("Aridia: Skid's agent, after the race")
    );
    CHECK(!rando_beatable(&p, NULL) || rando_stranded(&p) > 0);

    /* The Hydro-Pack on Drek's Fleet, whose tanker it swims. */
    CHECK(rando_solve(&s, &p) == 0);
    swap_rewards(&p, where(&p, REWARD_ITEM, ITEM_HYDRO_PACK), named("Drek's Fleet: the infobot"));
    CHECK(!rando_beatable(&p, NULL) || rando_stranded(&p) > 0);

    /* Gemlik's towers, on the game's own placement: either weapon before it will do, neither will
     * not. */
    memset(&p, 0, sizeof p);
    p.settings.difficulty = DIFFICULTY_EXPERT;

    for (size_t i = 0; i < rando_location_count; ++i) {
        p.reward[i] = rando_locations[i].vanilla;
    }

    CHECK(rando_beatable(&p, NULL));
    swap_rewards(&p, where(&p, REWARD_ITEM, ITEM_VISIBOMB), named("Oltanis: the PDA's seller"));
    CHECK(rando_beatable(&p, NULL));
    swap_rewards(
        &p, where(&p, REWARD_ITEM, ITEM_DEVASTATOR), named("Oltanis: the Morph-o-Ray's pickup")
    );
    CHECK(!rando_beatable(&p, NULL));
}

int main(int argc, char** argv) {
    CHECK(argc >= 2);
    const long seeds = argc > 2 ? atol(argv[2]) : 40;
    check_disc(argv[1], "pal");
    check_disc(argv[1], "ntsc");
    check_words();
    check_old_softlocks();

    static rando_plan p;
    long caught = 0;
    const int pools[] =
        {POOL_ALL, POOL_GADGETS, POOL_GADGETS | POOL_VENDOR, POOL_GADGETS | POOL_PLANETS};

    for (size_t k = 0; k < sizeof pools / sizeof pools[0]; ++k) {
        for (int d = 0; d <= DIFFICULTY_EXPERT; ++d) {
            for (long n = 0; n < seeds; ++n) {
                const rando_settings s =
                    {(uint64_t)n * 977u + 1u, (uint8_t)pools[k], 1, (uint8_t)d};
                CHECK(rando_solve(&s, &p) == 0);
                caught += mutate(&p);
            }
        }
    }

    printf("mutations: %ld broken placements, all seen\n", caught);
    return 0;
}
