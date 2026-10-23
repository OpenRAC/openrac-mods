/* SPDX-License-Identifier: GPL-3.0-or-later
 * The ammo-max library as the port loads it, against a host table made here (openrac_mod.h): it
 * reports its id and interface, refuses another game and an unknown disc, takes no hook point but a
 * tick callback; then, on a game memory laid out as each disc's vendor has it, the buy dialog's
 * count starts at the most the bolts and the weapon's room allow, once each time the dialog opens,
 * and only for ammo the game lets the count rise for.
 *
 * Run with the library's path: load_test <ammo_max.dylib|.so|.dll>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openrac/openrac_mod.h"

#include "ammo_max.h"

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

#define MEMORY_SIZE 0x00200000u
static uint8_t* s_memory;
static char s_failed[256];
static int s_level = -1;
static int s_hooks;
static int s_trace_lines;
static openrac_frame_fn s_tick;
static void* s_tick_user;

static void host_log(openrac_mod* mod, int level, const char* text) {
    (void)mod;
    ++s_trace_lines;
    printf("[%d] ammo-max: %s\n", level, text);
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
/* Level 0's globals as the level loaded has them: here 0x100 bytes on, to see they are asked for. */
static openrac_gaddr identity(openrac_gaddr a) {
    return a;
}
static openrac_gaddr level_data(int level, openrac_gaddr a) {
    CHECK(level == 0);
    return a + 0x100u;
}
static int loaded_level(void) {
    return s_level;
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

static void put_s32(openrac_gaddr a, int32_t v) {
    memcpy(s_memory + a, &v, 4);
}
static void put_u16(openrac_gaddr a, uint16_t v) {
    memcpy(s_memory + a, &v, 2);
}
static int32_t get_s32(openrac_gaddr a) {
    int32_t v;
    memcpy(&v, s_memory + a, 4);
    return v;
}

/* The vendor of disc D: row 2 chosen, item 0x0B's ammo (10 bolts a round, 15 with the dialog's
 * other price, 160 at most), the player with `ammo` of it and `bolts`. */
static const ammo_max_disc* s_d;
static void lay_vendor(int32_t ammo, int32_t bolts) {
    const openrac_gaddr dialog = s_d->dialog + 0x100u, items = s_d->item_table + 0x100u;
    memset(s_memory + dialog, 0, 0x200);
    put_s32(dialog + 0x58u, 2);
    put_s32(dialog + 0xD0u + 2u * 0x14u, 0x0B);
    put_s32(dialog + 0xD0u + 2u * 0x14u + 4u, 1);
    put_u16(items + 0x0Bu * 0x18u + 8u, 10);
    put_u16(items + 0x0Bu * 0x18u + 0xAu, 15);
    put_u16(items + 0x0Bu * 0x18u + 0xEu, 160);
    put_s32(s_d->ammo + 4u * 0x0Bu, ammo);
    put_s32(s_d->bolts, bolts);
}
static int32_t state(void) {
    return get_s32(s_d->dialog + 0x100u + 0x5Cu);
}
static int32_t count(void) {
    return get_s32(s_d->count + 0x100u);
}
/* The game's state 1 as VendorBuyDialogUpdate runs it: the count 1, Up allowed when a round can be
 * bought, then state 2; and the tick's callback after it. */
static void open_dialog(int can_raise) {
    put_s32(s_d->dialog + 0x100u + 0x5Cu, 1);
    s_tick(s_tick_user);
    put_s32(s_d->count + 0x100u, 1);
    put_s32(s_d->can_raise + 0x100u, can_raise);
    put_s32(s_d->dialog + 0x100u + 0x5Cu, 2);
    s_tick(s_tick_user);
}
static void close_dialog(void) {
    put_s32(s_d->dialog + 0x100u + 0x5Cu, 3);
    s_tick(s_tick_user);
    put_s32(s_d->dialog + 0x100u + 0x5Cu, 0);
    s_tick(s_tick_user);
}

static void play(const char* version, openrac_mod* mod, openrac_mod_load_fn load) {
    s_d = strcmp(version, "pal") == 0 ? &k_ammo_max_discs[0] : &k_ammo_max_discs[1];
    memset(s_memory, 0, MEMORY_SIZE);
    openrac_api api = make_api("rac1", version);
    s_failed[0] = 0;
    s_hooks = 0;
    s_tick = NULL;
    s_level = 3;
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0 && s_hooks == 0 && s_tick != NULL);

    /* 0 of 160 and bolts for all of it: 160. */
    lay_vendor(0, 5000);
    s_tick(s_tick_user);
    open_dialog(1);
    CHECK(state() == 2 && count() == 160);
    /* The player lowers it: kept, tick after tick. */
    put_s32(s_d->count + 0x100u, 7);
    s_tick(s_tick_user);
    s_tick(s_tick_user);
    CHECK(count() == 7);
    close_dialog();

    /* Bolts for 40 rounds only: 40. */
    lay_vendor(0, 409);
    open_dialog(1);
    CHECK(count() == 40);
    close_dialog();

    /* 150 of 160 and rich: the 10 it has room for. */
    lay_vendor(150, 9999999);
    open_dialog(1);
    CHECK(count() == 10);
    close_dialog();

    /* The dialog's other price (15 a round): 400 bolts buy 26. */
    lay_vendor(0, 400);
    put_s32(s_d->dialog + 0x100u + 0x40u, 1);
    open_dialog(1);
    CHECK(count() == 26);
    close_dialog();

    /* The game keeps Up off (too few bolts, full, a weapon not ammo): its 1 stays. */
    lay_vendor(0, 5000);
    open_dialog(0);
    CHECK(count() == 1);
    close_dialog();
    lay_vendor(0, 5000);
    put_s32(s_d->dialog + 0x100u + 0xD0u + 2u * 0x14u + 4u, 0);
    open_dialog(1);
    CHECK(count() == 1);
    close_dialog();

    /* A level loaded with the dialog's state already 2 (what the level's file holds): untouched. */
    lay_vendor(0, 5000);
    put_s32(s_d->count + 0x100u, 1);
    put_s32(s_d->can_raise + 0x100u, 1);
    put_s32(s_d->dialog + 0x100u + 0x5Cu, 2);
    s_level = 4;
    s_tick(s_tick_user);
    CHECK(count() == 1);
    /* No level loaded: nothing read. */
    s_level = -1;
    s_tick(s_tick_user);
    CHECK(count() == 1);
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
    CHECK(strcmp(info.id, "ammo-max") == 0 && strcmp(info.version, "1.0.0") == 0);

    s_memory = calloc(1, MEMORY_SIZE);
    CHECK(s_memory != NULL);
    openrac_mod* mod = (openrac_mod*)&info; /* any handle: the mod only passes it back */

    /* Another game, an unknown disc: refused, with a reason, before any callback. */
    openrac_api api = make_api("rac2", "ntsc");
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "rac1") != NULL && s_tick == NULL);
    api = make_api("rac1", "jp");
    s_failed[0] = 0;
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "jp") != NULL && s_tick == NULL);

    /* Each disc's vendor at its own addresses. */
    s_trace_lines = 0;
    play("pal", mod, load);
    CHECK(s_trace_lines > 0);
    play("ntsc", mod, load);

    free(s_memory);
    printf("ammo_max_load_test: ok\n");
    return 0;
}
