/* SPDX-License-Identifier: GPL-3.0-or-later
 * The item randomizer's library as the port loads it, against a host table and a game memory of
 * the test's own (openrac_mod.h). It reports its id and interface, refuses another game and an
 * unknown disc, takes its hook points with their signatures, writes the remembered seed and
 * the spoiler log to its data folder; then, with game functions of the test's own that set the
 * game's bytes as the game's do:
 *
 *   - GiveItem at a shuffled place gives that place's reward, marks the place had, and leaves the
 *     reward's own place unhad; a place left as it is gives its own item;
 *   - Helga's update sees the Swingshot's owned byte as her place's had state, and the real one is
 *     back after it; an infobot's update and UnlockPlanet give the infobot's reward;
 *   - the vendor, with a purchase as the game's buy dialog makes it, for each vendor slot: the
 *     reward is owned (so it stays when the hero changes weapons), the weapon the dialog hands the
 *     hero after is the reward (never the slot's weapon, which is not owned), the stock is the
 *     slots', the list sells only slots not bought and the ammo of weapons owned, the bought
 *     slot's place is its reward's ammo, the cursor stays on the list and the menu redraws the
 *     item under it when that changed;
 *   - with the starting gear shuffled, the level start gives the start's reward, and the Bomb
 *     Glove is neither in the stock nor the double tap's weapon unless it was the reward;
 *   - the gold bolt count is the gold bolt rewards had; a gold bolt picked up gives its reward at
 *     the next tick;
 *   - a reward's banner is its name, written once the player is in control and held over the
 *     game's, never GiveItem's; the Hydro-Pack is never equipped, nothing is while Clank plays;
 *   - a place had whose reward a save lost gives it back at the level's start, without a banner;
 *   - in a party, what is passed on spends the place that held it, and leaving gives nothing;
 *   - Orxon shows no O2 Mask until Clank's part is done; the Pilot's Helmet's pickup hands the hero
 *     no helmet he does not own; a tutorial of what was not given is left out; a crate's drop sees
 *     the stock as the menu does; the line over the vendor names what the slot holds.
 *
 * Game addresses are the PAL and NTSC-U rows of src/discs.h; the test's game functions copy what
 * the routines named in src/item_randomizer.c's header do to those bytes.
 *
 * Run with the library's path: load_test <item_randomizer.dylib|.so|.dll>.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* setenv, mkdir, rmdir */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "discs.h"
#include "hooks.h"
#include "openrac/openrac_mod.h"
#include "rando.h"

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>

static void* open_library(const char* path) {
    return (void*)LoadLibraryA(path);
}

static void* symbol(void* lib, const char* name) {
    return (void*)GetProcAddress((HMODULE)lib, name);
}

static void set_env(const char* k, const char* v) {
    _putenv_s(k, v);
}
#else
#include <dlfcn.h>
#include <unistd.h>

static void* open_library(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void* symbol(void* lib, const char* name) {
    return dlsym(lib, name);
}

static void set_env(const char* k, const char* v) {
    setenv(k, v, 1);
}
#endif

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define MEMORY_SIZE 0x00200000u
static uint8_t* s_memory;
static char s_failed[256];
static const rando_disc* D;
static const char* s_seed = "load-test";

static struct {
    const char* name;
    const char* signature;
    void* replacement;
} s_hooks[32];

static int s_hook_count;
static openrac_frame_fn s_frame;

/* ---- the game's functions, as the test plays them ---- */

static int s_gives, s_unlocks, s_banners, s_list_builds, s_counted;
static uint8_t s_helga_saw = 0xEE;
static int s_keep_start = 1;
static int32_t s_last_equip = -1;
static uint8_t s_ship_saw = 0xEE, s_class22_saw = 0xEE;
static int s_helps;
static int s_drop_saw;
static char s_notice[128];
static int s_phase = OPENRAC_PLAY_PLAYING;

static int32_t get32(const uint8_t* p) {
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void set32(uint8_t* p, int32_t v) {
    memcpy(p, &v, 4);
}

/* The weapons the vendor sells (the game's table of one new weapon per planet). */
static int vendor_weapon(int item) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (rando_locations[i].kind == LOC_VENDOR && rando_locations[i].key == item) {
            return 1;
        }
    }

    return 0;
}

/* Whether the vendor sells an item's ammo (the test's own table: the weapons but the Taunter and
 * the Walloper). */
static int has_ammo(int item) {
    return item >= ITEM_SUCK_CANNON && item <= ITEM_DECOY_GLOVE && item != ITEM_TAUNTER
           && item != ITEM_WALLOPER;
}

/* GiveItem as the game's: obtained set; the first time, owned set, and a vendor weapon marked
 * bought in the stock (its byte with 0x40, added when it is not there). */
static void game_give(int32_t id, int32_t equip) {
    s_last_equip = equip;
    s_memory[D->obtained + (uint32_t)id] = 1;
    /* The banner GiveItem shows: in a level without the item's text, the game's "Paradox!". */
    strcpy((char*)s_memory + D->banner_text, "Paradox");
    memcpy(s_memory + D->banner_ticks, &(int32_t){300}, 4);

    if (s_memory[D->owned + (uint32_t)id] == 0) {
        s_memory[D->owned + (uint32_t)id] = 1;

        if (vendor_weapon(id)) {
            uint8_t* stock = s_memory + D->vendor_stock;

            for (int j = 0; j < VENDOR_STOCK_SIZE; ++j) {
                if ((stock[j] & 0x3F) == id || stock[j] == 0xFF) {
                    stock[j] = (uint8_t)(id | 0x40);
                    break;
                }
            }
        }
    }

    ++s_gives;
}

static void game_unlock(int32_t planet) {
    s_memory[D->planets + (uint32_t)planet] = 1;
    ++s_unlocks;
}

/* VendorBuildItemList as the game's: the stock's bytes (for sale, or with 0x40 ammo, when the
 * item has ammo), then the ammo of each other item owned. */
static void game_build(int32_t skip) {
    const uint8_t* stock = s_memory + D->vendor_stock;
    uint8_t* menu = s_memory + D->vendor_menu;
    int n = 0;

    for (int i = 0; i < VENDOR_STOCK_SIZE && stock[i] != 0xFF; ++i) {
        const int item = stock[i] & 0x3F;

        if ((stock[i] & 0x40) ? !has_ammo(item) : skip) {
            continue;
        }

        set32(menu + VENDOR_ENTRIES + (uint32_t)n * VENDOR_ENTRY_SIZE, item);
        set32(menu + VENDOR_ENTRIES + (uint32_t)n * VENDOR_ENTRY_SIZE + 4, (stock[i] & 0x40) != 0);
        ++n;
    }

    for (int k = 0; k < RANDO_ITEM_COUNT; ++k) {
        int listed = 0;

        for (int j = 0; j < n; ++j) {
            listed |= get32(menu + VENDOR_ENTRIES + (uint32_t)j * VENDOR_ENTRY_SIZE) == k;
        }

        if (s_memory[D->owned + (uint32_t)k] && has_ammo(k) && !listed) {
            set32(menu + VENDOR_ENTRIES + (uint32_t)n * VENDOR_ENTRY_SIZE, k);
            set32(menu + VENDOR_ENTRIES + (uint32_t)n * VENDOR_ENTRY_SIZE + 4, 1);
            ++n;
        }
    }

    set32(menu + VENDOR_COUNT, n);
    ++s_list_builds;
}

