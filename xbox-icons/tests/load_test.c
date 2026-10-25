/* SPDX-License-Identifier: GPL-3.0-or-later
 * The xbox-icons library as the port loads it, against a host table made here (openrac_mod.h): it
 * reports its id and interface, refuses another game and an unknown disc, takes no hook point but a
 * tick callback; with no effect bank, a bank out of the console's memory or fonts that are not the
 * game's, it changes nothing. With OPENRAC_RAC1_LEVEL naming a level that openrac-extractor
 * --decompile wrote (decompiler_out/rac1/levels/NN), the game's own fonts are laid at each disc's
 * addresses too: painted on the first tick, and left alone after.
 *
 * Run with the library's path: load_test <xbox_icons.dylib|.so|.dll>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openrac/openrac_mod.h"

#include "paint.h"
#include "xbox_icons.h"

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

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define MEMORY_SIZE XBOX_ICONS_RAM
#define BANK 0x01000000u /* where the test puts the effect bank */
static uint8_t* s_memory;
static char s_failed[256];
static int s_hooks;
static int s_lines;
static openrac_frame_fn s_tick;
static void* s_tick_user;

static void host_log(openrac_mod* mod, int level, const char* text) {
    (void)mod;
    ++s_lines;
    printf("[%d] xbox-icons: %s\n", level, text);
}
static void host_fail(openrac_mod* mod, const char* reason) {
    (void)mod;
    snprintf(s_failed, sizeof s_failed, "%s", reason);
}
static int setting_bool(openrac_mod* mod, const char* key) {
    (void)mod;
    return strcmp(key, "trace") == 0;
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
    (void)mod, (void)key;
    return "";
}
static int host_hook(openrac_mod* mod, const char* name, const char* signature, void* replacement,
                     void** original) {
    (void)mod, (void)name, (void)signature, (void)replacement, (void)original;
    ++s_hooks;
    return OPENRAC_HOOK_UNKNOWN;
}
static int host_on_frame(openrac_mod* mod, openrac_frame_fn fn, void* user) {
    (void)mod;
    s_tick = fn;
    s_tick_user = user;
    return OPENRAC_HOOK_OK;
}
static int host_on_pad(openrac_mod* mod, openrac_pad_fn fn, void* user) {
    (void)mod, (void)fn, (void)user;
    return OPENRAC_HOOK_OK;
}
static void* host_function(const char* name) {
    (void)name;
    return NULL;
}
static void* host_function_at(openrac_gaddr a) {
    (void)a;
    return NULL;
}
static openrac_gaddr identity(openrac_gaddr a) {
    return a;
}
static openrac_gaddr level_data(int level, openrac_gaddr a) {
    (void)level;
    return a;
}
static int loaded_level(void) {
    return 3;
}
static openrac_gaddr stack_push(uint32_t size, openrac_gaddr* saved) {
    (void)size;
    *saved = 0;
    return 0x001F0000u;
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
    api.relocate_level_data = level_data;
    api.loaded_level = loaded_level;
    api.stack_push = stack_push;
    api.stack_pop = stack_pop;
    return api;
}

static void put_u32(openrac_gaddr a, uint32_t v) {
    memcpy(s_memory + a, &v, 4);
}
static void put_u16(openrac_gaddr a, uint16_t v) {
    memcpy(s_memory + a, &v, 2);
}

/* The effect table of disc D: the three fonts' records, palette at bank + 0x800, the fonts at
 * + 0xC00, + 0x8C00, + 0x10C00 (as every level of the PAL disc has them). */
static void lay_table(const xbox_icons_disc* d, uint32_t bank) {
    put_u32(d->bank, bank);
    for (uint32_t f = 0; f < 3; ++f) {
        put_u16(d->table + 16u * (f + 1u) + 8u, (uint16_t)((0xC00u + f * 0x8000u) / 16u));
        put_u16(d->table + 16u * (f + 1u) + 0xAu, 0x800u / 16u);
    }
}
static uint32_t fonts_hash(void) {
    uint8_t* fonts[3] = {s_memory + BANK + 0xC00u, s_memory + BANK + 0x8C00u,
                         s_memory + BANK + 0x10C00u};
    return xbox_icons_hash(fonts, s_memory + BANK + 0x800u);
}

