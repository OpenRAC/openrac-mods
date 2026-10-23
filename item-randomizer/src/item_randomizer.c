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
 * Nothing of the mod's goes into a save: what is had is the game's own bytes, and the placement
 * comes from the seed. A save made with the mod belongs to its seed; with the mod off the game is
 * the console's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "openrac/openrac_mod.h"

#include "config.h"
#include "discs.h"
#include "hooks.h"
#include "paths.h"
#include "rando.h"

#define MOD_ID "item-randomizer"
#define MOD_VERSION "1.0.1"

OPENRAC_MOD_DEFINE(MOD_ID, MOD_VERSION)

static const openrac_api* s_api;
static openrac_mod* s_mod;
static const rando_disc* s_disc;
static rando_plan s_plan;
static char s_code[RANDO_CODE_SIZE];

/* Each location's index by its key: the item id (LOC_GIVE, LOC_VENDOR, LOC_START), the level and
 * mission (LOC_INFOBOT); -1 for none or not shuffled. Gold bolts are found by the check each tick. */
static int s_by_item[RANDO_ITEM_COUNT];
static int s_by_infobot[LEVEL_COUNT][16];
/* Each location's had state as the last tick saw it (the check each tick). */
static uint8_t s_seen[RANDO_MAX_LOCATIONS];

/* What each hook point goes on to. */
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
static void (*s_show_banner)(int32_t text, int32_t ticks);

/* ---- game memory ---- */

static uint8_t* byte_at(uint32_t address) {
    return (uint8_t*)OPENRAC_G(s_api, s_api->relocate_data(address));
}

static int32_t i32_at(openrac_gaddr a) {
    int32_t v;
    memcpy(&v, OPENRAC_G(s_api, a), 4);
    return v;
}

static void put_i32(openrac_gaddr a, int32_t v) {
    memcpy(OPENRAC_G(s_api, a), &v, 4);
}

/* A level program's global, by level 0's address (the vendor's menu is in each level's). */
static uint8_t* level_byte_at(uint32_t address) {
    return (uint8_t*)OPENRAC_G(s_api, s_api->relocate_level_data(0, address));
}

static int32_t get32(const uint8_t* p) {
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void set32(uint8_t* p, int32_t v) {
    memcpy(p, &v, 4);
}

static int current_level(void) {
    int32_t v;
    memcpy(&v, byte_at(s_disc->level), 4);
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

/* ---- the game's bytes shown otherwise while one of its routines runs ---- */

/* A byte the game is shown as a location's had state (its `shown`), with its real value kept. */
typedef struct swap {
    uint8_t* at;
    uint8_t real;
    int location;
} swap;

static swap s_swaps[4];
static int s_swap_count;

static int had(int location);

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

/* The real bytes back while the game's GiveItem or UnlockPlanet runs for a reward... */
static void swaps_suspend(void) {
    for (int i = s_swap_count - 1; i >= 0; --i) {
        *s_swaps[i].at = s_swaps[i].real;
    }
}

/* ...and shown again after, with what it changed kept as real. */
static void swaps_resume(void) {
    for (int i = 0; i < s_swap_count; ++i) {
        s_swaps[i].real = *s_swaps[i].at;
        *s_swaps[i].at = (uint8_t)had(s_swaps[i].location);
    }
}

/* ---- locations ---- */

static int had(int location) {
    const rando_location* l = &rando_locations[location];
    switch (l->kind) {
        case LOC_GIVE:
        case LOC_VENDOR:
        case LOC_START:
            return *obtained(l->key) != 0;
        case LOC_INFOBOT:
            return *byte_at(s_disc->triggers + (uint32_t)l->key + (uint32_t)l->level * 16u) == 0xFF;
        case LOC_GOLD:
            return *byte_at(s_disc->gold_bolts + (uint32_t)l->level * 4u + (uint32_t)l->key) != 0;
        default:
            return 0;
    }
}

/* Whether the reward is the player's already (a gold bolt is counted, never held). */
static int held(rando_reward r) {
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
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (rando_locations[i].kind == LOC_VENDOR && rando_locations[i].key == item) {
            return 1;
        }
    }
    return 0;
}

/* Whether item is some had location's reward. */
static int received(int item) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (s_plan.shuffled[i] && s_plan.reward[i].kind == REWARD_ITEM && s_plan.reward[i].value == item && had((int)i)) {
            return 1;
        }
    }
    return 0;
}