/* The buy dialog's purchase of the entry at cursor, as VendorBuyDialogUpdate makes it: GiveItem,
 * the entry's item taken as the one bought, the list built again; on a weapon for sale the cursor
 * kept on the list and the item redrawn. */
static int s_redraws;

static void game_buy(
    openrac_rac1_Item_Give give, openrac_rac1_Vendor_BuildItemList build, int cursor
) {
    uint8_t* menu = s_memory + D->vendor_menu;
    uint8_t* entry = menu + VENDOR_ENTRIES + (uint32_t)cursor * VENDOR_ENTRY_SIZE;
    set32(menu + VENDOR_CURSOR, cursor);
    set32(menu + VENDOR_DIALOG, VENDOR_DIALOG_BUY);
    give(get32(entry), 1);
    set32(menu + VENDOR_LAST_BOUGHT, get32(entry));
    build(0);
    const int32_t idx = get32(menu + VENDOR_CURSOR);

    if (get32(menu + VENDOR_ENTRIES + (uint32_t)idx * VENDOR_ENTRY_SIZE + 4) != 1) {
        if (get32(menu + VENDOR_COUNT) - 1 < idx) {
            set32(menu + VENDOR_CURSOR, get32(menu + VENDOR_COUNT) - 1);
        }

        ++s_redraws;
    }

    set32(menu + VENDOR_DIALOG, 0);
}

/* VendorStockLevelInit's start, as the game's: the Bomb Glove given when it is not owned, put first
 * in the stock and made the double tap's weapon; then the level's weapon added (the Blaster). */
static openrac_rac1_Item_Give s_give_hook;

static void game_init(void) {
    uint8_t* stock = s_memory + D->vendor_stock;

    if (s_memory[D->owned + ITEM_BOMB_GLOVE] == 0) {
        s_give_hook(ITEM_BOMB_GLOVE, 1);
        stock[0] = ITEM_BOMB_GLOVE | 0x40;
        set32(s_memory + D->last_weapon, ITEM_BOMB_GLOVE);
    }

    int j = 0;

    while ((stock[j] & 0x3F) != ITEM_BLASTER && stock[j] != 0xFF) {
        ++j;
    }

    if (stock[j] == 0xFF) {
        stock[j] = ITEM_BLASTER;
    }
}

static int32_t game_count(void) {
    ++s_counted;
    return 123;
}

/* Helga: what she sees of the Swingshot; then she gives it when she is not done. */
static void game_helga(openrac_gaddr moby) {
    (void)moby;
    s_helga_saw = s_memory[D->owned + ITEM_SWINGSHOT];
}

/* An infobot: unlocks its planet when its planet's flag is clear, and marks its trigger slot. */
static openrac_rac1_Planet_Unlock s_unlock_hook;

static void game_infobot(openrac_gaddr moby) {
    const openrac_gaddr pvars = *(openrac_gaddr*)(s_memory + moby + MOBY_PVARS);
    int32_t planet;
    memcpy(&planet, s_memory + pvars + INFOBOT_PLANET, 4);

    if (s_memory[D->planets + (uint32_t)planet] == 0) {
        s_unlock_hook(planet);
        s_memory[D->triggers + s_memory[moby + MOBY_MISSION] + 16u * s_memory[D->level]] = 0xFF;
    }
}

static void game_moby(openrac_gaddr moby) {
    (void)moby;
}

/* The game's text of a message: "m" and its id, at a place of the test's game memory. */
static uint32_t game_msg(int32_t id) {
    const uint32_t at = 0x00190000u;
    sprintf((char*)s_memory + at, "m%X", (unsigned)id);
    return at;
}

static void game_ship(int32_t mode) {
    (void)mode;
    s_ship_saw = s_memory[D->owned + ITEM_O2_MASK];
}

static void game_class22(openrac_gaddr moby) {
    (void)moby;
    s_class22_saw = s_memory[D->owned + ITEM_O2_MASK];
}

static int32_t game_help(int32_t message, int32_t slot) {
    (void)message, (void)slot;
    ++s_helps;
    return 1;
}

static int32_t game_drop(openrac_gaddr moby, openrac_gaddr out_item) {
    (void)moby, (void)out_item;
    s_drop_saw = s_memory[D->vendor_stock];
    return 1;
}

static void game_helmet(openrac_gaddr moby) {
    (void)moby;
    memcpy(s_memory + D->hero_pending, &(int32_t){ITEM_PILOTS_HELMET}, 4);
}

/* ---- the host ---- */

static void host_log(openrac_mod* mod, int level, const char* text) {
    /* A gold bolt given has no byte of its own: the log says it. */
    s_banners += strstr(text, " gave a gold bolt") != NULL;
    (void)mod;
    printf("[%d] item-randomizer: %s\n", level, text);
}

static void host_fail(openrac_mod* mod, const char* reason) {
    (void)mod;
    snprintf(s_failed, sizeof s_failed, "%s", reason);
}

static int setting_bool(openrac_mod* mod, const char* key) {
    (void)mod;

    if (strcmp(key, "vendor_hints") == 0) {
        return 1;
    }

    return (strcmp(key, "keep_starting_gear") == 0 && s_keep_start)
           || strncmp(key, "shuffle_", 8) == 0 || strcmp(key, "spoiler_log") == 0;
}

static int64_t setting_int(openrac_mod* mod, const char* key) {
    (void)mod, (void)key;
    return 0;
}

static double setting_float(openrac_mod* mod, const char* key) {
    (void)mod, (void)key;
    return 0.0;
}

static const char* setting_string(openrac_mod* mod, const char* key) {
    (void)mod;

    if (strcmp(key, "seed") == 0) {
        return s_seed;
    }

    return strcmp(key, "difficulty") == 0 ? "standard" : "";
}

static void* original_for(const char* name) {
    if (strcmp(name, OPENRAC_RAC1_HOOK_Item_Give) == 0) {
        return (void*)game_give;
    }

    if (strcmp(name, OPENRAC_RAC1_HOOK_Planet_Unlock) == 0) {
        return (void*)game_unlock;
    }

    if (strcmp(name, OPENRAC_RAC1_HOOK_Vendor_InitLevelStock) == 0) {
        return (void*)game_init;
    }

    if (strcmp(name, OPENRAC_RAC1_HOOK_Vendor_BuildItemList) == 0) {
        return (void*)game_build;
    }

    if (strcmp(name, OPENRAC_RAC1_HOOK_GoldBolts_CountCollected) == 0) {
        return (void*)game_count;
    }

    if (strcmp(name, OPENRAC_RAC1_HOOK_Moby_Helga_Update) == 0) {
        return (void*)game_helga;
    }

    if (strcmp(name, OPENRAC_RAC1_HOOK_Moby_Infobot_Update) == 0) {
        return (void*)game_infobot;
    }

    if (strcmp(name, OPENRAC_RAC1_HOOK_Moby_PilotsHelmet_Update) == 0) {
        return (void*)game_helmet;
    }

    if (strcmp(name, OPENRAC_RAC1_HOOK_Moby_Class22_Update) == 0) {
        return (void*)game_class22;
    }

    if (strcmp(name, OPENRAC_RAC1_HOOK_Ship_InitTransition) == 0) {
        return (void*)game_ship;
    }

    if (strcmp(name, OPENRAC_RAC1_HOOK_Help_Request) == 0) {
        return (void*)game_help;
    }

    if (strcmp(name, OPENRAC_RAC1_HOOK_Ammo_ChooseDrop) == 0) {
        return (void*)game_drop;
    }

    return (void*)game_moby;
}

