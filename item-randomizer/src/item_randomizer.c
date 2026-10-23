/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: the item randomizer, a mod of OpenRAC's native port (README.md). At load it
 * reads its settings, makes the seed's placement (solver.c) and writes the spoiler log; in play it
 * gives each location's reward in place of the game's.
 *
 * How the game is changed, and only where a location is shuffled:
 *
 *   - Item_Give (GiveItem): the game gives item X at X's own place (an NPC, a pickup, a vendor
 *     purchase, the starting Bomb Glove). The mod gives that location's reward instead and marks
 *     the location had: X's "obtained" byte, which the game's pickups test before they appear and
 *     which the save keeps. A reward that is an item Y is given by GiveItem itself (owned, its
 *     banner, ammo, quick select, equipped), with Y's obtained byte put back as it was, so that it
 *     stays Y's own location's, and the vendor's stock as it was when Y is a vendor slot.
 *   - A vendor purchase: the buy dialog then takes the entry's item as the weapon bought, whose
 *     demo plays as the menu closes and which the hero is handed; the mod makes that the reward
 *     when it is a vendor weapon (else the one bought before), never the slot's weapon.
 *   - Five places test "owned" instead of "obtained" before they give (Helga, Big Al, Bob,
 *     Edwina, the Pilot's Helmet's pickup), and the starting Bomb Glove does too: while their update
 *     runs, the mod shows the game their item's owned byte as the location's had state, and puts
 *     the real one back after. A player who has the Swingshot from elsewhere still meets Helga.
 *   - Moby_Infobot_Update and Planet_Unlock: the same for an infobot, whose test is its planet's
 *     flag and whose had state is its trigger slot (set by the game right after it unlocks).
 *   - GoldBolts_CountCollected: with gold bolts shuffled, the count the pause screen shows and the
 *     gold weapons spend is the gold bolt rewards had, not the gold bolt places visited.
 *   - Vendor_BuildItemList: the vendor's stock as the menu sees it is the slots not yet bought
 *     for sale, a bought slot as its reward's ammo, and the weapons owned as ammo; after a
 *     purchase the cursor stays on the list and the menu shows the entry now under it.
 *   - Vendor_InitLevelStock: with the starting gear shuffled, the Bomb Glove the game puts in the
 *     stock and as the double tap's weapon is taken out again when the reward was another.
 *   - Each tick (on_frame), a location the game marked had another way (a gold bolt picked up, a
 *     route that writes the bytes itself) gets its reward then.
 *
 * The settings (openrac-mod.json; config.c turns them into the placement's settings):
 *
 *   - seed: a seed code (it carries the other settings, which are then ignored) or any word (its
 *     hash is the seed); blank rolls a random seed once and remembers it in seed.txt;
 *   - difficulty ("Logic"): relaxed, standard or expert, what a place is taken to need (solver.c);
 *   - shuffle_gadgets, shuffle_weapons: the items NPCs and pickups give, as two pools;
 *   - shuffle_vendor: the Gadgetron vendor's weapons are slots that give other rewards;
 *   - shuffle_gold_bolts: gold bolts join the pool, so one may hold a gadget and a gadget's place
 *     may hold a gold bolt;
 *   - shuffle_infobots: the planets' coordinates the infobots give join the pool;
 *   - keep_starting_gear: the Bomb Glove stays the start's, as in the game; off shuffles it too;
 *   - spoiler_log: write where everything is, and a way through, to the mod's data folder.
 *
 * Nothing of the mod's goes into a save: what is had is the game's own bytes, and the placement
 * comes from the seed. A save made with the mod belongs to its seed; with the mod off the game is
 * the console's.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "discs.h"
#include "hooks.h"
#include "openrac/openrac_mod.h"
#include "paths.h"
#include "rando.h"

/* ---- constants ---- */

/* The game's addresses of the globals this mod reads and writes are per disc, in discs.h (each
 * `(seen in game code)`: the decompilation's globals, named in their comments there). What is
 * below is the shape of the game's tables those addresses point at. */

/* Each level has this many trigger slots, 0xFF once done (D_0014C150; seen in game code). */
#define TRIGGER_SLOTS 16

/* The value of a trigger slot that is done (SetLevelTriggerDone; seen in game code). */
#define TRIGGER_DONE 0xFF

/* Each level has this many gold bolt bytes (D_0014BFC0; seen in game code). */
#define GOLD_BOLT_SLOTS 4

/* The most gold bolts the count hook reports: the count is clamped to this (assumed). */
#define MAX_GOLD_BOLTS 40

/* The vendor's stock ends with this byte (D_0015EED0; seen in game code). */
#define STOCK_END 0xFF

/* In a stock byte: the item's ammo is listed, not the item (VendorBuildItemList; seen in game
 * code). The low bits are the item's id. */
#define STOCK_AMMO 0x40
#define STOCK_ITEM 0x3F

/* A vendor menu entry's ammo field: after its item (VendorBuildItemList; seen in game code). */
#define VENDOR_ENTRY_AMMO 4

/* The bytes of an i32 in game memory (the game's int is 32 bits). */
#define I32_BYTES 4

/* The ticks the gold bolt's banner is shown for (assumed). */
#define BANNER_TICKS (-1)

/* How many bytes the game is shown otherwise at once, at most (assumed: an infobot's planet flag
 * and the start's Bomb Glove are the deepest nesting). */
#define MAX_SWAPS 4

/* Twelve letters and digits make a seed code (rando.h's RANDO_CODE_SIZE: "XXXX-XXXX-XXXX"). */
#define CODE_CHARS 12

/* Room for the mod's data folder and a file's path in it, and for the spoiler log's text. */
#define DIR_SIZE 1024
#define PATH_SIZE 1200
#define SPOILER_SIZE (64 * 1024)

#define MOD_ID "item-randomizer"
#define MOD_VERSION "1.0.1"

OPENRAC_MOD_DEFINE(MOD_ID, MOD_VERSION)

/* ---- types ---- */

/* A byte the game is shown as a location's had state (its `shown`), with its real value kept. */
typedef struct swap {
    uint8_t* at;  /* the byte in game memory */
    uint8_t real; /* its value while the swap is not showing */
    int location; /* the location whose had state it shows */
} swap;

/* ---- state ---- */

/* All of this is touched only from the OpenRAC callbacks below (load, the hooks and the frame
 * callback), which the port runs one at a time on the game's thread (assumed). */

/* The port's API, this mod's handle, the disc it runs on and the placement it plays. */
static const openrac_api* s_api;
static openrac_mod* s_mod;
static const rando_disc* s_disc;
static rando_plan s_plan;

/* The seed's code ("XXXX-XXXX-XXXX"), for the log and the spoiler's file name. */
static char s_code[RANDO_CODE_SIZE];

/* Each location's index by its key: the item id (LOC_GIVE, LOC_VENDOR, LOC_START), the level and
 * mission (LOC_INFOBOT); -1 for none or not shuffled. Gold bolts are found by the check each tick. */
static int s_by_item[RANDO_ITEM_COUNT];
static int s_by_infobot[LEVEL_COUNT][TRIGGER_SLOTS];

/* Each location's had state as the last tick saw it (the check each tick). */
static uint8_t s_seen[RANDO_MAX_LOCATIONS];

/* The bytes the game is shown otherwise right now, a stack, and how many there are. */
static swap s_swaps[MAX_SWAPS];
static int s_swap_count;

/* A purchase whose list the vendor builds next (build_item_list): the slot's item, -1 for none. */
static int s_bought = -1;

/* The infobot whose update is running, and the location it is; -1 for none. */
static int s_infobot = -1;

/* What each hook point goes on to: the game's own function. */
static openrac_rac1_Item_Give s_item_give;
static openrac_rac1_Planet_Unlock s_planet_unlock;
static openrac_rac1_Vendor_InitLevelStock s_init_level_stock;
static openrac_rac1_Vendor_BuildItemList s_build_item_list;
static openrac_rac1_GoldBolts_CountCollected s_count_gold_bolts;
static openrac_rac1_Moby_Infobot_Update s_infobot_update;
static openrac_rac1_Moby_PilotsHelmet_Update s_helmet_update;
static openrac_rac1_Moby_Helga_Update s_helga_update;
static openrac_rac1_Moby_BigAl_Update s_big_al_update;
static openrac_rac1_Moby_Bob_Update s_bob_update;
static openrac_rac1_Moby_Edwina_Update s_edwina_update;

/* The game's banner function, null when this disc's build has none by that name. */
static void (*s_show_banner)(int32_t text, int32_t ticks);

/* ---- game memory ---- */

/* The host address of a global of the game at the address given (relocated as the port does). */
static uint8_t* byte_at(uint32_t address) {
    return (uint8_t*)OPENRAC_G(s_api, s_api->relocate_data(address));
}

/* A 32-bit integer in game memory at a game address. */
static int32_t i32_at(openrac_gaddr a) {
    int32_t v;
    memcpy(&v, OPENRAC_G(s_api, a), I32_BYTES);
    return v;
}

/* Stores a 32-bit integer in game memory at a game address. */
static void put_i32(openrac_gaddr a, int32_t v) {
    memcpy(OPENRAC_G(s_api, a), &v, I32_BYTES);
}

/* A level program's global, by level 0's address (the vendor's menu is in each level's). */
static uint8_t* level_byte_at(uint32_t address) {
    return (uint8_t*)OPENRAC_G(s_api, s_api->relocate_level_data(0, address));
}

/* A 32-bit integer at a host pointer (an entry of the vendor's menu). */
static int32_t get32(const uint8_t* p) {
    int32_t v;
    memcpy(&v, p, I32_BYTES);
    return v;
}

/* Stores a 32-bit integer at a host pointer. */
static void set32(uint8_t* p, int32_t v) {
    memcpy(p, &v, I32_BYTES);
}

/* The level the game has loaded (only meaningful while one is). */
static int current_level(void) {
    int32_t v;
    memcpy(&v, byte_at(s_disc->level), I32_BYTES);
    return v;
}

/* The "owned" byte of an item: nonzero when the hero has it. */
static uint8_t* owned(int item) {
    return byte_at(s_disc->owned + (uint32_t)item);
}

/* The "obtained" byte of an item: nonzero once the item's own place has given it. */
static uint8_t* obtained(int item) {
    return byte_at(s_disc->obtained + (uint32_t)item);
}

/* The unlocked flag of a planet. */
static uint8_t* planet_flag(int planet) {
    return byte_at(s_disc->planets + (uint32_t)planet);
}

/* Whether a level of the story is loaded: the levels past it are left as the game has them
 * (RANDO_LAST_LEVEL). */
static int playing_level(void) {
    const int level = s_api->loaded_level() >= 0 ? current_level() : -1;
    return level >= 0 && level <= RANDO_LAST_LEVEL;
}

/* ---- locations ---- */

static int had(int location);

/* Whether the location has been had, by the game's own bytes (rando.h says which for each kind). */
static int had(int location) {
    const rando_location* l = &rando_locations[location];

    // Each kind of location keeps its had state in its own game byte.
    switch (l->kind) {
        case LOC_GIVE:
        case LOC_VENDOR:
        case LOC_START:
            return *obtained(l->key) != 0;
        case LOC_INFOBOT:
            return *byte_at(
                       s_disc->triggers + (uint32_t)l->key
                       + (uint32_t)l->level * (uint32_t)TRIGGER_SLOTS
                   )
                   == TRIGGER_DONE;
        case LOC_GOLD:
            return *byte_at(
                       s_disc->gold_bolts + (uint32_t)l->level * (uint32_t)GOLD_BOLT_SLOTS
                       + (uint32_t)l->key
                   )
                   != 0;
        default:
            return 0;
    }
}

/* Whether the reward is the player's already (a gold bolt is counted, never held). */
static int held(rando_reward r) {
    // A held item is owned, a held planet is unlocked.
    switch (r.kind) {
        case REWARD_ITEM:
            return *owned(r.value) != 0;
        case REWARD_PLANET:
            return *planet_flag(r.value) != 0;
        default:
            return 0;
    }
}

/* The vendor slot of item, when the vendor's weapons are shuffled; -1 for none. */
static int vendor_slot(int item) {
    const int location = item >= 0 && item < RANDO_ITEM_COUNT ? s_by_item[item] : -1;
    return location >= 0 && rando_locations[location].kind == LOC_VENDOR ? location : -1;
}

/* Whether the vendor sells item in the game (logic.c's vendor slots, shuffled or not). */
static int sold_by_vendor(int item) {
    // Look through the table for a vendor row of this item.
    for (size_t i = 0; i < rando_location_count; ++i) {
        // A vendor row whose item it is.
        if (rando_locations[i].kind == LOC_VENDOR && rando_locations[i].key == item) {
            return 1;
        }
    }

    return 0;
}

/* Whether item is some had location's reward. */
static int received(int item) {
    // Look through the placement for a had location that gave this item.
    for (size_t i = 0; i < rando_location_count; ++i) {
        // A shuffled location that gives this item and has been had.
        if (s_plan.shuffled[i] && s_plan.reward[i].kind == REWARD_ITEM
            && s_plan.reward[i].value == item && had((int)i)) {
            return 1;
        }
    }

    return 0;
}

/* ---- the game's bytes shown otherwise while one of its routines runs ---- */

/* Shows the game a byte as the location's had state, keeping its real value to restore with
 * swap_pop. At most MAX_SWAPS are pushed at once. */
static void swap_push(uint8_t* at, int location) {
    swap* w = &s_swaps[s_swap_count++];
    w->at = at;
    w->real = *at;
    w->location = location;
    *at = (uint8_t)had(location);
}

/* Puts the most recent swap's real value back and drops it. */
static void swap_pop(void) {
    swap* w = &s_swaps[--s_swap_count];
    *w->at = w->real;
}

/* The real bytes back while the game's GiveItem or UnlockPlanet runs for a reward... */
static void swaps_suspend(void) {
    // Back to front, so each swap's real value is restored in the reverse of the order it was pushed.
    for (int i = s_swap_count - 1; i >= 0; --i) {
        *s_swaps[i].at = s_swaps[i].real;
    }
}

/* ...and shown again after, with what it changed kept as real. */
static void swaps_resume(void) {
    // Each swap again, in the order pushed: what the game's routine changed is the new real value.
    for (int i = 0; i < s_swap_count; ++i) {
        s_swaps[i].real = *s_swaps[i].at;
        *s_swaps[i].at = (uint8_t)had(s_swaps[i].location);
    }
}

/* ---- giving rewards ---- */

/* Writes a line to the port's log for this mod. */
static void log_line(int level, const char* text) {
    s_api->log(s_mod, level, text);
}

/* Gives location's reward: an item through the game's GiveItem (keeping that item's obtained
 * byte as it was: it is its own location's), a planet through UnlockPlanet, a gold bolt's banner.
 * mark: an item whose obtained byte is then set (the location's own, when it is GiveItem's), or -1. */
static void give_reward(int location, int32_t equip, int mark) {
    const rando_reward r = s_plan.reward[location];
    swaps_suspend();

    // The reward is an item, a planet or a gold bolt, each given its own way.
    if (r.kind == REWARD_ITEM) {
        const uint8_t before = *obtained(r.value);
        // GiveItem marks a vendor weapon bought in the stock (or adds it): for a weapon that is a
        // slot, the stock is the slots', so it stays as it was (the list sells its ammo).
        uint8_t* stock = byte_at(s_disc->vendor_stock);
        uint8_t kept[VENDOR_STOCK_SIZE];
        const int slot = vendor_slot(r.value) >= 0;

        // A vendor slot's weapon: remember the stock to put it back after GiveItem.
        if (slot) {
            memcpy(kept, stock, sizeof kept);
        }

        s_item_give(r.value, equip);

        // Put the slots' stock back, as said above.
        if (slot) {
            memcpy(stock, kept, sizeof kept);
        }

        // The reward's own obtained byte belongs to its own location: put it back, unless this
        // location is that item's own and not an infobot's or gold bolt's.
        if (rando_locations[location].key != r.value
            || rando_locations[location].kind >= LOC_INFOBOT) {
            *obtained(r.value) = before;
        }
    } else if (r.kind == REWARD_PLANET) {
        s_planet_unlock(r.value);
    } else if (r.kind == REWARD_GOLD_BOLT && s_show_banner != NULL) {
        s_show_banner(s_disc->gold_bolt_text, BANNER_TICKS);
    }

    // The location's own item is marked had, which its pickups and NPCs test and the save keeps.
    if (mark >= 0) {
        *obtained(mark) = 1;
    }

    swaps_resume();
}

/* ---- the hooks ---- */

/* The vendor's menu entry the buy dialog is buying slot `item` from: the entry under the cursor,
 * while the dialog makes its purchase; null for any other GiveItem. */
static uint8_t* vendor_purchase(int item) {
    // Only a vendor slot's item can be a purchase.
    if (vendor_slot(item) < 0) {
        return NULL;
    }

    uint8_t* menu = level_byte_at(s_disc->vendor_menu);
    const int32_t cursor = get32(menu + VENDOR_CURSOR);

    // Only the buy dialog's purchase step, with the cursor on a real entry, is a purchase.
    if (get32(menu + VENDOR_DIALOG) != VENDOR_DIALOG_BUY || cursor < 0
        || cursor >= VENDOR_MAX_ENTRIES) {
        return NULL;
    }

    uint8_t* entry = menu + VENDOR_ENTRIES + (uint32_t)cursor * VENDOR_ENTRY_SIZE;
    return get32(entry) == item && get32(entry + VENDOR_ENTRY_AMMO) == 0 ? entry : NULL;
}

/* Hook for GiveItem(id, equip): id's location's reward, and the location had. */
static void item_give(int32_t id, int32_t equip) {
    const int location = id >= 0 && id < RANDO_ITEM_COUNT ? s_by_item[id] : -1;

    // An item with no shuffled location, or any give outside a level, is the game's own.
    if (location < 0 || !playing_level()) {
        s_item_give(id, equip);
        return;
    }

    uint8_t* entry = vendor_purchase(id);
    give_reward(location, equip, id);
    s_seen[location] = 1;

    // A vendor purchase also sets the weapon the buy dialog takes as bought.
    if (entry != NULL) {
        // Right after GiveItem the dialog takes the entry's item as the one bought, whose demo
        // plays as the menu closes and which the hero is then handed: the reward when it is one of
        // the vendor's weapons (each level has their demos), else the one bought before, as it
        // was (GiveItem has equipped the reward already).
        const rando_reward r = s_plan.reward[location];
        const int demo = r.kind == REWARD_ITEM && sold_by_vendor(r.value);
        set32(
            entry, demo ? r.value : get32(level_byte_at(s_disc->vendor_menu) + VENDOR_LAST_BOUGHT)
        );
        s_bought = id;
    }
}

/* Hook for UnlockPlanet(planet): the infobot's reward while its update runs, else the game's. */
static void planet_unlock(int32_t planet) {
    // An infobot's update is running and this is its planet: give its location's reward.
    if (s_infobot >= 0 && rando_locations[s_infobot].vanilla.value == planet) {
        const int location = s_infobot;
        give_reward(location, 0, -1);
        s_seen[location] = 1; /* the game marks its trigger slot next */
        return;
    }

    s_planet_unlock(planet); /* an NPC's coordinates, or the planet arrived at */
}

/* Hook for an infobot's update: runs it with its planet's flag showing its location's had state,
 * and its banner showing the planet it now gives. */
static void infobot_update(openrac_gaddr moby) {
    const int level = current_level();
    const int mission = OPENRAC_GREF(s_api, uint8_t, moby + MOBY_MISSION);
    const int location = level >= 0 && level < LEVEL_COUNT && mission < TRIGGER_SLOTS
                             ? s_by_infobot[level][mission]
                             : -1;
    const openrac_gaddr pvars = OPENRAC_GREF(s_api, openrac_gaddr, moby + MOBY_PVARS);
    const int planet = location >= 0 && pvars != 0 ? i32_at(pvars + INFOBOT_PLANET) : -1;

    // Not a shuffled infobot, or its planet is not the one the table has: the game's own.
    if (location < 0 || planet != rando_locations[location].vanilla.value) {
        s_infobot_update(moby);
        return;
    }

    swap_push(planet_flag(planet), location);
    s_infobot = location;
    // Its last state shows its planet's banner: the planet it gave, when it gave one.
    const rando_reward r = s_plan.reward[location];
    const int banner = OPENRAC_GREF(s_api, uint8_t, moby + MOBY_STATE) == INFOBOT_STATE_BANNER;

    // Show the banner of the planet the reward is, in place of the infobot's own.
    if (banner && r.kind == REWARD_PLANET) {
        put_i32(pvars + INFOBOT_PLANET, r.value);
    }

    s_infobot_update(moby);

    // The infobot's own planet back in its pvars.
    if (banner && r.kind == REWARD_PLANET) {
        put_i32(pvars + INFOBOT_PLANET, planet);
    }

    s_infobot = -1;
    swap_pop();
}

/* An update that tests owned[item] for "already given": run with that byte showing the
 * location's had state. */
static void run_shown(void (*update)(openrac_gaddr), openrac_gaddr moby, int item) {
    const int location = s_by_item[item];

    // The item's place is not shuffled: the game's own update.
    if (location < 0) {
        update(moby);
        return;
    }

    swap_push(owned(item), location);
    update(moby);
    swap_pop();
}

/* Hooks for the five NPC and pickup updates that test "owned": Pilot's Helmet, Helga, Big Al, Bob
 * and Edwina. */
static void helmet_update(openrac_gaddr moby) {
    run_shown(s_helmet_update, moby, ITEM_PILOTS_HELMET);
}

static void helga_update(openrac_gaddr moby) {
    run_shown(s_helga_update, moby, ITEM_SWINGSHOT);
}

static void big_al_update(openrac_gaddr moby) {
    run_shown(s_big_al_update, moby, ITEM_HELI_PACK);
}

static void bob_update(openrac_gaddr moby) {
    run_shown(s_bob_update, moby, ITEM_THRUSTER_PACK);
}

static void edwina_update(openrac_gaddr moby) {
    run_shown(s_edwina_update, moby, ITEM_HYDRO_PACK);
}

/* The level's start: the check each tick takes what is had now as seen (a save loaded, a new
 * game), and the starting Bomb Glove, when shuffled, is its location's. */
static void sync_seen(void) {
    // Every location's current state, so the tick check starts from what the game already has.
    for (size_t i = 0; i < rando_location_count; ++i) {
        s_seen[i] = s_plan.shuffled[i] ? (uint8_t)had((int)i) : 0;
    }
}

/* After the game gave the Bomb Glove at a level start and the reward was another, undoes what
 * the game did for the glove: its place first in the stock, and as the weapon a double tap goes
 * back to. before: the stock as it was; last_before: that weapon as it was. */
static void take_back_bomb_glove(
    uint8_t* stock, const uint8_t before[VENDOR_STOCK_SIZE], uint8_t* last, int32_t last_before
) {
    // The glove is not owned (the reward was another), the game put it first in the stock, and
    // that is a change: take it out again.
    if (!*owned(ITEM_BOMB_GLOVE) && stock[0] == (ITEM_BOMB_GLOVE | STOCK_AMMO)
        && before[0] != stock[0]) {
        memmove(stock, stock + 1, VENDOR_STOCK_SIZE - 1);
        stock[VENDOR_STOCK_SIZE - 1] = STOCK_END;

        // The game's first entry was pushed out of a full stock or dropped: bring it back first,
        // unless the stock lists that item already.
        if (before[0] != STOCK_END && stock[VENDOR_STOCK_SIZE - 1] == STOCK_END) {
            int present = 0;

            // Whether the stock lists the item the game's first entry held, as the item or as its ammo.
            for (int i = 0; i < VENDOR_STOCK_SIZE && stock[i] != STOCK_END; ++i) {
                present |= (stock[i] & STOCK_ITEM) == (before[0] & STOCK_ITEM);
            }

            // Not listed: it goes back to the front.
            if (!present) {
                memmove(stock + 1, stock, VENDOR_STOCK_SIZE - 1);
                stock[0] = before[0];
            }
        }

        // The double tap's weapon was made the glove: put it back.
        if (get32(last) == ITEM_BOMB_GLOVE) {
            set32(last, last_before);
        }
    }
}

/* The level's stock init when the starting Bomb Glove is shuffled: the game gives the Bomb Glove
 * (the reward, through item_give), then puts it first in the stock and makes it the weapon a
 * double tap goes back to: when the reward was something else, the stock and that weapon are put
 * back as they were. start: the Bomb Glove's location. */
static void init_stock_for_start(int start) {
    uint8_t* stock = byte_at(s_disc->vendor_stock);
    uint8_t* last = byte_at(s_disc->last_weapon);
    uint8_t before[VENDOR_STOCK_SIZE];
    memcpy(before, stock, sizeof before);
    const int32_t last_before = get32(last);
    swap_push(owned(ITEM_BOMB_GLOVE), start);
    s_init_level_stock();
    swap_pop();
    take_back_bomb_glove(stock, before, last, last_before);
}

/* Hook for VendorStockLevelInit: the game's, with the starting Bomb Glove given as its location's
 * reward when that is shuffled. */
static void init_level_stock(void) {
    sync_seen();
    const int start = s_by_item[ITEM_BOMB_GLOVE];

    // The starting Bomb Glove is a shuffled location, inside a level: give its reward.
    if (start >= 0 && rando_locations[start].kind == LOC_START && playing_level()) {
        init_stock_for_start(start);
    } else {
        s_init_level_stock();
    }

    sync_seen();
}

/* The ammo entries for the slots that were bought: in place of a bought slot, the ammo of its
 * reward when that is a weapon owned, else of its own weapon when owned. list: the stock being
 * built; bought: each entry's bought slot or -1; ammo: the items whose ammo is listed already. */
static void list_bought_ammo(
    uint8_t* list, const int* bought, int n, uint8_t ammo[RANDO_ITEM_COUNT]
) {
    // Each entry of the list that stands for a bought slot.
    for (int i = 0; i < n; ++i) {
        // Only the entries of bought slots.
        if (bought[i] < 0) {
            continue;
        }

        const rando_reward r = s_plan.reward[bought[i]];
        const int own = rando_locations[bought[i]].key;

        // The reward's ammo when the reward is owned and not listed already; else the slot's
        // own weapon's ammo in the same way.
        if (r.kind == REWARD_ITEM && r.value < RANDO_ITEM_COUNT && *owned(r.value)
            && !ammo[r.value]) {
            ammo[r.value] = 1;
            list[i] = (uint8_t)(r.value | STOCK_AMMO);
        } else if (*owned(own) && !ammo[own]) {
            ammo[own] = 1;
            list[i] = (uint8_t)(own | STOCK_AMMO);
        }
    }
}

/* The vendor's stock as the menu is built from it (the game lists each byte: the item for sale,
 * or with 0x40 its ammo; then the ammo of every other weapon owned):
 *   - a slot not bought: its weapon, for sale at its price;
 *   - a slot bought: in its place the ammo of its reward when that is a weapon owned, else of its
 *     own weapon when owned (from elsewhere), else nothing;
 *   - a weapon owned whose slot is still for sale: its ammo too, after the rest;
 *   - anything else as it is, but the ammo of an item not owned.
 * Each item's ammo is listed once. */
static void vendor_stock_shown(
    const uint8_t real[VENDOR_STOCK_SIZE], uint8_t shown[VENDOR_STOCK_SIZE]
) {
    uint8_t list[VENDOR_STOCK_SIZE];
    int bought[VENDOR_STOCK_SIZE];
    uint8_t ammo[RANDO_ITEM_COUNT] = {0}, sale[RANDO_ITEM_COUNT] = {0};
    int n = 0;

    // First pass: each entry of the real stock, up to its end.
    for (int i = 0; i < VENDOR_STOCK_SIZE && real[i] != STOCK_END; ++i) {
        const int item = real[i] & STOCK_ITEM;
        const int location = item < RANDO_ITEM_COUNT ? vendor_slot(item) : -1;
        bought[n] = -1;

        // An entry that is not a vendor slot of ours: kept, but ammo of what is not owned (or
        // listed already) is dropped.
        if (location < 0) {
            // Ammo of an item not owned, or listed already, is not shown.
            if ((real[i] & STOCK_AMMO)
                && (item >= RANDO_ITEM_COUNT || !*owned(item) || ammo[item])) {
                continue;
            }

            // Remember that this item's ammo is listed.
            if (real[i] & STOCK_AMMO) {
                ammo[item] = 1;
            }

            list[n++] = real[i];
        } else if (!had(location)) {
            // A slot not bought: its weapon for sale.
            sale[item] = 1;
            list[n++] = (uint8_t)item;
        } else {
            // A slot bought: a gap, filled below with ammo when there is any to list.
            bought[n] = location;
            list[n++] = STOCK_END;
        }
    }

    list_bought_ammo(list, bought, n, ammo);

    int m = 0;

    // Second pass: the gaps closed up.
    for (int i = 0; i < n; ++i) {
        // A gap left by a slot bought with no ammo to show.
        if (list[i] != STOCK_END) {
            shown[m++] = list[i];
        }
    }

    // Last, the ammo of owned weapons whose slot is still for sale, while there is room.
    for (int item = 0; item < RANDO_ITEM_COUNT && m < VENDOR_STOCK_SIZE; ++item) {
        // Last, the ammo of owned weapons whose slot is still for sale, while there is room.
        if (sale[item] && *owned(item) && !ammo[item]) {
            shown[m++] = (uint8_t)(item | STOCK_AMMO);
        }
    }

    memset(shown + m, STOCK_END, (size_t)(VENDOR_STOCK_SIZE - m));
}

/* After a purchase: the list may hold another entry where the slot was (its reward's ammo), or
 * one fewer. The cursor stays on the list, and the menu shows the item now under it (the game
 * redraws it only when the cursor moves, or when a weapon for sale is under it). */
static void settle_menu_after_purchase(void) {
    uint8_t* menu = level_byte_at(s_disc->vendor_menu);
    const int32_t count = get32(menu + VENDOR_COUNT);
    int32_t cursor = get32(menu + VENDOR_CURSOR);

    // The list got shorter and the cursor is past its end: pull it back onto the last entry.
    if (count > 0 && cursor >= count) {
        cursor = count - 1;
        set32(menu + VENDOR_CURSOR, cursor);
    }

    // The item under the cursor is not the one just bought: have the menu redraw it.
    if (cursor >= 0 && cursor < VENDOR_MAX_ENTRIES
        && get32(menu + VENDOR_ENTRIES + (uint32_t)cursor * VENDOR_ENTRY_SIZE) != s_bought) {
        set32(level_byte_at(s_disc->vendor_redraw), 1);
    }

    s_bought = -1;
}

/* Hook for VendorBuildItemList(skip_unflagged): the game's, built from the stock as the menu
 * should see it (vendor_stock_shown), the real stock put back after. */
static void build_item_list(int32_t skip_unflagged) {
    uint8_t* stock = byte_at(s_disc->vendor_stock);
    uint8_t real[VENDOR_STOCK_SIZE], shown[VENDOR_STOCK_SIZE];
    memcpy(real, stock, sizeof real);
    vendor_stock_shown(real, shown);
    memcpy(stock, shown, sizeof shown);
    s_build_item_list(skip_unflagged);
    memcpy(stock, real, sizeof real);

    // Right after a purchase: the menu is settled for the list that changed under it.
    if (s_bought >= 0) {
        settle_menu_after_purchase();
    }
}

/* Hook for GoldBolts_CountCollected: with gold bolts shuffled, the count of gold bolt rewards had,
 * at most MAX_GOLD_BOLTS; else the game's own count. */
static int32_t count_gold_bolts_hook(void) {
    // Gold bolts are not shuffled: the game's count is right.
    if (!(s_plan.settings.pools & POOL_GOLD)) {
        return s_count_gold_bolts();
    }

    int n = 0;

    // Count the gold bolt rewards that have been had.
    for (size_t i = 0; i < rando_location_count; ++i) {
        n += s_plan.shuffled[i] && s_plan.reward[i].kind == REWARD_GOLD_BOLT && had((int)i);
    }

    return n < MAX_GOLD_BOLTS ? n : MAX_GOLD_BOLTS;
}

/* Each tick: a location had since the last tick without passing through a hook (a gold bolt, a
 * route that writes the bytes itself) gets its reward, unless it is the player's already (a save
 * just loaded has its rewards). A place that wrote its own item's bytes gives that item back. */
static void each_tick(void* user) {
    (void)user;

    // Outside the story's levels the mod leaves the game alone.
    if (!playing_level()) {
        return;
    }

    // Every location, for one this tick has newly had.
    for (size_t i = 0; i < rando_location_count; ++i) {
        // Only locations this seed shuffles.
        if (!s_plan.shuffled[i]) {
            continue;
        }

        const int now = had((int)i);

        // Had since the last tick, and no hook gave its reward.
        if (now && !s_seen[i]) {
            const rando_location* l = &rando_locations[i];

            // The place wrote its own item as owned, and no reward is that item: take it back.
            if ((l->kind == LOC_GIVE || l->kind == LOC_VENDOR || l->kind == LOC_START)
                && *owned(l->key) && !received(l->key)) {
                *owned(l->key) = 0;
            }

            // The player does not hold the reward yet: give it.
            if (!held(s_plan.reward[i])) {
                give_reward((int)i, 0, -1);
            }
        }

        s_seen[i] = (uint8_t)now;
    }
}

/* ---- loading ---- */

/* Checks a hook's result: 1 when it is in place; 0, after telling the port why the mod fails. */
static int take(int result, const char* point) {
    // The port refused: its API has no such hook point, or another signature.
    if (result != OPENRAC_HOOK_OK) {
        char reason[160];
        snprintf(
            reason,
            sizeof reason,
            "this OpenRAC cannot hook %s (%s): it needs an OpenRAC with the item hook points",
            point,
            result == OPENRAC_HOOK_SIGNATURE ? "its signature is another" : "no such hook point"
        );
        s_api->fail(s_mod, reason);
        return 0;
    }

    return 1;
}

/* Hooks one point (hooks.h's names): this mod's function in place of the game's, the game's kept
 * in `original`. 1 on success. */
#define HOOK(point, fn, original)                                                                  \
    take(                                                                                          \
        s_api->hook(                                                                               \
            s_mod,                                                                                 \
            OPENRAC_RAC1_HOOK_##point,                                                             \
            OPENRAC_RAC1_SIG_##point,                                                              \
            (void*)(fn),                                                                           \
            (void**)(original)                                                                     \
        ),                                                                                         \
        #point                                                                                     \
    )

/* Takes every hook point the mod needs; 1 when all are in place, 0 (the port told) when not. */
static int install_hooks(void) {
    return HOOK(Item_Give, item_give, &s_item_give)
           && HOOK(Planet_Unlock, planet_unlock, &s_planet_unlock)
           && HOOK(Vendor_InitLevelStock, init_level_stock, &s_init_level_stock)
           && HOOK(Vendor_BuildItemList, build_item_list, &s_build_item_list)
           && HOOK(GoldBolts_CountCollected, count_gold_bolts_hook, &s_count_gold_bolts)
           && HOOK(Moby_Infobot_Update, infobot_update, &s_infobot_update)
           && HOOK(Moby_PilotsHelmet_Update, helmet_update, &s_helmet_update)
           && HOOK(Moby_Helga_Update, helga_update, &s_helga_update)
           && HOOK(Moby_BigAl_Update, big_al_update, &s_big_al_update)
           && HOOK(Moby_Bob_Update, bob_update, &s_bob_update)
           && HOOK(Moby_Edwina_Update, edwina_update, &s_edwina_update);
}

/* Whether the text looks meant as a seed code: twelve letters and digits, hyphens and spaces
 * aside. Used to warn about a code mistyped so that it reads as a word. */
static int looks_like_code(const char* text) {
    int n = 0;

    // Each character must be a letter or digit (counted), a hyphen or a space.
    for (const char* p = text != NULL ? text : ""; *p != 0; ++p) {
        // A letter or digit counts toward the twelve.
        if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            ++n;
        } else if (*p != '-' && *p != ' ') {
            // Any other character: not a code.
            return 0;
        }
    }

    return n == CODE_CHARS;
}

