/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Going Commando: the item randomizer's library as the port loads it (src/rac2/rac2.c), against a
 * host table and game memory of the test's own. The test's game functions do to the owned bytes
 * what the routines named in rac2.c's header do: GiveInventoryItem sets an item's owned and new
 * bytes when it is not owned; a giver gives its item while its owned byte is clear.
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

/* NTSC-U v1.01's places, as rac2.c's header names them. */
#define OWNED 0x001A7AF8u
#define NEW_FLAG 0x001A7B30u
#define STOCK_TABLE 0x002403C0u
#define MARK_BITS (0x001395B8u + 0xC4u)
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

/* A giver: gives its item while the owned byte it sees is clear; what it saw, into s_saw. */
static int s_giver_item;
static int s_saw;
static void game_giver(uint32_t moby) {
    (void)moby;
    s_saw = s_memory[OWNED + (uint32_t)s_giver_item];
    if (s_saw == 0) {
        give(s_giver_item);
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
    *original = strcmp(name, "rac2.item.give") == 0 ? (void*)game_give : (void*)game_giver;
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

    // The game's vendor table.
    for (int i = 0; i < R2_VENDOR_COUNT; ++i) {
        set32(STOCK_TABLE + 8u * (uint32_t)i, r2_vendor_stock[i].item);
        set32(STOCK_TABLE + 8u * (uint32_t)i + 4u, r2_vendor_stock[i].level);
    }
    set32(STOCK_TABLE + 8u * R2_VENDOR_COUNT, (uint32_t)-2);

    // PAL and v2.00: refused before any hook.
    openrac_api api = make_api("pal");
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "NTSC-U") != NULL && s_hook_count == 0);

    // NTSC-U v1.01: the give and the twelve givers, then the tick.
    api = make_api("ntsc");
    s_failed[0] = 0;
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0 && s_hook_count == 13 && s_frame != NULL);
    for (int i = 0; i < s_hook_count; ++i) {
        CHECK(strncmp(s_hooks[i].name, "rac2.", 5) == 0);
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

    if (s_failures == 0) {
        printf("rac2 load: all passed\n");
    }
    return s_failures != 0;
}
