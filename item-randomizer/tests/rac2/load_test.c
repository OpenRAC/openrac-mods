/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Going Commando: the item randomizer's library as the port loads it (src/rac2/rac2.c), against a
 * host table and game memory of the test's own. The test's game functions do to the owned bytes
 * what the routines named in rac2.c's header do: GiveInventoryItem sets an item's owned and new
 * bytes when it is not owned; a giver gives its item while its owned byte is clear, then puts it in
 * the hero's hand (EquipItem), as Todano's Sheepinator pickup and most givers do.
 *
 * Run with the library's path: rac2_load_test <item_randomizer.dylib|.so|.dll>.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "discs2.h"
#include "openrac/openrac_mod.h"
#include "rando2.h"

#if defined(_WIN32)
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

static int s_failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            ++s_failures;                                                                          \
        }                                                                                          \
    } while (0)

/* The disc under test's places (discs2.h), as rac2.c's header names them. */
static const r2_disc* s_disc;
#define OWNED (s_disc->owned)
#define NEW_FLAG (s_disc->new_flag)
#define STOCK_TABLE (s_disc->stock_table)
#define MARK_BITS (s_disc->progress + 0xC4u)
#define MEMORY_SIZE 0x00300000u

static uint8_t* s_memory;
static char s_failed[256];
static char s_notice[256];
static const char* s_seed = "going commando";
static int s_vendor = 1;

typedef struct hooked {
    const char* name;
    const char* signature;
    void* replacement;
} hooked;
static hooked s_hooks[32];
static int s_hook_count;
static openrac_frame_fn s_frame;
static openrac_play_status s_status;

static uint32_t get32(uint32_t a) {
    uint32_t v;
    memcpy(&v, s_memory + a, 4);
    return v;
}
static void set32(uint32_t a, uint32_t v) {
    memcpy(s_memory + a, &v, 4);
}

/* ---- the test's game ---- */

static void game_give(int32_t id) {
    if (s_memory[OWNED + (uint32_t)id] == 0) {
        s_memory[OWNED + (uint32_t)id] = 1;
        s_memory[NEW_FLAG + (uint32_t)id] = 1;
    }
}

/* EquipItem: the item last asked for, -1 none. */
static int s_equipped = -1;
static int32_t game_equip(int32_t id) {
    s_equipped = id;
    return 1;
}

static void* hook_named(const char* name) {
    for (int i = 0; i < s_hook_count; ++i) {
        if (strcmp(s_hooks[i].name, name) == 0) {
            return s_hooks[i].replacement;
        }
    }
    return NULL;
}

/* The game's call of GiveInventoryItem: through the mod's hook. */
static void give(int id) {
    ((void (*)(int32_t))hook_named("rac2.item.give"))(id);
}

/* The game's call of EquipItem: through the mod's hook. */
static int32_t equip(int id) {
    return ((int32_t (*)(int32_t))hook_named("rac2.item.equip"))(id);
}

/* A giver: gives its item while the owned byte it sees is clear; what it saw, into s_saw. */
static int s_giver_item;
static int s_saw;
static void game_giver(uint32_t moby) {
    (void)moby;
    s_saw = s_memory[OWNED + (uint32_t)s_giver_item];
    if (s_saw == 0) {
        give(s_giver_item);
        equip(s_giver_item);
    }
}

/* ---- the host ---- */

