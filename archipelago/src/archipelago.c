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
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ap_bridge.h"
#include "ap_ids.h"
#include "ap_gold_weapons.h"
#include "ap_npc_infobots.h"
#include "ap_skills.h"
#include "discs.h"
#include "hooks.h"
#include "openrac/openrac_mod.h"
#include "rando.h"

/* ---- constants ---- */

/* Each level has this many trigger slots, 0xFF once done (D_0014C150; seen in game code). */
#define TRIGGER_SLOTS 16
#define TRIGGER_DONE 0xFF
#define BANNER_TEXT_SIZE 96 /* item_randomizer.c's */

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

/* How long a game from the title waits for its save's slot before it is judged without one (a new
 * game has none), in ticks of play. */
#define JUDGE_WAIT_TICKS 120

/* How long a line stays at the top of the screen, in ticks (60 a second). */
#define NOTICE_TICKS 300
#define NOTICE_SIZE 256

/* The items the game side reads at once; a longer list is read into a larger buffer. */
#define RECEIVED_CHUNK 1024

#define MOD_ID "archipelago"
#define MOD_VERSION "2.2.0"

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

/* Each skill point's flag as the last tick saw it. */
static uint8_t s_skill_seen[AP_SKILL_POINT_COUNT];

static swap s_swaps[MAX_SWAPS];
static int s_swap_count;

/* A purchase whose list the vendor builds next: the slot's item, -1 for none. */
static int s_bought = -1;

/* The infobot whose update is running (its row), -1 for none. */
static int s_infobot = -1;

/* What the server has sent this slot: each item and planet, and the gold bolts. */
static uint8_t s_received_item[RANDO_ITEM_COUNT];
static uint8_t s_received_gold_weapon[RANDO_ITEM_COUNT];
static int s_gold_case = -1;
static int s_npc_running = -1;
static uint8_t s_gold_real_upgrade;
static openrac_rac1_GoldBolts_CountUnspent s_count_unspent;
static openrac_rac1_Moby_GoldWeaponCase_Update s_gold_case_update;
static item_randomizer_Save_NewGamePlus s_new_game_plus;
static item_randomizer_Save_NewGame s_new_game;
static void (*s_save_update)(void);
static int32_t (*s_restore_data)(uint32_t, int32_t, uint32_t);
static int s_in_game_reset;
static int s_restoring_save;
static int s_has_gold_currency_hook;
static int s_gold_hook_warned;
static int64_t s_gold_hint_location = -1;
static float s_gold_hint_distance = 9.0f;
static unsigned s_gold_hint_price;
static uint8_t s_received_planet[LEVEL_COUNT];
static int s_received_gold;
static uint32_t s_generation = UINT32_MAX;

/* The line at the top of the screen and the ticks it has left. */
static int s_notice_ticks;

/* The save's memory card slot (interface 1.11): the game came from the title since the last level
 * start; its save waits to be judged until its slot is known; the slot known in this game. */
static int s_from_title = 1;
static int s_judge_wait;
static int s_judge_ticks;
static int s_slot_known = -1;

/* The goal: told once a start; Drek's slot as last seen, the level load it was in. */
static int s_goal;
static int s_drek_slot = -1;
static uint32_t s_drek_loads;
static int s_drek_tick;

static openrac_rac1_Item_Give s_item_give;
static openrac_rac1_Planet_Unlock s_planet_unlock;
static openrac_rac1_Vendor_InitLevelStock s_init_level_stock;
static openrac_rac1_Vendor_BuildItemList s_build_item_list;
static openrac_rac1_Vendor_OpenMenu s_open_vendor_menu;
static openrac_rac1_GoldBolts_CountCollected s_count_gold_bolts;
static openrac_rac1_Moby_Infobot_Update s_infobot_update;
static openrac_rac1_Moby_PilotsHelmet_Update s_helmet_update;
static openrac_rac1_Moby_Helga_Update s_helga_update;
static openrac_rac1_Moby_BigAl_Update s_big_al_update;
static openrac_rac1_Moby_Bob_Update s_bob_update;
static openrac_rac1_Moby_Edwina_Update s_edwina_update;
static openrac_rac1_Ship_InitTransition s_ship_transition;
static openrac_rac1_Moby_Class22_Update s_orxon_start_update;
static openrac_rac1_Help_Request s_help_request;

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

