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
 *   - `hard`, `any` and `planets`, what a location cannot be had without, from the game's own words
 *     where it says so (the mission list and the help messages, gameplay's text: "Destroy Blarg
 *     ships for O2 Mask", "You need a Hoverboard to race"; the row's comment gives each), else what
 *     the game's route carries there. A row asks for too much rather than too little: what a row
 *     asks for needlessly only narrows the shuffle, what it leaves out strands a player;
 *   - `soft`, what a place can be done without but play there expects (not "expert").
 *
 * What the mission list says a place costs is not counted: the game's route always has it by then.
 * A slot or an NPC that sells for more than a player has early is LOCF_COSTLY or LOCF_PRICEY.
 *
 * A gold bolt asks for every gadget that opens a way (GOLD_NEED). The game does not say what each
 * one takes, and they are its hidden corners; so a gold bolt never holds what the way on needs.
 * This costs a seed nothing but a place: its reward is still had, late or early.
 *
 * Not shuffled, and why:
 *   - the Persuader (Pokitaru): its NPC writes the owned and obtained bytes itself, without
 *     GiveItem (PokitaruVendorUpdate, func_L11_002F21B0), so there is nothing to give in its place;
 *   - the NPCs' planet coordinates (LOC_FIXED rows): they test their own story state, not one the
 *     randomizer can keep apart from the planet's flag; the infobots are the planets' pool;
 *   - the OmniWrench: it is in the game's new-game state, not given by any code;
 *   - the second Trespasser pickup (update_trespasser_pickup, no placed instance) and the O2 Mask's
 *     second route (update_pokitaru_pickup, which writes the bytes itself): the mod's check each
 *     tick (item_randomizer.c) treats either as the location it stands for.
 */
#include "rando.h"

/* ---- names ---- */

/* The items' names for the spoiler log, indexed by the game's item id (games/rac1's GiveItem ids);
 * an id with no entry has none. */
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

/* The planets' names for the spoiler log, indexed by level (the levels are the planets). */
static const char* const k_planets[LEVEL_COUNT] = {
    "Veldin",  "Novalis", "Aridia",     "Kerwan",       "Eudora",          "Rilgar", "Blarg",
    "Umbris",  "Batalia", "Gaspar",     "Orxon",        "Pokitaru",        "Hoven",  "Gemlik",
    "Oltanis", "Quartu",  "Kalebo III", "Drek's Fleet", "Veldin (return)",
};

/* The item's name, or "an unknown item" for an id outside the table or without a name. */
const char* rando_item_name(int item) {
    return item >= 0 && item < RANDO_ITEM_COUNT && k_items[item] != 0 ? k_items[item]
                                                                      : "an unknown item";
}

/* The planet's name, or "an unknown planet" for a level outside the table. */
const char* rando_planet_name(int planet) {
    return planet >= 0 && planet < LEVEL_COUNT ? k_planets[planet] : "an unknown planet";
}

/* ---- the rows' vocabulary ---- */

/* The items that open the way somewhere: those some row below needs. */
#define HELI BIT(ITEM_HELI_PACK)
#define THRUST BIT(ITEM_THRUSTER_PACK)
#define HYDROPACK BIT(ITEM_HYDRO_PACK)
#define SONIC BIT(ITEM_SONIC_SUMMONER)
#define O2 BIT(ITEM_O2_MASK)
#define HELMET BIT(ITEM_PILOTS_HELMET)
#define SWING BIT(ITEM_SWINGSHOT)
#define HYDRO BIT(ITEM_HYDRODISPLACER)
#define MAGNE BIT(ITEM_MAGNEBOOTS)
#define GRIND BIT(ITEM_GRINDBOOTS)
#define HOVER BIT(ITEM_HOVERBOARD)
#define HOLO BIT(ITEM_HOLOGUISE)
#define TRESP BIT(ITEM_TRESPASSER)
#define DEVAST BIT(ITEM_DEVASTATOR)
#define VISI BIT(ITEM_VISIBOMB)

/* What the levels past Kerwan take to walk: the game's route always has these two there. */
#define WALK (HELI | SWING)

/* The items the solver places first: the union of the ones above. */
const uint64_t rando_progression_items = HELI | THRUST | HYDROPACK | SONIC | O2 | HELMET | SWING
                                         | HYDRO | MAGNE | GRIND | HOVER | HOLO | TRESP | DEVAST
                                         | VISI;

/* What a gold bolt is taken to need: every gadget that opens a way, since the game does not say. */
#define GOLD_NEED                                                                                  \
    (HELI | THRUST | HYDROPACK | SONIC | O2 | SWING | HYDRO | MAGNE | GRIND | HOVER | TRESP)

/* A planet as a row's `planets`. */
#define ON(level) (1u << (level))

/* A row's vanilla reward, as a rando_reward initialiser. */
#define ITEM(i) {REWARD_ITEM, (i)}
#define PLANET(p) {REWARD_PLANET, (p)}
#define GOLD {REWARD_GOLD_BOLT, 0}
#define NONE {REWARD_NONE, 0}

/* A row of each kind of location, as a rando_location initialiser: an NPC or pickup's item (the
 * weapons and the gadgets are two pools), a vendor slot, an infobot, a gold bolt, a fixed one. The
 * _X forms take `any` (at least one of these items) and `planets` too. */
#define GIVE_X(name, level, item, pool, step, flags, hard, soft, any, planets)                     \
    {name, LOC_GIVE, level, item, pool, ITEM(item), step, flags, hard, soft, any, planets}
#define GIVE_W(name, level, item, step, flags, hard, soft)                                         \
    GIVE_X(name, level, item, POOL_WEAPONS, step, flags, hard, soft, 0, 0)
#define GIVE_G(name, level, item, step, flags, hard, soft)                                         \
    GIVE_X(name, level, item, POOL_GADGETS, step, flags, hard, soft, 0, 0)
#define VENDOR(name, level, item, step, flags)                                                     \
    {name, LOC_VENDOR, level, item, POOL_VENDOR, ITEM(item), step, flags, 0, 0, 0, 0}
#define INFOBOT(name, level, mission, planet, step, hard, soft)                                    \
    {name, LOC_INFOBOT, level, mission, POOL_PLANETS, PLANET(planet), step, 0, hard, soft, 0, 0}
#define GOLDBOLT(name, level, slot, step)                                                          \
    {name, LOC_GOLD, level, slot, POOL_GOLD, GOLD, step, 0, GOLD_NEED, 0, 0, 0}
#define FIXED_X(name, level, reward, step, flags, hard, soft, any, planets)                        \
    {name, LOC_FIXED, level, 0, 0, reward, step, flags, hard, soft, any, planets}
#define FIXED(name, level, reward, step, flags, hard, soft)                                        \
    {name, LOC_FIXED, level, 0, 0, reward, step, flags, hard, soft, 0, 0}

/* ---- the locations ---- */

/* Every location of the game, by level in the order the game reaches them. Each row's columns are
 * the rando_location's (rando.h); the macros above fill the usual ones. The comment above each
 * level quotes what the game says it takes. */
const rando_location rando_locations[] = {
    // Veldin: the game's start. The Bomb Glove: init_level_item_inventory gives it at a level start
    // while none is owned. The ship's way to Novalis is open from the start.
    {"Veldin: the starting Bomb Glove",
     LOC_START,
     LEVEL_VELDIN,
     ITEM_BOMB_GLOVE,
     POOL_WEAPONS,
     ITEM(ITEM_BOMB_GLOVE),
     0,
     0,
     0,
     0,
     0,
     0},
    FIXED("Veldin: the way to Novalis", LEVEL_VELDIN, PLANET(LEVEL_NOVALIS), 1, 0, 0, 0),

    // Novalis: "Buy Infobot from mechanic" (the_plumber_update), "Find a new ship"
    // (update_agnogg_ship). Both take only walking.
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
    GOLDBOLT("Novalis: gold bolt 1", LEVEL_NOVALIS, 0, 3),
    GOLDBOLT("Novalis: gold bolt 2", LEVEL_NOVALIS, 1, 3),
    GOLDBOLT("Novalis: gold bolt 3", LEVEL_NOVALIS, 2, 3),

    // Aridia: "Eliminate sandsharks for Hoverboard" (Skid McMarx); "You will need a Gadgetron
    // Swingshot to traverse this area" on the way to the Trespasser's pickup; "If you want to
    // impress this Agent, you'll have to win a Hoverboard race ... in Blackwater City on Planet
    // Rilgar", "Talk to agent after winning race" (the Sonic Summoner).
    GIVE_G("Aridia: Skid McMarx", LEVEL_ARIDIA, ITEM_HOVERBOARD, 5, 0, 0, 0),
    GIVE_G("Aridia: the Trespasser's pickup", LEVEL_ARIDIA, ITEM_TRESPASSER, 6, 0, SWING, 0),
    GIVE_X(
        "Aridia: Skid's agent, after the race",
        LEVEL_ARIDIA,
        ITEM_SONIC_SUMMONER,
        POOL_GADGETS,
        21,
        0,
        HOVER,
        0,
        0,
        ON(LEVEL_RILGAR)
    ),
    GOLDBOLT("Aridia: gold bolt 1", LEVEL_ARIDIA, 0, 7),
    GOLDBOLT("Aridia: gold bolt 2", LEVEL_ARIDIA, 1, 7),
    GOLDBOLT("Aridia: gold bolt 3", LEVEL_ARIDIA, 2, 7),
    GOLDBOLT("Aridia: gold bolt 4", LEVEL_ARIDIA, 3, 7),

    // Kerwan: "Buy Heli-Pack from Al" at the landing; "You'll need your new Heli-Pack's Stretch
    // Jump feature to cross this gap" to the fitness course and "Buy Swingshot from trainer";
    // "You'll need your Swingshot to get to the next barge" to the infobot. The Thruster-Pack has
    // the same jumps in the help text, but the hero's code tests the Heli-Pack's owned byte for all
    // of them (hero_high_jump_input, update_hero_idle_and_item_input), so only that opens the gap.
    GIVE_G("Kerwan: Big Al", LEVEL_KERWAN, ITEM_HELI_PACK, 8, 0, 0, 0),
    VENDOR("Kerwan: the vendor's Blaster", LEVEL_KERWAN, ITEM_BLASTER, 8, 0),
    GIVE_G("Kerwan: Helga", LEVEL_KERWAN, ITEM_SWINGSHOT, 9, 0, HELI, 0),
    INFOBOT("Kerwan: the infobot", LEVEL_KERWAN, 3, LEVEL_EUDORA, 10, WALK, 0),
    GOLDBOLT("Kerwan: gold bolt 1", LEVEL_KERWAN, 0, 11),
    GOLDBOLT("Kerwan: gold bolt 2", LEVEL_KERWAN, 1, 11),
    GOLDBOLT("Kerwan: gold bolt 3", LEVEL_KERWAN, 2, 11),

    // Eudora: "You'll need a Gadgetron Trespasser to get past this security door": the game does
    // not say what lies behind it, so the Suck Cannon asks for it too.
    VENDOR("Eudora: the vendor's Glove of Doom", LEVEL_EUDORA, ITEM_GLOVE_OF_DOOM, 12, LOCF_PRICEY),
    GIVE_W("Eudora: the warp pad", LEVEL_EUDORA, ITEM_SUCK_CANNON, 12, 0, WALK | TRESP, 0),
    FIXED("Eudora: the coordinates of Blarg", LEVEL_EUDORA, PLANET(LEVEL_BLARG), 13, 0, WALK, 0),
    GOLDBOLT("Eudora: gold bolt", LEVEL_EUDORA, 0, 13),

    // Blarg: Clank "explore outside the station" without the O2 Mask, Ratchet with it: both lead
    // to the Hydrodisplacer's pickup, so it needs neither. "Buy Grindboots" from Fred.
    VENDOR("Blarg: the vendor's Taunter", LEVEL_BLARG, ITEM_TAUNTER, 14, 0),
    GIVE_G("Blarg: the Hydrodisplacer's pickup", LEVEL_BLARG, ITEM_HYDRODISPLACER, 14, 0, WALK, 0),
    GIVE_G("Blarg: Fred", LEVEL_BLARG, ITEM_GRINDBOOTS, 15, 0, WALK, 0),
    INFOBOT("Blarg: the infobot", LEVEL_BLARG, 0, LEVEL_RILGAR, 16, WALK, 0),
    FIXED(
        "Blarg: the shuttle, the coordinates of Rilgar",
        LEVEL_BLARG,
        PLANET(LEVEL_RILGAR),
        16,
        0,
        WALK,
        0
    ),
    GOLDBOLT("Blarg: gold bolt 1", LEVEL_BLARG, 0, 17),
    GOLDBOLT("Blarg: gold bolt 2", LEVEL_BLARG, 1, 17),

    // Rilgar: "You need a Hoverboard to race", "Bribe Bouncer with 4,000 bolts"; "Purchase the
    // R.Y.N.O." for 150,000 bolts.
    VENDOR("Rilgar: the vendor's Mine Glove", LEVEL_RILGAR, ITEM_MINE_GLOVE, 18, LOCF_PRICEY),
    GIVE_W("Rilgar: the shady salesman", LEVEL_RILGAR, ITEM_RYNO, 18, LOCF_COSTLY, WALK, 0),
    FIXED(
        "Rilgar: the bouncer, the coordinates of Umbris",
        LEVEL_RILGAR,
        PLANET(LEVEL_UMBRIS),
        19,
        0,
        HOVER | WALK,
        0
    ),
    GOLDBOLT("Rilgar: gold bolt 1", LEVEL_RILGAR, 0, 20),
    GOLDBOLT("Rilgar: gold bolt 2", LEVEL_RILGAR, 1, 20),

    // Umbris: "To progress any further in this area, you must have a Gadgetron Hydrodisplacer".
    INFOBOT("Umbris: the infobot", LEVEL_UMBRIS, 3, LEVEL_BATALIA, 22, HYDRO | WALK, 0),
    FIXED(
        "Umbris: the coordinates of Batalia",
        LEVEL_UMBRIS,
        PLANET(LEVEL_BATALIA),
        22,
        0,
        HYDRO | WALK,
        0
    ),
    GOLDBOLT("Umbris: gold bolt 1", LEVEL_UMBRIS, 0, 23),
    GOLDBOLT("Umbris: gold bolt 2", LEVEL_UMBRIS, 1, 23),

    // Batalia: "There is nothing you can do here without a pair of Grindboots". The commando at the
    // turret gives the Metal Detector and Orxon, the deserter sells Gaspar.
    VENDOR("Batalia: the vendor's Devastator", LEVEL_BATALIA, ITEM_DEVASTATOR, 24, LOCF_COSTLY),
    GIVE_G(
        "Batalia: the turret's commando", LEVEL_BATALIA, ITEM_METAL_DETECTOR, 24, 0, GRIND | WALK, 0
    ),
    FIXED(
        "Batalia: the commando, the coordinates of Orxon",
        LEVEL_BATALIA,
        PLANET(LEVEL_ORXON),
        25,
        0,
        GRIND | WALK,
        0
    ),
    FIXED(
        "Batalia: the deserter, the coordinates of Gaspar",
        LEVEL_BATALIA,
        PLANET(LEVEL_GASPAR),
        25,
        0,
        GRIND | WALK,
        0
    ),
    GOLDBOLT("Batalia: gold bolt 1", LEVEL_BATALIA, 0, 26),
    GOLDBOLT("Batalia: gold bolt 2", LEVEL_BATALIA, 1, 26),

    // Gaspar: "Find the Pilot's Helmet".
    VENDOR("Gaspar: the vendor's Walloper", LEVEL_GASPAR, ITEM_WALLOPER, 27, LOCF_PRICEY),
    GIVE_G("Gaspar: the Pilot's Helmet's pickup", LEVEL_GASPAR, ITEM_PILOTS_HELMET, 27, 0, WALK, 0),
    GOLDBOLT("Gaspar: gold bolt 1", LEVEL_GASPAR, 0, 28),
    GOLDBOLT("Gaspar: gold bolt 2", LEVEL_GASPAR, 1, 28),

    // Orxon: "Clank will have to take care of this one": landing without the O2 Mask starts Clank
    // (update_hero_relocation_trigger), and he finds the Magneboots ("your feet appear to be too
    // small to equip them") and "Catch the Infobot". The mod shows the game no O2 Mask until that is
    // done, so these need nothing. "Explore as Ratchet" takes the mask.
    VENDOR("Orxon: the vendor's Visibomb Gun", LEVEL_ORXON, ITEM_VISIBOMB, 29, LOCF_COSTLY),
    GIVE_G("Orxon: Clank finds the Magneboots", LEVEL_ORXON, ITEM_MAGNEBOOTS, 29, 0, 0, 0),
    INFOBOT("Orxon: Clank's infobot", LEVEL_ORXON, 2, LEVEL_POKITARU, 29, 0, 0),
    INFOBOT("Orxon: Ratchet's infobot", LEVEL_ORXON, 4, LEVEL_HOVEN, 36, O2 | MAGNE | WALK, 0),
    GOLDBOLT("Orxon: gold bolt 1", LEVEL_ORXON, 0, 36),
    GOLDBOLT("Orxon: gold bolt 2", LEVEL_ORXON, 1, 36),

    // Pokitaru: "Buy Thrusterpack from Bob", "If you're going to open the jet fighter garage, you
    // should stop by Bob's Roboshack" (the Power Slam switch), "You need the Pilot's Helmet to fly
    // this plane", "Destroy Blarg ships for O2 Mask": the mask takes all of them. The Persuader
    // ("Trade Raritanium for Persuader") stays with its NPC.
    VENDOR("Pokitaru: the vendor's Decoy Glove", LEVEL_POKITARU, ITEM_DECOY_GLOVE, 30, LOCF_PRICEY),
    GIVE_G("Pokitaru: Bob", LEVEL_POKITARU, ITEM_THRUSTER_PACK, 30, 0, WALK, 0),
    GIVE_G(
        "Pokitaru: the resort owner", LEVEL_POKITARU, ITEM_O2_MASK, 31, 0, THRUST | HELMET | WALK, 0
    ),
    FIXED_X(
        "Pokitaru: the Persuader",
        LEVEL_POKITARU,
        ITEM(ITEM_PERSUADER),
        38,
        0,
        WALK,
        0,
        0,
        ON(LEVEL_HOVEN)
    ),
    GOLDBOLT("Pokitaru: gold bolt", LEVEL_POKITARU, 0, 33),

    // Hoven: "With this Hydro-Pack, Ratchet can travel more quickly underwater. Get it, you'll need
    // it." "You need a Devastator to take out that helicopter!" (play expects it; the game's hint,
    // not a wall).
    VENDOR("Hoven: the vendor's Drone Device", LEVEL_HOVEN, ITEM_DRONE_DEVICE, 37, LOCF_PRICEY),
    GIVE_G("Hoven: Edwina", LEVEL_HOVEN, ITEM_HYDRO_PACK, 37, 0, WALK, 0),
    FIXED(
        "Hoven: the coordinates of Gemlik",
        LEVEL_HOVEN,
        PLANET(LEVEL_GEMLIK),
        38,
        0,
        GRIND | HYDROPACK | WALK,
        DEVAST
    ),
    GOLDBOLT("Hoven: gold bolt 1", LEVEL_HOVEN, 0, 39),
    GOLDBOLT("Hoven: gold bolt 2", LEVEL_HOVEN, 1, 39),

    // Gemlik Base: its walls are walked with the Magneboots. "Those pulsing towers control the
    // force fields. In order to knock them all out, you're going to need the Gadgetron Devastator
    // or the Visibomb Gun." "Shoot down Captain Qwark" in a fighter: the Pilot's Helmet.
    FIXED_X(
        "Gemlik: the coordinates of Oltanis",
        LEVEL_GEMLIK,
        PLANET(LEVEL_OLTANIS),
        40,
        0,
        MAGNE | HELMET | WALK,
        0,
        DEVAST | VISI,
        0
    ),
    GOLDBOLT("Gemlik: gold bolt", LEVEL_GEMLIK, 0, 40),

    // Oltanis: "Purchase Gadgetron PDA from 'Steve'", the Morph-o-Ray's pickup, "Buy Infobot from
    // scrap merchant" (Sam).
    VENDOR("Oltanis: the vendor's Tesla Claw", LEVEL_OLTANIS, ITEM_TESLA_CLAW, 41, LOCF_COSTLY),
    GIVE_G("Oltanis: the PDA's seller", LEVEL_OLTANIS, ITEM_PDA, 41, 0, WALK, 0),
    GIVE_W(
        "Oltanis: the Morph-o-Ray's pickup", LEVEL_OLTANIS, ITEM_MORPH_O_RAY, 42, 0, MAGNE | WALK, 0
    ),
    FIXED(
        "Oltanis: Sam, the coordinates of Quartu",
        LEVEL_OLTANIS,
        PLANET(LEVEL_QUARTU),
        43,
        0,
        MAGNE | WALK,
        0
    ),
    GOLDBOLT("Oltanis: gold bolt 1", LEVEL_OLTANIS, 0, 44),
    GOLDBOLT("Oltanis: gold bolt 2", LEVEL_OLTANIS, 1, 44),
    GOLDBOLT("Oltanis: gold bolt 3", LEVEL_OLTANIS, 2, 44),
    GOLDBOLT("Oltanis: gold bolt 4", LEVEL_OLTANIS, 3, 44),

    // Quartu: "Find out more about the Ultra-Mech" (the scientist). "You'll need the Gadgetron
    // Hologuise to explore the factory" and "You will need a Hydro-Pack upgrade to swim through
    // those Tri-Rings and open the door", on the way to the secure area and Drek's Fleet.
    GIVE_G(
        "Quartu: the Bolt Grabber's pickup", LEVEL_QUARTU, ITEM_BOLT_GRABBER, 45, 0, MAGNE | WALK, 0
    ),
    FIXED(
        "Quartu: the scientist, the coordinates of Kalebo III",
        LEVEL_QUARTU,
        PLANET(LEVEL_KALEBO),
        46,
        0,
        MAGNE | WALK,
        0
    ),
    FIXED(
        "Quartu: in disguise, the coordinates of Drek's Fleet",
        LEVEL_QUARTU,
        PLANET(LEVEL_FLEET),
        50,
        0,
        HOLO | HYDROPACK | MAGNE | WALK,
        0
    ),
    GOLDBOLT("Quartu: gold bolt 1", LEVEL_QUARTU, 0, 49),
    GOLDBOLT("Quartu: gold bolt 2", LEVEL_QUARTU, 1, 49),

    // Kalebo III: "Win the Hoverboard race ... Race for Hologuise" ("If you want to enter this
    // race, you need a hoverboard"), "Ride the Grind Rail" to the help desk.
    GIVE_G(
        "Kalebo III: the Gadgetron CEO's race",
        LEVEL_KALEBO,
        ITEM_HOLOGUISE,
        47,
        0,
        HOVER | GRIND | MAGNE | WALK,
        0
    ),
    GIVE_G("Kalebo III: the help desk", LEVEL_KALEBO, ITEM_MAP_O_MATIC, 48, 0, GRIND | WALK, 0),
    GOLDBOLT("Kalebo III: gold bolt 1", LEVEL_KALEBO, 0, 49),
    GOLDBOLT("Kalebo III: gold bolt 2", LEVEL_KALEBO, 1, 49),

    // Drek's Fleet: "This huge tanker ... you'll need to use your Hydro-Pack to get through
    // something this big", "Swim through the water tanker" (the O2 Mask breathes underwater).
    INFOBOT(
        "Drek's Fleet: the infobot",
        LEVEL_FLEET,
        3,
        LEVEL_VELDIN2,
        51,
        MAGNE | HYDROPACK | O2 | WALK,
        0
    ),
    GOLDBOLT("Drek's Fleet: gold bolt 1", LEVEL_FLEET, 0, 52),
    GOLDBOLT("Drek's Fleet: gold bolt 2", LEVEL_FLEET, 1, 52),

    // Veldin, again: "Save Ratchet's planet".
    GOLDBOLT("Veldin (return): gold bolt 1", LEVEL_VELDIN2, 0, 53),
    GOLDBOLT("Veldin (return): gold bolt 2", LEVEL_VELDIN2, 1, 53),
    GOLDBOLT("Veldin (return): gold bolt 3", LEVEL_VELDIN2, 2, 53),
    FIXED(
        "Veldin (return): Drek", LEVEL_VELDIN2, NONE, 54, LOCF_GOAL, MAGNE | GRIND | O2 | WALK, 0
    ),
};

/* How many rows the table has. */
const size_t rando_location_count = sizeof rando_locations / sizeof rando_locations[0];

/* ---- what the game says of an item ---- */

/* The tutorials the game asks for right after it gives an item, the same ids on both discs. Each
 * belongs to the item; given something else in its place, the message would teach what is not
 * there. */
const rando_item_help rando_item_helps[] = {
    {ITEM_HOVERBOARD, 0x7D0},
    {ITEM_TRESPASSER, 0x7D9},
    {ITEM_GRINDBOOTS, 0x1775},
    {ITEM_PILOTS_HELMET, 0x2328},
    {ITEM_O2_MASK, 0x2AF8},
    {ITEM_THRUSTER_PACK, 0x2AFB},
    {ITEM_MORPH_O_RAY, 0x36B0},
    {ITEM_MAP_O_MATIC, 0x3E82},
};
const size_t rando_item_help_count = sizeof rando_item_helps / sizeof rando_item_helps[0];

/* The game's words for a reward: every level has these, unlike the ones a level gives its own items
 * in, and each is in the player's language (help_msgs). */
static const uint16_t k_item_names[RANDO_ITEM_COUNT] = {
    [ITEM_HELI_PACK] = 0x4E6E,
    [ITEM_THRUSTER_PACK] = 0x4E6D,
    [ITEM_HYDRO_PACK] = 0x4E6F,
    [ITEM_SONIC_SUMMONER] = 0x4E6A,
    [ITEM_O2_MASK] = 0x4E6B,
    [ITEM_PILOTS_HELMET] = 0x4E6C,
    [ITEM_WRENCH] = 0x4E46,
    [ITEM_SUCK_CANNON] = 0x4E4A,
    [ITEM_BOMB_GLOVE] = 0x4E48,
    [ITEM_DEVASTATOR] = 0x4E4D,
    [ITEM_SWINGSHOT] = 0x4E61,
    [ITEM_VISIBOMB] = 0x4E4F,
    [ITEM_TAUNTER] = 0x4E51,
    [ITEM_BLASTER] = 0x4E47,
    [ITEM_PYROCITOR] = 0x4E49,
    [ITEM_MINE_GLOVE] = 0x4E43,
    [ITEM_WALLOPER] = 0x4E45,
    [ITEM_TESLA_CLAW] = 0x4E4B,
    [ITEM_GLOVE_OF_DOOM] = 0x4E4C,
    [ITEM_MORPH_O_RAY] = 0x4E44,
    [ITEM_HYDRODISPLACER] = 0x4E62,
    [ITEM_RYNO] = 0x4E4E,
    [ITEM_DRONE_DEVICE] = 0x4E42,
    [ITEM_DECOY_GLOVE] = 0x4E52,
    [ITEM_TRESPASSER] = 0x4E63,
    [ITEM_METAL_DETECTOR] = 0x4E65,
    [ITEM_MAGNEBOOTS] = 0x4E67,
    [ITEM_GRINDBOOTS] = 0x4E68,
    [ITEM_HOVERBOARD] = 0x4E69,
    [ITEM_HOLOGUISE] = 0x4E64,
    [ITEM_PDA] = 0x4E66,
    [ITEM_MAP_O_MATIC] = 0x4E70,
    [ITEM_BOLT_GRABBER] = 0x4E78,
    [ITEM_PERSUADER] = 0x4E73,
};
static const uint16_t k_planet_names[LEVEL_COUNT] = {
    0x4EDE, 0x4ECD, 0x4ECE, 0x4ECF, 0x4ED0, 0x4ED1, 0x4ED2, 0x4ED3, 0x4ED4, 0x4ED5,
    0x4ED6, 0x4ED7, 0x4ED8, 0x4EC6, 0x4EDA, 0x4EDB, 0x4EDC, 0x4ECA, 0x4EDE,
};

/* The message id of a reward's name; 0 for none. */
int rando_reward_text(rando_reward r) {
    // An item, a planet or a gold bolt each has its own table of names.
    switch (r.kind) {
        case REWARD_ITEM:
            return r.value < RANDO_ITEM_COUNT ? k_item_names[r.value] : 0;
        case REWARD_PLANET:
            return r.value < LEVEL_COUNT ? k_planet_names[r.value] : 0;
        case REWARD_GOLD_BOLT:
            return RANDO_TEXT_GOLD_BOLT;
        default:
            return 0;
    }
}