static void host_log(openrac_mod* mod, int level, const char* text) {
    (void)mod, (void)level, (void)text;
}
static void host_fail(openrac_mod* mod, const char* reason) {
    (void)mod;
    snprintf(s_failed, sizeof s_failed, "%s", reason);
}
static int setting_bool(openrac_mod* mod, const char* key) {
    (void)mod;
    if (strcmp(key, "shuffle_vendor") == 0) {
        return s_vendor;
    }
    return strcmp(key, "spoiler_log") == 0 || strncmp(key, "shuffle_", 8) == 0
           || strcmp(key, "keep_starting_gear") == 0;
}
static int64_t setting_int(openrac_mod* mod, const char* key) {
    (void)mod, (void)key;
    return 0;
}
static double setting_float(openrac_mod* mod, const char* key) {
    (void)mod, (void)key;
    return 0;
}
static const char* setting_string(openrac_mod* mod, const char* key) {
    (void)mod;
    if (strcmp(key, "seed") == 0) {
        return s_seed;
    }
    return strcmp(key, "difficulty") == 0 ? "standard" : "";
}
static int host_hook(openrac_mod* mod, const char* name, const char* signature, void* replacement,
                     void** original) {
    (void)mod;
    CHECK(s_hook_count < 32);
    s_hooks[s_hook_count].name = name;
    s_hooks[s_hook_count].signature = signature;
    s_hooks[s_hook_count].replacement = replacement;
    ++s_hook_count;
    *original = strcmp(name, "rac2.item.give") == 0    ? (void*)game_give
                : strcmp(name, "rac2.item.equip") == 0 ? (void*)game_equip
                                                       : (void*)game_giver;
    return OPENRAC_HOOK_OK;
}
static int host_on_frame(openrac_mod* mod, openrac_frame_fn fn, void* user) {
    (void)mod, (void)user;
    s_frame = fn;
    return 0;
}
static openrac_gaddr identity(openrac_gaddr a) {
    return a;
}
static int host_play_status(openrac_play_status* out) {
    *out = s_status;
    out->size = sizeof *out;
    return OPENRAC_HOOK_OK;
}
static int host_play_notice(openrac_mod* mod, const char* text) {
    (void)mod;
    snprintf(s_notice, sizeof s_notice, "%s", text != NULL ? text : "");
    return OPENRAC_HOOK_OK;
}
static int host_replaying(void) {
    return 0;
}

/* The port's side of a party's rules: what the mod published and its listener. */
static char s_published[64];
static char s_published_text[192];
static openrac_party_rules_fn s_rules_fn;
static int host_rules_publish(openrac_mod* mod, const void* data, uint32_t size, const char* text) {
    (void)mod;
    CHECK(size < sizeof s_published);
    memcpy(s_published, data, size);
    s_published[size] = 0;
    snprintf(s_published_text, sizeof s_published_text, "%s", text != NULL ? text : "");
    return OPENRAC_HOOK_OK;
}
static int host_on_rules(openrac_mod* mod, openrac_party_rules_fn fn, void* user) {
    (void)mod, (void)user;
    s_rules_fn = fn;
    return OPENRAC_HOOK_OK;
}

static openrac_api make_api(const char* version) {
    openrac_api api;
    memset(&api, 0, sizeof api);
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = "rac2";
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
    api.relocate_data = identity;
    api.play_status = host_play_status;
    api.play_notice = host_play_notice;
    api.party_replaying = host_replaying;
    return api;
}

/* ---- the checks ---- */

static void tick(int level, uint32_t loads) {
    s_status.phase = OPENRAC_PLAY_PLAYING;
    s_status.level = level;
    s_status.loads = loads;
    s_frame(NULL);
}

static int location_of(int item) {
    for (size_t i = 0; i < r2_location_count; ++i) {
        if (r2_locations[i].item == item) {
            return (int)i;
        }
    }
    return -1;
}

