/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank in an Archipelago multiworld (README.md): the game side.
 *
 * Every place the item randomizer shuffles (item-randomizer/src/logic.c's table, but its fixed
 * rows) is a location of the multiworld. What it holds is the server's: when the player has a
 * location, the game gives nothing there and the server is told (ap_bridge.h); what the server
 * sends this slot is given, whoever found it. The game's own save keeps which locations were had
 * (rando.h says where for each kind), and every item the server sends is one the hero holds for
 * good, so the mod keeps nothing of its own in a save: at each level start it tells the server
 * every location had, and each tick it gives what the server sent that the hero lacks.
 *
 * The hooks are the item randomizer's (item_randomizer.c), on the same hook points, with the
 * location's reward always "nothing here". All of this runs on the game's thread; the network is
 * the client's thread (ap_client.cpp).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ap_bridge.h"
#include "ap_ids.h"
#include "discs.h"
#include "hooks.h"
#include "openrac/openrac_mod.h"
#include "rando.h"

/* ---- constants ---- */

/* Each level has this many trigger slots, 0xFF once done (D_0014C150; seen in game code). */
#define TRIGGER_SLOTS 16
#define TRIGGER_DONE 0xFF

/* Each level has this many gold bolt bytes (D_0014BFC0; seen in game code). */
#define GOLD_BOLT_SLOTS 4

/* The most gold bolts the count hook reports (assumed: the game has 40). */
#define MAX_GOLD_BOLTS 40

/* The vendor's stock: its end byte, and in a byte the "ammo of" bit and the item (VendorBuildItemList). */
#define STOCK_END 0xFF
#define STOCK_AMMO 0x40
#define STOCK_ITEM 0x3F

/* A vendor menu entry's ammo field: after its item (VendorBuildItemList; seen in game code). */
#define VENDOR_ENTRY_AMMO 4

#define I32_BYTES 4

/* How many bytes the game is shown otherwise at once, at most (item_randomizer.c's). */
#define MAX_SWAPS 4

/* Drek's robot on Veldin (return): its class (drek_final_boss_robot_update, class 1422), and the
 * scenes its update starts when he is beaten (StartCutscene 4, then 5, then the slideshow and the
 * ending movie; games/rac1 src/overlays/l18_veldin2/drek_boss_fight.c). Scene 7 is the hero's
 * death, which is not the goal. */
#define DREK_CLASS 1422
#define DREK_SCENE_BEATEN 4
#define DREK_SCENE_ENDING 5

/* How often the goal's backup check lists the level's mobys, in ticks. */
#define DREK_LIST_TICKS 30
#define MOBY_LIST_MAX 512

/* How long a line stays at the top of the screen, in ticks (60 a second). */
#define NOTICE_TICKS 300
#define NOTICE_SIZE 256

/* The most items the server may send a slot that the game side reads (far more than the game has). */
#define MAX_RECEIVED 1024

#define MOD_ID "archipelago"
#define MOD_VERSION "1.0.0"

OPENRAC_MOD_DEFINE(MOD_ID, MOD_VERSION)

/* ---- types ---- */

/* A byte the game is shown as a location's had state, with its real value kept. */
typedef struct swap {
    uint8_t* at;
    uint8_t real;
    int location;
} swap;

/* ---- state (the game's thread only) ---- */

static const openrac_api* s_api;
static openrac_mod* s_mod;
static const rando_disc* s_disc;

/* Each location's row by its key (as item_randomizer.c's); -1 for none. */
static int s_by_item[RANDO_ITEM_COUNT];
static int s_by_infobot[LEVEL_COUNT][TRIGGER_SLOTS];

/* Each location's had state as the last tick saw it. */
static uint8_t s_seen[RANDO_MAX_LOCATIONS];

static swap s_swaps[MAX_SWAPS];
static int s_swap_count;

/* A purchase whose list the vendor builds next: the slot's item, -1 for none. */
static int s_bought = -1;

/* The infobot whose update is running (its row), -1 for none. */
static int s_infobot = -1;