/* A decompiled level's effect bank, from OPENRAC_RAC1_LEVEL; 0 when there is none. */
static uint8_t* s_bank;
static size_t s_bank_size;
static int read_level(void) {
    const char* dir = getenv("OPENRAC_RAC1_LEVEL");
    if (dir == NULL || dir[0] == 0) {
        return 0;
    }
    char path[1024];
    uint8_t index[0x70];
    snprintf(path, sizeof path, "%s/core_index.bin", dir);
    FILE* f = fopen(path, "rb");
    CHECK(f != NULL && fread(index, 1, sizeof index, f) == sizeof index);
    fclose(f);
    int32_t bank;
    memcpy(&bank, index + 0x68, 4);
    snprintf(path, sizeof path, "%s/unpacked/core_data.bin", dir);
    f = fopen(path, "rb");
    CHECK(f != NULL);
    s_bank_size = 0x18C00u;
    s_bank = malloc(s_bank_size);
    CHECK(fseek(f, bank, SEEK_SET) == 0 && fread(s_bank, 1, s_bank_size, f) == s_bank_size);
    fclose(f);
    return 1;
}

static void play(const char* version, openrac_mod* mod, openrac_mod_load_fn load) {
    const xbox_icons_disc* d =
        strcmp(version, "pal") == 0 ? &k_xbox_icons_discs[0] : &k_xbox_icons_discs[1];
    memset(s_memory, 0, MEMORY_SIZE);
    openrac_api api = make_api("rac1", version);
    s_failed[0] = 0;
    s_hooks = 0;
    s_tick = NULL;
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0 && s_hooks == 0 && s_tick != NULL);

    /* No bank yet (a level loading), then one past the console's memory: nothing read. */
    s_tick(s_tick_user);
    lay_table(d, XBOX_ICONS_RAM - 0x1000u);
    s_tick(s_tick_user);

    /* Fonts that are not the game's (here all zero): left as they are, and said once. */
    lay_table(d, BANK);
    memset(s_memory + BANK + 0x800u, 7, 0x400);
    const uint32_t other = fonts_hash();
    s_lines = 0;
    s_tick(s_tick_user);
    s_tick(s_tick_user);
    CHECK(fonts_hash() == other && s_lines == 1);

    if (s_bank != NULL) {
        /* The game's fonts: painted on the first tick, then left as painted. */
        memcpy(s_memory + BANK, s_bank, s_bank_size);
        CHECK(fonts_hash() == k_xbox_icons_game_fonts[0]);
        s_tick(s_tick_user);
        const uint32_t painted = fonts_hash();
        CHECK(painted != k_xbox_icons_game_fonts[0]);
        s_tick(s_tick_user);
        CHECK(fonts_hash() == painted);
        /* The next level loads the game's fonts again: painted again, the same. */
        memcpy(s_memory + BANK, s_bank, s_bank_size);
        s_tick(s_tick_user);
        CHECK(fonts_hash() == painted);
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
    /* The interface its manifest names (1.1), whatever the SDK it was built with. */
    CHECK(info.api_major == OPENRAC_MOD_API_MAJOR && info.api_minor == 1);
    CHECK(strcmp(info.id, "xbox-icons") == 0 && strcmp(info.version, "1.0.0") == 0);

    s_memory = calloc(1, MEMORY_SIZE);
    CHECK(s_memory != NULL);
    openrac_mod* mod = (openrac_mod*)&info; /* any handle: the mod only passes it back */

    /* Another game, an unknown disc: refused, with a reason, before any callback. */
    openrac_api api = make_api("rac2", "ntsc");
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "rac1") != NULL && s_tick == NULL);
    api = make_api("rac1", "jp");
    s_failed[0] = 0;
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "jp") != NULL && s_tick == NULL);

    if (!read_level()) {
        printf("xbox_icons_load_test: no OPENRAC_RAC1_LEVEL, the game's own fonts not tried\n");
    }
    play("pal", mod, load);
    play("ntsc", mod, load);

    free(s_bank);
    free(s_memory);
    printf("xbox_icons_load_test: ok\n");
    return 0;
}
