/* SPDX-License-Identifier: GPL-3.0-or-later
 * The item randomizer's library as the port loads it, against a host table and a game memory of
 * the test's own (openrac_mod.h). It reports its id and interface, refuses another game and an
 * unknown disc, takes its eleven hook points with their signatures, writes the remembered seed and
 * the spoiler log to its data folder; then, with game functions of the test's own that set the
 * game's bytes as the game's do:
 *
 *   - GiveItem at a shuffled place gives that place's reward, marks the place had, and leaves the
 *     reward's own place unhad; a place left as it is gives its own item;
 *   - Helga's update sees the Swingshot's owned byte as her place's had state, and the real one is
 *     back after it; an infobot's update and UnlockPlanet give the infobot's reward;
 *   - the vendor's list is built from the stock as the slots stand; the gold bolt count is the
 *     gold bolt rewards had; a gold bolt picked up gives its reward at the next tick.
 *
 * Run with the library's path: load_test <item_randomizer.dylib|.so|.dll>.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* setenv, mkdir, rmdir */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openrac/openrac_mod.h"

#include "config.h"
#include "discs.h"
#include "hooks.h"
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
} s_hooks[16];
static int s_hook_count;
static openrac_frame_fn s_frame;

/* ---- the game's functions, as the test plays them ---- */

static int s_gives, s_unlocks, s_banners, s_list_builds, s_counted;
static uint8_t s_helga_saw = 0xEE, s_stock_seen[12];