/* A new random seed (64 bits) from the clock and an address, for a blank seed setting. Not a
 * secure source: a seed only picks a game. */
static uint64_t fresh_seed(void) {
    rando_rng r = {(uint64_t)time(NULL) ^ ((uint64_t)clock() << 32) ^ (uint64_t)(uintptr_t)&r};
    return rando_next(&r);
}

/* The remembered seed, the spoiler log: in the mod's data folder. */
static void write_files(int source) {
    char dir[DIR_SIZE], path[PATH_SIZE];

    // No home folder means nowhere to write; the game plays on without the files.
    if (rando_data_dir(dir, sizeof dir) != 0) {
        log_line(OPENRAC_LOG_WARN, "no data folder for the spoiler log (no home folder)");
        return;
    }

    // A seed rolled just now is remembered, so a blank setting plays the same game next start.
    if (source == SEED_FROM_NEW) {
        rando_join(path, sizeof path, dir, "seed.txt");
        char line[RANDO_CODE_SIZE + 1];
        snprintf(line, sizeof line, "%s\n", s_code);
        rando_write_file(path, line, strlen(line));
    }

    // The spoiler log is a setting; the log line says where it went when it was written whole.
    if (s_api->setting_bool(s_mod, "spoiler_log")) {
        static char text[SPOILER_SIZE];
        const size_t need = rando_spoiler(&s_plan, MOD_VERSION, text, sizeof text);
        char name[64];
        snprintf(name, sizeof name, "spoiler-%s.txt", s_code);
        rando_join(path, sizeof path, dir, name);

        // Written, and not cut short for want of room.
        if (rando_write_file(path, text, strlen(text)) == 0 && need <= sizeof text) {
            char line[1300];
            snprintf(line, sizeof line, "spoiler log: %s", path);
            log_line(OPENRAC_LOG_INFO, line);
        }
    }
}

