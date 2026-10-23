/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: the item randomizer's locations and what each needs (rando.h, README.md).
 *
 * Where each row comes from:
 *   - the locations and what they give, from the decompilation (OpenRAC games/rac1): every call of
 *     GiveItem (func_L00_002618D8) and UnlockPlanet (func_L00_00261848) and the function around it,
 *     the gold bolt's update (GoldBoltUpdate, func_L01_00309078) and the vendor's stock
 *     (VendorStockLevelInit, func_L00_0023DB30);
 *   - which level each is on, the infobots' planets and mission slots, the gold bolts' slots and
 *     the vendor's weapon per level, from the PAL disc's level data read with OpenRAC's own
 *     readers (editor/gameplay.py; the infobot pvars' planet at +4, the gold bolt's slot at +0,
 *     the moby record's mission at +4; D_L00_001C4198 for the vendor). These are the same on
 *     NTSC-U: the levels are the same;
 *   - `step`, the order the game's own route reaches them in: the planets in the order the
 *     game's coordinates open them (each unlock's level), and within a planet the order its NPCs
 *     come in;
 *   - `hard` and `soft`, what a location needs. These are the one part not read from the game's
 *     code: they say how a level is played (the Hydrodisplacer drains Umbris, the O2 Mask is
 *     Orxon's air, ...), conservatively. `hard` is what the place cannot be reached or done
 *     without; `soft` what the game's route has there and a player expects to have. A row
 *     someone finds too strict or too loose is changed here, and the tests check every seed
 *     again (and that the game's own placement can still be finished at every difficulty).
 *
 * Not shuffled, and why:
 *   - the Persuader (Pokitaru): its NPC writes the owned and obtained bytes itself, without
 *     GiveItem (PokitaruVendorUpdate, func_L11_002F21B0), so there is nothing to give in its place;
 *   - the NPCs' planet coordinates (LOC_FIXED rows): they test their own story state, not one the
 *     randomizer can keep apart from the planet's flag; the infobots are the planets' pool;
 *   - the OmniWrench: it is in the game's new-game state, not given by any code;
 *   - the second Trespasser pickup (Moby188, no placed instance) and the O2 Mask's second route
 *     (Pokitaru's PickupUpdate, which writes the bytes itself): the mod's check each tick
 *     (item_randomizer.c) treats either as the location it stands for.
 */
#include "rando.h"

static const char* const k_items[RANDO_ITEM_COUNT] = {
    [ITEM_HELI_PACK] = "Heli-Pack",
    [ITEM_THRUSTER_PACK] = "Thruster-Pack",
    [ITEM_HYDRO_PACK] = "Hydro-Pack",
    [ITEM_SONIC_SUMMONER] = "Sonic Summoner",
    [ITEM_O2_MASK] = "O2 Mask",
    [ITEM_PILOTS_HELMET] = "Pilot's Helmet",
    [ITEM_WRENCH] = "OmniWrench",
    [ITEM_SUCK_CANNON] = "Suck Cannon",
    [ITEM_BOMB_GLOVE] = "Bomb Glove",
    [ITEM_DEVASTATOR] = "Devastator",
    [ITEM_SWINGSHOT] = "Swingshot",
    [ITEM_VISIBOMB] = "Visibomb Gun",
    [ITEM_TAUNTER] = "Taunter",
    [ITEM_BLASTER] = "Blaster",
    [ITEM_PYROCITOR] = "Pyrocitor",
    [ITEM_MINE_GLOVE] = "Mine Glove",
    [ITEM_WALLOPER] = "Walloper",
    [ITEM_TESLA_CLAW] = "Tesla Claw",
    [ITEM_GLOVE_OF_DOOM] = "Glove of Doom",
    [ITEM_MORPH_O_RAY] = "Morph-o-Ray",
    [ITEM_HYDRODISPLACER] = "Hydrodisplacer",
    [ITEM_RYNO] = "R.Y.N.O.",
    [ITEM_DRONE_DEVICE] = "Drone Device",
    [ITEM_DECOY_GLOVE] = "Decoy Glove",
    [ITEM_TRESPASSER] = "Trespasser",
    [ITEM_METAL_DETECTOR] = "Metal Detector",
    [ITEM_MAGNEBOOTS] = "Magneboots",
    [ITEM_GRINDBOOTS] = "Grindboots",
    [ITEM_HOVERBOARD] = "Hoverboard",
    [ITEM_HOLOGUISE] = "Hologuise",
    [ITEM_PDA] = "PDA",
    [ITEM_MAP_O_MATIC] = "Map-o-Matic",
    [ITEM_BOLT_GRABBER] = "Bolt Grabber",
    [ITEM_PERSUADER] = "Persuader",
};

static const char* const k_planets[LEVEL_COUNT] = {
    "Veldin",  "Novalis", "Aridia",     "Kerwan",       "Eudora",          "Rilgar", "Blarg",
    "Umbris",  "Batalia", "Gaspar",     "Orxon",        "Pokitaru",        "Hoven",  "Gemlik",
    "Oltanis", "Quartu",  "Kalebo III", "Drek's Fleet", "Veldin (return)",
};

const char* rando_item_name(int item) {
    return item >= 0 && item < RANDO_ITEM_COUNT && k_items[item] != 0 ? k_items[item]
                                                                      : "an unknown item";
}

const char* rando_planet_name(int planet) {
    return planet >= 0 && planet < LEVEL_COUNT ? k_planets[planet] : "an unknown planet";
}

/* The items that open the way somewhere: those some row below needs. */
#define HELI BIT(ITEM_HELI_PACK)
#define HYDROPACK BIT(ITEM_HYDRO_PACK)
#define O2 BIT(ITEM_O2_MASK)
#define HELMET BIT(ITEM_PILOTS_HELMET)
#define SWING BIT(ITEM_SWINGSHOT)
#define HYDRO BIT(ITEM_HYDRODISPLACER)
#define MAGNE BIT(ITEM_MAGNEBOOTS)
#define GRIND BIT(ITEM_GRINDBOOTS)
#define HOVER BIT(ITEM_HOVERBOARD)
#define HOLO BIT(ITEM_HOLOGUISE)
#define TRESP BIT(ITEM_TRESPASSER)

const uint64_t rando_progression_items =
    HELI | HYDROPACK | O2 | HELMET | SWING | HYDRO | MAGNE | GRIND | HOVER | HOLO | TRESP;

/* What a gold bolt asks beyond its level's: they are the levels' hidden corners. */
#define GOLD_SOFT (HELI | SWING)

#define ITEM(i) {REWARD_ITEM, (i)}
#define PLANET(p) {REWARD_PLANET, (p)}
#define GOLD {REWARD_GOLD_BOLT, 0}
#define NONE {REWARD_NONE, 0}

/* An NPC or pickup's item: weapons and gadgets are two pools. */
#define GIVE_W(name, level, item, step, flags, hard, soft)                                         \
    {name, LOC_GIVE, level, item, POOL_WEAPONS, ITEM(item), step, flags, hard, soft}
#define GIVE_G(name, level, item, step, flags, hard, soft)                                         \
    {name, LOC_GIVE, level, item, POOL_GADGETS, ITEM(item), step, flags, hard, soft}
#define VENDOR(name, level, item, step, flags)                                                     \
    {name, LOC_VENDOR, level, item, POOL_VENDOR, ITEM(item), step, flags, 0, 0}
#define INFOBOT(name, level, mission, planet, step, hard, soft)                                    \
    {name, LOC_INFOBOT, level, mission, POOL_PLANETS, PLANET(planet), step, 0, hard, soft}
#define GOLDBOLT(name, level, slot, step, hard, soft)                                              \
    {name, LOC_GOLD, level, slot, POOL_GOLD, GOLD, step, 0, hard, (soft) | GOLD_SOFT}
#define FIXED(name, level, reward, step, flags, hard, soft)                                        \
    {name, LOC_FIXED, level, 0, 0, reward, step, flags, hard, soft}

const rando_location rando_locations[] = {
    /* Veldin: the game's start. The Bomb Glove: VendorStockLevelInit gives it at a level start while
     * none is owned. The ship's way to Novalis is open from the start. */
    {"Veldin: the starting Bomb Glove",
     LOC_START,
     LEVEL_VELDIN,
     ITEM_BOMB_GLOVE,
     POOL_WEAPONS,
     ITEM(ITEM_BOMB_GLOVE),
     0,
     0,
     0,
     0},
    FIXED("Veldin: the way to Novalis", LEVEL_VELDIN, PLANET(LEVEL_NOVALIS), 1, 0, 0, 0),

    /* Novalis: the vendor's first weapon; the coordinates of Aridia (TalkingNpcUpdate,
     * func_L01_003004F0) and Kerwan (AgnoggShipUpdate, func_L01_002FC140). */
    VENDOR("Novalis: the vendor's Pyrocitor", LEVEL_NOVALIS, ITEM_PYROCITOR, 2, 0),
    FIXED("Novalis: the coordinates of Aridia", LEVEL_NOVALIS, PLANET(LEVEL_ARIDIA), 2, 0, 0, 0),
    FIXED(
        "Novalis: Agnogg's ship, the coordinates of Kerwan",
        LEVEL_NOVALIS,
        PLANET(LEVEL_KERWAN),
        3,
        0,
        0,
        0
    ),
    GOLDBOLT("Novalis: gold bolt 1", LEVEL_NOVALIS, 0, 3, 0, 0),
    GOLDBOLT("Novalis: gold bolt 2", LEVEL_NOVALIS, 1, 3, 0, 0),
    GOLDBOLT("Novalis: gold bolt 3", LEVEL_NOVALIS, 2, 3, 0, 0),

    /* Aridia: Skid's agent (SkidsAgentUpdate, func_L02_002E21F8), Skid McMarx
     * (SkidMcMarxAridiaUpdate, func_L02_002E2D88), the Trespasser's pickup (Moby1005Update,
     * class 1005). */
    GIVE_G("Aridia: Skid's agent", LEVEL_ARIDIA, ITEM_SONIC_SUMMONER, 4, 0, 0, 0),
    GIVE_G("Aridia: Skid McMarx", LEVEL_ARIDIA, ITEM_HOVERBOARD, 5, 0, 0, 0),
    GIVE_G("Aridia: the Trespasser's pickup", LEVEL_ARIDIA, ITEM_TRESPASSER, 6, 0, 0, 0),
    GOLDBOLT("Aridia: gold bolt 1", LEVEL_ARIDIA, 0, 7, 0, HOVER),
    GOLDBOLT("Aridia: gold bolt 2", LEVEL_ARIDIA, 1, 7, 0, HOVER),
    GOLDBOLT("Aridia: gold bolt 3", LEVEL_ARIDIA, 2, 7, 0, HOVER),
    GOLDBOLT("Aridia: gold bolt 4", LEVEL_ARIDIA, 3, 7, 0, HOVER),

    /* Kerwan: Big Al (GuideUpdate, func_L03_002DC920), Helga (QuestGiverUpdate,
     * func_L03_002DBC38) and her course, the infobot to Eudora. */
    GIVE_G("Kerwan: Big Al", LEVEL_KERWAN, ITEM_HELI_PACK, 8, 0, 0, 0),
    VENDOR("Kerwan: the vendor's Blaster", LEVEL_KERWAN, ITEM_BLASTER, 8, 0),
    GIVE_G("Kerwan: Helga", LEVEL_KERWAN, ITEM_SWINGSHOT, 9, 0, 0, HELI),
    INFOBOT("Kerwan: the infobot", LEVEL_KERWAN, 3, LEVEL_EUDORA, 10, 0, HELI | SWING),
    GOLDBOLT("Kerwan: gold bolt 1", LEVEL_KERWAN, 0, 11, 0, 0),
    GOLDBOLT("Kerwan: gold bolt 2", LEVEL_KERWAN, 1, 11, 0, 0),
    GOLDBOLT("Kerwan: gold bolt 3", LEVEL_KERWAN, 2, 11, 0, 0),

    /* Eudora: the Suck Cannon at the warp pad (WarpPortalUpdate, func_L04_002E2DF0), the
     * coordinates of Blarg (UnlockPlanetPad_Update, func_L04_002E4458). */
    VENDOR("Eudora: the vendor's Glove of Doom", LEVEL_EUDORA, ITEM_GLOVE_OF_DOOM, 12, 0),
    GIVE_W("Eudora: the warp pad", LEVEL_EUDORA, ITEM_SUCK_CANNON, 12, 0, 0, HELI | SWING),
    FIXED(
        "Eudora: the coordinates of Blarg",
        LEVEL_EUDORA,
        PLANET(LEVEL_BLARG),
        13,
        0,
        0,
        HELI | SWING
    ),
    GOLDBOLT("Eudora: gold bolt", LEVEL_EUDORA, 0, 13, 0, 0),

    /* Blarg: the Hydrodisplacer's pickup (class 1016, Moby1005Update), Fred (Moby1105Update,
     * func_L06_003024C8), the infobot to Rilgar and the shuttle (ShuttleUpdate, func_L06_003039B8),
     * which gives Rilgar too. */
    VENDOR("Blarg: the vendor's Taunter", LEVEL_BLARG, ITEM_TAUNTER, 14, 0),
    GIVE_G(
        "Blarg: the Hydrodisplacer's pickup",
        LEVEL_BLARG,
        ITEM_HYDRODISPLACER,
        14,
        0,
        0,
        HELI | SWING
    ),
    GIVE_G("Blarg: Fred", LEVEL_BLARG, ITEM_GRINDBOOTS, 15, 0, 0, HELI | SWING),
    INFOBOT("Blarg: the infobot", LEVEL_BLARG, 0, LEVEL_RILGAR, 16, 0, HELI | SWING),
    FIXED(
        "Blarg: the shuttle, the coordinates of Rilgar",
        LEVEL_BLARG,
        PLANET(LEVEL_RILGAR),
        16,
        0,
        0,
        HELI | SWING
    ),
    GOLDBOLT("Blarg: gold bolt 1", LEVEL_BLARG, 0, 17, 0, 0),
    GOLDBOLT("Blarg: gold bolt 2", LEVEL_BLARG, 1, 17, 0, 0),

    /* Rilgar: the R.Y.N.O. from the shady salesman (ShadySalesmanUpdate, func_L05_003195B0), bought
     * for 150 000 bolts; the bouncer's coordinates of Umbris (BouncerUpdate, func_L05_00318980),
     * after the hoverboard race. */
    VENDOR("Rilgar: the vendor's Mine Glove", LEVEL_RILGAR, ITEM_MINE_GLOVE, 18, 0),
    GIVE_W("Rilgar: the shady salesman", LEVEL_RILGAR, ITEM_RYNO, 18, LOCF_COSTLY, 0, HELI | SWING),
    FIXED(
        "Rilgar: the bouncer, the coordinates of Umbris",
        LEVEL_RILGAR,
        PLANET(LEVEL_UMBRIS),
        19,
        0,
        HOVER,
        HELI | SWING
    ),
    GOLDBOLT("Rilgar: gold bolt 1", LEVEL_RILGAR, 0, 20, 0, HYDRO | HOVER),
    GOLDBOLT("Rilgar: gold bolt 2", LEVEL_RILGAR, 1, 20, 0, HYDRO | HOVER),

    /* Umbris: drained with the Hydrodisplacer. The infobot to Batalia and the coordinates its
     * story gives too (Moby436Update, func_L07_002F6F20). */
    INFOBOT("Umbris: the infobot", LEVEL_UMBRIS, 3, LEVEL_BATALIA, 21, HYDRO, HELI | SWING),
    FIXED(
        "Umbris: the coordinates of Batalia",
        LEVEL_UMBRIS,
        PLANET(LEVEL_BATALIA),
        21,
        0,
        HYDRO,
        HELI | SWING
    ),
    GOLDBOLT("Umbris: gold bolt 1", LEVEL_UMBRIS, 0, 22, HYDRO, 0),
    GOLDBOLT("Umbris: gold bolt 2", LEVEL_UMBRIS, 1, 22, HYDRO, 0),

    /* Batalia: the plumber's Metal Detector (Moby1283Update, func_L08_00307A98); the commando's
     * coordinates of Orxon (CommandoUpdate, func_L08_003041A8) and the deserter's of Gaspar
     * (DeserterUpdate, func_L08_00306730). */
    VENDOR("Batalia: the vendor's Devastator", LEVEL_BATALIA, ITEM_DEVASTATOR, 23, LOCF_COSTLY),
    GIVE_G(
        "Batalia: the plumber", LEVEL_BATALIA, ITEM_METAL_DETECTOR, 23, 0, 0, HELI | SWING | GRIND
    ),
    FIXED(
        "Batalia: the commando, the coordinates of Orxon",
        LEVEL_BATALIA,
        PLANET(LEVEL_ORXON),
        24,
        0,
        0,
        HELI | SWING | GRIND
    ),
    FIXED(
        "Batalia: the deserter, the coordinates of Gaspar",
        LEVEL_BATALIA,
        PLANET(LEVEL_GASPAR),
        24,
        0,
        0,
        HELI | SWING | GRIND
    ),
    GOLDBOLT("Batalia: gold bolt 1", LEVEL_BATALIA, 0, 25, 0, GRIND),
    GOLDBOLT("Batalia: gold bolt 2", LEVEL_BATALIA, 1, 25, 0, GRIND),

    /* Gaspar: the Pilot's Helmet's pickup (Moby1290Update, class 1290). */
    VENDOR("Gaspar: the vendor's Walloper", LEVEL_GASPAR, ITEM_WALLOPER, 26, 0),
    GIVE_G(
        "Gaspar: the Pilot's Helmet's pickup",
        LEVEL_GASPAR,
        ITEM_PILOTS_HELMET,
        26,
        0,
        0,
        HELI | SWING
    ),
    GOLDBOLT("Gaspar: gold bolt 1", LEVEL_GASPAR, 0, 27, 0, 0),
    GOLDBOLT("Gaspar: gold bolt 2", LEVEL_GASPAR, 1, 27, 0, 0),

    /* Orxon: Clank's side has the infobot to Pokitaru; Ratchet's needs air (the O2 Mask, from
     * Pokitaru: the game's route comes back): the Magneboots (SpinningPickupUpdate,
     * func_L10_00299818) and the infobot to Hoven. */
    VENDOR("Orxon: the vendor's Visibomb Gun", LEVEL_ORXON, ITEM_VISIBOMB, 28, LOCF_COSTLY),
    INFOBOT("Orxon: Clank's infobot", LEVEL_ORXON, 2, LEVEL_POKITARU, 28, 0, 0),
    GIVE_G("Orxon: the Magneboots' pickup", LEVEL_ORXON, ITEM_MAGNEBOOTS, 34, 0, O2, HELI | SWING),
    INFOBOT("Orxon: Ratchet's infobot", LEVEL_ORXON, 4, LEVEL_HOVEN, 35, O2, HELI | SWING | MAGNE),
    GOLDBOLT("Orxon: gold bolt 1", LEVEL_ORXON, 0, 35, O2, MAGNE),
    GOLDBOLT("Orxon: gold bolt 2", LEVEL_ORXON, 1, 35, O2, MAGNE),

    /* Pokitaru: Bob (Moby90Update, func_L11_002D18D0), the resort owner's O2 Mask
     * (PokitaruGuideUpdate, func_L11_002D2670), the Persuader (not shuffled: logic.c's header). */
    VENDOR("Pokitaru: the vendor's Decoy Glove", LEVEL_POKITARU, ITEM_DECOY_GLOVE, 29, 0),
    GIVE_G("Pokitaru: Bob", LEVEL_POKITARU, ITEM_THRUSTER_PACK, 29, 0, 0, HELI | SWING),
    GIVE_G("Pokitaru: the resort owner", LEVEL_POKITARU, ITEM_O2_MASK, 30, 0, 0, HELI | SWING),
    FIXED("Pokitaru: the Persuader", LEVEL_POKITARU, ITEM(ITEM_PERSUADER), 31, 0, 0, HELI | SWING),
    GOLDBOLT("Pokitaru: gold bolt", LEVEL_POKITARU, 0, 32, 0, 0),

    /* Hoven: Edwina (EdwinaUpdate, func_L12_002EC8F0); the coordinates of Gemlik after the boss
     * (Moby1267Update, func_L12_00304920). */
    VENDOR("Hoven: the vendor's Drone Device", LEVEL_HOVEN, ITEM_DRONE_DEVICE, 36, 0),
    GIVE_G("Hoven: Edwina", LEVEL_HOVEN, ITEM_HYDRO_PACK, 36, 0, 0, HELI | SWING),
    FIXED(
        "Hoven: the coordinates of Gemlik",
        LEVEL_HOVEN,
        PLANET(LEVEL_GEMLIK),
        37,
        0,
        0,
        HELI | SWING | GRIND | HYDROPACK
    ),
    GOLDBOLT("Hoven: gold bolt 1", LEVEL_HOVEN, 0, 38, 0, GRIND | HYDROPACK),
    GOLDBOLT("Hoven: gold bolt 2", LEVEL_HOVEN, 1, 38, 0, GRIND | HYDROPACK),

    /* Gemlik Base: its walls are walked with the Magneboots; Qwark's fight gives Oltanis
     * (Moby1353Update, func_L13_0030CAE0). */
    FIXED(
        "Gemlik: the coordinates of Oltanis",
        LEVEL_GEMLIK,
        PLANET(LEVEL_OLTANIS),
        39,
        0,
        MAGNE,
        SWING | HELMET | HELI
    ),
    GOLDBOLT("Gemlik: gold bolt", LEVEL_GEMLIK, 0, 39, MAGNE, 0),

    /* Oltanis: the PDA (Moby851Update, func_L14_002FCEA8), the Morph-o-Ray's pickup
     * (CollectiblePickupUpdate, func_L14_00306BE0), Sam's coordinates of Quartu (Moby924Update,
     * func_L14_00300468). */
    VENDOR("Oltanis: the vendor's Tesla Claw", LEVEL_OLTANIS, ITEM_TESLA_CLAW, 40, LOCF_COSTLY),
    GIVE_G("Oltanis: the PDA's seller", LEVEL_OLTANIS, ITEM_PDA, 40, 0, 0, HELI | SWING),
    GIVE_W(
        "Oltanis: the Morph-o-Ray's pickup",
        LEVEL_OLTANIS,
        ITEM_MORPH_O_RAY,
        41,
        0,
        0,
        HELI | SWING | MAGNE
    ),
    FIXED(
        "Oltanis: Sam, the coordinates of Quartu",
        LEVEL_OLTANIS,
        PLANET(LEVEL_QUARTU),
        42,
        0,
        0,
        HELI | SWING | MAGNE
    ),
    GOLDBOLT("Oltanis: gold bolt 1", LEVEL_OLTANIS, 0, 43, 0, MAGNE | GRIND),
    GOLDBOLT("Oltanis: gold bolt 2", LEVEL_OLTANIS, 1, 43, 0, MAGNE | GRIND),
    GOLDBOLT("Oltanis: gold bolt 3", LEVEL_OLTANIS, 2, 43, 0, MAGNE | GRIND),
    GOLDBOLT("Oltanis: gold bolt 4", LEVEL_OLTANIS, 3, 43, 0, MAGNE | GRIND),

    /* Quartu: the Bolt Grabber's pickup (Moby1388Update, func_L15_002EBB38); the scientist's
     * coordinates of Kalebo III (UltramechScientistUpdate, func_L15_002EDB50); in disguise (the
     * Hologuise, from Kalebo III), the coordinates of Drek's Fleet (Moby1419Update,
     * func_L15_002EC8B0). */
    GIVE_G(
        "Quartu: the Bolt Grabber's pickup",
        LEVEL_QUARTU,
        ITEM_BOLT_GRABBER,
        44,
        0,
        0,
        HELI | SWING | MAGNE
    ),
    FIXED(
        "Quartu: the scientist, the coordinates of Kalebo III",
        LEVEL_QUARTU,
        PLANET(LEVEL_KALEBO),
        45,
        0,
        0,
        HELI | SWING | MAGNE
    ),
    FIXED(
        "Quartu: in disguise, the coordinates of Drek's Fleet",
        LEVEL_QUARTU,
        PLANET(LEVEL_FLEET),
        49,
        0,
        HOLO,
        HELI | SWING | MAGNE
    ),
    GOLDBOLT("Quartu: gold bolt 1", LEVEL_QUARTU, 0, 48, 0, MAGNE | GRIND),
    GOLDBOLT("Quartu: gold bolt 2", LEVEL_QUARTU, 1, 48, 0, MAGNE | GRIND),

    /* Kalebo III: the Gadgetron CEO's challenge (ChallengeNpcUpdate, func_L16_002E7C70) and the
     * help desk (WatcherUpdate, func_L16_002E45F8), on its grind rails. */
    GIVE_G(
        "Kalebo III: the Gadgetron CEO",
        LEVEL_KALEBO,
        ITEM_HOLOGUISE,
        46,
        0,
        GRIND,
        HELI | SWING | MAGNE
    ),
    GIVE_G(
        "Kalebo III: the help desk", LEVEL_KALEBO, ITEM_MAP_O_MATIC, 47, 0, 0, HELI | SWING | GRIND
    ),
    GOLDBOLT("Kalebo III: gold bolt 1", LEVEL_KALEBO, 0, 48, 0, GRIND | MAGNE),
    GOLDBOLT("Kalebo III: gold bolt 2", LEVEL_KALEBO, 1, 48, 0, GRIND | MAGNE),

    /* Drek's Fleet: the infobot home. */
    INFOBOT(
        "Drek's Fleet: the infobot", LEVEL_FLEET, 3, LEVEL_VELDIN2, 50, MAGNE, HELI | SWING | O2
    ),
    GOLDBOLT("Drek's Fleet: gold bolt 1", LEVEL_FLEET, 0, 51, MAGNE, O2),
    GOLDBOLT("Drek's Fleet: gold bolt 2", LEVEL_FLEET, 1, 51, MAGNE, O2),

    /* Veldin, again: Drek. */
    GOLDBOLT("Veldin (return): gold bolt 1", LEVEL_VELDIN2, 0, 52, 0, MAGNE | GRIND),
    GOLDBOLT("Veldin (return): gold bolt 2", LEVEL_VELDIN2, 1, 52, 0, MAGNE | GRIND),
    GOLDBOLT("Veldin (return): gold bolt 3", LEVEL_VELDIN2, 2, 52, 0, MAGNE | GRIND),
    FIXED(
        "Veldin (return): Drek",
        LEVEL_VELDIN2,
        NONE,
        53,
        LOCF_GOAL,
        SWING | MAGNE,
        HELI | GRIND | O2
    ),
};

const size_t rando_location_count = sizeof rando_locations / sizeof rando_locations[0];
