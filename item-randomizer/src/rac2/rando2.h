/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Going Commando: the item randomizer's core (README.md, Going Commando), apart from game memory so
 * that tests can run it: the game's item locations and what each needs (logic2.c), and the
 * placement with its solver and spoiler log (solver2.c). The seed code and the settings are the
 * shared ones (seed.c, config.c). Everything is integer arithmetic on fixed tables, so a seed gives
 * the same placement on every system.
 *
 * The item ids are GiveInventoryItem's (func_00288308 on NTSC-U v1.01): the index into the owned
 * bytes at 0x1A7AF8, the launcher's save editor's ids (launcher/core/src/save_edit_rac2.rs). The
 * levels are the game's own numbers (games/rac2 docs/LEVELS.md).
 */
#ifndef RANDO2_H
#define RANDO2_H

#include <stddef.h>
#include <stdint.h>

#include "rando.h"

#ifdef __cplusplus
extern "C" {
#endif

#define R2_ITEM_COUNT 56 /* the owned bytes, 0x1A7AF8..0x1A7B2F */

enum {
    R2_HELI_PACK = 2,
    R2_THRUSTER_PACK = 3,
    R2_HYDRO_PACK = 4,
    R2_MAPPER = 5,
    R2_ARMOR_MAGNETIZER = 7,
    R2_LEVITATOR = 8,
    R2_SWINGSHOT = 13,
    R2_SHEEPINATOR = 16,
    R2_GRAVITY_BOOTS = 19,
    R2_GRINDBOOTS = 20,
    R2_GLIDER = 21,
    R2_LANCER = 30,
    R2_DYNAMO = 36,
    R2_ELECTROLYZER = 38,
    R2_THERMANATOR = 39,
    R2_GRAVITY_BOMB = 42,
    R2_TRACTOR_BEAM = 46,
    R2_QWARK_STATUETTE = 49,
    R2_BOX_BREAKER = 50,
    R2_INFILTRATOR = 51,
    R2_CHARGE_BOOTS = 54,
    R2_HYPNOMATIC = 55,
};

/* The levels that hold locations (the game's numbers). */
enum {
    R2_OOZLA = 1,
    R2_MAKTAR = 2,
    R2_ENDAKO = 3,
    R2_BARLOW = 4,
    R2_TABORA = 8,
    R2_JOBA = 11,
    R2_TODANO = 12,
    R2_ARANOS_PRISON = 14,
    R2_DAMOSEL = 18,
    R2_YEEDIL = 20,
    R2_LEVEL_COUNT = 31,
};

#define R2_BIT(item) ((uint64_t)1 << (item))

/* A location's flags. */
#define R2F_JUMP 0x01u   /* the Heli-Pack or the Thruster-Pack, either */
#define R2F_DIRECT 0x02u /* the game writes the owned byte itself (no GiveInventoryItem) */
#define R2F_PRICEY 0x04u /* bought for many bolts: out of "relaxed"'s progression */
#define R2F_GOAL 0x08u   /* the end of the game: what every seed must reach */

typedef struct r2_location {
    const char* name; /* for the spoiler log: "Oozla: Megacorp scientist" */
    uint8_t level;    /* where it is */
    uint8_t item;     /* the item the game gives there: the location's key; 0 for the goal */
    uint8_t pool;     /* POOL_GADGETS or POOL_WEAPONS; 0: never shuffled */
    uint8_t flags;    /* R2F_* */
    uint64_t need;    /* items it cannot be had without, its planet's way there included */
} r2_location;

extern const r2_location r2_locations[];
extern const size_t r2_location_count;

/* The items some location needs: the solver places them first, where they can be had. */
extern const uint64_t r2_progression_items;

const char* r2_item_name(int item);
const char* r2_level_name(int level);

/* The vendor's stock (the table at 0x2403C0 on NTSC-U v1.01, func_002886C8): each weapon on sale
 * from the first time a level of at least this number loads. */
#define R2_VENDOR_COUNT 16
typedef struct r2_stock {
    uint8_t item;
    uint8_t level;
} r2_stock;
extern const r2_stock r2_vendor_stock[R2_VENDOR_COUNT];

/* ---- the placement (solver2.c) ---- */

#define R2_MAX_LOCATIONS 32

typedef struct r2_plan {
    rando_settings settings;
    uint8_t shuffled[R2_MAX_LOCATIONS]; /* 1: in a pool the settings shuffle */
    uint8_t reward[R2_MAX_LOCATIONS];   /* the item each location gives with this seed */
    uint8_t stock[R2_VENDOR_COUNT];     /* each vendor weapon's first level with this seed */
    int attempts;
} r2_plan;

/* Whether location i is shuffled with these settings (rac2 knows the gadget and weapon pools; the
 * vendor's is its stock, below). */
int r2_location_shuffled(const rando_settings* s, size_t i);
/* What location i needs with this difficulty. */
uint64_t r2_requirement(size_t i);
/* Fills plan for its settings: 0, or -1 when no placement held (never, for these tables). */
int r2_solve(const rando_settings* s, r2_plan* plan);
/* A playthrough: 1 when the goal is reached; spheres[i] the sphere location i is had in (-1 never). */
int r2_beatable(const r2_plan* plan, int* spheres);
/* The spoiler log; returns the length it needed. */
size_t r2_spoiler(const r2_plan* plan, const char* mod_version, char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* RANDO2_H */