/* What the server has sent this slot: each item and planet, and the gold bolts. */
static uint8_t s_received_item[RANDO_ITEM_COUNT];
static uint8_t s_received_planet[LEVEL_COUNT];
static int s_received_gold;
static uint32_t s_generation = UINT32_MAX;

/* The line at the top of the screen and the ticks it has left. */
static int s_notice_ticks;

/* The goal: told once a start; Drek's slot as last seen, the level load it was in. */
static int s_goal;
static int s_drek_slot = -1;
static uint32_t s_drek_loads;
static int s_drek_tick;

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

/* ---- game memory ---- */

static uint8_t* byte_at(uint32_t address) {
    return (uint8_t*)OPENRAC_G(s_api, s_api->relocate_data(address));
}

static int32_t i32_at(openrac_gaddr a) {
    int32_t v;
    memcpy(&v, OPENRAC_G(s_api, a), I32_BYTES);
    return v;
}

static void put_i32(openrac_gaddr a, int32_t v) {
    memcpy(OPENRAC_G(s_api, a), &v, I32_BYTES);
}

static uint8_t* level_byte_at(uint32_t address) {
    return (uint8_t*)OPENRAC_G(s_api, s_api->relocate_level_data(0, address));
}

static int32_t get32(const uint8_t* p) {
    int32_t v;
    memcpy(&v, p, I32_BYTES);
    return v;
}

static void set32(uint8_t* p, int32_t v) {
    memcpy(p, &v, I32_BYTES);
}

static int current_level(void) {
    int32_t v;
    memcpy(&v, byte_at(s_disc->level), I32_BYTES);
    return v;
}

static uint8_t* owned(int item) {
    return byte_at(s_disc->owned + (uint32_t)item);
}

static uint8_t* obtained(int item) {
    return byte_at(s_disc->obtained + (uint32_t)item);
}

static uint8_t* planet_flag(int planet) {
    return byte_at(s_disc->planets + (uint32_t)planet);
}

/* Whether a level of the story is loaded (the levels past it are left as the game has them). */
static int playing_level(void) {
    const int level = s_api->loaded_level() >= 0 ? current_level() : -1;
    return level >= 0 && level <= RANDO_LAST_LEVEL;
}

/* What the game is doing (OPENRAC_PLAY_*), and the level load it is in. */
static int play_phase(uint32_t* loads) {
    openrac_play_status st;
    memset(&st, 0, sizeof st);
    st.size = sizeof st;

    // A port without the call, or no game: nothing playing.
    if (s_api->play_status(&st) != OPENRAC_HOOK_OK) {
        return OPENRAC_PLAY_NONE;
    }

    if (loads != NULL) {
        *loads = st.loads;
    }

    return st.phase;
}

static void log_line(int level, const char* text) {
    s_api->log(s_mod, level, text);
}

/* ---- locations ---- */