static void log_line(int level, const char* text) {
    s_api->log(s_mod, level, text);
}

/* Gives location's reward: an item through the game's GiveItem (keeping that item's obtained
 * byte as it was: it is its own location's), a planet through UnlockPlanet, a gold bolt's banner.
 * mark: an item whose obtained byte is then set (the location's own, when it is GiveItem's), or -1. */
static void give_reward(int location, int32_t equip, int mark) {
    const rando_reward r = s_plan.reward[location];
    swaps_suspend();
    if (r.kind == REWARD_ITEM) {
        const uint8_t before = *obtained(r.value);
        /* GiveItem marks a vendor weapon bought in the stock (or adds it): for a weapon that is a
         * slot, the stock is the slots', so it stays as it was (the list sells its ammo). */
        uint8_t* stock = byte_at(s_disc->vendor_stock);
        uint8_t kept[VENDOR_STOCK_SIZE];
        const int slot = vendor_slot(r.value) >= 0;
        if (slot) {
            memcpy(kept, stock, sizeof kept);
        }
        s_item_give(r.value, equip);
        if (slot) {
            memcpy(stock, kept, sizeof kept);
        }
        if (rando_locations[location].key != r.value || rando_locations[location].kind >= LOC_INFOBOT) {
            *obtained(r.value) = before;
        }
    } else if (r.kind == REWARD_PLANET) {
        s_planet_unlock(r.value);
    } else if (r.kind == REWARD_GOLD_BOLT && s_show_banner != NULL) {
        s_show_banner(s_disc->gold_bolt_text, -1);
    }
    if (mark >= 0) {
        *obtained(mark) = 1;
    }
    swaps_resume();
}

static int playing_level(void) {
    const int level = s_api->loaded_level() >= 0 ? current_level() : -1;
    return level >= 0 && level <= RANDO_LAST_LEVEL;
}

/* ---- the hooks ---- */

/* The vendor's menu entry the buy dialog is buying slot `item` from: the entry under the cursor,
 * while the dialog makes its purchase; null for any other GiveItem. */
static uint8_t* vendor_purchase(int item) {
    if (vendor_slot(item) < 0) {
        return NULL;
    }
    uint8_t* menu = level_byte_at(s_disc->vendor_menu);
    const int32_t cursor = get32(menu + VENDOR_CURSOR);
    if (get32(menu + VENDOR_DIALOG) != VENDOR_DIALOG_BUY || cursor < 0 || cursor >= VENDOR_MAX_ENTRIES) {
        return NULL;
    }
    uint8_t* entry = menu + VENDOR_ENTRIES + (uint32_t)cursor * VENDOR_ENTRY_SIZE;
    return get32(entry) == item && get32(entry + 4) == 0 ? entry : NULL;
}

/* A purchase whose list the vendor builds next (build_item_list): the slot's item, -1 for none. */
static int s_bought = -1;

/* GiveItem(id, equip): id's location's reward, and the location had. */
static void item_give(int32_t id, int32_t equip) {
    const int location = id >= 0 && id < RANDO_ITEM_COUNT ? s_by_item[id] : -1;
    if (location < 0 || !playing_level()) {
        s_item_give(id, equip);
        return;
    }
    uint8_t* entry = vendor_purchase(id);
    give_reward(location, equip, id);
    s_seen[location] = 1;
    if (entry != NULL) {
        /* Right after GiveItem the dialog takes the entry's item as the one bought, whose demo
         * plays as the menu closes and which the hero is then handed: the reward when it is one of
         * the vendor's weapons (each level has their demos), else the one bought before, as it
         * was (GiveItem has equipped the reward already). */
        const rando_reward r = s_plan.reward[location];
        const int demo = r.kind == REWARD_ITEM && sold_by_vendor(r.value);
        set32(entry, demo ? r.value : get32(level_byte_at(s_disc->vendor_menu) + VENDOR_LAST_BOUGHT));
        s_bought = id;
    }
}