static int host_hook(
    openrac_mod* mod, const char* name, const char* signature, void* replacement, void** original
) {
    (void)mod;
    CHECK(s_hook_count < 32);
    s_hooks[s_hook_count].name = name;
    s_hooks[s_hook_count].signature = signature;
    s_hooks[s_hook_count].replacement = replacement;
    ++s_hook_count;
    *original = original_for(name);
    return OPENRAC_HOOK_OK;
}

static int host_on_frame(openrac_mod* mod, openrac_frame_fn fn, void* user) {
    (void)mod, (void)user;
    s_frame = fn;
    return 0;
}

static int host_on_pad(openrac_mod* mod, openrac_pad_fn fn, void* user) {
    (void)mod, (void)fn, (void)user;
    return 0;
}

static void* host_function(const char* name) {
    return strcmp(name, D->msg_string) == 0 ? (void*)game_msg : NULL;
}

static void* host_function_at(openrac_gaddr a) {
    (void)a;
    return NULL;
}

static openrac_gaddr identity(openrac_gaddr a) {
    return a;
}

static openrac_gaddr identity_level(int level, openrac_gaddr a) {
    (void)level;
    return a;
}

static int loaded_level(void) {
    return 3;
}

static openrac_gaddr stack_push(uint32_t size, openrac_gaddr* saved) {
    (void)size;
    *saved = 0;
    return 0x1F0000u;
}

static void stack_pop(openrac_gaddr saved) {
    (void)saved;
}

/* The port's side of the party's rules: what the mod published, its listener, the replay switch. */
static char s_published[64];
static char s_published_text[256];
static openrac_party_rules_fn s_rules_fn;
static int s_replaying;

static int host_rules_publish(openrac_mod* mod, const void* data, uint32_t size, const char* text) {
    (void)mod;
    CHECK(size < sizeof s_published);
    memcpy(s_published, data, size);
    s_published[size] = 0;
    snprintf(s_published_text, sizeof s_published_text, "%s", text);
    return OPENRAC_HOOK_OK;
}

static int host_on_rules(openrac_mod* mod, openrac_party_rules_fn fn, void* user) {
    (void)mod;
    (void)user;
    s_rules_fn = fn;
    return 0;
}

static int host_replaying(void) {
    return s_replaying;
}

static int host_play_status(openrac_play_status* out) {
    out->phase = s_phase;
    return OPENRAC_HOOK_OK;
}

static int host_play_notice(openrac_mod* mod, const char* text) {
    (void)mod;
    snprintf(s_notice, sizeof s_notice, "%s", text != NULL ? text : "");
    return OPENRAC_HOOK_OK;
}

static openrac_api make_api(const char* game, const char* version) {
    openrac_api api;
    memset(&api, 0, sizeof api);
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = game;
    api.version = version;
    api.openrac_version = "test";
    api.platform = "test";
    api.memory = s_memory;
    api.log = host_log;
    api.fail = host_fail;
    api.setting_bool = setting_bool;
    api.setting_int = setting_int;
    api.setting_float = setting_float;
    api.setting_string = setting_string;
    api.hook = host_hook;
    api.on_frame = host_on_frame;
    api.on_pad = host_on_pad;
    api.function = host_function;
    api.function_at = host_function_at;
    api.relocate_data = identity;
    api.relocate_level_data = identity_level;
    api.loaded_level = loaded_level;
    api.stack_push = stack_push;
    api.stack_pop = stack_pop;
    api.party_rules_publish = host_rules_publish;
    api.on_party_rules = host_on_rules;
    api.party_replaying = host_replaying;
    api.play_status = host_play_status;
    api.play_notice = host_play_notice;
    return api;
}

static void* hook_named(const char* name) {
    for (int i = 0; i < s_hook_count; ++i) {
        if (strcmp(s_hooks[i].name, name) == 0) {
            return s_hooks[i].replacement;
        }
    }

    return NULL;
}

static int location_of(int kind, int level, int key) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];

        if (l->kind == kind && l->key == key && (kind <= LOC_START || l->level == level)) {
            return (int)i;
        }
    }

    return -1;
}

/* A location's had state in the test's memory, as the mod reads it (rando.h). */
static int had_here(const rando_location* l) {
    switch (l->kind) {
        case LOC_INFOBOT:
            return s_memory[D->triggers + l->key + 16u * l->level] == 0xFF;
        case LOC_GOLD:
            return s_memory[D->gold_bolts + 4u * l->level + l->key] != 0;
        case LOC_FIXED:
            return 0;
        default:
            return s_memory[D->obtained + l->key] != 0;
    }
}

/* The kinds of reward the vendor checks met: a vendor weapon, another item, a planet, a gold
 * bolt; an item without ammo. */
static int s_vendor_kinds[5];

/* Each of plan's vendor slots bought as the buy dialog buys it, from a list of the Bomb Glove's
 * ammo, the slot and another slot for sale; returns how many. */
