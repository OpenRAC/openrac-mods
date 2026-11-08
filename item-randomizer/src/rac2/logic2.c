/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Going Commando: the item locations and what each needs (rando2.h).
 *
 * Where the items are given, from the decompilation (rac2-decomp, NTSC-U v1.01): every call of
 * GiveInventoryItem (func_00288308) and every store into the owned bytes (0x1A7AF8 + id) in the
 * executable and the 27 level programs. The givers that test their own item's owned byte before
 * they give it (and so would vanish or stop selling once the player has that item from elsewhere)
 * are hook points of their own (rac2.moby.*.update): the mod shows them their location's state in
 * place of the owned byte while their update runs (rac2.c).
 *
 * What each place needs. The planets keep the game's order (their coordinates are not shuffled):
 * each planet's need is what its coordinates' giver needs plus the planet it is on, so a location's
 * `need` holds its whole way there. The needs of the places themselves follow the Archipelago world
 * for Going Commando (APRac2, MIT: Logic.py's rules with no glitches in logic), which agree with
 * the game's own tests where the code has one (Endako's Swingshot area, Barlow's Thermanator area,
 * the Hypnomatic maker's three parts). The coordinates' givers are the decompilation's
 * MarkLevelAvailable callers where a constant names the planet (Maktar for Endako, the Jamming
 * Array for Barlow, Barlow's race for Feltzin, Feltzin for Notak, Notak's seller for Siberius,
 * Siberius for Tabora, Dobbo for Joba and Hrugis, Hrugis for Todano, Gorn for Snivelak, Snivelak's
 * boss for Smolg, Smolg's seller for Damosel, Grelbin for Yeedil); the planets the level data
 * names (Dobbo, Boldan, Aranos Prison, Gorn, Grelbin) follow the game's story.
 */
#include "rando2.h"

#define B(i) R2_BIT(i)
#define HE B(R2_HELI_PACK)
#define DY B(R2_DYNAMO)
#define EL B(R2_ELECTROLYZER)
#define TR B(R2_TRACTOR_BEAM)
#define SW B(R2_SWINGSHOT)
#define GR B(R2_GRINDBOOTS)
#define TH B(R2_THERMANATOR)
#define GL B(R2_GLIDER)
#define GB B(R2_GRAVITY_BOOTS)
#define IN B(R2_INFILTRATOR)
#define LE B(R2_LEVITATOR)
#define HY B(R2_HYPNOMATIC)
#define QS B(R2_QWARK_STATUETTE)

/* Each planet's way there: what its coordinates' giver needs, and that giver's planet's. */
#define P_OOZLA 0
#define P_MAKTAR (P_OOZLA | DY)            /* Oozla: the store's end scene */
#define P_ENDAKO (P_MAKTAR | EL)           /* Maktar: the photo booth */
#define P_BARLOW (P_MAKTAR | TR)           /* Maktar: the Jamming Array */
#define P_NOTAK (P_BARLOW | EL)            /* Barlow's race (and a pack), then Feltzin */
#define P_SIBERIUS (P_NOTAK | HE | TH)     /* Notak: the worker bots */
#define P_TABORA (P_SIBERIUS | SW)         /* Siberius: the thief; Tabora's landing needs both */
#define P_DOBBO (P_TABORA)                 /* Tabora: Angela */
#define P_JOBA (P_DOBBO | DY)              /* Dobbo: the thug leader */
#define P_TODANO (P_DOBBO | GL | EL)       /* Dobbo's terminal: Hrugis, then Todano */
#define P_BOLDAN (P_TODANO | IN)           /* Todano: the rocket silo */
#define P_PRISON (P_BOLDAN | LE | GB | IN) /* Boldan: Fizzwidget; the prison's own needs */
#define P_SMOLG (P_PRISON | GR)            /* the prison, Gorn, Snivelak's Angela */
#define P_DAMOSEL (P_SMOLG)                /* Smolg: the balloon */
#define P_GRELBIN (P_DAMOSEL | GR)         /* Damosel */
#define P_YEEDIL (P_GRELBIN | HY)          /* Grelbin: Angela */

#define G POOL_GADGETS
#define W POOL_WEAPONS

const r2_location r2_locations[] = {
    {"Oozla: Dynamo outside the Megacorp store", R2_OOZLA, R2_DYNAMO, G, 0, P_OOZLA},
    {"Oozla: Megacorp scientist", R2_OOZLA, R2_TRACTOR_BEAM, G, 0, P_OOZLA},
    {"Oozla: Swamp monster's Box Breaker", R2_OOZLA, R2_BOX_BREAKER, G, 0, P_OOZLA | DY | GB},
    {"Maktar Nebula: arena's first challenge", R2_MAKTAR, R2_ELECTROLYZER, G, 0, P_MAKTAR},
    {"Endako: Clank's apartment, Swingshot", R2_ENDAKO, R2_SWINGSHOT, G, 0, P_ENDAKO},
    {"Endako: Clank's apartment, Grindboots", R2_ENDAKO, R2_GRINDBOOTS, G, R2F_DIRECT, P_ENDAKO},
    {"Endako: Clank rescued, Heli-Pack", R2_ENDAKO, R2_HELI_PACK, G, R2F_DIRECT, P_ENDAKO | EL},
    {"Endako: Clank rescued, Thruster-Pack", R2_ENDAKO, R2_THRUSTER_PACK, G, R2F_DIRECT, P_ENDAKO | EL},
    {"Endako: Clank rescued, Hydro-Pack", R2_ENDAKO, R2_HYDRO_PACK, G, R2F_DIRECT, P_ENDAKO | EL},
    {"Barlow: the inventor", R2_BARLOW, R2_THERMANATOR, G, 0, P_BARLOW | SW},
    {"Tabora: end of the underground mines", R2_TABORA, R2_GLIDER, G, 0, P_TABORA | HE | SW | TH},
    {"Joba: first hoverbike race", R2_JOBA, R2_CHARGE_BOOTS, G, 0, P_JOBA | SW},
    {"Joba: shady salesman", R2_JOBA, R2_LEVITATOR, G, R2F_JUMP, P_JOBA | DY},
    {"Joba: arena battle", R2_JOBA, R2_GRAVITY_BOOTS, G, R2F_JUMP, P_JOBA | DY | LE},
    {"Joba: arena cage match", R2_JOBA, R2_INFILTRATOR, G, R2F_JUMP, P_JOBA | DY | LE},
    {"Todano: facility interior", R2_TODANO, R2_SHEEPINATOR, W, 0, P_TODANO | EL | TR},
    {"Todano: Stuart Zurgo's trade", R2_TODANO, R2_ARMOR_MAGNETIZER, 0, 0, P_TODANO | EL | TR | QS},
    {"Aranos Prison: the plumber", R2_ARANOS_PRISON, R2_QWARK_STATUETTE, 0, 0, P_PRISON | GB | LE},
    {"Damosel: back from the Giant Clank battle", R2_DAMOSEL, R2_MAPPER, G, 0, P_DAMOSEL},
    /* The maker sells it with the three parts: Smolg's facility, Damosel's and Grelbin's. */
    {"Damosel: the Hypnomatic maker", R2_DAMOSEL, R2_HYPNOMATIC, G, R2F_JUMP | R2F_PRICEY,
     P_DAMOSEL | SW | TH | GR | IN | DY | EL | GL},
    {"Aranos (tutorial): the Lancer", 0, R2_LANCER, 0, 0, 0},
    {"Aranos (tutorial): the Gravity Bomb", 0, R2_GRAVITY_BOMB, 0, 0, 0},
    {"Yeedil: the mutated Protopet", R2_YEEDIL, 0, 0, R2F_JUMP | R2F_GOAL,
     P_YEEDIL | HY | SW | IN | DY | EL},
};
const size_t r2_location_count = sizeof r2_locations / sizeof r2_locations[0];

const uint64_t r2_progression_items = HE | B(R2_THRUSTER_PACK) | DY | EL | TR | SW | GR | TH | GL
                                      | GB | IN | LE | HY;

const r2_stock r2_vendor_stock[R2_VENDOR_COUNT] = {
    {22, 1}, {23, 3}, {24, 4}, {26, 1}, {27, 9},  {28, 11}, {29, 8},  {30, 0},
    {31, 6}, {32, 11}, {41, 3}, {42, 0}, {43, 14}, {45, 14}, {37, 8}, {25, 12},
};

/* The launcher's save editor's names (save_edit_rac2.rs), by id; NULL for an id with none. */
static const char* const k_items[R2_ITEM_COUNT] = {
    [2] = "Heli-Pack",        [3] = "Thruster-Pack",     [4] = "Hydro-Pack",
    [5] = "Mapper",           [7] = "Armor Magnetizer",  [8] = "Levitator",
    [9] = "Clank Zapper",     [12] = "Bomb Glove",       [13] = "Swingshot",
    [14] = "Visibomb Gun",    [16] = "Sheepinator",      [17] = "Decoy Glove",
    [18] = "Tesla Claw",      [19] = "Gravity Boots",    [20] = "Grindboots",
    [21] = "Glider",          [22] = "Chopper",          [23] = "Pulse Rifle",
    [24] = "Seeker Gun",      [25] = "Hoverbomb Gun",    [26] = "Blitz Gun",
    [27] = "Minirocket Tube", [28] = "Plasma Coil",      [29] = "Lava Gun",
    [30] = "Lancer",          [31] = "Synthenoid",       [32] = "Spiderbot Glove",
    [36] = "Dynamo",          [37] = "Bouncer",          [38] = "Electrolyzer",
    [39] = "Thermanator",     [41] = "Miniturret Glove", [42] = "Gravity Bomb",
    [43] = "Zodiac",          [44] = "RYNO II",          [45] = "Shield Charger",
    [46] = "Tractor Beam",    [49] = "Qwark Statuette",  [50] = "Box Breaker",
    [51] = "Infiltrator",     [53] = "Walloper",         [54] = "Charge Boots",
    [55] = "Hypnomatic",
};

const char* r2_item_name(int item) {
    if (item < 0 || item >= R2_ITEM_COUNT || k_items[item] == NULL) {
        return "an item";
    }
    return k_items[item];
}

static const char* const k_levels[R2_LEVEL_COUNT] = {
    "Aranos (tutorial)", "Oozla", "Maktar Nebula", "Endako", "Barlow", "Feltzin System", "Notak",
    "Siberius", "Tabora", "Dobbo", "Hrugis Cloud", "Joba", "Todano", "Boldan", "Aranos Prison",
    "Gorn", "Snivelak", "Smolg", "Damosel", "Grelbin", "Yeedil", NULL, "Dobbo Orbit",
    "Damosel Orbit", "Ship Shack", "Wupash Nebula", "Jamming Array", NULL, NULL, NULL,
    "Insomniac Museum",
};

const char* r2_level_name(int level) {
    if (level < 0 || level >= R2_LEVEL_COUNT || k_levels[level] == NULL) {
        return "a level";
    }
    return k_levels[level];
}
