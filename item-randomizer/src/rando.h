/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: the item randomizer's core (README.md), apart from game memory so that tests
 * can run it: the game's item locations and what each gives (logic.c), the seed code (seed.c),
 * and the placement with its solver and spoiler log (solver.c). Everything here is integer
 * arithmetic on fixed tables, so a seed gives the same placement on every system.
 *
 * Words used throughout:
 *   location  a place in the game that gives something once: an NPC or a pickup that gives an
 *             item, a slot of the Gadgetron vendor's stock, an infobot, a gold bolt, the starting
 *             Bomb Glove;
 *   reward    what a location gives: an item, a planet's coordinates, or a gold bolt;
 *   checked   a location the player has had; the game keeps that in its own save (logic.c says
 *             where for each kind), so the randomizer keeps nothing of its own in a save.
 */
#ifndef RANDO_H
#define RANDO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the game's ids (games/rac1: GiveItem's item ids, UnlockPlanet's planet ids) ---- */

#define RANDO_ITEM_COUNT 0x25 /* D_0013D5C8 / D_0013D5F0: a byte per item id */
#define RANDO_PLANET_COUNT 20 /* D_0013DE48: a byte per level */

enum {
    ITEM_HELI_PACK = 0x02,
    ITEM_THRUSTER_PACK = 0x03,
    ITEM_HYDRO_PACK = 0x04,
    ITEM_SONIC_SUMMONER = 0x05,
    ITEM_O2_MASK = 0x06,
    ITEM_PILOTS_HELMET = 0x07,
    ITEM_WRENCH = 0x08,
    ITEM_SUCK_CANNON = 0x09,
    ITEM_BOMB_GLOVE = 0x0A,
    ITEM_DEVASTATOR = 0x0B,
    ITEM_SWINGSHOT = 0x0C,
    ITEM_VISIBOMB = 0x0D,
    ITEM_TAUNTER = 0x0E,
    ITEM_BLASTER = 0x0F,
    ITEM_PYROCITOR = 0x10,
    ITEM_MINE_GLOVE = 0x11,
    ITEM_WALLOPER = 0x12,
    ITEM_TESLA_CLAW = 0x13,
    ITEM_GLOVE_OF_DOOM = 0x14,
    ITEM_MORPH_O_RAY = 0x15,
    ITEM_HYDRODISPLACER = 0x16,
    ITEM_RYNO = 0x17,
    ITEM_DRONE_DEVICE = 0x18,
    ITEM_DECOY_GLOVE = 0x19,
    ITEM_TRESPASSER = 0x1A,
    ITEM_METAL_DETECTOR = 0x1B,
    ITEM_MAGNEBOOTS = 0x1C,
    ITEM_GRINDBOOTS = 0x1D,
    ITEM_HOVERBOARD = 0x1E,
    ITEM_HOLOGUISE = 0x1F,
    ITEM_PDA = 0x20,
    ITEM_MAP_O_MATIC = 0x21,
    ITEM_BOLT_GRABBER = 0x22,
    ITEM_PERSUADER = 0x23,
};

/* Levels, which are also the planets (docs/OVERLAYS.md in games/rac1). */
enum {
    LEVEL_VELDIN = 0,
    LEVEL_NOVALIS,
    LEVEL_ARIDIA,
    LEVEL_KERWAN,
    LEVEL_EUDORA,
    LEVEL_RILGAR,
    LEVEL_BLARG,
    LEVEL_UMBRIS,
    LEVEL_BATALIA,
    LEVEL_GASPAR,
    LEVEL_ORXON,
    LEVEL_POKITARU,
    LEVEL_HOVEN,
    LEVEL_GEMLIK,
    LEVEL_OLTANIS,
    LEVEL_QUARTU,
    LEVEL_KALEBO,
    LEVEL_FLEET,
    LEVEL_VELDIN2,
    LEVEL_COUNT
};

#define BIT(item) ((uint64_t)1 << (item))

const char* rando_item_name(int item);
const char* rando_planet_name(int planet);

/* ---- rewards ---- */