/* Whether the location has been had, by the game's own bytes (rando.h says which for each kind). */
static int had(int location) {
    const rando_location* l = &rando_locations[location];

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

/* The location had: the server is told (now, or when the client is next connected). */
static void found(int location) {
    s_seen[location] = 1;
    ap_client_check(ap_location_id(&rando_locations[location]));
}

/* The vendor slot of item; -1 for none. */
static int vendor_slot(int item) {
    const int location = item >= 0 && item < RANDO_ITEM_COUNT ? s_by_item[item] : -1;
    return location >= 0 && rando_locations[location].kind == LOC_VENDOR ? location : -1;
}

/* ---- the game's bytes shown otherwise while one of its routines runs (item_randomizer.c) ---- */

static void swap_push(uint8_t* at, int location) {
    swap* w = &s_swaps[s_swap_count++];
    w->at = at;
    w->real = *at;
    w->location = location;
    *at = (uint8_t)had(location);
}

static void swap_pop(void) {
    swap* w = &s_swaps[--s_swap_count];
    *w->at = w->real;
}

static void swaps_suspend(void) {
    // Back to front, the reverse of the order pushed.
    for (int i = s_swap_count - 1; i >= 0; --i) {
        *s_swaps[i].at = s_swaps[i].real;
    }
}

static void swaps_resume(void) {
    // In the order pushed: what the game's routine changed is the new real value.
    for (int i = 0; i < s_swap_count; ++i) {
        s_swaps[i].real = *s_swaps[i].at;
        *s_swaps[i].at = (uint8_t)had(s_swaps[i].location);
    }
}

/* ---- giving what the server sent ---- */

/* Gives an item through the game's GiveItem, as the item randomizer gives a reward: the item's
 * obtained byte kept as it was (it is its own location's), and the vendor's stock too when the
 * item is one of its slots (the stock is the slots'). */
static void give_item(int item) {
    uint8_t* stock = byte_at(s_disc->vendor_stock);
    uint8_t kept[VENDOR_STOCK_SIZE];
    const int slot = vendor_slot(item) >= 0;
    const uint8_t before = *obtained(item);
    swaps_suspend();

    // A vendor slot's weapon: the stock as it was after GiveItem.
    if (slot) {
        memcpy(kept, stock, sizeof kept);
    }

    s_item_give(item, 0);

    if (slot) {
        memcpy(stock, kept, sizeof kept);
    }

    *obtained(item) = before;
    swaps_resume();
}

/* The server's list read again when it changed: what this slot has been sent. */
static void read_received(void) {
    static int64_t ids[MAX_RECEIVED];
    uint32_t generation = 0;
    size_t n = ap_client_items(ids, MAX_RECEIVED, &generation);

    // Unchanged since the last look.
    if (generation == s_generation) {
        return;
    }

    s_generation = generation;
    n = n < MAX_RECEIVED ? n : MAX_RECEIVED;
    memset(s_received_item, 0, sizeof s_received_item);
    memset(s_received_planet, 0, sizeof s_received_planet);
    s_received_gold = 0;

    // Each item by its kind.
    for (size_t i = 0; i < n; ++i) {
        const rando_reward r = ap_reward_of(ids[i]);

        if (r.kind == REWARD_ITEM) {
            s_received_item[r.value] = 1;
        } else if (r.kind == REWARD_PLANET) {
            s_received_planet[r.value] = 1;
        } else if (r.kind == REWARD_GOLD_BOLT) {
            ++s_received_gold;
        }
    }
}

/* Gives what the server sent that the hero lacks: in a level, with the player in control. */
static void give_received(void) {
    // Each item sent and not owned.
    for (int item = 0; item < RANDO_ITEM_COUNT; ++item) {
        if (s_received_item[item] && !*owned(item)) {
            give_item(item);
        }
    }

    // Each planet sent and not unlocked.
    for (int planet = 0; planet < LEVEL_COUNT; ++planet) {
        if (s_received_planet[planet] && !*planet_flag(planet)) {
            s_planet_unlock(planet);
        }
    }
}

/* ---- the hooks (item_randomizer.c's, the reward always nothing) ---- */

/* The vendor's menu entry the buy dialog is buying slot `item` from; null for any other GiveItem. */
static uint8_t* vendor_purchase(int item) {
    if (vendor_slot(item) < 0) {
        return NULL;
    }

    uint8_t* menu = level_byte_at(s_disc->vendor_menu);
    const int32_t cursor = get32(menu + VENDOR_CURSOR);

    // Only the buy dialog's purchase step, with the cursor on a real entry.
    if (get32(menu + VENDOR_DIALOG) != VENDOR_DIALOG_BUY || cursor < 0
        || cursor >= VENDOR_MAX_ENTRIES) {
        return NULL;
    }

    uint8_t* entry = menu + VENDOR_ENTRIES + (uint32_t)cursor * VENDOR_ENTRY_SIZE;
    return get32(entry) == item && get32(entry + VENDOR_ENTRY_AMMO) == 0 ? entry : NULL;
}

/* Hook for GiveItem(id, equip): a location's own item is not given; the location is had. */
static void item_give(int32_t id, int32_t equip) {
    const int location = id >= 0 && id < RANDO_ITEM_COUNT ? s_by_item[id] : -1;

    // An item with no location, or any give outside a level, is the game's own.
    if (location < 0 || !playing_level()) {
        s_item_give(id, equip);
        return;
    }

    uint8_t* entry = vendor_purchase(id);
    *obtained(id) = 1; /* the location's own byte, which its place tests and the save keeps */
    found(location);

    // A vendor purchase: the dialog takes the entry's item as the one bought, whose demo plays and
    // which the hero is handed: the one bought before, as nothing was given here.
    if (entry != NULL) {
        set32(entry, get32(level_byte_at(s_disc->vendor_menu) + VENDOR_LAST_BOUGHT));
        s_bought = id;
    }
}

/* Hook for UnlockPlanet(planet): an infobot's planet is its location's; the rest the game's. */
static void planet_unlock(int32_t planet) {
    // The infobot whose update runs gives its planet: the location is had, nothing given.
    if (s_infobot >= 0 && rando_locations[s_infobot].vanilla.value == planet) {
        found(s_infobot);
        return;
    }

    s_planet_unlock(planet);
}

/* Hook for an infobot's update: its planet's flag shows its location's had state; its banner the
 * planet it holds when that is this slot's own planet, else its own. */
static void infobot_update(openrac_gaddr moby) {
    const int level = current_level();
    const int mission = OPENRAC_GREF(s_api, uint8_t, moby + MOBY_MISSION);
    const int location = level >= 0 && level < LEVEL_COUNT && mission < TRIGGER_SLOTS
                             ? s_by_infobot[level][mission]
                             : -1;
    const openrac_gaddr pvars = OPENRAC_GREF(s_api, openrac_gaddr, moby + MOBY_PVARS);
    const int planet = location >= 0 && pvars != 0 ? i32_at(pvars + INFOBOT_PLANET) : -1;

    // Not one of the table's infobots: the game's own.
    if (location < 0 || planet != rando_locations[location].vanilla.value) {
        s_infobot_update(moby);
        return;
    }

    int64_t item = -1;
    int own = 0;
    const int known =
        ap_client_location_item(ap_location_id(&rando_locations[location]), &item, &own);
    const rando_reward r = ap_reward_of(item);
    const int show = known && own && r.kind == REWARD_PLANET
                     && OPENRAC_GREF(s_api, uint8_t, moby + MOBY_STATE) == INFOBOT_STATE_BANNER;
    swap_push(planet_flag(planet), location);
    s_infobot = location;

    if (show) {
        put_i32(pvars + INFOBOT_PLANET, r.value);
    }

    s_infobot_update(moby);

    if (show) {
        put_i32(pvars + INFOBOT_PLANET, planet);
    }

    s_infobot = -1;
    swap_pop();
}

/* An update that tests owned[item] for "already given": run with that byte showing the
 * location's had state. */
static void run_shown(void (*update)(openrac_gaddr), openrac_gaddr moby, int item) {
    const int location = s_by_item[item];

    if (location < 0) {
        update(moby);
        return;
    }

    swap_push(owned(item), location);
    update(moby);
    swap_pop();
}

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

/* The level's start: what is had now is seen (a save loaded, a new game), and the server is told
 * every location had (it ignores the ones it knows). */
static void sync_seen(void) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        const int is = ap_is_location(&rando_locations[i]) && had((int)i);
        s_seen[i] = (uint8_t)is;

        if (is) {
            found((int)i);
        }
    }
}