/* Chunk 24 is unused by retail (discs.h save_tag). Keep purchase checks separate from the
 * gold-upgrade ownership bytes: a received upgrade never spends currency or buys its case. */
#define GOLD_CHECK_MAGIC 0xA7500000u /* legacy: ten case bits */
#define AP_CHECK_MAGIC 0xA8000000u
static uint32_t saved_checks(void) {
    const uint32_t word = (uint32_t)get32(byte_at(s_disc->save_tag));
    if ((word & ~0x3FFu) == GOLD_CHECK_MAGIC) return word & 0x3FFu;
    return (word & 0xFF000000u) == AP_CHECK_MAGIC ? word & 0x7FFFFFu : 0;
}
static void save_checks(uint32_t bits) { set32(byte_at(s_disc->save_tag), (int32_t)(AP_CHECK_MAGIC | bits)); }
static uint32_t gold_case_checks(void) { return saved_checks() & 0x3FFu; }
static int npc_had(int index) { return (saved_checks() >> (10 + index)) & 1u; }
static uint8_t* gold_owned(int base) {
    return byte_at(strcmp(s_disc->version, "pal") == 0 ? 0x0013E620u + base : 0x0013E520u + base);
}

/* ---- locations ---- */

/* Whether the location has been had, by the game's own bytes (rando.h says which for each kind). */
static int had(int location) {
    // A byte shown as nothing, not as a location's (Orxon's O2 Mask).
    if (location < 0) {
        return 0;
    }

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
        case LOC_FIXED: {
            const int npc = l->vanilla.kind == REWARD_PLANET ? ap_npc_index(l->level,l->vanilla.value) : -1;
            return npc >= 0 && npc_had(npc);
        }
        default:
            return 0;
    }
}

