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
 *     ammo, quick select), with Y's obtained byte put back as it was, so that it stays Y's own
 *     location's, the vendor's stock and the banner as they were.
 *   - What the player sees: a level's text has only that level's items, so GiveItem's banner would
 *     read "Paradox! This message does not exist" for most rewards, and the game's own banner for
 *     X follows it. The mod writes the banner's text itself, from the names every level has, once
 *     the player is in control, and holds it there for a few ticks over the game's. Help_Request:
 *     the tutorial of X is left out when X was not given.
 *   - Equipping: a reward goes where it would on its own: never the Hydro-Pack (which takes the
 *     place of the pack on the hero's back), nothing while Clank plays.
 *   - A vendor purchase: the buy dialog then takes the entry's item as the weapon bought, whose
 *     demo plays as the menu closes and which the hero is handed; the mod makes that the reward
 *     when it is a vendor weapon (else the one bought before), never the slot's weapon.
 *   - Seven places test "owned" instead of "obtained" before they give or go on (Helga and the
 *     robot Qwark who starts her course, Big Al, Bob, Edwina, the Pilot's Helmet's pickup and the
 *     O2 Mask's second pickup), and the starting Bomb Glove does too: while their update runs, the
 *     mod shows the game their item's owned byte as the location's had state, and puts the real one
 *     back after. A player who has the Swingshot from elsewhere still meets Helga.
 *   - Moby_Infobot_Update and Planet_Unlock: the same for an infobot, whose test is its planet's
 *     flag and whose had state is its trigger slot (set by the game right after it unlocks).
 *   - Orxon: a landing with the O2 Mask starts Ratchet, and only one without it starts Clank's
 *     part, which holds two locations. Until that part is done, Ship_InitTransition and class 22's
 *     update, the two that decide, are shown no O2 Mask.
 *   - GoldBolts_CountCollected: with gold bolts shuffled, the count the pause screen shows and the
 *     gold weapons spend is the gold bolt rewards had, not the gold bolt places visited.
 *   - Vendor_BuildItemList: the vendor's stock as the menu sees it is the slots not yet bought
 *     for sale, a bought slot as its reward's ammo, and the weapons owned as ammo; after a
 *     purchase the cursor stays on the list and the menu shows the entry now under it.
 *     Ammo_ChooseDrop: the same stock, so that a crate drops ammo for a weapon owned out of its
 *     level's order. Vendor_MenuUpdate: the line above the picture names what a slot holds.
 *   - Vendor_InitLevelStock: with the starting gear shuffled, the Bomb Glove the game puts in the
 *     stock and as the double tap's weapon is taken out again when the reward was another.
 *   - Each level's start: every location had holds its reward: a gold bolt's autosave is written
 *     in the update that picks it up, before the check below can give the reward, so a save can
 *     hold a place had without what it gave.
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
 *   - spoiler_log: write where everything is, and a way through, to the mod's data folder;
 *   - vendor_hints: the line above the picture names what a vendor slot holds.
 *
 * Nothing of the mod's goes into a save: what is had is the game's own bytes, and the placement
 * comes from the seed. A save made with the mod belongs to its seed; with the mod off the game is
 * the console's.
 *
 * In a party (multiplayer's README, Mods in a party) the host's seed counts: this game publishes its
 * seed code (it carries the settings), and in a member's game the same mod takes it and plays that
 * placement until the party goes. What a party shares is passed on as it is: while
 * party_replaying() says so, GiveItem and UnlockPlanet give what is named, and the location that
 * holds it is had here too, since the placement is the same and the player who found it has its
 * reward: nobody pays for it twice.
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

/* How long a reward's banner shows, and for how many ticks it is written again over what the
 * game's own update does next (an infobot's last state comes up to three ticks after its scene;
 * assumed). */
#define NOTICE_TICKS 240
#define NOTICE_HOLD 6
/* Rewards waiting for their banner, at most: more than a tick's or a scene's worth (assumed). */
#define NOTICE_QUEUE 8
/* The game's marks around a word in a banner: highlighted, and back (gameplay's text). */
#define TEXT_HIGHLIGHT 0x0C
#define TEXT_NORMAL 0x08
/* The banner's text at most: a line of the game's box, for what a name takes (assumed). */
#define BANNER_TEXT_SIZE 96

/* The line above the picture over the vendor. */
#define HINT_SIZE 96

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
#define MOD_VERSION "2.0.0"

OPENRAC_MOD_DEFINE(MOD_ID, MOD_VERSION)

/* ---- types ---- */

/* A byte the game is shown otherwise, with its real value kept. */
typedef struct swap {
    uint8_t* at;  /* the byte in game memory */
    uint8_t real; /* its value while the swap is not showing */
    int location; /* the location whose had state it shows; -1 for none: it shows 0 */
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

/* The placement this game's own settings gave, for when a party's host's is over. */
static rando_settings s_own_settings;
static int s_following;

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

/* Rewards whose banner is still to show, oldest first, and the one showing: how many ticks it still
 * goes on being written (NOTICE_HOLD) and its text. */
static rando_reward s_notices[NOTICE_QUEUE];
static int s_notice_count;
static int s_notice_hold;
static char s_notice_text[BANNER_TEXT_SIZE];

/* Whether the line over the vendor shows, and whether the setting allows it. */
static int s_hint_showing;
static int s_vendor_hints;

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
static openrac_rac1_Moby_RobotQwark_Update s_robot_qwark_update;
static openrac_rac1_Moby_Class23_Update s_o2_pickup_update;
static openrac_rac1_Moby_Class22_Update s_orxon_start_update;
static openrac_rac1_Ammo_ChooseDrop s_choose_drop;
static openrac_rac1_Vendor_MenuUpdate s_vendor_menu_update;
static openrac_rac1_Help_Request s_help_request;
static openrac_rac1_Ship_InitTransition s_ship_transition;

/* The game's text of a message id (a game address), null when this disc's build has none by that
 * name. */
static uint32_t (*s_msg_string)(int32_t id);

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

/* Whether this tick's game has Clank playing, not Ratchet. */
static int clank_playing(void) {
    return *byte_at(s_disc->hero_clank) != 0;
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

/* A level's trigger slot. */
static uint8_t* trigger(int level, int mission) {
    return byte_at(s_disc->triggers + (uint32_t)mission + (uint32_t)level * (uint32_t)TRIGGER_SLOTS);
}

/* A gold bolt's byte. */
static uint8_t* gold_bolt(int level, int slot) {
    return byte_at(s_disc->gold_bolts + (uint32_t)level * (uint32_t)GOLD_BOLT_SLOTS + (uint32_t)slot);
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
            return *trigger(l->level, l->key) == TRIGGER_DONE;
        case LOC_GOLD:
            return *gold_bolt(l->level, l->key) != 0;
        default:
            return 0;
    }
}

/* Marks the location had in the game's own byte, as the game would. */
static void set_had(int location) {
    const rando_location* l = &rando_locations[location];

    // Each kind of location keeps its had state in its own game byte.
    switch (l->kind) {
        case LOC_GIVE:
        case LOC_VENDOR:
        case LOC_START:
            *obtained(l->key) = 1;
            break;
        case LOC_INFOBOT:
            *trigger(l->level, l->key) = TRIGGER_DONE;
            break;
        case LOC_GOLD:
            *gold_bolt(l->level, l->key) = 1;
            break;
        default:
            break;
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
    *at = location >= 0 ? (uint8_t)had(location) : 0;
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
        *s_swaps[i].at = s_swaps[i].location >= 0 ? (uint8_t)had(s_swaps[i].location) : 0;
    }
}

/* ---- giving rewards ---- */

/* Writes a line to the port's log for this mod. */
static void log_line(int level, const char* text) {
    s_api->log(s_mod, level, text);
}

/* The text of a message of this level's table, in game memory; null without the function. */
static const char* message(int id) {
    return s_msg_string != NULL && id != 0 ? (const char*)OPENRAC_G(s_api, s_msg_string(id)) : NULL;
}

/* Adds a word to the banner's text at `at`, highlighted or plain; the new end. */
static size_t add_word(char* text, size_t at, const char* word, int highlight) {
    const size_t room = BANNER_TEXT_SIZE - 1 - at;

    // Marks and the word, all of it or none: the game's box takes a line.
    if (word != NULL && strlen(word) + 3 <= room) {
        // The highlight's marks go around the word.
        if (highlight) {
            text[at++] = TEXT_HIGHLIGHT;
        }

        memcpy(text + at, word, strlen(word));
        at += strlen(word);

        // ...and the mark back.
        if (highlight) {
            text[at++] = TEXT_NORMAL;
        }
    }

    text[at] = 0;
    return at;
}

/* The banner of a reward: its name, in the player's language; "Infobot" and the name for a planet. */
static void notice_text(rando_reward r, char* text) {
    size_t at = 0;
    text[0] = 0;

    // A planet is an infobot's coordinates: the word before the name says so.
    if (r.kind == REWARD_PLANET) {
        at = add_word(text, at, message(RANDO_TEXT_INFOBOT), 1);
        at = add_word(text, at, " ", 0);
    }

    add_word(text, at, message(rando_reward_text(r)), 1);
}

/* Puts this notice's text in the game's banner. */
static void show_notice(void) {
    memcpy(level_byte_at(s_disc->banner_text), s_notice_text, strlen(s_notice_text) + 1);
    set32(level_byte_at(s_disc->banner_ticks), NOTICE_TICKS);
}

/* A reward's banner is due. Oldest are shown first; one past the queue is dropped (the log has it). */
static void queue_notice(rando_reward r) {
    // The queue holds what a scene's worth of rewards can be.
    if (s_notice_count < NOTICE_QUEUE) {
        s_notices[s_notice_count++] = r;
    }
}

/* Each tick: the banner that shows goes on being written over the game's for NOTICE_HOLD ticks; the
 * next waits for the player to be in control, and for the banner before it to have been read. */
static void run_notices(void) {
    // The one showing goes first, so that what the game's update showed this tick is overwritten.
    if (s_notice_hold > 0) {
        --s_notice_hold;
        show_notice();
        return;
    }

    openrac_play_status status;
    memset(&status, 0, sizeof status);
    status.size = sizeof status;

    // Nothing waits, or a scene, a menu or a load is on: the banner would be lost.
    if (s_notice_count == 0 || s_api->play_status(&status) != OPENRAC_HOOK_OK
        || status.phase != OPENRAC_PLAY_PLAYING
        || get32(level_byte_at(s_disc->banner_ticks)) > NOTICE_TICKS / 2) {
        return;
    }

    notice_text(s_notices[0], s_notice_text);
    memmove(s_notices, s_notices + 1, (size_t)--s_notice_count * sizeof s_notices[0]);

    // No text (a disc's build without the function): the log has said it.
    if (s_notice_text[0] != 0) {
        s_notice_hold = NOTICE_HOLD;
        show_notice();
    }
}

/* The equip GiveItem takes for a reward at a place that gives with `equip`: the place's, but while
 * Clank plays (he holds no item), and never the Hydro-Pack: it sits in the back slot, where the
 * hero's jumps test for another pack, and comes on by itself in the water, as Edwina's give has it. */
static int32_t reward_equip(int item, int32_t equip) {
    return item != ITEM_HYDRO_PACK && !clank_playing() ? equip : 0;
}

/* Gives item through the game's GiveItem, as itself: its obtained byte (its own location's), the
 * vendor's stock and the banner left as they were: the banner GiveItem shows is in the text of the
 * level the item comes from, and "Paradox! This message does not exist" in any other. */
static void give_as_itself(int item, int32_t equip) {
    const uint8_t before = *obtained(item);
    // GiveItem marks a vendor weapon bought in the stock (or adds it): for a weapon that is a
    // slot, the stock is the slots', so it stays as it was (the list sells its ammo).
    uint8_t* stock = byte_at(s_disc->vendor_stock);
    uint8_t kept[VENDOR_STOCK_SIZE];
    const int slot = vendor_slot(item) >= 0;
    uint8_t* banner = level_byte_at(s_disc->banner_text);
    uint8_t* ticks = level_byte_at(s_disc->banner_ticks);
    char text[BANNER_TEXT_SIZE];
    const int32_t left = get32(ticks);
    memcpy(text, banner, sizeof text);

    // A vendor slot's weapon: remember the stock to put it back after GiveItem.
    if (slot) {
        memcpy(kept, stock, sizeof kept);
    }

    s_item_give(item, equip);

    // Put the slots' stock back, as said above.
    if (slot) {
        memcpy(stock, kept, sizeof kept);
    }

    memcpy(banner, text, sizeof text);
    set32(ticks, left);

    // The game sets this for a Drone Device bought, however it is had here.
    if (item == ITEM_DRONE_DEVICE) {
        *byte_at(s_disc->drone_flag) = 1;
    }

    *obtained(item) = before;
}

/* The player has the reward in his hands now. equip: as GiveItem's. */
static void put_reward(rando_reward r, int32_t equip) {
    // The reward is an item, a planet or a gold bolt, each given its own way.
    if (r.kind == REWARD_ITEM) {
        give_as_itself(r.value, reward_equip(r.value, equip));
    } else if (r.kind == REWARD_PLANET) {
        s_planet_unlock(r.value);
    }
}

/* Gives location's reward, with its banner to come. mark: an item whose obtained byte is then set
 * (the location's own, when it is GiveItem's), or -1. */
static void give_reward(int location, int32_t equip, int mark) {
    const rando_reward r = s_plan.reward[location];
    char line[160];
    char what[64];
    swaps_suspend();

    // The location is its own item's: the byte is its own.
    if (r.kind == REWARD_ITEM && rando_locations[location].key == r.value
        && rando_locations[location].kind < LOC_INFOBOT) {
        put_reward(r, equip);
        *obtained(r.value) = 1;
    } else {
        put_reward(r, equip);
    }

    // The location's own item is marked had, which its pickups and NPCs test and the save keeps.
    if (mark >= 0) {
        *obtained(mark) = 1;
    }

    swaps_resume();
    queue_notice(r);
    rando_reward_name(r, what, sizeof what);
    snprintf(line, sizeof line, "%s gave %s", rando_locations[location].name, what);
    log_line(OPENRAC_LOG_INFO, line);
}

/* Each level's start: every location had has given its reward. One that has not lost it to a save
 * made before the reward came (a gold bolt's autosave comes before this game's tick check). No
 * banner: this is not a find. */
static void give_lost_rewards(void) {
    // Every location this seed shuffles that the game has had.
    for (size_t i = 0; i < rando_location_count; ++i) {
        // Had, and the player does not hold what it gives (a gold bolt counts by the place itself).
        if (s_plan.shuffled[i] && s_plan.reward[i].kind != REWARD_GOLD_BOLT && had((int)i)
            && !held(s_plan.reward[i])) {
            char line[160];
            char what[64];
            put_reward(s_plan.reward[i], 0);
            rando_reward_name(s_plan.reward[i], what, sizeof what);
            snprintf(line, sizeof line, "%s had given %s: back", rando_locations[i].name, what);
            log_line(OPENRAC_LOG_INFO, line);
        }
    }
}

/* What a party passed on came from somewhere: that place of this game is spent. */
static void spend_location_of(rando_reward r) {
    // The one shuffled location that holds this reward, when it is still to be had here.
    for (size_t i = 0; i < rando_location_count; ++i) {
        // Rewards are unique in a placement: at most one row holds this one.
        if (s_plan.shuffled[i] && s_plan.reward[i].kind == r.kind
            && s_plan.reward[i].value == r.value && !had((int)i)) {
            set_had((int)i);
            s_seen[i] = 1;
        }
    }
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
    // Another player's item passed on: it, as it is, and no place of this game is had by it.
    if (s_api->api_minor >= 11 && s_api->party_replaying() && id >= 0 && id < RANDO_ITEM_COUNT) {
        const rando_reward r = {REWARD_ITEM, (uint8_t)id};
        give_as_itself(id, equip);
        spend_location_of(r);
        return;
    }

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
    // Another player's coordinates passed on: they, as they are, and their place is spent here.
    if (s_api->api_minor >= 11 && s_api->party_replaying()) {
        const rando_reward r = {REWARD_PLANET, (uint8_t)planet};
        s_planet_unlock(planet);
        spend_location_of(r);
        return;
    }

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

/* Hook for the Pilot's Helmet's pickup: it tests "owned", and after it gives it hands the hero the
 * helmet to wear (g_hero_pending_item_2): with something else given, the head stays as it was. */
static void helmet_update(openrac_gaddr moby) {
    uint8_t* pending = byte_at(s_disc->hero_pending);
    const int32_t before = get32(pending);
    run_shown(s_helmet_update, moby, ITEM_PILOTS_HELMET);

    // The hero was handed what he does not own.
    if (get32(pending) == ITEM_PILOTS_HELMET && !*owned(ITEM_PILOTS_HELMET)) {
        set32(pending, before);
    }
}

/* Hooks for the other updates that test "owned": Helga, the robot Qwark who starts her course, Big
 * Al, Bob, Edwina and the O2 Mask's second pickup. */
static void robot_qwark_update(openrac_gaddr moby) {
    run_shown(s_robot_qwark_update, moby, ITEM_SWINGSHOT);
}

static void o2_pickup_update(openrac_gaddr moby) {
    run_shown(s_o2_pickup_update, moby, ITEM_O2_MASK);
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

/* Whether Orxon's landing should play Clank's part however the O2 Mask stands: it holds two
 * locations and starts only without the mask, which a seed can give before Orxon. */
static int orxon_clank_part_due(void) {
    return *owned(ITEM_O2_MASK) && *trigger(LEVEL_ORXON, ORXON_CLANK_MISSION) != TRIGGER_DONE
           && current_level() == LEVEL_ORXON;
}

/* Hook for the ship's landing and take-off: the chunks it loads on Orxon go by the O2 Mask. */
static void ship_transition(int32_t mode) {
    const int shown = orxon_clank_part_due();

    // Orxon's chunks for Clank's start.
    if (shown) {
        swap_push(owned(ITEM_O2_MASK), -1);
    }

    s_ship_transition(mode);

    // The real byte back.
    if (shown) {
        swap_pop();
    }
}

/* Hook for class 22 on Orxon, which starts Clank there when there is no O2 Mask. */
static void orxon_start_update(openrac_gaddr moby) {
    const int shown = orxon_clank_part_due();

    // Clank's start.
    if (shown) {
        swap_push(owned(ITEM_O2_MASK), -1);
    }

    s_orxon_start_update(moby);

    // The real byte back.
    if (shown) {
        swap_pop();
    }
}

/* Hook for Help_Request: a tutorial of the item the game thinks it gave is left out when it gave
 * another. 1: as queued, so that the game asks no more. */
static int32_t help_request(int32_t message_id, int32_t slot) {
    // The messages that teach an item.
    for (size_t i = 0; i < rando_item_help_count; ++i) {
        const rando_item_help* h = &rando_item_helps[i];

        // This one's item has a shuffled place whose reward is another.
        if (h->message == message_id && s_by_item[h->item] >= 0) {
            const rando_reward r = s_plan.reward[s_by_item[h->item]];

            // Another reward came: nothing to teach.
            if (r.kind != REWARD_ITEM || r.value != h->item) {
                return 1;
            }
        }
    }

    return s_help_request(message_id, slot);
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

    give_lost_rewards();
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

    // A list with nothing on it has no entry for the cursor: the Bomb Glove's ammo holds its place.
    if (m == 0) {
        shown[m++] = (uint8_t)(ITEM_BOMB_GLOVE | STOCK_AMMO);
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

/* Hook for Ammo_ChooseDrop: from the stock as the menu sees it, so that a weapon owned out of its
 * level's order has its ammo drop too. */
static int32_t choose_drop(openrac_gaddr moby, openrac_gaddr out_item) {
    uint8_t* stock = byte_at(s_disc->vendor_stock);
    uint8_t real[VENDOR_STOCK_SIZE], shown[VENDOR_STOCK_SIZE];
    memcpy(real, stock, sizeof real);
    vendor_stock_shown(real, shown);
    memcpy(stock, shown, sizeof shown);
    const int32_t result = s_choose_drop(moby, out_item);
    memcpy(stock, real, sizeof real);
    return result;
}

/* The location whose slot the menu's cursor is on, while it sells one; -1 for none. */
static int slot_under_cursor(void) {
    uint8_t* menu = level_byte_at(s_disc->vendor_menu);
    const int32_t cursor = get32(menu + VENDOR_CURSOR);

    // The cursor on a real entry, and that entry for sale.
    if (cursor < 0 || cursor >= VENDOR_MAX_ENTRIES || cursor >= get32(menu + VENDOR_COUNT)) {
        return -1;
    }

    const uint8_t* entry = menu + VENDOR_ENTRIES + (uint32_t)cursor * VENDOR_ENTRY_SIZE;
    return get32(entry + VENDOR_ENTRY_AMMO) == 0 ? vendor_slot(get32(entry)) : -1;
}

/* Hook for the vendor's menu, each tick it is open: the line above the picture names what the slot
 * under the cursor holds; each_tick takes it away when the menu has gone. */
static void vendor_menu_update(void) {
    s_vendor_menu_update();
    const int location = s_vendor_hints ? slot_under_cursor() : -1;

    // A slot that gives something: say what.
    if (location >= 0) {
        char line[HINT_SIZE];
        char what[64];
        rando_reward_name(s_plan.reward[location], what, sizeof what);
        snprintf(line, sizeof line, "Buying this gives: %s", what);
        s_api->play_notice(s_mod, line);
        s_hint_showing = 2;
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

    // The line over the vendor goes a tick after the menu's hook stopped asking for it.
    if (s_hint_showing > 0 && --s_hint_showing == 0) {
        s_api->play_notice(s_mod, NULL);
    }

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

    run_notices();
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
           && HOOK(Moby_Edwina_Update, edwina_update, &s_edwina_update)
           && HOOK(Moby_RobotQwark_Update, robot_qwark_update, &s_robot_qwark_update)
           && HOOK(Moby_Class23_Update, o2_pickup_update, &s_o2_pickup_update)
           && HOOK(Moby_Class22_Update, orxon_start_update, &s_orxon_start_update)
           && HOOK(Ammo_ChooseDrop, choose_drop, &s_choose_drop)
           && HOOK(Vendor_MenuUpdate, vendor_menu_update, &s_vendor_menu_update);
}

/* Takes what a port of interface 1.12 has: without them the mod still runs, with a tutorial now and
 * then for what was not given, and with Clank's part of Orxon passed over when the O2 Mask came
 * first. */
static void install_late_hooks(void) {
    const int help = s_api->hook(
        s_mod,
        OPENRAC_RAC1_HOOK_Help_Request,
        OPENRAC_RAC1_SIG_Help_Request,
        (void*)help_request,
        (void**)&s_help_request
    );
    const int ship = s_api->hook(
        s_mod,
        OPENRAC_RAC1_HOOK_Ship_InitTransition,
        OPENRAC_RAC1_SIG_Ship_InitTransition,
        (void*)ship_transition,
        (void**)&s_ship_transition
    );

    // Said once, in the log a report comes with.
    if (help != OPENRAC_HOOK_OK || ship != OPENRAC_HOOK_OK) {
        log_line(OPENRAC_LOG_WARN, "this OpenRAC is older than the mod: update it for Orxon's order");
    }
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

/* What this game plays by, for a party it hosts: the seed code, which carries the settings, and the
 * words for a player. */
static void publish_rules(void) {
    static const struct {
        int pool;
        const char* name;
    } k_pools[] = {
        {POOL_GADGETS, "gadgets"},
        {POOL_WEAPONS, "weapons"},
        {POOL_VENDOR, "the vendor"},
        {POOL_GOLD, "gold bolts"},
        {POOL_PLANETS, "infobots"},
    };
    char text[160];
    int at = snprintf(text, sizeof text, "Logic: %s", rando_difficulty_name(s_plan.settings.difficulty));

    // Only what this seed shuffles.
    for (size_t i = 0; i < sizeof k_pools / sizeof k_pools[0]; ++i) {
        if ((s_plan.settings.pools & k_pools[i].pool) != 0) {
            at += snprintf(text + at, sizeof text - (size_t)at, "\nShuffles %s", k_pools[i].name);
        }
    }

    s_api->party_rules_publish(s_mod, s_code, (uint32_t)strlen(s_code), text);
}

/* Plays the placement of `settings`: the solver finds one for every setting, the same on every
 * computer. 1 when it did. */
static int play_settings(const rando_settings* settings) {
    if (rando_solve(settings, &s_plan) != 0) {
        return 0;
    }

    rando_encode(&s_plan.settings, s_code);
    index_locations();
    return 1;
}

/* The host's rules reached this game: its seed's placement in place of this game's; none, the party
 * went, and this game's own is back. A game in a level starts from what it has there. */
static void on_party_rules(void* user, const void* data, uint32_t size) {
    (void)user;
    char line[96];

    if (data == NULL) {
        if (s_following) {
            s_following = 0;
            play_settings(&s_own_settings);

            // What is had now is not news for this placement's check each tick.
            if (s_api->loaded_level() >= 0) {
                sync_seen();
            }

            log_line(OPENRAC_LOG_INFO, "out of the party: this game's own seed counts again");
        }

        return;
    }

    char code[RANDO_CODE_SIZE];
    rando_settings host;

    if (size >= sizeof code || (memcpy(code, data, size), code[size] = 0, !rando_decode(code, &host))) {
        log_line(OPENRAC_LOG_WARN, "the host's seed cannot be read here");
        return;
    }

    if (!play_settings(&host)) {
        log_line(OPENRAC_LOG_ERROR, "no placement for the host's seed");
        return;
    }

    s_following = 1;

    if (s_api->loaded_level() >= 0) {
        sync_seen();
    }

    snprintf(line, sizeof line, "in a party: playing the host's seed %s", s_code);
    log_line(OPENRAC_LOG_INFO, line);
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
    s_notice_count = 0;
    s_notice_hold = 0;

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
    s_msg_string = (uint32_t (*)(int32_t))api->function(s_disc->msg_string);
    s_vendor_hints = api->setting_bool(mod, "vendor_hints");

    s_own_settings = s_plan.settings;
    s_following = 0;
    // A port with party rules may yet bring a host's seed that shuffles something.
    const int rules = api->api_minor >= 11;

    // With no pool shuffled the mod changes nothing, so it hooks nothing.
    if (s_plan.settings.pools != 0 || rules) {
        // The port refused a hook point; it has been told why, so the mod does not run.
        if (!install_hooks()) {
            return 1;
        }

        install_late_hooks();
        api->on_frame(mod, each_tick, NULL);
    }

    if (rules) {
        publish_rules();
        api->on_party_rules(mod, on_party_rules, NULL);
    }

    log_start_up(source);
    write_files(source);
    return 0;
}