/* Fills the lookup tables of locations by key from the placement. */
static void index_locations(void) {
    // No location is known for any item yet.
    for (int i = 0; i < RANDO_ITEM_COUNT; ++i) {
        s_by_item[i] = -1;
    }

    memset(s_by_infobot, 0xFF, sizeof s_by_infobot);

    // Every shuffled location goes into the table that finds it by its key.
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];

        // Only the locations this seed shuffles are hooked.
        if (!s_plan.shuffled[i]) {
            continue;
        }

        // Infobots are found by level and mission; items by id; gold bolts by the check each
        // tick, so they have no entry.
        if (l->kind == LOC_INFOBOT) {
            s_by_infobot[l->level][l->key] = (int)i;
        } else if (l->kind != LOC_GOLD) {
            s_by_item[l->key] = (int)i;
        }
    }
}

/* The start-up log line: the seed, where it came from, the difficulty, the disc and how many
 * placements were tried. */
static void log_start_up(int source) {
    char line[256];
    static const char* const k_sources[] =
        {"", "from the code", "from the word", "remembered", "new"};
    snprintf(
        line,
        sizeof line,
        "seed %s (%s), %s, on the %s disc: %d placement%s tried",
        s_code,
        k_sources[source],
        rando_difficulty_name(s_plan.settings.difficulty),
        s_api->version,
        s_plan.attempts,
        s_plan.attempts == 1 ? "" : "s"
    );
    log_line(OPENRAC_LOG_INFO, line);
}