/* The infobot whose update is running, and the location it is; -1 for none. */
static int s_infobot = -1;

static void planet_unlock(int32_t planet) {
    if (s_infobot >= 0 && rando_locations[s_infobot].vanilla.value == planet) {
        const int location = s_infobot;
        give_reward(location, 0, -1);
        s_seen[location] = 1; /* the game marks its trigger slot next */
        return;
    }
    s_planet_unlock(planet); /* an NPC's coordinates, or the planet arrived at */
}

static void infobot_update(openrac_gaddr moby) {
    const int level = current_level();
    const int mission = OPENRAC_GREF(s_api, uint8_t, moby + MOBY_MISSION);
    const int location = level >= 0 && level < LEVEL_COUNT && mission < 16 ? s_by_infobot[level][mission] : -1;
    const openrac_gaddr pvars = OPENRAC_GREF(s_api, openrac_gaddr, moby + MOBY_PVARS);
    const int planet = location >= 0 && pvars != 0 ? i32_at(pvars + INFOBOT_PLANET) : -1;
    if (location < 0 || planet != rando_locations[location].vanilla.value) {
        s_infobot_update(moby);
        return;
    }
    swap_push(planet_flag(planet), location);
    s_infobot = location;
    /* Its last state shows its planet's banner: the planet it gave, when it gave one. */
    const rando_reward r = s_plan.reward[location];
    const int banner = OPENRAC_GREF(s_api, uint8_t, moby + MOBY_STATE) == INFOBOT_STATE_BANNER;
    if (banner && r.kind == REWARD_PLANET) {
        put_i32(pvars + INFOBOT_PLANET, r.value);
    }
    s_infobot_update(moby);
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

/* The level's start: the check each tick takes what is had now as seen (a save loaded, a new
 * game), and the starting Bomb Glove, when shuffled, is its location's. */
static void sync_seen(void) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        s_seen[i] = s_plan.shuffled[i] ? (uint8_t)had((int)i) : 0;
    }
}