enum {
    REWARD_NONE = 0,
    REWARD_ITEM,
    REWARD_PLANET,
    REWARD_GOLD_BOLT
};

typedef struct rando_reward {
    uint8_t kind;  /* REWARD_* */
    uint8_t value; /* the item id, the planet, 0 for a gold bolt */
} rando_reward;

/* ---- locations (logic.c) ---- */

enum {
    LOC_GIVE,    /* an NPC or a pickup: the game calls GiveItem(key), key its own item */
    LOC_VENDOR,  /* the vendor's slot for item key, in the stock from level `level` on */
    LOC_START,   /* the Bomb Glove the game gives at the first level start (key 0x0A) */
    LOC_INFOBOT, /* an infobot: key its moby's mission slot on `level` */
    LOC_GOLD,    /* a gold bolt: key its slot (0..3) on `level` */
    LOC_FIXED,   /* never shuffled: an NPC's planet coordinates, the Persuader, the goal */
};

/* The pools (the settings), each a set of locations whose rewards are shuffled together. */
#define POOL_WEAPONS 0x01u /* the NPCs' and pickups' weapons; the starting Bomb Glove */
#define POOL_GADGETS 0x02u /* the NPCs' and pickups' gadgets */
#define POOL_GOLD 0x04u    /* the gold bolts */
#define POOL_PLANETS 0x08u /* the infobots */
#define POOL_VENDOR 0x10u  /* the vendor's weapons */
#define POOL_ALL 0x1Fu

/* A location's flags. */
#define LOCF_COSTLY 0x01u /* bought for more bolts than a player has early on */
#define LOCF_GOAL 0x02u   /* beating the game: what every seed must reach */
#define LOCF_PRICEY 0x04u /* a purchase a player saves up for: out of "relaxed"'s progression */

typedef struct rando_location {
    const char* name;     /* for the spoiler log: "Kerwan: Helga" */
    uint8_t kind;         /* LOC_* */
    uint8_t level;        /* where it is (its planet must be unlocked) */
    uint8_t key;          /* LOC_GIVE/VENDOR/START: the item id; INFOBOT: mission; GOLD: slot */
    uint8_t pool;         /* POOL_*, 0 for LOC_FIXED */
    rando_reward vanilla; /* what the game gives there */
    uint16_t step;        /* the order the game's own route reaches it in */
    uint8_t flags;        /* LOCF_* */
    uint64_t hard;        /* items it cannot be had without (every difficulty) */
    uint64_t soft;        /* items the game's route has there and play expects (not "expert") */
    uint64_t any;         /* at least one of these items, when not 0 (every difficulty) */
    uint32_t planets;     /* 1 << level of each other planet it takes (every difficulty) */
} rando_location;

extern const rando_location rando_locations[];
extern const size_t rando_location_count;

/* The items that open the way somewhere (the solver places them first, where they can be had). */
extern const uint64_t rando_progression_items;

/* A tutorial message the game asks for right after it gives item. */
typedef struct rando_item_help {
    uint8_t item;
    uint16_t message;
} rando_item_help;

extern const rando_item_help rando_item_helps[];
extern const size_t rando_item_help_count;

/* A word every level's text has on both discs: "Infobot", said with a planet's name. */
#define RANDO_TEXT_INFOBOT 0x4E7C
#define RANDO_TEXT_GOLD_BOLT 0x4E9F

/* The message id of a reward's name, in every level's text; 0 for none. */
int rando_reward_text(rando_reward r);

/* Where a location's checked state is kept by the game (the randomizer's memory of what was had,
 * and what it reads back from a save):
 *   LOC_GIVE, LOC_VENDOR, LOC_START  the item's "obtained" byte (D_0013D5F0[key]): GiveItem sets it,
 *                                    and most of the game's pickups test it before they appear; the
 *                                    randomizer gives the reward's own item without its obtained
 *                                    byte, so that byte stays the location's;
 *   LOC_INFOBOT                      the level's trigger slot (D_0014C150[key + level * 16] = 0xFF:
 *                                    SetLevelTriggerDone, right after the infobot's UnlockPlanet);
 *   LOC_GOLD                         the gold bolt's byte (D_0014BFC0[level * 4 + key]). */