/* One disc: the give, the givers, the vendor's table and the level starts, at its addresses. */
static void play_disc(openrac_mod_load_fn load, openrac_mod* mod, const r2_disc* disc) {
    s_disc = disc;
    memset(s_memory, 0, MEMORY_SIZE);
    s_hook_count = 0;
    s_frame = NULL;
    s_failed[0] = 0;
    s_notice[0] = 0;

    // The game's vendor table.
    for (int i = 0; i < R2_VENDOR_COUNT; ++i) {
        set32(STOCK_TABLE + 8u * (uint32_t)i, r2_vendor_stock[i].item);
        set32(STOCK_TABLE + 8u * (uint32_t)i + 4u, r2_vendor_stock[i].level);
    }
    set32(STOCK_TABLE + 8u * R2_VENDOR_COUNT, (uint32_t)-2);

    // The give and the twelve givers, then the tick.
    openrac_api api = make_api(disc->version);
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0 && s_hook_count == 14 && s_frame != NULL);
    for (int i = 0; i < s_hook_count; ++i) {
        CHECK(strncmp(s_hooks[i].name, "rac2.", 5) == 0);
    }
    if (s_frame == NULL || s_hook_count != 14) {
        return;
    }

    // The placement the settings make, as the mod made it.
    const rando_options o = {s_seed, 1, 1, 0, 0, 1, 1, "standard"};
    rando_settings s;
    rando_settings_of(&o, NULL, 0, &s);
    s.pools &= (uint8_t)(POOL_GADGETS | POOL_WEAPONS | POOL_VENDOR);
    s.keep_start = 1;
    r2_plan plan;
    CHECK(r2_solve(&s, &plan) == 0);

    // The vendor's table gets the seed's levels on the first tick.
    tick(1, 1);
    for (int i = 0; i < R2_VENDOR_COUNT; ++i) {
        CHECK(get32(STOCK_TABLE + 8u * (uint32_t)i) == r2_vendor_stock[i].item);
        CHECK(get32(STOCK_TABLE + 8u * (uint32_t)i + 4u) == plan.stock[i]);
    }
    // It is level data: the next level's program brings the game's own levels again, and they get
    // the seed's again; within one load it is written once.
    for (int i = 0; i < R2_VENDOR_COUNT; ++i) {
        set32(STOCK_TABLE + 8u * (uint32_t)i + 4u, r2_vendor_stock[i].level);
    }
    tick(1, 1);
    CHECK(get32(STOCK_TABLE + 4u) == (uint32_t)r2_vendor_stock[0].level);
    tick(5, 5);
    for (int i = 0; i < R2_VENDOR_COUNT; ++i) {
        CHECK(get32(STOCK_TABLE + 8u * (uint32_t)i + 4u) == plan.stock[i]);
    }

    // The Dynamo's place gives its reward, once.
    const int dynamo = location_of(R2_DYNAMO);
    const int reward = plan.reward[dynamo];
    give(R2_DYNAMO);
    CHECK(s_memory[OWNED + (uint32_t)reward] == 1);
    CHECK(reward == R2_DYNAMO || s_memory[OWNED + R2_DYNAMO] == 0);
    CHECK((get32(MARK_BITS) >> dynamo) & 1u);
    CHECK(strstr(s_notice, r2_item_name(reward)) != NULL);
    memset(s_memory + OWNED, 0, R2_ITEM_COUNT);
    give(R2_DYNAMO);
    CHECK(s_memory[OWNED + (uint32_t)reward] == 0);

    // A seller whose item the player has from elsewhere still sells: it is shown the place's state.
    const int tractor = location_of(R2_TRACTOR_BEAM);
    s_memory[OWNED + R2_TRACTOR_BEAM] = 1;
    s_giver_item = R2_TRACTOR_BEAM;
    ((void (*)(uint32_t))hook_named("rac2.moby.tractor_beam_seller.update"))(0x1000);
    CHECK(s_saw == 0);
    CHECK((get32(MARK_BITS) >> tractor) & 1u);
    CHECK(s_memory[OWNED + R2_TRACTOR_BEAM] == 1); /* the player's own back */
    CHECK(s_memory[OWNED + plan.reward[tractor]] == 1);
    // Once had, it is shown sold.
    ((void (*)(uint32_t))hook_named("rac2.moby.tractor_beam_seller.update"))(0x1000);
    CHECK(s_saw == 1);

    // Todano's Sheepinator pickup gives it, then puts it in his hand: he takes the place's reward,
    // never a Sheepinator he does not own (2026-11-08: held, in no list, gone once put away).
    const int sheep = location_of(R2_SHEEPINATOR);
    CHECK(sheep >= 0 && plan.shuffled[sheep]);
    const int sheep_reward = plan.reward[sheep];
    memset(s_memory + OWNED, 0, R2_ITEM_COUNT);
    s_equipped = -1;
    give(R2_SHEEPINATOR);
    CHECK(equip(R2_SHEEPINATOR) == 1);
    CHECK(s_memory[OWNED + (uint32_t)sheep_reward] == 1);
    CHECK(s_equipped == sheep_reward);
    CHECK(s_equipped == R2_SHEEPINATOR || s_memory[OWNED + R2_SHEEPINATOR] == 0);
    // Once had, the pickup gives nothing again, and puts nothing the player lacks in his hand.
    memset(s_memory + OWNED, 0, R2_ITEM_COUNT);
    s_equipped = -1;
    give(R2_SHEEPINATOR);
    CHECK(equip(R2_SHEEPINATOR) == 0 && s_equipped == -1);
    // An item the player owns is put in his hand as the game asks, and so is any item outside the
    // shuffled places.
    s_memory[OWNED + R2_SHEEPINATOR] = 1;
    CHECK(equip(R2_SHEEPINATOR) == 1 && s_equipped == R2_SHEEPINATOR);
    const int sold = r2_vendor_stock[0].item; /* the vendor's, no shuffled place's */
    CHECK(equip(sold) == 1 && s_equipped == sold);

    // A giver under a stand-in: the Hypnomatic's maker shows its place's state as the owned byte
    // while it runs, and its equip still takes the reward, not the Hypnomatic.
    const int hypno = location_of(R2_HYPNOMATIC);
    if (hypno >= 0 && plan.shuffled[hypno] && plan.reward[hypno] != R2_HYPNOMATIC) {
        memset(s_memory + OWNED, 0, R2_ITEM_COUNT);
        s_equipped = -1;
        s_giver_item = R2_HYPNOMATIC;
        ((void (*)(uint32_t))hook_named("rac2.moby.hypnomatic_maker.update"))(0x1000);
        CHECK(s_saw == 0);
        CHECK(s_equipped == plan.reward[hypno]);
        CHECK(s_memory[OWNED + R2_HYPNOMATIC] == 0);
    }

    // Endako's own sets: the Grindboots' place had, its byte the player's.
    const int grind = location_of(R2_GRINDBOOTS);
    const int grind_reward = plan.reward[grind];
    memset(s_memory + OWNED, 0, R2_ITEM_COUNT);
    s_memory[OWNED + R2_GRINDBOOTS] = 1;
    tick(R2_ENDAKO, 2);
    CHECK((get32(MARK_BITS) >> grind) & 1u);
    CHECK(s_memory[OWNED + (uint32_t)grind_reward] == 1);
    CHECK(grind_reward == R2_GRINDBOOTS || s_memory[OWNED + R2_GRINDBOOTS] == 0);

    // A level's start gives back what the places had gave.
    memset(s_memory + OWNED, 0, R2_ITEM_COUNT);
    tick(R2_OOZLA, 3);
    CHECK(s_memory[OWNED + (uint32_t)reward] == 1);
    CHECK(s_memory[OWNED + (uint32_t)plan.reward[tractor]] == 1);
    CHECK(s_memory[OWNED + (uint32_t)grind_reward] == 1);

    // A new game (the progress bytes cleared): the places give again.
    set32(MARK_BITS, 0);
    memset(s_memory + OWNED, 0, R2_ITEM_COUNT);
    give(R2_DYNAMO);
    CHECK(s_memory[OWNED + (uint32_t)reward] == 1);

    // A vendor table that is not the game's is left alone.
    s_hook_count = 0;
    set32(STOCK_TABLE, 99);
    set32(STOCK_TABLE + 4, 7);
    CHECK(load(&api, mod) == 0);
    tick(1, 4);
    CHECK(get32(STOCK_TABLE + 4) == 7);
    set32(STOCK_TABLE, r2_vendor_stock[0].item);
    set32(STOCK_TABLE + 4, r2_vendor_stock[0].level);

    // A party. A port with its rules: the host's game publishes its seed's code and the words.
    openrac_api party = make_api(disc->version);
    party.party_rules_publish = host_rules_publish;
    party.on_party_rules = host_on_rules;
    s_hook_count = 0;
    s_rules_fn = NULL;
    s_published[0] = 0;
    s_seed = "party-host";
    CHECK(load(&party, mod) == 0 && s_rules_fn != NULL && s_published[0] != 0);
    CHECK(strstr(s_published_text, "Logic: standard") != NULL);
    CHECK(strstr(s_published_text, "Shuffles the vendor") != NULL);
    char host_code[64];
    snprintf(host_code, sizeof host_code, "%s", s_published);

    // A member with a seed of its own, and nothing shuffled: it hooks all the same (a host's seed
    // may shuffle), and plays the host's placement once its rules come.
    s_hook_count = 0;
    s_seed = "party-member";
    s_vendor = 0;
    CHECK(load(&party, mod) == 0 && s_hook_count == 14 && strcmp(s_published, host_code) != 0);
    rando_settings host;
    CHECK(rando_decode(host_code, &host));
    r2_plan host_plan;
    CHECK(r2_solve(&host, &host_plan) == 0);
    s_rules_fn(NULL, host_code, (uint32_t)strlen(host_code));
    memset(s_memory + OWNED, 0, R2_ITEM_COUNT);
    set32(MARK_BITS, 0);
    give(R2_DYNAMO);
    CHECK(s_memory[OWNED + (uint32_t)host_plan.reward[dynamo]] == 1);
    // The vendor's table by the host's seed, at the next tick.
    for (int i = 0; i < R2_VENDOR_COUNT; ++i) {
        set32(STOCK_TABLE + 8u * (uint32_t)i + 4u, r2_vendor_stock[i].level);
    }
    tick(1, 6);
    for (int i = 0; i < R2_VENDOR_COUNT; ++i) {
        CHECK(get32(STOCK_TABLE + 8u * (uint32_t)i + 4u) == (uint32_t)host_plan.stock[i]);
    }
    // The party goes: this game's own again, the vendor's table the game's own back.
    s_rules_fn(NULL, NULL, 0);
    tick(1, 7);
    for (int i = 0; i < R2_VENDOR_COUNT; ++i) {
        CHECK(get32(STOCK_TABLE + 8u * (uint32_t)i + 4u) == r2_vendor_stock[i].level);
    }
    // A code that is not a seed's: refused, the placement as it was.
    s_rules_fn(NULL, "not a code", 10);
    s_vendor = 1;
    s_seed = "going commando";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: rac2_load_test <library>\n");
        return 2;
    }
    void* lib = open_library(argv[1]);
    CHECK(lib != NULL);
    if (lib == NULL) {
        return 1;
    }
    openrac_mod_load_fn load = (openrac_mod_load_fn)symbol(lib, OPENRAC_MOD_LOAD);
    CHECK(load != NULL);

    s_memory = calloc(1, MEMORY_SIZE);
    set_env("OPENRAC_MOD_DATA_DIR", "item-randomizer-rac2-load-test-data");
    openrac_mod* mod = (openrac_mod*)&s_failed; /* the mod's handle: opaque to it */

    // A disc the mod does not know: refused before any hook.
    openrac_api api = make_api("ntsc3");
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "disc") != NULL && s_hook_count == 0);

    // NTSC-U v1.01, PAL and USA v2.00, each at its own addresses.
    CHECK(sizeof k_r2_discs / sizeof k_r2_discs[0] == 3);
    for (size_t d = 0; d < sizeof k_r2_discs / sizeof k_r2_discs[0]; ++d) {
        play_disc(load, mod, &k_r2_discs[d]);
    }

    if (s_failures == 0) {
        printf("rac2 load: all passed\n");
    }
    return s_failures != 0;
}