/* After the game gave the Bomb Glove at a level start and none was given, undoes what the game
 * did for the glove (item_randomizer.c's). */
static void take_back_bomb_glove(
    uint8_t* stock, const uint8_t before[VENDOR_STOCK_SIZE], uint8_t* last, int32_t last_before
) {
    if (!*owned(ITEM_BOMB_GLOVE) && stock[0] == (ITEM_BOMB_GLOVE | STOCK_AMMO)
        && before[0] != stock[0]) {
        memmove(stock, stock + 1, VENDOR_STOCK_SIZE - 1);
        stock[VENDOR_STOCK_SIZE - 1] = STOCK_END;

        // The game's first entry was pushed out: back first, unless the stock lists it.
        if (before[0] != STOCK_END && stock[VENDOR_STOCK_SIZE - 1] == STOCK_END) {
            int present = 0;

            for (int i = 0; i < VENDOR_STOCK_SIZE && stock[i] != STOCK_END; ++i) {
                present |= (stock[i] & STOCK_ITEM) == (before[0] & STOCK_ITEM);
            }

            if (!present) {
                memmove(stock + 1, stock, VENDOR_STOCK_SIZE - 1);
                stock[0] = before[0];
            }
        }

        if (get32(last) == ITEM_BOMB_GLOVE) {
            set32(last, last_before);
        }
    }
}

