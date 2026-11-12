/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * NG++: the library as the port loads it (src/ng_plus_plus.c), against a host table and
 * game memory of the test's own, on each of the three discs: a Lancer at V4 offered Alpha at the
 * vendor and bought, levelled to Beta by experience, its tint, damage and most ammo; the RYNO II
 * reaching Heavy; the levels kept by the save's clock in the mod's data folder.
 *
 * Run with the library's path: ng_plus_plus_load_test <ng_plus_plus.dylib|.so|.dll>.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "discs.h"
#include "hooks.h"
#include "openrac/openrac_mod.h"

#if defined(_WIN32)
#include <windows.h>

static void* open_library(const char* path) {
    return (void*)LoadLibraryA(path);
}

static void* symbol(void* lib, const char* name) {
    return (void*)GetProcAddress((HMODULE)lib, name);
}
#else
#include <dlfcn.h>

static void* open_library(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void* symbol(void* lib, const char* name) {
    return dlsym(lib, name);
}
#endif

static int s_failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: [%s] %s\n", __FILE__, __LINE__, s_disc->version, #c);          \
            ++s_failures;                                                                          \
        }                                                                                          \
    } while (0)

#define MEMORY_SIZE 0x00400000u
#define TEXT_AT 0x00300000u /* the test's string of the Lancer's name */
#define TINT_AT 0x00300100u
#define REQUEST_AT 0x00300200u
#define LANCER 30
#define LANCER_V4 80
#define RYNO 44
#define MOBY_HERO 0x00310000u
#define MOBY_ENEMY 0x00310100u

static const wt_disc* s_disc;
static uint8_t* s_memory;
static char s_failed[256];
static char s_notice[256];
static char s_dir[512];

typedef struct hooked {
    const char* name;
    void* replacement;
} hooked;

static hooked s_hooks[16];
static int s_hook_count;
static openrac_frame_fn s_frame;
static void* s_frame_user;

static int32_t get32(uint32_t a) {
    int32_t v;
    memcpy(&v, s_memory + a, 4);
    return v;
}

static void set32(uint32_t a, int32_t v) {
    memcpy(s_memory + a, &v, 4);
}

static uint16_t get16(uint32_t a) {
    uint16_t v;
    memcpy(&v, s_memory + a, 2);
    return v;
}

static void set16(uint32_t a, uint16_t v) {
    memcpy(s_memory + a, &v, 2);
}

static float getf(uint32_t a) {
    float v;
    memcpy(&v, s_memory + a, 4);
    return v;
}

static void setf(uint32_t a, float v) {
    memcpy(s_memory + a, &v, 4);
}

static uint32_t row(int r) {
    return s_disc->weapon_table + (uint32_t)r * WT_ROW_SIZE;
}

/* ---- the test's game ---- */

static int s_upgraded = -1;
static float s_damage_seen;

static void game_build_list(int32_t hide) {
    (void)hide;
    const uint32_t ui = s_disc->vendor;
    for (int i = 0; i < 2; ++i) {
        const uint32_t e = ui + WT_VENDOR_ENTRIES + (uint32_t)i * WT_VENDOR_ENTRY_SIZE;
        set32(e + WT_ENTRY_ITEM, 22 + i);
        set32(e + WT_ENTRY_KIND, 1);
        set32(e + WT_ENTRY_UPGRADE, 0);
    }
    set32(ui + WT_VENDOR_COUNT, 2);
}

static int32_t game_price(void) {
    return 77;
}

static int32_t game_upgrade(int32_t id) {
    s_upgraded = id;
    return 0;
}

static void game_colors(int32_t id, uint32_t first, uint32_t second, uint32_t tint) {
    (void)id, (void)first, (void)second;
    set32(tint, (int32_t)0x60F08040u);
}

static void game_post_hit(uint32_t victim, uint32_t request) {
    (void)victim;
    s_damage_seen = getf(request + WT_HIT_DAMAGE);
}

static int32_t s_taken;

static void game_take_damage(int32_t amount) {
    s_taken += amount;
}

static int32_t game_text(int32_t id) {
    return id == 0x1234 ? (int32_t)TEXT_AT : 0;
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
    return strcmp(key, "notices") == 0 || strcmp(key, "enemy_scaling") == 0;
}

static double setting_float(openrac_mod* mod, const char* key) {
    (void)mod;
    return strcmp(key, "enemy_strength") == 0 ? 1.0 : 0.0;
}

static int host_hook(
    openrac_mod* mod, const char* name, const char* sig, void* replacement, void** original
) {
    (void)mod, (void)sig;
    void* game = NULL;
    if (strcmp(name, WT_HOOK_Vendor_BuildList) == 0) {
        game = (void*)game_build_list;
    } else if (strcmp(name, WT_HOOK_Vendor_Price) == 0) {
        game = (void*)game_price;
    } else if (strcmp(name, WT_HOOK_Item_Upgrade) == 0) {
        game = (void*)game_upgrade;
    } else if (strcmp(name, WT_HOOK_Item_Colors) == 0) {
        game = (void*)game_colors;
    } else if (strcmp(name, WT_HOOK_Moby_PostHit) == 0) {
        game = (void*)game_post_hit;
    } else if (strcmp(name, WT_HOOK_Hero_TakeDamage) == 0) {
        game = (void*)game_take_damage;
    } else {
        return OPENRAC_HOOK_UNKNOWN;
    }
    *original = game;
    s_hooks[s_hook_count].name = name;
    s_hooks[s_hook_count].replacement = replacement;
    ++s_hook_count;
    return OPENRAC_HOOK_OK;
}

static void* hooked_fn(const char* name) {
    for (int i = 0; i < s_hook_count; ++i) {
        if (strcmp(s_hooks[i].name, name) == 0) {
            return s_hooks[i].replacement;
        }
    }
    return NULL;
}

static int host_on_frame(openrac_mod* mod, openrac_frame_fn fn, void* user) {
    (void)mod;
    s_frame = fn;
    s_frame_user = user;
    return 0;
}

static void* host_function(const char* name) {
    return strcmp(name, s_disc->text) == 0 ? (void*)game_text : NULL;
}

static openrac_gaddr identity(openrac_gaddr a) {
    return a;
}

static int host_level(void) {
    return 1;
}

static int host_notice(openrac_mod* mod, const char* text) {
    (void)mod;
    snprintf(s_notice, sizeof s_notice, "%s", text);
    return OPENRAC_HOOK_OK;
}

static const char* host_data_dir(openrac_mod* mod) {
    (void)mod;
    return s_dir;
}

static int host_publish(openrac_mod* mod, const void* data, uint32_t size, const char* text) {
    (void)mod, (void)data, (void)size, (void)text;
    return OPENRAC_HOOK_OK;
}

static int host_on_rules(openrac_mod* mod, openrac_party_rules_fn fn, void* user) {
    (void)mod, (void)fn, (void)user;
    return 0;
}

static void tick(int n) {
    for (int i = 0; i < n; ++i) {
        s_frame(s_frame_user);
    }
}

static void set_clock(uint8_t first) {
    for (int i = 0; i < 8; ++i) {
        s_memory[s_disc->clock + (uint32_t)i] = first != 0 ? (uint8_t)(first + i) : 0;
    }
}

static void run(const char* path, const wt_disc* disc) {
    s_disc = disc;
    memset(s_memory, 0, MEMORY_SIZE);
    s_hook_count = 0;
    s_upgraded = -1;
    s_failed[0] = 0;
    s_notice[0] = 0;
    {
        char file[600];
        snprintf(file, sizeof file, "%s/tiers.txt", s_dir);
        remove(file);
    }

    void* lib = open_library(path);
    if (lib == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        ++s_failures;
        return;
    }
    openrac_mod_load_fn load = (openrac_mod_load_fn)symbol(lib, "openrac_mod_load");
    static openrac_api api;
    memset(&api, 0, sizeof api);
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = "rac2";
    api.version = disc->version;
    api.openrac_version = "test";
    api.platform = "test";
    api.memory = s_memory;
    api.log = host_log;
    api.fail = host_fail;
    api.setting_bool = setting_bool;
    api.setting_float = setting_float;
    api.hook = host_hook;
    api.on_frame = host_on_frame;
    api.function = host_function;
    api.relocate_data = identity;
    api.loaded_level = host_level;
    api.play_notice = host_notice;
    api.data_dir = host_data_dir;
    api.party_rules_publish = host_publish;
    api.on_party_rules = host_on_rules;

    CHECK(load != NULL && load(&api, (openrac_mod*)&api) == 0);
    CHECK(s_failed[0] == 0);
    CHECK(s_hook_count == 6);

    /* A Lancer at V4 (its own row 30 linked on to row 80, the last), the RYNO II at its one row. */
    s_memory[s_disc->owned + LANCER] = 1;
    s_memory[s_disc->slot + LANCER] = LANCER_V4;
    set16(row(LANCER_V4) + WT_ROW_NAME, 0x1234);
    set16(row(LANCER_V4) + WT_ROW_AMMO, 500);
    set32(row(LANCER_V4) + WT_ROW_CLASS, 999);
    strcpy((char*)s_memory + TEXT_AT, "Heavy Lancer");
    s_memory[s_disc->owned + RYNO] = 1;
    s_memory[s_disc->slot + RYNO] = RYNO;
    set16(row(RYNO) + WT_ROW_AMMO, 100);
    set32(s_disc->hero + WT_HERO_MOBY, (int32_t)MOBY_HERO);
    set32(s_disc->hero + WT_HERO_HELD, -1);
    /* A save's RYNO II with all the experience it ever had: not Heavy at once, counted from 0. */
    set32(s_disc->exp + 4u * RYNO, 50000 << 5);
    set_clock(0x10); /* a save loaded the book does not have: kept as it is (no levels) */
    tick(1);
    tick(1);
    CHECK(get32(s_disc->exp + 4u * RYNO) == 0);
    CHECK(strstr(s_notice, "Heavy") == NULL);
    CHECK(get16(row(LANCER_V4) + WT_ROW_AMMO) == 500);

    /* The vendor: the game's two entries, then Alpha for the Lancer. */
    ((wt_Vendor_BuildList)hooked_fn(WT_HOOK_Vendor_BuildList))(0);
    const uint32_t ui = s_disc->vendor;
    CHECK(get32(ui + WT_VENDOR_COUNT) == 3);
    const uint32_t e = ui + WT_VENDOR_ENTRIES + 2u * WT_VENDOR_ENTRY_SIZE;
    CHECK(get32(e + WT_ENTRY_ITEM) == LANCER);
    CHECK(get32(e + WT_ENTRY_UPGRADE) == 1);
    CHECK(get32(e + WT_ENTRY_CLASS) == 999);
    set32(ui + WT_VENDOR_CURSOR, 0);
    CHECK(((wt_Vendor_Price)hooked_fn(WT_HOOK_Vendor_Price))() == 77);
    set32(ui + WT_VENDOR_CURSOR, 2);
    CHECK(((wt_Vendor_Price)hooked_fn(WT_HOOK_Vendor_Price))() == 2250000); /* no Mega row here */

    /* Bought: Alpha. */
    CHECK(((wt_Item_Upgrade)hooked_fn(WT_HOOK_Item_Upgrade))(LANCER) == 1);
    CHECK(s_upgraded == -1);
    CHECK(strcmp(s_notice, "Heavy Lancer Alpha") == 0);
    tick(1);
    CHECK(get16(row(LANCER_V4) + WT_ROW_AMMO) == 550);
    CHECK(get32(s_disc->ammo + 4u * LANCER) == 550);
    /* Not the mod's entry: the game's. */
    set32(ui + WT_VENDOR_CURSOR, 0);
    CHECK(((wt_Item_Upgrade)hooked_fn(WT_HOOK_Item_Upgrade))(22) == 0);
    CHECK(s_upgraded == 22);

    /* Beta by experience; then Gamma for sale. */
    set32(s_disc->exp + 4u * LANCER, 7999 << 5);
    tick(1);
    CHECK(strcmp(s_notice, "Heavy Lancer Alpha") == 0);
    set32(s_disc->exp + 4u * LANCER, 8000 << 5);
    tick(1);
    CHECK(strcmp(s_notice, "Heavy Lancer Beta") == 0);
    CHECK(get32(s_disc->exp + 4u * LANCER) == 0);
    CHECK(get16(row(LANCER_V4) + WT_ROW_AMMO) == 600);
    ((wt_Vendor_BuildList)hooked_fn(WT_HOOK_Vendor_BuildList))(0);
    CHECK(get32(ui + WT_VENDOR_COUNT) == 3);
    set32(ui + WT_VENDOR_CURSOR, 2);
    CHECK(((wt_Vendor_Price)hooked_fn(WT_HOOK_Vendor_Price))() == 3750000);

    /* Beta's tint, cyan, in the game's alpha. */
    ((wt_Item_Colors)hooked_fn(WT_HOOK_Item_Colors))(LANCER, TINT_AT + 8, TINT_AT + 4, TINT_AT);
    CHECK((uint32_t)get32(TINT_AT) == 0x60FFE000u);
    ((wt_Item_Colors)hooked_fn(WT_HOOK_Item_Colors))(22, TINT_AT + 8, TINT_AT + 4, TINT_AT);
    CHECK((uint32_t)get32(TINT_AT) == 0x60F08040u);

    /* Beta's damage, with the Lancer in hand: 1.5 times for the call, put back after. */
    set32(s_disc->hero + WT_HERO_HELD, LANCER);
    setf(REQUEST_AT + WT_HIT_DAMAGE, 10.0f);
    ((wt_Moby_PostHit)hooked_fn(WT_HOOK_Moby_PostHit))(MOBY_ENEMY, REQUEST_AT);
    CHECK(s_damage_seen == 15.0f);
    CHECK(getf(REQUEST_AT + WT_HIT_DAMAGE) == 10.0f);
    ((wt_Moby_PostHit)hooked_fn(WT_HOOK_Moby_PostHit))(MOBY_HERO, REQUEST_AT);
    CHECK(s_damage_seen == 10.0f);

    /* Challenge mode's third game (cycle 2): enemies have 1.5 times the health, deal 1.25 times. */
    set16(s_disc->progress + WT_PROGRESS_CYCLE, 2);
    setf(REQUEST_AT + WT_HIT_DAMAGE, 10.0f);
    ((wt_Moby_PostHit)hooked_fn(WT_HOOK_Moby_PostHit))(MOBY_ENEMY, REQUEST_AT);
    CHECK(s_damage_seen == 10.0f); /* Beta's 1.5 over the health's 1.5 */
    s_taken = 0;
    for (int i = 0; i < 4; ++i) {
        ((wt_Hero_TakeDamage)hooked_fn(WT_HOOK_Hero_TakeDamage))(1);
    }
    CHECK(s_taken == 5);
    set16(s_disc->progress + WT_PROGRESS_CYCLE, 1);
    s_taken = 0;
    ((wt_Hero_TakeDamage)hooked_fn(WT_HOOK_Hero_TakeDamage))(3);
    CHECK(s_taken == 3);

    /* The RYNO II: Heavy by experience. */
    set32(s_disc->exp + 4u * RYNO, 6000 << 5);
    tick(1);
    CHECK(strcmp(s_notice, "RYNO II Heavy") == 0);
    CHECK(get16(row(RYNO) + WT_ROW_AMMO) == 150);

    /* Saved (a new clock): kept. A new game (zeros) has none; the save loaded again has them. */
    set_clock(0x20);
    tick(1);
    set_clock(0);
    tick(1);
    CHECK(get16(row(LANCER_V4) + WT_ROW_AMMO) == 500);
    set_clock(0x20);
    tick(1);
    CHECK(get16(row(LANCER_V4) + WT_ROW_AMMO) == 600);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <ng_plus_plus library>\n", argv[0]);
        return 2;
    }
    s_memory = calloc(1, MEMORY_SIZE);
    {
        const char* tmp = getenv("TMPDIR");
#if defined(_WIN32)
        if (tmp == NULL) {
            tmp = getenv("TEMP");
        }
#endif
        snprintf(s_dir, sizeof s_dir, "%s", tmp != NULL ? tmp : ".");
    }
    for (size_t i = 0; i < sizeof k_wt_discs / sizeof k_wt_discs[0]; ++i) {
        run(argv[1], &k_wt_discs[i]);
    }
    free(s_memory);
    if (s_failures == 0) {
        printf("ng_plus_plus_load: ok\n");
    }
    return s_failures != 0;
}