/* Whether the game has set a skill point's flag. */
static int skill_had(int skill) {
    const uint32_t flags = ap_skill_flags_address(s_disc->version);
    return flags != 0 && *byte_at(flags + k_ap_skill_points[skill].flag) != 0;
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

/* The banner as it is, to put back after a routine of the game's that shows one: GiveItem's and
 * UnlockPlanet's are in the text of the level the reward comes from, and read "Paradox: This message
 * does not exist" in any other (item_randomizer.c). The server's line says what came. */
typedef struct kept_banner {
    char text[BANNER_TEXT_SIZE];
    int32_t ticks;
} kept_banner;

static void banner_keep(kept_banner* k) {
    memcpy(k->text, level_byte_at(s_disc->banner_text), sizeof k->text);
    k->ticks = get32(level_byte_at(s_disc->banner_ticks));
}

static void banner_put_back(const kept_banner* k) {
    memcpy(level_byte_at(s_disc->banner_text), k->text, sizeof k->text);
    set32(level_byte_at(s_disc->banner_ticks), k->ticks);
}

/* Gives an item through the game's GiveItem, as the item randomizer gives a reward: the item's
 * obtained byte kept as it was (it is its own location's), and the vendor's stock too when the
 * item is one of its slots (the stock is the slots'). */
static void give_item(int item) {
    uint8_t* stock = byte_at(s_disc->vendor_stock);
    uint8_t kept[VENDOR_STOCK_SIZE];
    const int slot = vendor_slot(item) >= 0;
    const uint8_t before = *obtained(item);
    kept_banner banner;
    banner_keep(&banner);
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
    banner_put_back(&banner);
}

/* The server's list read again when it changed: what this slot has been sent. */
static void read_received(void) {
    static int64_t chunk[RECEIVED_CHUNK];
    static int64_t* ids = chunk;
    static size_t capacity = RECEIVED_CHUNK;
    uint32_t generation = 0;
    size_t n = ap_client_items(ids, capacity, &generation);

    // Unchanged since the last look.
    if (generation == s_generation) {
        return;
    }

    // More than fits: a buffer for them all (every item counts, however many the server sends).
    if (n > capacity) {
        int64_t* larger = (int64_t*)malloc(n * sizeof *larger);

        if (larger != NULL) {
            if (ids != chunk) {
                free(ids);
            }

            ids = larger;
            capacity = n;
            n = ap_client_items(ids, capacity, &generation);
        }

        n = n < capacity ? n : capacity;
    }

    s_generation = generation;
    memset(s_received_item, 0, sizeof s_received_item);
    memset(s_received_gold_weapon, 0, sizeof s_received_gold_weapon);
    memset(s_received_planet, 0, sizeof s_received_planet);
    s_received_gold = 0;

    // Each item by its kind.
    for (size_t i = 0; i < n; ++i) {
        const int gold_base = ap_gold_weapon_base(ids[i]);
        if (gold_base >= 0) {
            s_received_gold_weapon[gold_base] = 1;
            s_received_item[gold_base] = 1;
            continue;
        }
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
    /* Set the variant before GiveItem so initial ammo/model setup sees the gold weapon. */
    for (int i = 0; i < AP_GOLD_WEAPON_COUNT; ++i) {
        const int base = k_ap_gold_bases[i];
        if (s_received_gold_weapon[base]) *gold_owned(base) = 1;
    }

    // Each item sent and not owned.
    for (int item = 0; item < RANDO_ITEM_COUNT; ++item) {
        if (s_received_item[item] && !*owned(item)) {
            give_item(item);
        }
    }

    // Each planet sent and not unlocked.
    for (int planet = 0; planet < LEVEL_COUNT; ++planet) {
        if (s_received_planet[planet] && !*planet_flag(planet)) {
            kept_banner banner;
            banner_keep(&banner);
            s_planet_unlock(planet);
            banner_put_back(&banner);
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

    if (s_npc_running >= 0 && ap_client_npc_infobots() == 1
        && planet == k_ap_npcs[s_npc_running].planet) {
        save_checks(saved_checks() | (1u << (10 + s_npc_running)));
        *planet_flag(planet) = 1; /* only the NPC sees this; saves serialize real ownership */
        ap_client_check(ap_npc_id(s_npc_running));
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

/* Whether Orxon's landing should play Clank's part however the O2 Mask stands: it holds two
 * locations and starts only without the mask, which the server can send before Orxon. */
static int orxon_clank_part_due(void) {
    const uint8_t state = *byte_at(
        s_disc->triggers + (uint32_t)ORXON_CLANK_MISSION + (uint32_t)LEVEL_ORXON * (uint32_t)TRIGGER_SLOTS
    );
    return *owned(ITEM_O2_MASK) && state != TRIGGER_DONE && current_level() == LEVEL_ORXON;
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

/* Hook for Help_Request: a tutorial of the item the game thinks it gave is left out when the place
 * holds another. Not known yet (no scout back): the game's. 1: as queued, so that it asks no more. */
static int32_t help_request(int32_t message_id, int32_t slot) {
    // The messages that teach an item.
    for (size_t i = 0; i < rando_item_help_count; ++i) {
        const rando_item_help* h = &rando_item_helps[i];

        // This one's item has a place in the multiworld.
        if (h->message == message_id && s_by_item[h->item] >= 0) {
            int64_t item = -1;
            int own = 0;
            const int known = ap_client_location_item(
                ap_location_id(&rando_locations[s_by_item[h->item]]), &item, &own
            );
            const rando_reward r = ap_reward_of(item);

            // Another item came, or one for another player: nothing to teach.
            if (known && (!own || r.kind != REWARD_ITEM || r.value != h->item)) {
                return 1;
            }
        }
    }

    return s_help_request(message_id, slot);
}

/* The level's start: what is had now is seen (a save loaded, a new game), and the client is given
 * every location had, which it sends unless the save is another seed's. */
/* The port has interface 1.11's save_slot_current. */
static int has_save_slot(void) {
    return s_api->api_minor >= 11
           && s_api->size >= offsetof(openrac_api, save_slot_current) + sizeof(void*)
           && s_api->save_slot_current != NULL;
}

/* The memory card slot of the game in progress, -1 for none known (or a port without it). */
static int save_slot_now(void) {
    return has_save_slot() ? s_api->save_slot_current() : -1;
}

/* The locations the game has had, sent to the client with the save's memory card slot. */
static void send_save(int card_slot) {
    static int64_t ids[RANDO_MAX_LOCATIONS + AP_SKILL_POINT_COUNT + AP_GOLD_WEAPON_COUNT + AP_NPC_COUNT];
    size_t n = 0;

    for (size_t i = 0; i < rando_location_count; ++i) {
        if (ap_is_location(&rando_locations[i]) && had((int)i)) {
            ids[n++] = ap_location_id(&rando_locations[i]);
        }
    }

    // The skill points; the client drops them in a room without any.
    for (int i = 0; i < AP_SKILL_POINT_COUNT; ++i) {
        if (skill_had(i)) {
            ids[n++] = ap_skill_id(&k_ap_skill_points[i]);
        }
    }

    for (int i = 0; i < AP_GOLD_WEAPON_COUNT; ++i) {
        if (gold_case_checks() & (1u << i)) ids[n++] = ap_gold_case_id(k_ap_gold_bases[i]);
    }
    for (int i = 0; i < AP_NPC_COUNT; ++i) if (npc_had(i)) ids[n++] = ap_npc_id(i);
    ap_client_save_loaded(ids, n, card_slot);
}

static void sync_seen(void) {
    s_restoring_save = 0;
    for (size_t i = 0; i < rando_location_count; ++i) {
        s_seen[i] = (uint8_t)(ap_is_location(&rando_locations[i]) && had((int)i));
    }

    for (int i = 0; i < AP_SKILL_POINT_COUNT; ++i) {
        s_skill_seen[i] = (uint8_t)skill_had(i);
    }

    // From the title (a game loaded or new): its slot comes once the level is up, so the save is
    // judged then (each_tick); the level start's second look waits with it. Else a travel in the
    // same game: its slot is the one known.
    if (s_judge_wait) {
        return;
    }

    if (s_from_title && has_save_slot()) {
        s_judge_wait = 1;
        s_judge_ticks = 0;
    } else {
        send_save(s_slot_known);
    }

    s_from_title = 0;
}

/* Each tick: the save's slot. A game from the title is judged once its slot is known (or not
 * coming: a new game); a slot that changes in play is the game saved there. */
static void watch_save_slot(int phase) {
    if (phase == OPENRAC_PLAY_FRONT_END) {
        s_from_title = 1;
        s_judge_wait = 0;
        s_slot_known = -1;
        return;
    }

    const int slot = save_slot_now();

    if (s_judge_wait) {
        if (slot >= 0 || (phase == OPENRAC_PLAY_PLAYING && ++s_judge_ticks > JUDGE_WAIT_TICKS)) {
            s_judge_wait = 0;
            s_slot_known = slot;
            send_save(slot);
        }

        return;
    }

    if (slot >= 0 && slot != s_slot_known) {
        s_slot_known = slot;
        /* A changed slot alone does not prove a successful overwrite. */
    }
}

/* Ownership does not gate Circle or Triangle: hero_switch_item reads last_weapon_id directly,
 * and the double-tap path also reads quick_swap_item. initialize_hero defaults a zero last
 * weapon to Bomb Glove, so use the wrench rather than zero until a real weapon is selected.
 * The hero's pending/selected/saved slot-0 items are +0x20B8/+0x20D4/+0x20F0 in struct Hero;
 * its current item is +0x10B8. Queue a normal switch instead of editing a live item's moby. */
static void guard_unowned_bomb_glove(void) {
    if (*owned(ITEM_BOMB_GLOVE)) {
        return;
    }

    uint8_t* last = byte_at(s_disc->last_weapon);
    uint8_t* quick_swap = byte_at(s_disc->level + 8u); /* D_0015EE8C / D_0015ED8C */
    uint8_t* hero = byte_at(strcmp(s_disc->version, "pal") == 0 ? 0x0013F450u : 0x0013F350u);
    uint8_t* pending = hero + 0x20B8u;
    uint8_t* saved = hero + 0x20F0u;

    if (get32(last) == ITEM_BOMB_GLOVE) {
        set32(last, ITEM_WRENCH);
    }
    if (get32(quick_swap) == ITEM_BOMB_GLOVE) {
        set32(quick_swap, ITEM_WRENCH);
    }
    if (get32(saved) == ITEM_BOMB_GLOVE) {
        set32(saved, ITEM_WRENCH);
    }

    const int32_t next = get32(pending);
    if (next == ITEM_BOMB_GLOVE
        || (next == 0 && (get32(hero + 0x10B8u) == ITEM_BOMB_GLOVE
                          || get32(hero + 0x20D4u) == ITEM_BOMB_GLOVE))) {
        set32(pending, ITEM_WRENCH);
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
        guard_unowned_bomb_glove();
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

/* Prevent opening a vendor whose Archipelago-filtered stock is empty. */
static void guard_empty_vendor(void) {
    if (s_disc == NULL || (strcmp(s_disc->version, "ntsc") != 0
        && strcmp(s_disc->version, "pal") != 0))
        return;

    uint8_t real[VENDOR_STOCK_SIZE];
    uint8_t shown[VENDOR_STOCK_SIZE];
    memcpy(real, byte_at(s_disc->vendor_stock), sizeof real);
    vendor_stock_shown(real, shown);

    const int empty = shown[0] == STOCK_END;

    const openrac_gaddr base =
        (openrac_gaddr)get32(level_byte_at(
            strcmp(s_disc->version, "pal") == 0 ? 0x00160098u : 0x0015FFD8u));

    if (!base)
        return;

    static openrac_moby mobys[MOBY_LIST_MAX];
    int n = s_api->moby_list(mobys, MOBY_LIST_MAX);
    if (n > MOBY_LIST_MAX)
        n = MOBY_LIST_MAX;

    static uint32_t guarded_loads = UINT32_MAX;
    static openrac_gaddr guarded_base = 0;
    static uint8_t saved_flags[MOBY_LIST_MAX];
    static uint8_t guarded[MOBY_LIST_MAX];

    uint32_t loads = 0;
    play_phase(&loads);

    if (guarded_loads != loads || guarded_base != base) {
        memset(guarded, 0, sizeof guarded);
        guarded_loads = loads;
        guarded_base = base;
    }

    for (int i = 0; i < n; ++i) {
        if (mobys[i].oclass != 0xB ||
            (mobys[i].flags & OPENRAC_MOBY_DEAD) ||
            mobys[i].slot < 0 ||
            mobys[i].slot >= MOBY_LIST_MAX)
            continue;

        openrac_gaddr address =
            base + (openrac_gaddr)mobys[i].slot * 0x100u;

        uint8_t* moby = (uint8_t*)OPENRAC_G(s_api, address);

        int16_t oclass;
        memcpy(&oclass, moby + 0xA6, sizeof oclass);

        if (oclass != 0xB)
            continue;

        const int slot = mobys[i].slot;
        if (slot >= MOBY_LIST_MAX)
            continue;

        if (empty) {
            if (!guarded[slot]) {
                saved_flags[slot] = moby[0x52];
                guarded[slot] = 1;
            }
            moby[0x52] = 0;
        } else if (guarded[slot]) {
            moby[0x52] = saved_flags[slot];
            guarded[slot] = 0;
        }
    }
}

/* Block an empty Gadgetron PDA vendor before the game builds its menu. */
static void open_vendor_menu_hook(uint32_t moby) {
    if (moby == 0 && s_disc != NULL) {
        uint8_t real[VENDOR_STOCK_SIZE];
        uint8_t shown[VENDOR_STOCK_SIZE];

        memcpy(real, byte_at(s_disc->vendor_stock), sizeof real);
        vendor_stock_shown(real, shown);

        int has_pda_stock = 0;
        uint8_t* item_info = level_byte_at(s_disc->item_price_table);

        for (int i = 0; i < VENDOR_STOCK_SIZE && shown[i] != STOCK_END; ++i) {
            const int item = shown[i] & 0x3F;
            if (item >= VENDOR_ITEM_COUNT) {
                continue;
            }
            uint16_t ammo_supported;
            memcpy(&ammo_supported, item_info + item * 0x18 + 0x08, sizeof ammo_supported);

            if ((shown[i] & STOCK_AMMO) && ammo_supported != 0) {
                has_pda_stock = 1;
                break;
            }
        }

        for (int item = 0; item < VENDOR_ITEM_COUNT && !has_pda_stock; ++item) {
            uint16_t ammo_supported;
            memcpy(&ammo_supported, item_info + item * 0x18 + 0x08, sizeof ammo_supported);

            if (*owned(item) != 0 && ammo_supported != 0)
                has_pda_stock = 1;
        }

        if (!has_pda_stock)
            return;
    }

    s_open_vendor_menu(moby);
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
        uint8_t* menu = level_byte_at(s_disc->vendor_menu);

        /*
         * The original purchase handler still initializes the selected
         * item after rebuilding the rows. Keep one valid row until the
         * vendor's normal closing transition has begun.
         */
        if ((strcmp(s_disc->version, "ntsc") == 0
             || strcmp(s_disc->version, "pal") == 0)
            && get32(menu + VENDOR_COUNT) == 0) {
            set32(menu + VENDOR_CURSOR, 0);
            set32(menu + VENDOR_ENTRIES, s_bought);
            set32(menu + VENDOR_ENTRIES + VENDOR_ENTRY_AMMO, 0);
            set32(menu + VENDOR_COUNT, 1);

            /* Version-specific vendor close countdown and request flag. */
            if (strcmp(s_disc->version, "pal") == 0) {
                set32(level_byte_at(0x00161F14u), 1);
                set32(level_byte_at(0x00161F18u), 1);
            } else {
                set32(level_byte_at(0x00161E14u), 1);
                set32(level_byte_at(0x00161E18u), 1);
            }
        }

        settle_menu_after_purchase();
    }
}

/* Hook for GoldBolts_CountCollected: the gold bolts the server has sent. */
static int32_t count_gold_bolts_hook(void) {
    read_received();
    return s_received_gold < MAX_GOLD_BOLTS ? s_received_gold : MAX_GOLD_BOLTS;
}

/* Retail still performs affordability checks and charges bolts. Only its bought/owned flag
 * is swapped while the case runs, because the random reward belongs to the AP server. */
static void gold_case_update(openrac_gaddr moby) {
    const int mode = ap_client_shuffle_gold_weapons();
    if (mode < 0) return; /* no purchases before the room settings/save are known */
    if (!mode) { s_gold_case_update(moby); return; }
    if (!s_has_gold_currency_hook) return;
    uint8_t* m = (uint8_t*)OPENRAC_G(s_api, moby);
    int16_t oclass;
    memcpy(&oclass, m + 0xA6, sizeof oclass);
    const uint32_t pvars = (uint32_t)get32(m + MOBY_PVARS);
    if (oclass != 304 || !pvars) { s_gold_case_update(moby); return; }
    const int slot = i32_at(pvars + 0x40u);
    if (slot < 0 || slot >= 20) { s_gold_case_update(moby); return; }
    const uint32_t table = strcmp(s_disc->version, "pal") == 0 ? 0x001DEDE0u : 0x001DEB10u;
    uint8_t* entries = (uint8_t*)OPENRAC_G(s_api, s_api->relocate_level_data(1, table));
    const int base = get32(entries + slot * 4);
    const int index = ap_gold_weapon_index(base);
    if (index < 0) { s_gold_case_update(moby); return; }
    const uint32_t bit = 1u << index;
    const int had_case = (gold_case_checks() & bit) != 0;
    if (!had_case && s_api->hero_pose) {
        openrac_pose pose = {0};
        pose.size = sizeof pose;
        if (s_api->hero_pose(&pose) == OPENRAC_HOOK_OK) {
            float position[3];
            memcpy(position, m + 0x10, sizeof position);
            float distance = 0;
            for (int axis = 0; axis < 3; ++axis) {
                const float d = pose.position[axis] - position[axis];
                distance += d * d;
            }
            if (distance < s_gold_hint_distance) {
                const uint32_t prices = strcmp(s_disc->version, "pal") == 0 ? 0x001C47B0u : 0x001C4530u;
                uint8_t* price_table = (uint8_t*)OPENRAC_G(s_api, s_api->relocate_level_data(1, prices));
                uint16_t price;
                memcpy(&price, price_table + base * 0x18 + 0x14, sizeof price);
                s_gold_hint_location = ap_gold_case_id(base);
                s_gold_hint_distance = distance;
                s_gold_hint_price = price;
            }
        }
    }
    const uint8_t upgrade = *gold_owned(base);
    const int previous = s_gold_case;
    const uint8_t previous_upgrade = s_gold_real_upgrade;
    s_gold_real_upgrade = upgrade;
    s_gold_case = index;
    *gold_owned(base) = (uint8_t)had_case;
    s_gold_case_update(moby);
    const int bought = *gold_owned(base) != 0 && !had_case;
    *gold_owned(base) = upgrade;
    s_gold_case = previous;
    s_gold_real_upgrade = previous_upgrade;
    if (bought) {
        save_checks(saved_checks() | bit);
        ap_client_check(ap_gold_case_id(base));
    }
}

static int32_t count_unspent_hook(void) {
    if (ap_client_shuffle_gold_weapons() != 1) return s_count_unspent();
    read_received();
    uint32_t checks = gold_case_checks();
    if (s_gold_case >= 0 && *gold_owned(k_ap_gold_bases[s_gold_case])) checks |= 1u << s_gold_case;
    int bought = 0;
    for (int i = 0; i < AP_GOLD_WEAPON_COUNT; ++i) bought += (checks >> i) & 1u;
    const int total = s_received_gold < MAX_GOLD_BOLTS ? s_received_gold : MAX_GOLD_BOLTS;
    const int remaining = total - 4 * bought;
    return remaining < 0 ? 0 : remaining > MAX_GOLD_BOLTS ? MAX_GOLD_BOLTS : remaining;
}

static void begin_game(int fresh) {
    ap_client_start_game(fresh);
    s_slot_known = -1;
    s_from_title = 1;
    s_judge_wait = 0;
    s_generation = UINT32_MAX;
    s_goal = 0;
}

static void new_game(void) {
    ++s_in_game_reset;
    s_new_game();
    --s_in_game_reset;
    if (!s_in_game_reset) { s_restoring_save = 0; begin_game(1); }
}

static int32_t restore_data(uint32_t data, int32_t index, uint32_t table) {
    if (!s_in_game_reset && !s_restoring_save) {
        s_restoring_save = 1;
        begin_game(0);
    }
    return s_restore_data(data, index, table);
}

static void save_update(void) {
    /* memcard_update_state: state 20/substate 3 consumes the completed close.
     * A busy tick only polls I/O. A failed write/close never reaches this success. */
    uint8_t* mc = byte_at(strcmp(s_disc->version, "pal") == 0 ? 0x0013D390u : 0x0013D290u);
    const int done = get32(mc + 0xDC) == 20 && get32(mc + 0xE0) == 3
        && get32(mc + 0xD4) == 0 && get32(mc + 0xC4) == 0 && get32(mc + 0xCC) == 0;
    const int slot = get32(mc + 0x14);
    s_save_update();
    if (done && slot >= 0 && slot < 5 && get32(mc + 0xDC) == 0) {
        s_slot_known = slot;
        ap_client_save_written(slot);
    }
}

static void new_game_plus(int32_t slot) {
    const uint32_t checks = saved_checks();
    ++s_in_game_reset;
    s_new_game_plus(slot);
    --s_in_game_reset;
    if (checks) save_checks(checks);
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
#include "ap_npc_infobots.inc"

static void each_tick(void* user) {
    (void)user;
    uint32_t loads = 0;
    const int phase = play_phase(&loads);
    show_notices();
    read_received();
    watch_save_slot(phase);

    if (!playing_level()) {
        return;
    }


    /* The game's challenge_mode flag (PAL D_0015EFA0, NTSC-U D_0015EEA0).
     * The server is authoritative; do not touch an unjudged or foreign save. A false/missing
     * option leaves a normally earned New Game+ save alone rather than downgrading it. */
    if (!s_from_title && !s_judge_wait && ap_client_new_game_plus() == 1) {
        uint8_t* challenge_mode = byte_at(s_disc->level + 0x11Cu);
        if (get32(challenge_mode) == 0) {
            set32(challenge_mode, 1);
            log_line(OPENRAC_LOG_INFO, "Archipelago: New Game+ enabled for this slot");
            s_api->play_notice(s_mod, "Archipelago: New Game+ enabled");
            s_notice_ticks = NOTICE_TICKS;
        }
        /* Full Novalis gold-case stock also reads the retail postgame-access flag. Only
         * enable that access when gold cases are shuffled; no upgrades or checks are granted. */
        if (ap_client_shuffle_gold_weapons() == 1) set32(byte_at(s_disc->level + 0x9Cu), 1);
    }

    if (ap_client_shuffle_gold_weapons() == 1 && !s_has_gold_currency_hook && !s_gold_hook_warned) {
        s_api->play_notice(s_mod, "Gold weapon shuffle needs an updated OpenRAC (gold-bolt spending hook)");
        s_notice_ticks = NOTICE_TICKS;
        s_gold_hook_warned = 1;
        log_line(OPENRAC_LOG_WARN, "Gold weapon case purchases disabled: this OpenRAC lacks GoldBolts_CountUnspent");
    }

    if (phase == OPENRAC_PLAY_PLAYING && s_notice_ticks == 0 && s_gold_hint_location >= 0) {
        char reward[160], line[NOTICE_SIZE];
        if (ap_client_location_hint(s_gold_hint_location, reward, sizeof reward)) {
            snprintf(line, sizeof line, "%s - %u bolts + 4 gold bolts", reward, s_gold_hint_price);
            s_api->play_notice(s_mod, line);
            s_notice_ticks = 120;
        }
    }
    s_gold_hint_location = -1;
    s_gold_hint_distance = 9.0f;

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

    // A skill point the game just gave: the game keeps its own banner.
    for (int i = 0; i < AP_SKILL_POINT_COUNT; ++i) {
        const int now = skill_had(i);

        if (now && !s_skill_seen[i]) {
            ap_client_check(ap_skill_id(&k_ap_skill_points[i]));
        }

        s_skill_seen[i] = (uint8_t)now;
    }

    // Gifts only with the player in control, not inside a scene or a menu.
    if (phase == OPENRAC_PLAY_PLAYING) {
        give_received();
    }

    guard_unowned_bomb_glove();
    guard_empty_vendor();

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
    if (!npc_infobot_hooks()) return 0;
    /* Older ports still support every other option. Never permit randomized purchases
     * without independent currency accounting; the player gets a clear update notice. */
    s_has_gold_currency_hook = s_api->hook(
        s_mod, OPENRAC_RAC1_HOOK_GoldBolts_CountUnspent, OPENRAC_RAC1_SIG_GoldBolts_CountUnspent,
        (void*)count_unspent_hook, (void**)&s_count_unspent) == OPENRAC_HOOK_OK;
    s_gold_hook_warned = 0;
    if (!take(s_api->hook(s_mod, "rac1.save.update_state", "void()", (void*)save_update,
        (void**)&s_save_update), "rac1.save.update_state")
        || !take(s_api->hook(s_mod, "rac1.save.restore_data", "i32(u32,i32,u32)", (void*)restore_data,
        (void**)&s_restore_data), "rac1.save.restore_data")
        || !take(s_api->hook(s_mod, ITEM_RANDOMIZER_HOOK_Save_NewGame, ITEM_RANDOMIZER_SIG_Save_NewGame,
        (void*)new_game, (void**)&s_new_game), ITEM_RANDOMIZER_HOOK_Save_NewGame)) return 0;
    return take(s_api->hook(s_mod, ITEM_RANDOMIZER_HOOK_Save_NewGamePlus,
                            ITEM_RANDOMIZER_SIG_Save_NewGamePlus, (void*)new_game_plus,
                            (void**)&s_new_game_plus), ITEM_RANDOMIZER_HOOK_Save_NewGamePlus)
           && HOOK(Moby_GoldWeaponCase_Update, gold_case_update, &s_gold_case_update)
           && HOOK(Vendor_OpenMenu, open_vendor_menu_hook, &s_open_vendor_menu)
           && HOOK(Item_Give, item_give, &s_item_give)
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
           && HOOK(Moby_Class22_Update, orxon_start_update, &s_orxon_start_update)
           && HOOK(Ship_InitTransition, ship_transition, &s_ship_transition)
           && HOOK(Help_Request, help_request, &s_help_request);
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
        api->api_minor >= 11 && api->size >= offsetof(openrac_api, data_dir) + sizeof(void*)
                && api->data_dir != NULL
            ? api->data_dir(mod)
            : NULL,
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