static int check_vendor(
    const rando_plan* plan, openrac_rac1_Item_Give give, openrac_rac1_Vendor_BuildItemList build
) {
    uint8_t* stock = s_memory + D->vendor_stock;
    uint8_t* menu = s_memory + D->vendor_menu;
    int vendor_slots = 0;

    for (size_t v = 0; v < rando_location_count; ++v) {
        const rando_location* vl = &rando_locations[v];

        if (vl->kind != LOC_VENDOR || !plan->shuffled[v]) {
            continue;
        }

        ++vendor_slots;
        const int x = vl->key;
        const int z = x == ITEM_BLASTER ? ITEM_PYROCITOR : ITEM_BLASTER; /* another slot */
        memset(s_memory + D->owned, 0, RANDO_ITEM_COUNT);
        memset(s_memory + D->obtained, 0, RANDO_ITEM_COUNT);
        memset(s_memory + D->planets, 0, RANDO_PLANET_COUNT);
        memset(s_memory + D->gold_bolts, 0, 4 * LEVEL_COUNT);
        s_memory[D->owned + ITEM_BOMB_GLOVE] = s_memory[D->obtained + ITEM_BOMB_GLOVE] = 1;
        memset(stock, 0xFF, VENDOR_STOCK_SIZE);
        stock[0] = ITEM_BOMB_GLOVE | 0x40;
        stock[1] = (uint8_t)x;
        stock[2] = (uint8_t)z;
        uint8_t stock_before[VENDOR_STOCK_SIZE];
        memcpy(stock_before, stock, sizeof stock_before);
        memset(menu, 0, VENDOR_COUNT + 4);
        set32(menu + VENDOR_LAST_BOUGHT, -1);
        set32(s_memory + D->vendor_redraw, 0);
        s_frame(NULL);
        build(0); /* the menu opens */
        CHECK(get32(menu + VENDOR_COUNT) == 3);
        CHECK(
            get32(menu + VENDOR_ENTRIES + VENDOR_ENTRY_SIZE) == x
            && get32(menu + VENDOR_ENTRIES + VENDOR_ENTRY_SIZE + 4) == 0
        );

        const rando_reward vr = plan->reward[v];
        s_vendor_kinds
            [vr.kind == REWARD_ITEM     ? (vendor_weapon(vr.value) ? 0 : 1)
             : vr.kind == REWARD_PLANET ? 2
                                        : 3] = 1;
        s_vendor_kinds[4] |= vr.kind == REWARD_ITEM && !has_ammo(vr.value);
        s_gives = s_unlocks = s_banners = s_redraws = 0;
        game_buy(give, build, 1);
        s_frame(NULL);
        CHECK(s_memory[D->obtained + x] == 1); /* the slot is had */
        const int32_t handed = get32(menu + VENDOR_LAST_BOUGHT);

        if (vr.kind == REWARD_ITEM) {
            /* Owned: it stays when the hero changes weapons; and, when the vendor has its demo, it
             * is what the hero is handed (else nothing is). */
            CHECK(s_gives == 1 && s_memory[D->owned + vr.value] == 1);
            CHECK(handed == (vendor_weapon(vr.value) ? vr.value : -1));
            CHECK(vr.value == x || s_memory[D->owned + x] == 0);
        } else {
            CHECK(handed == -1); /* nothing to hand: no demo */
            CHECK(vr.kind != REWARD_PLANET || s_memory[D->planets + vr.value] == 1);
            CHECK(vr.kind != REWARD_GOLD_BOLT || s_banners == 1);
        }
        /* The stock is the slots' (a weapon reward does not mark itself bought in it). */
        CHECK(
            vr.kind != REWARD_ITEM || !vendor_weapon(vr.value)
            || memcmp(stock, stock_before, VENDOR_STOCK_SIZE) == 0
        );
        /* The list: weapons for sale are slots not bought, ammo only of items owned, nothing twice. */
        const int32_t n = get32(menu + VENDOR_COUNT);
        const int32_t cursor = get32(menu + VENDOR_CURSOR);
        CHECK(n >= 1 && cursor >= 0 && cursor < n);

        for (int32_t i = 0; i < n; ++i) {
            const int32_t item = get32(menu + VENDOR_ENTRIES + (uint32_t)i * VENDOR_ENTRY_SIZE);
            const int32_t ammo = get32(menu + VENDOR_ENTRIES + (uint32_t)i * VENDOR_ENTRY_SIZE + 4);
            CHECK(item >= 0 && item < RANDO_ITEM_COUNT);

            if (ammo) {
                CHECK(s_memory[D->owned + item] == 1);
            } else {
                CHECK(item == z && s_memory[D->obtained + z] == 0);
            }

            for (int32_t j = 0; j < i; ++j) {
                CHECK(
                    get32(menu + VENDOR_ENTRIES + (uint32_t)j * VENDOR_ENTRY_SIZE) != item
                    || get32(menu + VENDOR_ENTRIES + (uint32_t)j * VENDOR_ENTRY_SIZE + 4) != ammo
                );
            }
        }
        /* Every weapon owned that has ammo is in it as ammo. */

        for (int k = 0; k < RANDO_ITEM_COUNT; ++k) {
            int listed = 0;

            for (int32_t i = 0; i < n; ++i) {
                listed |=
                    get32(menu + VENDOR_ENTRIES + (uint32_t)i * VENDOR_ENTRY_SIZE) == k
                    && get32(menu + VENDOR_ENTRIES + (uint32_t)i * VENDOR_ENTRY_SIZE + 4) == 1;
            }

            CHECK(!s_memory[D->owned + k] || !has_ammo(k) || listed);
        }
        /* The bought slot's place: its reward's ammo, when the reward is a weapon with ammo. */
        const int32_t under = get32(menu + VENDOR_ENTRIES + (uint32_t)cursor * VENDOR_ENTRY_SIZE);

        if (vr.kind == REWARD_ITEM && has_ammo(vr.value)) {
            CHECK(
                cursor == 1 && under == vr.value
                && get32(menu + VENDOR_ENTRIES + VENDOR_ENTRY_SIZE + 4) == 1
            );
        }
        /* The menu shows the item now under the cursor. */
        CHECK(under == x || get32(s_memory + D->vendor_redraw) == 1 || s_redraws == 1);
        CHECK(under != x || get32(s_memory + D->vendor_redraw) == 0);

        /* Bought again from the list (now its ammo, if anything): no second reward. */
        s_gives = s_unlocks = s_banners = 0;
        build(0);
        CHECK(s_gives + s_unlocks + s_banners == 0);
    }

    memset(s_memory + D->owned, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->obtained, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->gold_bolts, 0, 4 * LEVEL_COUNT);
    memset(stock, 0xFF, VENDOR_STOCK_SIZE);
    s_frame(NULL);
    return vendor_slots;
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    void* lib = open_library(argv[1]);
    CHECK(lib != NULL);
    openrac_mod_query_fn query = (openrac_mod_query_fn)symbol(lib, OPENRAC_MOD_QUERY);
    openrac_mod_load_fn load = (openrac_mod_load_fn)symbol(lib, OPENRAC_MOD_LOAD);
    CHECK(query != NULL && load != NULL);
    openrac_mod_info info;
    memset(&info, 0, sizeof info);
    info.size = sizeof info;
    query(&info);
    /* The interface its manifest names (1.11), whatever the SDK it was built with. */
    CHECK(info.api_major == OPENRAC_MOD_API_MAJOR && info.api_minor == 11);
    CHECK(strcmp(info.id, "item-randomizer") == 0 && strcmp(info.version, "2.0.0") == 0);

    s_memory = calloc(1, MEMORY_SIZE);
    CHECK(s_memory != NULL);
    openrac_mod* mod = (openrac_mod*)&info;

    /* Its data folder: a folder of the test's own. */
    char dir[512];
    snprintf(dir, sizeof dir, "%s", "item-randomizer-load-test-data");
    set_env("OPENRAC_MOD_DATA_DIR", dir);

    /* Another game, an unknown disc: refused before any hook. */
    D = &k_rando_discs[0];
    openrac_api api = make_api("rac2", "ntsc");
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "rac1") != NULL && s_hook_count == 0);
    api = make_api("rac1", "jp");
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "jp") != NULL && s_hook_count == 0);

    /* NTSC-U, then PAL: the hook points, with rac1.h's signatures. */
    D = &k_rando_discs[1];
    api = make_api("rac1", "ntsc");
    s_failed[0] = 0;
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0 && s_hook_count == 18 && s_frame != NULL);
    s_hook_count = 0;
    D = &k_rando_discs[0];
    api = make_api("rac1", "pal");
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0 && s_hook_count == 18);
    static const char* const points[][2] = {
        {OPENRAC_RAC1_HOOK_Item_Give, OPENRAC_RAC1_SIG_Item_Give},
        {OPENRAC_RAC1_HOOK_Planet_Unlock, OPENRAC_RAC1_SIG_Planet_Unlock},
        {OPENRAC_RAC1_HOOK_Vendor_InitLevelStock, OPENRAC_RAC1_SIG_Vendor_InitLevelStock},
        {OPENRAC_RAC1_HOOK_Vendor_BuildItemList, OPENRAC_RAC1_SIG_Vendor_BuildItemList},
        {OPENRAC_RAC1_HOOK_GoldBolts_CountCollected, OPENRAC_RAC1_SIG_GoldBolts_CountCollected},
        {OPENRAC_RAC1_HOOK_Moby_Infobot_Update, OPENRAC_RAC1_SIG_Moby_Infobot_Update},
        {OPENRAC_RAC1_HOOK_Moby_PilotsHelmet_Update, OPENRAC_RAC1_SIG_Moby_PilotsHelmet_Update},
        {OPENRAC_RAC1_HOOK_Moby_Helga_Update, OPENRAC_RAC1_SIG_Moby_Helga_Update},
        {OPENRAC_RAC1_HOOK_Moby_BigAl_Update, OPENRAC_RAC1_SIG_Moby_BigAl_Update},
        {OPENRAC_RAC1_HOOK_Moby_Bob_Update, OPENRAC_RAC1_SIG_Moby_Bob_Update},
        {OPENRAC_RAC1_HOOK_Moby_Edwina_Update, OPENRAC_RAC1_SIG_Moby_Edwina_Update},
        {OPENRAC_RAC1_HOOK_Moby_RobotQwark_Update, OPENRAC_RAC1_SIG_Moby_RobotQwark_Update},
        {OPENRAC_RAC1_HOOK_Moby_Class23_Update, OPENRAC_RAC1_SIG_Moby_Class23_Update},
        {OPENRAC_RAC1_HOOK_Moby_Class22_Update, OPENRAC_RAC1_SIG_Moby_Class22_Update},
        {OPENRAC_RAC1_HOOK_Ammo_ChooseDrop, OPENRAC_RAC1_SIG_Ammo_ChooseDrop},
        {OPENRAC_RAC1_HOOK_Vendor_MenuUpdate, OPENRAC_RAC1_SIG_Vendor_MenuUpdate},
        {OPENRAC_RAC1_HOOK_Help_Request, OPENRAC_RAC1_SIG_Help_Request},
        {OPENRAC_RAC1_HOOK_Ship_InitTransition, OPENRAC_RAC1_SIG_Ship_InitTransition},
    };

    for (int i = 0; i < 18; ++i) {
        CHECK(
            strcmp(s_hooks[i].name, points[i][0]) == 0
            && strcmp(s_hooks[i].signature, points[i][1]) == 0
        );
    }

    /* The plan it made: the same settings give the same placement here. */
    rando_options o = {s_seed, 1, 1, 1, 1, 1, 1, "standard"};
    rando_settings settings;
    CHECK(rando_settings_of(&o, NULL, 0, &settings) == SEED_FROM_WORD);
    static rando_plan plan;
    CHECK(rando_solve(&settings, &plan) == 0);
    char code[RANDO_CODE_SIZE], path[700];
    rando_encode(&settings, code);
    snprintf(path, sizeof path, "%s/spoiler-%s.txt", dir, code);
    FILE* f = fopen(path, "rb");
    CHECK(f != NULL); /* the spoiler log, in the data folder */
    fclose(f);
    remove(path);

    openrac_rac1_Item_Give give = (openrac_rac1_Item_Give)hook_named("Item_Give");
    openrac_rac1_Moby_Helga_Update helga =
        (openrac_rac1_Moby_Helga_Update)hook_named("Moby_Helga_Update");
    openrac_rac1_Moby_Infobot_Update infobot =
        (openrac_rac1_Moby_Infobot_Update)hook_named("Moby_Infobot_Update");
    openrac_rac1_Vendor_BuildItemList build =
        (openrac_rac1_Vendor_BuildItemList)hook_named("Vendor_BuildItemList");
    openrac_rac1_GoldBolts_CountCollected count =
        (openrac_rac1_GoldBolts_CountCollected)hook_named("GoldBolts_CountCollected");
    openrac_rac1_Vendor_InitLevelStock init =
        (openrac_rac1_Vendor_InitLevelStock)hook_named("Vendor_InitLevelStock");
    s_unlock_hook = (openrac_rac1_Planet_Unlock)hook_named("Planet_Unlock");
    CHECK(give && helga && infobot && build && count && init && s_unlock_hook);
    s_give_hook = give;

    /* On Kerwan, in play. */
    const int32_t kerwan = LEVEL_KERWAN;
    memcpy(s_memory + D->level, &kerwan, 4);
    memset(s_memory + D->vendor_stock, 0xFF, 12);
    init();

    /* Helga's place: she sees her place unhad though the Swingshot is owned from elsewhere. */
    const int helga_place = location_of(LOC_GIVE, 0, ITEM_SWINGSHOT);
    CHECK(helga_place >= 0 && plan.shuffled[helga_place]);
    s_memory[D->owned + ITEM_SWINGSHOT] = 1;
    helga(0x00100000u);
    CHECK(s_helga_saw == 0 && s_memory[D->owned + ITEM_SWINGSHOT] == 1);
    s_memory[D->owned + ITEM_SWINGSHOT] = 0;

    /* GiveItem(Swingshot): her place's reward. */
    const rando_reward r = plan.reward[helga_place];
    s_gives = s_unlocks = s_banners = 0;
    give(ITEM_SWINGSHOT, 1);
    CHECK(s_memory[D->obtained + ITEM_SWINGSHOT] == 1); /* her place is had */

    if (r.kind == REWARD_ITEM) {
        CHECK(s_gives == 1 && s_memory[D->owned + r.value] == 1);

        if (r.value != ITEM_SWINGSHOT) {
            CHECK(s_memory[D->obtained + r.value] == 0); /* the reward's own place is not */
            CHECK(s_memory[D->owned + ITEM_SWINGSHOT] == 0);
        }
    } else if (r.kind == REWARD_PLANET) {
        CHECK(s_unlocks == 1 && s_memory[D->planets + r.value] == 1);
    } else {
        CHECK(r.kind == REWARD_GOLD_BOLT && s_banners == 1);
    }

    printf("Helga gave %d/%d\n", r.kind, r.value);
    /* Now had: she sees it so. */
    helga(0x00100000u);
    CHECK(s_helga_saw == 1);

    /* The Persuader's place is left as the game has it: GiveItem gives the item itself. */
    s_gives = 0;
    give(ITEM_PERSUADER, 0);
    CHECK(s_gives == 1 && s_memory[D->owned + ITEM_PERSUADER] == 1);

    /* Kerwan's infobot: its update unlocks through the hook, which gives the infobot's reward. */
    const int bot = location_of(LOC_INFOBOT, LEVEL_KERWAN, 3);
    CHECK(bot >= 0 && plan.shuffled[bot]);
    const openrac_gaddr moby = 0x00120000u, pvars = 0x00121000u;
    memcpy(s_memory + moby + MOBY_PVARS, &pvars, 4);
    s_memory[moby + MOBY_MISSION] = 3;
    const int32_t eudora = LEVEL_EUDORA;
    memcpy(s_memory + pvars + INFOBOT_PLANET, &eudora, 4);
    /* Eudora unlocked already (from elsewhere): the infobot still has its place to give. */
    s_memory[D->planets + LEVEL_EUDORA] = 1;
    s_gives = s_unlocks = s_banners = 0;
    const rando_reward br = plan.reward[bot];
    infobot(moby);
    CHECK(s_memory[D->triggers + 3 + 16 * LEVEL_KERWAN] == 0xFF);
    CHECK(s_memory[D->planets + LEVEL_EUDORA] == 1);
    CHECK(br.kind != REWARD_ITEM || (s_gives == 1 && s_memory[D->owned + br.value] == 1));
    CHECK(br.kind != REWARD_PLANET || s_memory[D->planets + br.value] == 1);
    printf("the infobot gave %d/%d\n", br.kind, br.value);

    /* The vendor: every slot bought, with this seed and others. */
    uint8_t* stock = s_memory + D->vendor_stock;
    CHECK(check_vendor(&plan, give, build) > 0);
    static const char* const vendor_seeds[] =
        {"vendor-1",
         "vendor-2",
         "vendor-3",
         "vendor-4",
         "vendor-5",
         "vendor-6",
         "vendor-7",
         "vendor-8",
         "vendor-9",
         "vendor-10",
         "vendor-11",
         "vendor-12"};

    for (size_t k = 0; k < sizeof vendor_seeds / sizeof vendor_seeds[0]; ++k) {
        s_seed = vendor_seeds[k];
        s_hook_count = 0;
        CHECK(load(&api, mod) == 0);
        rando_options vo = o;
        vo.seed = s_seed;
        rando_settings vs;
        static rando_plan vplan;
        CHECK(
            rando_settings_of(&vo, NULL, 0, &vs) == SEED_FROM_WORD && rando_solve(&vs, &vplan) == 0
        );
        rando_encode(&vs, code);
        snprintf(path, sizeof path, "%s/spoiler-%s.txt", dir, code);
        remove(path);
        give = (openrac_rac1_Item_Give)hook_named("Item_Give");
        build = (openrac_rac1_Vendor_BuildItemList)hook_named("Vendor_BuildItemList");
        s_give_hook = give;
        CHECK(check_vendor(&vplan, give, build) > 0);
    }
    /* Each kind of reward was bought from a slot. */
    CHECK(
        s_vendor_kinds[0] && s_vendor_kinds[1] && s_vendor_kinds[2] && s_vendor_kinds[3]
        && s_vendor_kinds[4]
    );
    printf(
        "the vendor's slots bought, %zu seeds\n", 1 + sizeof vendor_seeds / sizeof vendor_seeds[0]
    );
    s_seed = "load-test";
    s_hook_count = 0;
    CHECK(load(&api, mod) == 0);
    rando_encode(&settings, code);
    snprintf(path, sizeof path, "%s/spoiler-%s.txt", dir, code);
    remove(path);
    give = (openrac_rac1_Item_Give)hook_named("Item_Give");
    count = (openrac_rac1_GoldBolts_CountCollected)hook_named("GoldBolts_CountCollected");
    init = (openrac_rac1_Vendor_InitLevelStock)hook_named("Vendor_InitLevelStock");
    s_give_hook = give;

    /* Gold bolts: the count is the gold bolt rewards had, and a gold bolt picked up gives its
     * place's reward at the next tick. */
    s_counted = 0;
    int32_t want = 0;

    for (size_t i = 0; i < rando_location_count; ++i) {
        want += plan.shuffled[i] && plan.reward[i].kind == REWARD_GOLD_BOLT
                && had_here(&rando_locations[i]);
    }

    CHECK(count() == want && s_counted == 0);
    const int bolt = location_of(LOC_GOLD, LEVEL_KERWAN, 1);
    CHECK(bolt >= 0);
    s_frame(NULL); /* nothing new */
    s_gives = s_unlocks = s_banners = 0;
    s_memory[D->gold_bolts + LEVEL_KERWAN * 4 + 1] = 1;
    const rando_reward gr = plan.reward[bolt];
    const int was_held = gr.kind == REWARD_ITEM     ? s_memory[D->owned + gr.value]
                         : gr.kind == REWARD_PLANET ? s_memory[D->planets + gr.value]
                                                    : 0;
    s_frame(NULL);
    printf("the gold bolt gave %d/%d\n", gr.kind, gr.value);

    if (!was_held) {
        CHECK(s_gives + s_unlocks + s_banners == 1);
    }

    s_frame(NULL); /* once */
    CHECK(s_gives + s_unlocks + s_banners <= 1);
    CHECK(count() == want + (gr.kind == REWARD_GOLD_BOLT));

    /* The starting gear shuffled: a new game's first level start gives the start's reward; the
     * Bomb Glove is in the stock and the double tap's weapon only when it was the reward. */
    s_keep_start = 0;
    s_hook_count = 0;
    CHECK(load(&api, mod) == 0);
    s_give_hook = (openrac_rac1_Item_Give)hook_named("Item_Give");
    init = (openrac_rac1_Vendor_InitLevelStock)hook_named("Vendor_InitLevelStock");
    CHECK(s_give_hook != NULL && init != NULL);
    o.keep_start = 0;
    CHECK(rando_settings_of(&o, NULL, 0, &settings) == SEED_FROM_WORD);
    CHECK(rando_solve(&settings, &plan) == 0);
    rando_encode(&settings, code);
    snprintf(path, sizeof path, "%s/spoiler-%s.txt", dir, code);
    CHECK(remove(path) == 0);
    const int start = location_of(LOC_START, 0, ITEM_BOMB_GLOVE);
    CHECK(start >= 0 && plan.shuffled[start]);
    const rando_reward sr = plan.reward[start];
    memset(s_memory + D->owned, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->obtained, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->planets, 0, RANDO_PLANET_COUNT);
    memset(stock, 0xFF, VENDOR_STOCK_SIZE);
    memset(s_memory + D->gold_bolts, 0, 80);
    memset(s_memory + D->triggers, 0, 320);
    set32(s_memory + D->last_weapon, 0);
    s_gives = s_unlocks = s_banners = 0;
    init();
    CHECK(s_memory[D->obtained + ITEM_BOMB_GLOVE] == 1); /* the start is had */
    CHECK(s_gives + s_unlocks + s_banners == 1);

    if (sr.kind == REWARD_ITEM && sr.value == ITEM_BOMB_GLOVE) {
        CHECK(
            stock[0] == (ITEM_BOMB_GLOVE | 0x40)
            && get32(s_memory + D->last_weapon) == ITEM_BOMB_GLOVE
        );
    } else {
        CHECK(s_memory[D->owned + ITEM_BOMB_GLOVE] == 0);
        CHECK(stock[0] == ITEM_BLASTER && stock[1] == 0xFF); /* the level's weapon, first */
        CHECK(get32(s_memory + D->last_weapon) == 0);
        CHECK(sr.kind != REWARD_ITEM || s_memory[D->owned + sr.value] == 1);
    }

    printf("the start gave %d/%d\n", sr.kind, sr.value);
    s_gives = s_unlocks = s_banners = 0;
    init(); /* the next level start: given once */
    CHECK(s_gives + s_unlocks + s_banners == 0);
    s_keep_start = 1;

    /* A party. The host's game publishes its seed's code, and words for a player. */
    s_seed = "party-host";
    s_hook_count = 0;
    CHECK(load(&api, mod) == 0);
    char host_code[RANDO_CODE_SIZE];
    snprintf(host_code, sizeof host_code, "%s", s_published);
    rando_settings hs;
    CHECK(rando_decode(host_code, &hs));
    CHECK(strstr(s_published_text, "Logic: standard") != NULL && strstr(s_published_text, "Shuffles gadgets") != NULL);
    static rando_plan host_plan, own_plan;
    CHECK(rando_solve(&hs, &host_plan) == 0);
    snprintf(path, sizeof path, "%s/spoiler-%s.txt", dir, host_code);
    remove(path);

    /* A member with a seed of its own takes the host's: a place gives what the host's plan puts
     * there, not what its own does. */
    s_seed = "party-member";
    s_hook_count = 0;
    CHECK(load(&api, mod) == 0);
    rando_settings ms;
    CHECK(rando_decode(s_published, &ms) && ms.seed != hs.seed);
    CHECK(rando_solve(&ms, &own_plan) == 0);
    snprintf(path, sizeof path, "%s/spoiler-%s.txt", dir, s_published);
    remove(path);
    give = (openrac_rac1_Item_Give)hook_named("Item_Give");
    s_rules_fn(NULL, host_code, (uint32_t)strlen(host_code));
    int spot = -1;

    /* A place the two plans fill differently, with an item, an item nobody has yet. */
    for (size_t i = 0; i < rando_location_count && spot < 0; ++i) {
        const rando_location* l = &rando_locations[i];

        if (l->kind == LOC_GIVE && host_plan.shuffled[i] && own_plan.shuffled[i]
            && host_plan.reward[i].kind == REWARD_ITEM && own_plan.reward[i].kind == REWARD_ITEM
            && host_plan.reward[i].value != own_plan.reward[i].value) {
            spot = (int)i;
        }
    }

    CHECK(spot >= 0);
    const int spot_item = rando_locations[spot].key;
    const int host_gives = host_plan.reward[spot].value;
    const int own_gives = own_plan.reward[spot].value;
    memset(s_memory + D->owned, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->obtained, 0, RANDO_ITEM_COUNT);
    give(spot_item, 0);
    CHECK(s_memory[D->owned + host_gives] == 1 && s_memory[D->owned + own_gives] == 0);

    /* Passed on from another player, an item is itself: no place had, no other reward. Mutation:
     * the same give with the switch off is that item's place's reward instead. */
    memset(s_memory + D->owned, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->obtained, 0, RANDO_ITEM_COUNT);
    s_replaying = 1;
    give(spot_item, 0);
    s_replaying = 0;
    CHECK(s_memory[D->owned + spot_item] == 1);

    if (host_gives != spot_item) {
        CHECK(s_memory[D->owned + host_gives] == 0);
    }

    /* The party goes: its own seed's again. */
    s_rules_fn(NULL, NULL, 0);
    memset(s_memory + D->owned, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->obtained, 0, RANDO_ITEM_COUNT);
    give(spot_item, 0);
    CHECK(s_memory[D->owned + own_gives] == 1 && s_memory[D->owned + host_gives] == 0);
    memset(s_memory + D->owned, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->obtained, 0, RANDO_ITEM_COUNT);

    /* The rest on this placement: the reconcile, the party, the words on the screen. */
    s_seed = "second-half";
    s_hook_count = 0;
    CHECK(load(&api, mod) == 0);
    rando_options so = {s_seed, 1, 1, 1, 1, 1, 1, "standard"};
    rando_settings ss;
    CHECK(rando_settings_of(&so, NULL, 0, &ss) == SEED_FROM_WORD);
    static rando_plan sp;
    CHECK(rando_solve(&ss, &sp) == 0);
    give = (openrac_rac1_Item_Give)hook_named("Item_Give");
    init = (openrac_rac1_Vendor_InitLevelStock)hook_named("Vendor_InitLevelStock");
    s_give_hook = give;
    s_unlock_hook = (openrac_rac1_Planet_Unlock)hook_named("Planet_Unlock");
    const int32_t novalis = LEVEL_NOVALIS;
    memcpy(s_memory + D->level, &novalis, 4);
    memset(s_memory + D->vendor_stock, 0xFF, 12);
    init();

    /* The banner: nothing of GiveItem's, the reward's name in the game's highlight, in control. */
    int item_spot = -1;

    for (size_t i = 0; i < rando_location_count && item_spot < 0; ++i) {
        if (sp.shuffled[i] && rando_locations[i].kind == LOC_GIVE && sp.reward[i].kind == REWARD_ITEM
            && sp.reward[i].value != ITEM_HYDRO_PACK) {
            item_spot = (int)i;
        }
    }

    CHECK(item_spot >= 0);
    s_phase = OPENRAC_PLAY_SCENE;
    memset(s_memory + D->banner_text, 0, 32);
    memcpy(s_memory + D->banner_ticks, &(int32_t){0}, 4);
    give(rando_locations[item_spot].key, 1);
    CHECK(strcmp((char*)s_memory + D->banner_text, "Paradox") != 0); /* GiveItem's is gone */
    CHECK(s_last_equip == 1);
    s_frame(NULL);
    CHECK(s_memory[D->banner_text] == 0); /* in a scene it waits */
    s_phase = OPENRAC_PLAY_PLAYING;
    s_frame(NULL);
    char shown_name[32];
    snprintf(shown_name, sizeof shown_name, "\x0cm%X\x08", (unsigned)rando_reward_text(sp.reward[item_spot]));
    CHECK(strcmp((char*)s_memory + D->banner_text, shown_name) == 0);
    /* The game's own banner for the place's item, a tick later, is written over. */
    strcpy((char*)s_memory + D->banner_text, "You've purchased a Swingshot");
    s_frame(NULL);
    CHECK(strcmp((char*)s_memory + D->banner_text, shown_name) == 0);

    /* The Hydro-Pack is never put on the hero's back, nothing while Clank plays. Mutation: passing
     * the place's equip through leaves the pack over the Heli-Pack. */
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (sp.shuffled[i] && sp.reward[i].kind == REWARD_ITEM && sp.reward[i].value == ITEM_HYDRO_PACK
            && rando_locations[i].kind == LOC_GIVE) {
            give(rando_locations[i].key, 1);
            CHECK(s_last_equip == 0);
        }
    }

    for (size_t i = 0; i < rando_location_count; ++i) {
        if (sp.shuffled[i] && sp.reward[i].kind == REWARD_ITEM && sp.reward[i].value != ITEM_HYDRO_PACK
            && rando_locations[i].kind == LOC_GIVE && (int)i != item_spot) {
            s_memory[D->hero_clank] = 1;
            s_memory[D->owned + sp.reward[i].value] = 0;
            give(rando_locations[i].key, 1);
            CHECK(s_last_equip == 0);
            s_memory[D->hero_clank] = 0;
            break;
        }
    }

    /* What is queued shows, one banner at a time. */
    for (int k = 0; k < 200; ++k) {
        memcpy(s_memory + D->banner_ticks, &(int32_t){0}, 4);
        s_frame(NULL);
    }

    /* A save holds a gold bolt's place with no reward: back at the level's start, silently. */
    memset(s_memory + D->owned, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->obtained, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->gold_bolts, 0, 80);
    int gold_item = -1;

    for (size_t i = 0; i < rando_location_count && gold_item < 0; ++i) {
        if (sp.shuffled[i] && rando_locations[i].kind == LOC_GOLD && sp.reward[i].kind == REWARD_ITEM) {
            gold_item = (int)i;
        }
    }

    CHECK(gold_item >= 0);
    s_memory[D->gold_bolts + (uint32_t)rando_locations[gold_item].level * 4u + rando_locations[gold_item].key] = 1;
    init();
    CHECK(s_memory[D->owned + sp.reward[gold_item].value] == 1);
    memset(s_memory + D->banner_text, 0, 32);
    s_frame(NULL);
    CHECK(s_memory[D->banner_text] == 0);

    /* A party's item spends the place that held it here: the same place in every game. */
    memset(s_memory + D->owned, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->obtained, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->gold_bolts, 0, 80);
    init();
    s_replaying = 1;
    give(sp.reward[item_spot].value, 0);
    s_replaying = 0;
    CHECK(s_memory[D->owned + sp.reward[item_spot].value] == 1);
    CHECK(s_memory[D->obtained + rando_locations[item_spot].key] == 1);

    /* Orxon: no O2 Mask shown until Clank's part is done; the real byte back; then as it is. */
    const int32_t orxon = LEVEL_ORXON;
    memcpy(s_memory + D->level, &orxon, 4);
    s_memory[D->owned + ITEM_O2_MASK] = 1;
    s_memory[D->triggers + LEVEL_ORXON * 16 + ORXON_CLANK_MISSION] = 0;
    openrac_rac1_Ship_InitTransition ship = (openrac_rac1_Ship_InitTransition)hook_named("Ship_InitTransition");
    openrac_rac1_Moby_Class22_Update start22 = (openrac_rac1_Moby_Class22_Update)hook_named("Moby_Class22_Update");
    CHECK(ship && start22);
    ship(0);
    start22(0x00100000u);
    CHECK(s_ship_saw == 0 && s_class22_saw == 0 && s_memory[D->owned + ITEM_O2_MASK] == 1);
    s_memory[D->triggers + LEVEL_ORXON * 16 + ORXON_CLANK_MISSION] = 0xFF;
    ship(0);
    start22(0x00100000u);
    CHECK(s_ship_saw == 1 && s_class22_saw == 1);
    memcpy(s_memory + D->level, &novalis, 4);
    s_memory[D->owned + ITEM_O2_MASK] = 0;

    /* The helmet's pickup hands the hero no helmet he does not own. */
    openrac_rac1_Moby_PilotsHelmet_Update helmet =
        (openrac_rac1_Moby_PilotsHelmet_Update)hook_named("Moby_PilotsHelmet_Update");
    memcpy(s_memory + D->hero_pending, &(int32_t){0}, 4);
    s_memory[D->owned + ITEM_PILOTS_HELMET] = 0;
    helmet(0x00100000u);
    CHECK(get32(s_memory + D->hero_pending) == 0);
    s_memory[D->owned + ITEM_PILOTS_HELMET] = 1;
    helmet(0x00100000u);
    CHECK(get32(s_memory + D->hero_pending) == ITEM_PILOTS_HELMET);
    s_memory[D->owned + ITEM_PILOTS_HELMET] = 0;

    /* A tutorial of what was not given stays out; one of what was comes. */
    openrac_rac1_Help_Request help = (openrac_rac1_Help_Request)hook_named("Help_Request");
    int other_help = -1;

    for (size_t i = 0; i < rando_item_help_count && other_help < 0; ++i) {
        const int place = location_of(LOC_GIVE, 0, rando_item_helps[i].item);
        if (place >= 0 && sp.reward[place].value != rando_item_helps[i].item) {
            other_help = rando_item_helps[i].message;
        }
    }

    CHECK(other_help >= 0);
    s_helps = 0;
    CHECK(help(other_help, 1) == 1 && s_helps == 0);
    CHECK(help(0x1234, 1) == 1 && s_helps == 1);

    /* A crate's drop sees the list's stock, the real one back after. */
    openrac_rac1_Ammo_ChooseDrop drop = (openrac_rac1_Ammo_ChooseDrop)hook_named("Ammo_ChooseDrop");
    memset(s_memory + D->vendor_stock, 0xFF, 12);
    s_memory[D->vendor_stock] = ITEM_PYROCITOR;
    s_memory[D->owned + ITEM_PYROCITOR] = 1;
    CHECK(drop(0, 0) == 1);
    CHECK(s_memory[D->vendor_stock] == ITEM_PYROCITOR && s_drop_saw == ITEM_PYROCITOR);

    /* The line over the vendor: the slot under the cursor, gone when the menu closes. */
    openrac_rac1_Vendor_MenuUpdate menu_update = (openrac_rac1_Vendor_MenuUpdate)hook_named("Vendor_MenuUpdate");
    uint8_t* vmenu = s_memory + D->vendor_menu;
    memset(vmenu, 0, 0x220);
    set32(vmenu + VENDOR_COUNT, 1);
    set32(vmenu + VENDOR_ENTRIES, ITEM_PYROCITOR);
    menu_update();
    CHECK(strncmp(s_notice, "Buying this gives: ", 19) == 0);
    s_frame(NULL);
    s_frame(NULL);
    CHECK(s_notice[0] == 0);
    memset(s_memory + D->owned, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->obtained, 0, RANDO_ITEM_COUNT);
    memset(s_memory + D->vendor_stock, 0xFF, 12);
    snprintf(path, sizeof path, "%s/spoiler-%s.txt", dir, "");
    char second[RANDO_CODE_SIZE];
    rando_encode(&ss, second);
    snprintf(path, sizeof path, "%s/spoiler-%s.txt", dir, second);
    remove(path);

    /* The remembered seed: a blank seed rolls one and keeps it. */
    s_seed = "";
    s_hook_count = 0;
    CHECK(load(&api, mod) == 0);
    snprintf(path, sizeof path, "%s/seed.txt", dir);
    f = fopen(path, "rb");
    CHECK(f != NULL);
    char kept[64] = "";
    CHECK(fgets(kept, sizeof kept, f) != NULL);
    fclose(f);
    kept[strcspn(kept, "\r\n")] = 0;
    rando_settings ks;
    CHECK(rando_decode(kept, &ks));
    remove(path);
    snprintf(path, sizeof path, "%s/spoiler-%s.txt", dir, kept);
    CHECK(remove(path) == 0);

#if defined(_WIN32)
    _rmdir(dir); /* the data folder, empty again */
#else
    rmdir(dir);
#endif
    free(s_memory);
    printf("load_test: ok\n");
    return 0;
}