/* Hook for VendorStockLevelInit: the starting Bomb Glove is its location's. */
static void init_level_stock(void) {
    sync_seen();
    const int start = s_by_item[ITEM_BOMB_GLOVE];

    if (start >= 0 && rando_locations[start].kind == LOC_START && playing_level()) {
        uint8_t* stock = byte_at(s_disc->vendor_stock);
        uint8_t* last = byte_at(s_disc->last_weapon);
        uint8_t before[VENDOR_STOCK_SIZE];
        memcpy(before, stock, sizeof before);
        const int32_t last_before = get32(last);
        swap_push(owned(ITEM_BOMB_GLOVE), start);
        s_init_level_stock();
        swap_pop();
        take_back_bomb_glove(stock, before, last, last_before);
    } else {
        s_init_level_stock();
    }

    sync_seen();
}

/* The vendor's stock as the menu is built from it: a slot not bought, its weapon for sale; a slot
 * bought, its weapon's ammo when the hero owns it (from wherever), else nothing; a weapon owned
 * whose slot is still for sale, its ammo after the rest; the rest as it is, but the ammo of an
 * item not owned. Each item's ammo once. */
static void vendor_stock_shown(
    const uint8_t real[VENDOR_STOCK_SIZE], uint8_t shown[VENDOR_STOCK_SIZE]
) {
    uint8_t ammo[RANDO_ITEM_COUNT] = {0}, sale[RANDO_ITEM_COUNT] = {0};
    int m = 0;

    for (int i = 0; i < VENDOR_STOCK_SIZE && real[i] != STOCK_END; ++i) {
        const int item = real[i] & STOCK_ITEM;
        const int location = item < RANDO_ITEM_COUNT ? vendor_slot(item) : -1;

        if (location < 0) {
            // Not a slot: kept, but ammo of what is not owned (or listed already).
            if ((real[i] & STOCK_AMMO)
                && (item >= RANDO_ITEM_COUNT || !*owned(item) || ammo[item])) {
                continue;
            }

            if (real[i] & STOCK_AMMO) {
                ammo[item] = 1;
            }

            shown[m++] = real[i];
        } else if (!had(location)) {
            sale[item] = 1;
            shown[m++] = (uint8_t)item;
        } else if (*owned(item) && !ammo[item]) {
            ammo[item] = 1;
            shown[m++] = (uint8_t)(item | STOCK_AMMO);
        }
    }

    // Last, the ammo of owned weapons whose slot is still for sale, while there is room.
    for (int item = 0; item < RANDO_ITEM_COUNT && m < VENDOR_STOCK_SIZE; ++item) {
        if (sale[item] && *owned(item) && !ammo[item]) {
            shown[m++] = (uint8_t)(item | STOCK_AMMO);
        }
    }

    memset(shown + m, STOCK_END, (size_t)(VENDOR_STOCK_SIZE - m));
}

/* After a purchase the list may be shorter: the cursor stays on it and the menu redraws. */
static void settle_menu_after_purchase(void) {
    uint8_t* menu = level_byte_at(s_disc->vendor_menu);
    const int32_t count = get32(menu + VENDOR_COUNT);
    int32_t cursor = get32(menu + VENDOR_CURSOR);

    if (count > 0 && cursor >= count) {
        cursor = count - 1;
        set32(menu + VENDOR_CURSOR, cursor);
    }

    if (cursor >= 0 && cursor < VENDOR_MAX_ENTRIES
        && get32(menu + VENDOR_ENTRIES + (uint32_t)cursor * VENDOR_ENTRY_SIZE) != s_bought) {
        set32(level_byte_at(s_disc->vendor_redraw), 1);
    }

    s_bought = -1;
}