/* ---- the entry point ---- */

/* The port calls this once when the mod loads, on the game's thread: picks the disc, reads the
 * settings, makes the placement, takes the hook points and writes the files. Returns 0 on
 * success; 1 (after api->fail told the port why) when the mod cannot run. */
OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    s_disc = NULL;

    // Only Ratchet & Clank.
    if (strcmp(api->game, "rac1") != 0) {
        api->fail(mod, "the item randomizer is for Ratchet & Clank (rac1)");
        return 1;
    }

    // The disc row for the game's version; the table has one row per version.
    for (size_t i = 0; i < sizeof k_rando_discs / sizeof k_rando_discs[0]; ++i) {
        // The disc table's row for the game's version.
        if (strcmp(api->version, k_rando_discs[i].version) == 0) {
            s_disc = &k_rando_discs[i];
        }
    }

    // A version with no row has no addresses to work with.
    if (s_disc == NULL) {
        char reason[96];
        snprintf(
            reason,
            sizeof reason,
            "the item randomizer knows the PAL and NTSC-U discs, not %s",
            api->version
        );
        api->fail(mod, reason);
        return 1;
    }

    // The settings and the seed.
    char dir[DIR_SIZE], path[PATH_SIZE], remembered[64] = "";
    int have_remembered = 0;

    // The remembered seed, when the data folder exists and holds one.
    if (rando_data_dir(dir, sizeof dir) == 0) {
        rando_join(path, sizeof path, dir, "seed.txt");
        have_remembered = rando_read_file(path, remembered, sizeof remembered) == 0;
    }

    const rando_options options = {
        api->setting_string(mod, "seed"),
        api->setting_bool(mod, "shuffle_weapons"),
        api->setting_bool(mod, "shuffle_gadgets"),
        api->setting_bool(mod, "shuffle_gold_bolts"),
        api->setting_bool(mod, "shuffle_infobots"),
        api->setting_bool(mod, "shuffle_vendor"),
        api->setting_bool(mod, "keep_starting_gear"),
        api->setting_string(mod, "difficulty"),
    };
    rando_settings settings;
    const int source =
        rando_settings_of(&options, have_remembered ? remembered : NULL, fresh_seed(), &settings);

    // The solver finds a placement for every setting; this is a bug if it ever happens.
    if (rando_solve(&settings, &s_plan) != 0) {
        api->fail(mod, "no placement for these settings (please report them)");
        return 1;
    }

    rando_encode(&s_plan.settings, s_code);

    // A word that looks like a code is probably a mistyped code: say so, as it plays another game.
    if (source == SEED_FROM_WORD && looks_like_code(options.seed)) {
        log_line(
            OPENRAC_LOG_WARN,
            "the seed setting looks like a seed code but is not one (a character mistyped?): it is "
            "read "
            "as a word, so this is not the shared game"
        );
    }

    index_locations();
    s_show_banner = (void (*)(int32_t, int32_t))api->function(s_disc->show_banner);

    // With no pool shuffled the mod changes nothing, so it hooks nothing.
    if (s_plan.settings.pools != 0) {
        // The port refused a hook point; it has been told why, so the mod does not run.
        if (!install_hooks()) {
            return 1;
        }

        api->on_frame(mod, each_tick, NULL);
    }

    log_start_up(source);
    write_files(source);
    return 0;
}
