/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Prints the item randomizer's table (item-randomizer/src/logic.c) as the APWorld reads it: the
 * items, the locations and the events, with the ids the mod uses (src/ap_ids.h), as JSON on
 * standard output. The build packs it into rac1.apworld; nothing writes it into the source tree.
 *
 *   archipelago_dump_table           the JSON
 *   archipelago_dump_table --check   checks the ids and names are each used once (a test)
 */
#include <stdio.h>
#include <string.h>

#include "ap_ids.h"
#include "rando.h"

/* A text as a JSON string. */
static void put_string(const char* s) {
    putchar('"');

    for (; *s != 0; ++s) {
        if (*s == '"' || *s == '\\') {
            putchar('\\');
        }

        putchar(*s);
    }

    putchar('"');
}

/* A reward's item name in the multiworld: the item's own, "<Planet> Coordinates", "Gold Bolt". */
static void reward_name(rando_reward r, char* out, size_t size) {
    switch (r.kind) {
        case REWARD_ITEM:
            snprintf(out, size, "%s", rando_item_name(r.value));
            break;
        case REWARD_PLANET:
            snprintf(out, size, "%s Coordinates", rando_planet_name(r.value));
            break;
        case REWARD_GOLD_BOLT:
            snprintf(out, size, "Gold Bolt");
            break;
        default:
            out[0] = 0;
    }
}

/* The items of a mask, as a JSON list of names. */
static void put_mask(uint64_t mask) {
    int first = 1;
    putchar('[');

    for (int item = 0; item < RANDO_ITEM_COUNT; ++item) {
        if (mask & BIT(item)) {
            printf(first ? "" : ", ");
            put_string(rando_item_name(item));
            first = 0;
        }
    }

    putchar(']');
}

/* Whether the reward's item is in the multiworld's pool: a reward of some location. */
static int in_pool(rando_reward r) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];

        if (ap_is_location(l) && l->vanilla.kind == r.kind && l->vanilla.value == r.value) {
            return 1;
        }
    }

    return 0;
}

static const char* const k_kinds[] = {"give", "vendor", "start", "infobot", "gold", "fixed"};

static void dump(void) {
    char name[96];
    int first = 1;
    printf("{\n  \"game\": ");
    put_string(AP_GAME);
    printf(",\n  \"planets\": [");

    for (int p = 0; p < LEVEL_COUNT; ++p) {
        printf(p ? ", " : "");
        put_string(rando_planet_name(p));
    }

    printf("],\n  \"items\": [\n");

    // Each item, planet and the gold bolt that some location gives.
    for (int kind = REWARD_ITEM; kind <= REWARD_GOLD_BOLT; ++kind) {
        const int count = kind == REWARD_ITEM     ? RANDO_ITEM_COUNT
                          : kind == REWARD_PLANET ? LEVEL_COUNT
                                                  : 1;

        for (int v = 0; v < count; ++v) {
            const rando_reward r = {(uint8_t)kind, (uint8_t)v};

            if (!in_pool(r)) {
                continue;
            }

            const int progression = kind == REWARD_PLANET
                                    || (kind == REWARD_ITEM && (rando_progression_items & BIT(v)));
            reward_name(r, name, sizeof name);
            printf("%s    {\"id\": %lld, \"name\": ", first ? "" : ",\n", (long long)ap_item_id(r));
            put_string(name);
            printf(
                ", \"kind\": \"%s\", \"value\": %d, \"progression\": %s}",
                kind == REWARD_ITEM     ? "item"
                : kind == REWARD_PLANET ? "planet"
                                        : "gold_bolt",
                v,
                progression ? "true" : "false"
            );
            first = 0;
        }
    }

    printf("\n  ],\n  \"locations\": [\n");

    // Every row: a location of the multiworld, or an event (a fixed row).
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        printf("%s    {\"name\": ", i ? ",\n" : "");
        put_string(l->name);

        if (ap_is_location(l)) {
            printf(", \"id\": %lld", (long long)ap_location_id(l));
        } else {
            printf(", \"id\": null");
        }

        reward_name(l->vanilla, name, sizeof name);
        printf(
            ", \"kind\": \"%s\", \"level\": %d, \"pool\": %d, \"vanilla\": ",
            k_kinds[l->kind],
            l->level,
            l->pool
        );

        if (name[0] != 0) {
            put_string(name);
        } else {
            printf("null");
        }

        printf(
            ", \"costly\": %s, \"goal\": %s, \"step\": %d, \"hard\": ",
            (l->flags & LOCF_COSTLY) ? "true" : "false",
            (l->flags & LOCF_GOAL) ? "true" : "false",
            l->step
        );
        put_mask(l->hard);
        printf(", \"soft\": ");
        put_mask(l->soft);
        printf(", \"any\": ");
        put_mask(l->any);
        printf(", \"pricey\": %s, \"planets\": [", (l->flags & LOCF_PRICEY) ? "true" : "false");

        // The other planets it takes, by number, as the APWorld reads them.
        for (int p = 0, n = 0; p < LEVEL_COUNT; ++p) {
            // One entry per planet the row asks for.
            if (l->planets & (1u << p)) {
                printf("%s%d", n++ ? ", " : "", p);
            }
        }

        printf("]}");
    }

    printf("\n  ]\n}\n");
}

/* Every id and name used once, and the table inside the mod's bounds. */
static int check(void) {
    int bad = 0;

    if (rando_location_count > RANDO_MAX_LOCATIONS) {
        fprintf(
            stderr, "the table has %zu rows, past %d\n", rando_location_count, RANDO_MAX_LOCATIONS
        );
        bad = 1;
    }

    for (size_t i = 0; i < rando_location_count; ++i) {
        for (size_t j = i + 1; j < rando_location_count; ++j) {
            const rando_location* a = &rando_locations[i];
            const rando_location* b = &rando_locations[j];

            if (strcmp(a->name, b->name) == 0) {
                fprintf(stderr, "two rows are named %s\n", a->name);
                bad = 1;
            }

            if (ap_is_location(a) && ap_is_location(b) && ap_location_id(a) == ap_location_id(b)) {
                fprintf(stderr, "%s and %s have one id\n", a->name, b->name);
                bad = 1;
            }
        }

        const rando_reward r = rando_locations[i].vanilla;

        if (r.kind != REWARD_NONE && ap_reward_of(ap_item_id(r)).value != r.value) {
            fprintf(stderr, "%s's item id does not read back\n", rando_locations[i].name);
            bad = 1;
        }
    }

    printf(bad ? "table: FAILED\n" : "table: ok, %zu rows\n", rando_location_count);
    return bad;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--check") == 0) {
        return check();
    }

    dump();
    return 0;
}