/* Hook for VendorBuildItemList: built from the stock as the menu should see it. */
static void build_item_list(int32_t skip_unflagged) {
    uint8_t* stock = byte_at(s_disc->vendor_stock);
    uint8_t real[VENDOR_STOCK_SIZE], shown[VENDOR_STOCK_SIZE];
    memcpy(real, stock, sizeof real);
    vendor_stock_shown(real, shown);
    memcpy(stock, shown, sizeof shown);
    s_build_item_list(skip_unflagged);
    memcpy(stock, real, sizeof real);

    if (s_bought >= 0) {
        settle_menu_after_purchase();
    }
}

/* Hook for GoldBolts_CountCollected: the gold bolts the server has sent. */
static int32_t count_gold_bolts_hook(void) {
    read_received();
    return s_received_gold < MAX_GOLD_BOLTS ? s_received_gold : MAX_GOLD_BOLTS;
}

/* ---- the goal ---- */

/* Whether Drek is beaten: on Veldin (return), the scenes his update starts when he is; or, should
 * those be missed, his robot deleted by its own update in this level load (it deletes itself
 * after the ending movie). */
static int drek_beaten(uint32_t loads) {
    const int scene = s_api->scene_playing(NULL);

    if (scene == DREK_SCENE_BEATEN || scene == DREK_SCENE_ENDING) {
        return 1;
    }

    // The robot's slot, now and then.
    if (++s_drek_tick < DREK_LIST_TICKS) {
        return 0;
    }

    static openrac_moby mobys[MOBY_LIST_MAX];
    int n = s_api->moby_list(mobys, MOBY_LIST_MAX);
    int seen = -1;
    s_drek_tick = 0;
    n = n < MOBY_LIST_MAX ? n : MOBY_LIST_MAX;

    for (int i = 0; i < n; ++i) {
        if (mobys[i].oclass == DREK_CLASS && !(mobys[i].flags & OPENRAC_MOBY_DEAD)) {
            seen = mobys[i].slot;
        }
    }

    // Gone since the last look, in the same level load: how?
    if (seen < 0 && s_drek_slot >= 0 && s_drek_loads == loads) {
        openrac_moby_deletion d;
        memset(&d, 0, sizeof d);
        d.size = sizeof d;
        const int gone = s_api->moby_deletion(s_drek_slot, &d) == OPENRAC_HOOK_OK
                         && d.oclass == DREK_CLASS && d.by_class == DREK_CLASS && !d.by_mod
                         && d.loads == loads;
        s_drek_slot = -1;
        return gone;
    }

    s_drek_slot = seen;
    s_drek_loads = loads;
    return 0;
}

/* ---- each tick ---- */

/* The top line: the client's next line once the last has had its time. */
static void show_notices(void) {
    char line[NOTICE_SIZE];

    if (s_notice_ticks > 0 && --s_notice_ticks == 0) {
        s_api->play_notice(s_mod, "");
    }

    if (s_notice_ticks == 0 && ap_client_message(line, sizeof line)) {
        s_api->play_notice(s_mod, line);
        log_line(OPENRAC_LOG_INFO, line);
        s_notice_ticks = NOTICE_TICKS;
    }
}

/* Each tick: a location had without passing through a hook (a gold bolt, a route that writes the
 * bytes itself) is told to the server, and the place's own item, when it wrote it as owned and the
 * server has not sent it, is taken back; then what the server sent is given; then the goal. */