static void init_level_stock(void) {
    sync_seen();
    const int start = s_by_item[ITEM_BOMB_GLOVE];
    if (start >= 0 && rando_locations[start].kind == LOC_START && playing_level()) {
        /* The game gives the Bomb Glove (the reward, through item_give), then puts it first in the
         * stock and makes it the weapon a double tap goes back to: when the reward was something
         * else, the stock and that weapon are put back as they were. */
        uint8_t* stock = byte_at(s_disc->vendor_stock);
        uint8_t* last = byte_at(s_disc->last_weapon);
        uint8_t before[VENDOR_STOCK_SIZE];
        memcpy(before, stock, sizeof before);
        const int32_t last_before = get32(last);
        swap_push(owned(ITEM_BOMB_GLOVE), start);
        s_init_level_stock();
        swap_pop();
        if (!*owned(ITEM_BOMB_GLOVE) && stock[0] == (ITEM_BOMB_GLOVE | 0x40) && before[0] != stock[0]) {
            memmove(stock, stock + 1, VENDOR_STOCK_SIZE - 1);
            stock[VENDOR_STOCK_SIZE - 1] = 0xFF;
            if (before[0] != 0xFF && stock[VENDOR_STOCK_SIZE - 1] == 0xFF) {
                int present = 0;
                for (int i = 0; i < VENDOR_STOCK_SIZE && stock[i] != 0xFF; ++i) {
                    present |= (stock[i] & 0x3F) == (before[0] & 0x3F);
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
    } else {
        s_init_level_stock();
    }
    sync_seen();
}

/* The vendor's stock as the menu is built from it (the game lists each byte: the item for sale,
 * or with 0x40 its ammo; then the ammo of every other weapon owned):
 *   - a slot not bought: its weapon, for sale at its price;
 *   - a slot bought: in its place the ammo of its reward when that is a weapon owned, else of its
 *     own weapon when owned (from elsewhere), else nothing;
 *   - a weapon owned whose slot is still for sale: its ammo too, after the rest;
 *   - anything else as it is, but the ammo of an item not owned.
 * Each item's ammo is listed once. */
static void vendor_stock_shown(const uint8_t real[VENDOR_STOCK_SIZE], uint8_t shown[VENDOR_STOCK_SIZE]) {
    uint8_t list[VENDOR_STOCK_SIZE];
    int bought[VENDOR_STOCK_SIZE];
    uint8_t ammo[RANDO_ITEM_COUNT] = {0}, sale[RANDO_ITEM_COUNT] = {0};
    int n = 0;
    for (int i = 0; i < VENDOR_STOCK_SIZE && real[i] != 0xFF; ++i) {
        const int item = real[i] & 0x3F;
        const int location = item < RANDO_ITEM_COUNT ? vendor_slot(item) : -1;
        bought[n] = -1;
        if (location < 0) {
            if ((real[i] & 0x40) && (item >= RANDO_ITEM_COUNT || !*owned(item) || ammo[item])) {
                continue;
            }
            if (real[i] & 0x40) {
                ammo[item] = 1;
            }
            list[n++] = real[i];
        } else if (!had(location)) {
            sale[item] = 1;
            list[n++] = (uint8_t)item;
        } else {
            bought[n] = location;
            list[n++] = 0xFF;
        }
    }
    for (int i = 0; i < n; ++i) {
        if (bought[i] < 0) {
            continue;
        }
        const rando_reward r = s_plan.reward[bought[i]];
        const int own = rando_locations[bought[i]].key;
        if (r.kind == REWARD_ITEM && r.value < RANDO_ITEM_COUNT && *owned(r.value) && !ammo[r.value]) {
            ammo[r.value] = 1;
            list[i] = (uint8_t)(r.value | 0x40);
        } else if (*owned(own) && !ammo[own]) {
            ammo[own] = 1;
            list[i] = (uint8_t)(own | 0x40);
        }
    }
    int m = 0;
    for (int i = 0; i < n; ++i) {
        if (list[i] != 0xFF) {
            shown[m++] = list[i];
        }
    }
    for (int item = 0; item < RANDO_ITEM_COUNT && m < VENDOR_STOCK_SIZE; ++item) {
        if (sale[item] && *owned(item) && !ammo[item]) {
            shown[m++] = (uint8_t)(item | 0x40);
        }
    }
    memset(shown + m, 0xFF, (size_t)(VENDOR_STOCK_SIZE - m));
}

static void build_item_list(int32_t skip_unflagged) {
    uint8_t* stock = byte_at(s_disc->vendor_stock);
    uint8_t real[VENDOR_STOCK_SIZE], shown[VENDOR_STOCK_SIZE];
    memcpy(real, stock, sizeof real);
    vendor_stock_shown(real, shown);
    memcpy(stock, shown, sizeof shown);
    s_build_item_list(skip_unflagged);
    memcpy(stock, real, sizeof real);

    /* Right after a purchase: the list may hold another entry where the slot was (its reward's
     * ammo), or one fewer. The cursor stays on the list, and the menu shows the item now under it
     * (the game redraws it only when the cursor moves, or when a weapon for sale is under it). */
    if (s_bought >= 0) {
        uint8_t* menu = level_byte_at(s_disc->vendor_menu);
        const int32_t count = get32(menu + VENDOR_COUNT);
        int32_t cursor = get32(menu + VENDOR_CURSOR);
        if (count > 0 && cursor >= count) {
            cursor = count - 1;
            set32(menu + VENDOR_CURSOR, cursor);
        }
        if (cursor >= 0 && cursor < VENDOR_MAX_ENTRIES && get32(menu + VENDOR_ENTRIES + (uint32_t)cursor * VENDOR_ENTRY_SIZE) != s_bought) {
            set32(level_byte_at(s_disc->vendor_redraw), 1);
        }
        s_bought = -1;
    }
}

static int32_t count_gold_bolts_hook(void) {
    if (!(s_plan.settings.pools & POOL_GOLD)) {
        return s_count_gold_bolts();
    }
    int n = 0;
    for (size_t i = 0; i < rando_location_count; ++i) {
        n += s_plan.shuffled[i] && s_plan.reward[i].kind == REWARD_GOLD_BOLT && had((int)i);
    }
    return n < 40 ? n : 40;
}

/* Each tick: a location had since the last tick without passing through a hook (a gold bolt, a
 * route that writes the bytes itself) gets its reward, unless it is the player's already (a save
 * just loaded has its rewards). A place that wrote its own item's bytes gives that item back. */
static void each_tick(void* user) {
    (void)user;
    if (!playing_level()) {
        return;
    }
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (!s_plan.shuffled[i]) {
            continue;
        }
        const int now = had((int)i);
        if (now && !s_seen[i]) {
            const rando_location* l = &rando_locations[i];
            if ((l->kind == LOC_GIVE || l->kind == LOC_VENDOR || l->kind == LOC_START) && *owned(l->key) && !received(l->key)) {
                *owned(l->key) = 0;
            }
            if (!held(s_plan.reward[i])) {
                give_reward((int)i, 0, -1);
            }
        }
        s_seen[i] = (uint8_t)now;
    }
}

/* ---- loading ---- */

static int take(int result, const char* point) {
    if (result != OPENRAC_HOOK_OK) {
        char reason[160];
        snprintf(reason, sizeof reason, "this OpenRAC cannot hook %s (%s): it needs an OpenRAC with the item hook points",
                 point, result == OPENRAC_HOOK_SIGNATURE ? "its signature is another" : "no such hook point");
        s_api->fail(s_mod, reason);
        return 0;
    }
    return 1;
}

#define HOOK(point, fn, original) take(s_api->hook(s_mod, OPENRAC_RAC1_HOOK_##point, OPENRAC_RAC1_SIG_##point, (void*)(fn), (void**)(original)), #point)

/* Twelve letters and digits, hyphens and spaces aside: meant as a code. */
static int looks_like_code(const char* text) {
    int n = 0;
    for (const char* p = text != NULL ? text : ""; *p != 0; ++p) {
        if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            ++n;
        } else if (*p != '-' && *p != ' ') {
            return 0;
        }
    }
    return n == 12;
}

static uint64_t fresh_seed(void) {
    rando_rng r = {(uint64_t)time(NULL) ^ ((uint64_t)clock() << 32) ^ (uint64_t)(uintptr_t)&r};
    return rando_next(&r);
}

/* The remembered seed, the spoiler log: in the mod's data folder. */
static void write_files(int source) {
    char dir[1024], path[1200];
    if (rando_data_dir(dir, sizeof dir) != 0) {
        log_line(OPENRAC_LOG_WARN, "no data folder for the spoiler log (no home folder)");
        return;
    }
    if (source == SEED_FROM_NEW) {
        rando_join(path, sizeof path, dir, "seed.txt");
        char line[RANDO_CODE_SIZE + 1];
        snprintf(line, sizeof line, "%s\n", s_code);
        rando_write_file(path, line, strlen(line));
    }
    if (s_api->setting_bool(s_mod, "spoiler_log")) {
        static char text[64 * 1024];
        const size_t need = rando_spoiler(&s_plan, MOD_VERSION, text, sizeof text);
        char name[64];
        snprintf(name, sizeof name, "spoiler-%s.txt", s_code);
        rando_join(path, sizeof path, dir, name);
        if (rando_write_file(path, text, strlen(text)) == 0 && need <= sizeof text) {
            char line[1300];
            snprintf(line, sizeof line, "spoiler log: %s", path);
            log_line(OPENRAC_LOG_INFO, line);
        }
    }
}

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    s_disc = NULL;
    if (strcmp(api->game, "rac1") != 0) {
        api->fail(mod, "the item randomizer is for Ratchet & Clank (rac1)");
        return 1;
    }
    for (size_t i = 0; i < sizeof k_rando_discs / sizeof k_rando_discs[0]; ++i) {
        if (strcmp(api->version, k_rando_discs[i].version) == 0) {
            s_disc = &k_rando_discs[i];
        }
    }
    if (s_disc == NULL) {
        char reason[96];
        snprintf(reason, sizeof reason, "the item randomizer knows the PAL and NTSC-U discs, not %s", api->version);
        api->fail(mod, reason);
        return 1;
    }

    /* The settings and the seed. */
    char dir[1024], path[1200], remembered[64] = "";
    int have_remembered = 0;
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
    const int source = rando_settings_of(&options, have_remembered ? remembered : NULL, fresh_seed(), &settings);
    if (rando_solve(&settings, &s_plan) != 0) {
        api->fail(mod, "no placement for these settings (please report them)");
        return 1;
    }
    rando_encode(&s_plan.settings, s_code);
    if (source == SEED_FROM_WORD && looks_like_code(options.seed)) {
        log_line(OPENRAC_LOG_WARN, "the seed setting looks like a seed code but is not one (a character mistyped?): it is read "
                                   "as a word, so this is not the shared game");
    }

    for (int i = 0; i < RANDO_ITEM_COUNT; ++i) {
        s_by_item[i] = -1;
    }
    memset(s_by_infobot, 0xFF, sizeof s_by_infobot);
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        if (!s_plan.shuffled[i]) {
            continue;
        }
        if (l->kind == LOC_INFOBOT) {
            s_by_infobot[l->level][l->key] = (int)i;
        } else if (l->kind != LOC_GOLD) {
            s_by_item[l->key] = (int)i;
        }
    }

    s_show_banner = (void (*)(int32_t, int32_t))api->function(s_disc->show_banner);
    if (s_plan.settings.pools != 0) {
        if (!HOOK(Item_Give, item_give, &s_item_give) || !HOOK(Planet_Unlock, planet_unlock, &s_planet_unlock)
            || !HOOK(Vendor_InitLevelStock, init_level_stock, &s_init_level_stock)
            || !HOOK(Vendor_BuildItemList, build_item_list, &s_build_item_list)
            || !HOOK(GoldBolts_CountCollected, count_gold_bolts_hook, &s_count_gold_bolts)
            || !HOOK(Moby_Infobot_Update, infobot_update, &s_infobot_update)
            || !HOOK(Moby_PilotsHelmet_Update, helmet_update, &s_helmet_update)
            || !HOOK(Moby_Helga_Update, helga_update, &s_helga_update)
            || !HOOK(Moby_BigAl_Update, big_al_update, &s_big_al_update)
            || !HOOK(Moby_Bob_Update, bob_update, &s_bob_update)
            || !HOOK(Moby_Edwina_Update, edwina_update, &s_edwina_update)) {
            return 1;
        }
        api->on_frame(mod, each_tick, NULL);
    }

    char line[256];
    static const char* const k_sources[] = {"", "from the code", "from the word", "remembered", "new"};
    snprintf(line, sizeof line, "seed %s (%s), %s, on the %s disc: %d placement%s tried", s_code, k_sources[source],
             rando_difficulty_name(s_plan.settings.difficulty), api->version, s_plan.attempts,
             s_plan.attempts == 1 ? "" : "s");
    log_line(OPENRAC_LOG_INFO, line);
    write_files(source);
    return 0;
}