/* ---- settings and the seed code (seed.c) ---- */

enum {
    DIFFICULTY_RELAXED = 0,
    DIFFICULTY_STANDARD = 1,
    DIFFICULTY_EXPERT = 2
};

typedef struct rando_settings {
    uint64_t seed;      /* 40 bits */
    uint8_t pools;      /* POOL_* */
    uint8_t keep_start; /* 1: the starting Bomb Glove stays where it is */
    uint8_t difficulty; /* DIFFICULTY_* */
} rando_settings;

#define RANDO_SEED_BITS 40
#define RANDO_SEED_MASK ((((uint64_t)1) << RANDO_SEED_BITS) - 1)
#define RANDO_CODE_FORMAT 2
/* "XXXX-XXXX-XXXX": 12 characters of Crockford's base 32 and two hyphens, and the terminator. */
#define RANDO_CODE_SIZE 15

/* The settings' code: format, difficulty, starting gear, pools, seed and a checksum, 60 bits. */
void rando_encode(const rando_settings* s, char out[RANDO_CODE_SIZE]);
/* 1 and *s when text is a code (case, hyphens and spaces do not matter; I and L read as 1, O as
 * 0); 0 when it is not one, or its checksum or format is wrong. */
int rando_decode(const char* text, rando_settings* s);
/* A word that is not a code: its seed (64-bit FNV-1a of its bytes, folded to 40 bits). */
uint64_t rando_seed_of_text(const char* text);
/* What the "seed" setting says: a code (its settings win: *s is the code's, returns 2), a word
 * (s->seed its hash, the rest as given, returns 1), or nothing (returns 0, *s unchanged). */
int rando_parse_seed_setting(const char* text, rando_settings* s);
/* The settings' names, for the log and the spoiler: "standard", "relaxed", "expert". */
const char* rando_difficulty_name(int difficulty);
int rando_difficulty_of_name(const char* name); /* -1 for none */

/* ---- the random numbers: SplitMix64, the same on every system ---- */

typedef struct rando_rng {
    uint64_t state;
} rando_rng;

uint64_t rando_next(rando_rng* r);
/* Uniform in 0..n-1 (n > 0), without modulo bias. */
uint32_t rando_below(rando_rng* r, uint32_t n);

/* ---- the placement (solver.c) ---- */

#define RANDO_MAX_LOCATIONS 128

typedef struct rando_plan {
    rando_settings settings;
    uint8_t shuffled[RANDO_MAX_LOCATIONS];    /* 1: in a pool the settings shuffle */
    rando_reward reward[RANDO_MAX_LOCATIONS]; /* what each location gives with this seed */
    int attempts;                             /* fills tried before one held */
} rando_plan;

/* Whether a location is shuffled with these settings. */
int rando_location_shuffled(const rando_settings* s, const rando_location* l);
/* The items a location needs with this difficulty: expert its `hard`, the others `hard` and
 * `soft` (relaxed differs in where progression goes: solver.c). */
uint64_t rando_requirement(int difficulty, size_t location);

/* How many shuffled locations of plan cannot be had even at the end of the game; 0 for a placement
 * with no reward locked behind itself. */
int rando_stranded(const rando_plan* plan);

/* Fills plan for its settings. 0 on success; -1 when no placement was found (never, for the
 * tables here: the tests try many seeds of every setting). */
int rando_solve(const rando_settings* s, rando_plan* plan);

/* A playthrough of plan: what can be had, sphere by sphere, from the start. Returns 1 when the
 * goal is reached; spheres[i] is the sphere location i is had in (0 for the start, -1 never),
 * when spheres is not null. */
int rando_beatable(const rando_plan* plan, int* spheres);

/* The reward in words, for the log and the spoiler: "the coordinates of Rilgar", into buf. */
void rando_reward_name(rando_reward r, char* buf, size_t n);

/* The spoiler log: settings, code, where everything is (by planet) and a playthrough. Writes at
 * most size bytes with the terminator; returns the length it needed. */
size_t rando_spoiler(const rando_plan* plan, const char* mod_version, char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* RANDO_H */