static void each_tick(void* user) {
    (void)user;
    uint32_t loads = 0;
    const int phase = play_phase(&loads);
    show_notices();
    read_received();

    if (!playing_level()) {
        return;
    }

    for (size_t i = 0; i < rando_location_count; ++i) {
        if (!ap_is_location(&rando_locations[i])) {
            continue;
        }

        const int now = had((int)i);

        if (now && !s_seen[i]) {
            const rando_location* l = &rando_locations[i];

            // The place wrote its own item as owned: it is the server's to give.
            if ((l->kind == LOC_GIVE || l->kind == LOC_VENDOR || l->kind == LOC_START)
                && *owned(l->key) && !s_received_item[l->key]) {
                *owned(l->key) = 0;
            }

            found((int)i);
        }

        s_seen[i] = (uint8_t)now;
    }

    // Gifts only with the player in control, not inside a scene or a menu.
    if (phase == OPENRAC_PLAY_PLAYING) {
        give_received();
    }

    // The goal, once.
    if (!s_goal && current_level() == LEVEL_VELDIN2 && drek_beaten(loads)) {
        s_goal = 1;
        ap_client_goal();
        log_line(OPENRAC_LOG_INFO, "Drek is beaten: the goal is sent");
    }
}

/* ---- loading ---- */

static int take(int result, const char* point) {
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

/* Every row that is a location, found by its key. */
static void index_locations(void) {
    for (int i = 0; i < RANDO_ITEM_COUNT; ++i) {
        s_by_item[i] = -1;
    }

    memset(s_by_infobot, 0xFF, sizeof s_by_infobot);

    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];

        if (!ap_is_location(l)) {
            continue;
        }

        // Infobots by level and mission; items by id; gold bolts by the check each tick.
        if (l->kind == LOC_INFOBOT) {
            s_by_infobot[l->level][l->key] = (int)i;
        } else if (l->kind != LOC_GOLD) {
            s_by_item[l->key] = (int)i;
        }
    }
}

/* An id for this start, for the server's list of clients (not a secret). */
static void make_uuid(char out[33]) {
    uint64_t x = (uint64_t)time(NULL) ^ ((uint64_t)clock() << 32) ^ (uint64_t)(uintptr_t)out;

    for (int i = 0; i < 32; ++i) {
        // SplitMix64's step: enough to tell two starts apart.
        x += UINT64_C(0x9E3779B97F4A7C15);
        uint64_t z = x;
        z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
        out[i] = "0123456789abcdef"[(z ^ (z >> 31)) & 15];
    }

    out[32] = 0;
}

/* The port has every call the mod needs (interface 1.9: play_status, scenes, mobys, notices). */
static int has_calls(const openrac_api* api) {
    return api->api_minor >= 9 && api->size >= offsetof(openrac_api, play_notice) + sizeof(void*);
}

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    s_disc = NULL;

    if (strcmp(api->game, "rac1") != 0) {
        api->fail(mod, "the Archipelago mod is for Ratchet & Clank (rac1)");
        return 1;
    }

    if (!has_calls(api)) {
        api->fail(mod, "the Archipelago mod needs OpenRAC's mod interface 1.9 or newer");
        return 1;
    }

    for (size_t i = 0; i < sizeof k_rando_discs / sizeof k_rando_discs[0]; ++i) {
        if (strcmp(api->version, k_rando_discs[i].version) == 0) {
            s_disc = &k_rando_discs[i];
        }
    }

    if (s_disc == NULL) {
        char reason[96];
        snprintf(
            reason,
            sizeof reason,
            "the Archipelago mod knows the PAL and NTSC-U discs, not %s",
            api->version
        );
        api->fail(mod, reason);
        return 1;
    }

    index_locations();

    if (!install_hooks()) {
        return 1;
    }

    api->on_frame(mod, each_tick, NULL);

    char uuid[33];
    make_uuid(uuid);
    const ap_config config = {
        api->setting_string(mod, "server"),
        api->setting_string(mod, "slot"),
        api->setting_string(mod, "password"),
        uuid,
    };
    char line[256];
    snprintf(
        line,
        sizeof line,
        "slot %s on %s, on the %s disc",
        config.slot != NULL ? config.slot : "",
        config.server != NULL ? config.server : "",
        api->version
    );
    log_line(OPENRAC_LOG_INFO, line);
    ap_client_start(&config);
    return 0;
}

OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    ap_client_stop();
}