/* GiveItem as the game's: owned and obtained set. */
static void game_give(int32_t id, int32_t equip) {
    (void)equip;
    s_memory[D->owned + (uint32_t)id] = 1;
    s_memory[D->obtained + (uint32_t)id] = 1;
    ++s_gives;
}
static void game_unlock(int32_t planet) {
    s_memory[D->planets + (uint32_t)planet] = 1;
    ++s_unlocks;
}
static void game_void(void) {
}
static void game_build(int32_t skip) {
    (void)skip;
    memcpy(s_stock_seen, s_memory + D->vendor_stock, 12);
    ++s_list_builds;
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
static void game_banner(int32_t text, int32_t ticks) {
    (void)text, (void)ticks;
    ++s_banners;
}

/* ---- the host ---- */

static void host_log(openrac_mod* mod, int level, const char* text) {
    (void)mod;
    printf("[%d] item-randomizer: %s\n", level, text);
}
static void host_fail(openrac_mod* mod, const char* reason) {
    (void)mod;
    snprintf(s_failed, sizeof s_failed, "%s", reason);
}
static int setting_bool(openrac_mod* mod, const char* key) {
    (void)mod;
    return strcmp(key, "keep_starting_gear") == 0 || strncmp(key, "shuffle_", 8) == 0 || strcmp(key, "spoiler_log") == 0;
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
    if (strcmp(name, OPENRAC_RAC1_HOOK_Item_Give) == 0) return (void*)game_give;
    if (strcmp(name, OPENRAC_RAC1_HOOK_Planet_Unlock) == 0) return (void*)game_unlock;
    if (strcmp(name, OPENRAC_RAC1_HOOK_Vendor_InitLevelStock) == 0) return (void*)game_void;
    if (strcmp(name, OPENRAC_RAC1_HOOK_Vendor_BuildItemList) == 0) return (void*)game_build;
    if (strcmp(name, OPENRAC_RAC1_HOOK_GoldBolts_CountCollected) == 0) return (void*)game_count;
    if (strcmp(name, OPENRAC_RAC1_HOOK_Moby_Helga_Update) == 0) return (void*)game_helga;
    if (strcmp(name, OPENRAC_RAC1_HOOK_Moby_Infobot_Update) == 0) return (void*)game_infobot;
    return (void*)game_moby;
}
static int host_hook(openrac_mod* mod, const char* name, const char* signature, void* replacement, void** original) {
    (void)mod;
    CHECK(s_hook_count < 16);
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
    return strcmp(name, D->show_banner) == 0 ? (void*)game_banner : NULL;
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
    CHECK(info.api_major == OPENRAC_MOD_API_MAJOR && info.api_minor == OPENRAC_MOD_API_MINOR);
    CHECK(strcmp(info.id, "item-randomizer") == 0 && strcmp(info.version, "1.0.0") == 0);

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

    /* NTSC-U, then PAL: the eleven hook points, with rac1.h's signatures. */
    D = &k_rando_discs[1];
    api = make_api("rac1", "ntsc");
    s_failed[0] = 0;
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0 && s_hook_count == 11 && s_frame != NULL);
    s_hook_count = 0;
    D = &k_rando_discs[0];
    api = make_api("rac1", "pal");
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0 && s_hook_count == 11);
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
    };
    for (int i = 0; i < 11; ++i) {
        CHECK(strcmp(s_hooks[i].name, points[i][0]) == 0 && strcmp(s_hooks[i].signature, points[i][1]) == 0);
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
    openrac_rac1_Moby_Helga_Update helga = (openrac_rac1_Moby_Helga_Update)hook_named("Moby_Helga_Update");
    openrac_rac1_Moby_Infobot_Update infobot = (openrac_rac1_Moby_Infobot_Update)hook_named("Moby_Infobot_Update");
    openrac_rac1_Vendor_BuildItemList build = (openrac_rac1_Vendor_BuildItemList)hook_named("Vendor_BuildItemList");
    openrac_rac1_GoldBolts_CountCollected count = (openrac_rac1_GoldBolts_CountCollected)hook_named("GoldBolts_CountCollected");
    openrac_rac1_Vendor_InitLevelStock init = (openrac_rac1_Vendor_InitLevelStock)hook_named("Vendor_InitLevelStock");
    s_unlock_hook = (openrac_rac1_Planet_Unlock)hook_named("Planet_Unlock");
    CHECK(give && helga && infobot && build && count && init && s_unlock_hook);

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

    /* The vendor: Kerwan's Blaster slot not bought is for sale; a bought slot whose item is not
     * owned is gone; the Bomb Glove (not a slot) is as it is. */
    uint8_t* stock = s_memory + D->vendor_stock;
    stock[0] = 0x4A;                      /* the Bomb Glove, owned */
    stock[1] = ITEM_BLASTER | 0x40;       /* marked bought by some GiveItem */
    stock[2] = ITEM_PYROCITOR;            /* not bought */
    s_memory[D->obtained + ITEM_PYROCITOR] = 1; /* ...but had (bought), its item not owned */
    s_memory[D->owned + ITEM_PYROCITOR] = 0;
    build(0);
    CHECK(s_list_builds == 1 && s_stock_seen[0] == 0x4A && s_stock_seen[1] == ITEM_BLASTER && s_stock_seen[2] == 0xFF);
    CHECK(stock[1] == (ITEM_BLASTER | 0x40) && stock[2] == ITEM_PYROCITOR); /* the real stock back */

    /* Gold bolts: the count is the gold bolt rewards had, and a gold bolt picked up gives its
     * place's reward at the next tick. */
    s_counted = 0;
    int32_t want = 0;
    for (size_t i = 0; i < rando_location_count; ++i) {
        want += plan.shuffled[i] && plan.reward[i].kind == REWARD_GOLD_BOLT && had_here(&rando_locations[i]);
    }
    CHECK(count() == want && s_counted == 0);
    const int bolt = location_of(LOC_GOLD, LEVEL_KERWAN, 1);
    CHECK(bolt >= 0);
    s_frame(NULL); /* nothing new */
    s_gives = s_unlocks = s_banners = 0;
    s_memory[D->gold_bolts + LEVEL_KERWAN * 4 + 1] = 1;
    const rando_reward gr = plan.reward[bolt];
    const int was_held = gr.kind == REWARD_ITEM ? s_memory[D->owned + gr.value] : gr.kind == REWARD_PLANET ? s_memory[D->planets + gr.value] : 0;
    s_frame(NULL);
    printf("the gold bolt gave %d/%d\n", gr.kind, gr.value);
    if (!was_held) {
        CHECK(s_gives + s_unlocks + s_banners == 1);
    }
    s_frame(NULL); /* once */
    CHECK(s_gives + s_unlocks + s_banners <= 1);
    CHECK(count() == want + (gr.kind == REWARD_GOLD_BOLT));

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
