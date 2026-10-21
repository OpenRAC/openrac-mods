/* SPDX-License-Identifier: GPL-3.0-or-later
 * The lock-strafe's library as the port loads it, against a host table made here (openrac_mod.h):
 * it reports its id and interface, refuses another game and an unknown disc, takes the six hook
 * points with their signatures and finds the game's functions by each disc's names; then, on a
 * game memory made here, its pad hook takes L2 out in play on foot and leaves it out of play, and
 * its turn hook turns toward the camera while locked, calling the original each time.
 *
 * Run with the library's path: load_test <lock_strafe.dylib|.so|.dll>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openrac/games/rac1.h"
#include "openrac/openrac_mod.h"

#include "../src/discs.h"
#include "../src/strafe.h"

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
static const char* s_disc = "pal";
static const char* s_setting_mode = "lock";

/* The hooks the mod took. */
static struct {
    const char* name;
    const char* signature;
    void* replacement;
} s_hooks[16];
static int s_hook_count;

/* What the hook points go on to: the game's, here counting calls. */
static int s_original_calls;
static float s_move_yaw_seen;
static void game_pad(openrac_gaddr pad, openrac_gaddr report, int32_t size) {
    (void)pad, (void)report, (void)size;
    ++s_original_calls;
}
static void game_void(void) {
    ++s_original_calls;
}
static void game_turn(int32_t mode, float accel, float damping, float limit) {
    (void)mode, (void)accel, (void)damping, (void)limit;
    memcpy(&s_move_yaw_seen, s_memory + k_lock_strafe_discs[0].hero + 0x180u, 4);
    ++s_original_calls;
}
static void game_float(float angle) {
    (void)angle;
    ++s_original_calls;
}
static void game_camera(openrac_gaddr camera) {
    (void)camera;
    ++s_original_calls;
}
static float game_cos(float a) {
    return a;
}
static void game_int(int32_t a) {
    (void)a;
}
static void game_two(float a, float b) {
    (void)a, (void)b;
}
static void game_look(openrac_gaddr d, float a, float b) {
    (void)d, (void)a, (void)b;
}

static void host_log(openrac_mod* mod, int level, const char* text) {
    (void)mod;
    printf("[%d] lock-strafe: %s\n", level, text);
}
static void host_fail(openrac_mod* mod, const char* reason) {
    (void)mod;
    snprintf(s_failed, sizeof s_failed, "%s", reason);
}
static int setting_bool(openrac_mod* mod, const char* key) {
    (void)mod, (void)key;
    return 0;
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
    return strcmp(key, "mode") == 0 ? s_setting_mode : "";
}
static void* original_for(const char* name) {
    if (strcmp(name, "Pad_ProcessReport") == 0) return (void*)game_pad;
    if (strcmp(name, "Hero_TurnToHeading") == 0) return (void*)game_turn;
    if (strcmp(name, "Hero_SetPlanarVelocity") == 0) return (void*)game_float;
    if (strcmp(name, "FollowCamera_ApplyOverrides") == 0) return (void*)game_camera;
    return (void*)game_void;
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
    (void)mod, (void)fn, (void)user;
    return 0;
}
static int host_on_pad(openrac_mod* mod, openrac_pad_fn fn, void* user) {
    (void)mod, (void)fn, (void)user;
    return 0;
}
/* The game's functions by name: only the disc's own names are found. */
static int s_functions_asked;
static void* host_function(const char* name) {
    const lock_strafe_disc* d = strcmp(s_disc, "pal") == 0 ? &k_lock_strafe_discs[0] : &k_lock_strafe_discs[1];
    ++s_functions_asked;
    if (strcmp(name, d->fast_cos) == 0 || strcmp(name, d->fast_sin) == 0) return (void*)game_cos;
    if (strcmp(name, d->camera_slide) == 0) return (void*)game_int;
    if (strcmp(name, d->camera_spring) == 0) return (void*)game_two;
    if (strcmp(name, d->camera_pan_off) == 0) return (void*)game_void;
    if (strcmp(name, d->camera_look_along) == 0) return (void*)game_look;
    return NULL;
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
    return s_level;
}
static openrac_gaddr s_sp = 0x001F0000u;
static openrac_gaddr stack_push(uint32_t size, openrac_gaddr* saved) {
    *saved = s_sp;
    s_sp -= (size + 15u) & ~15u;
    return s_sp;
}
static void stack_pop(openrac_gaddr saved) {
    s_sp = saved;
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

static void put_u32(openrac_gaddr a, uint32_t v) {
    memcpy(s_memory + a, &v, 4);
}
static uint32_t get_u32(openrac_gaddr a) {
    uint32_t v;
    memcpy(&v, s_memory + a, 4);
    return v;
}
static void put_f32(openrac_gaddr a, float v) {
    memcpy(s_memory + a, &v, 4);
}
static float get_f32(openrac_gaddr a) {
    float v;
    memcpy(&v, s_memory + a, 4);
    return v;
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
    CHECK(strcmp(info.id, "lock-strafe") == 0 && strcmp(info.version, "1.0.0") == 0);

    s_memory = calloc(1, MEMORY_SIZE);
    CHECK(s_memory != NULL);
    openrac_mod* mod = (openrac_mod*)&info; /* any handle: the mod only passes it back */

    /* Another game, an unknown disc: refused, with a reason, before any hook. */
    openrac_api api = make_api("rac2", "ntsc");
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "rac1") != NULL && s_hook_count == 0);
    api = make_api("rac1", "jp");
    s_failed[0] = 0;
    CHECK(load(&api, mod) != 0 && strstr(s_failed, "jp") != NULL && s_hook_count == 0);

    /* NTSC-U: its own function names (host_function knows only those). */
    s_disc = "ntsc";
    api = make_api("rac1", "ntsc");
    s_failed[0] = 0;
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0 && s_hook_count == 6);
    s_hook_count = 0;

    /* PAL: the six hook points with games/rac1.h's signatures. */
    s_disc = "pal";
    api = make_api("rac1", "pal");
    CHECK(load(&api, mod) == 0 && s_failed[0] == 0);
    CHECK(s_hook_count == 6);
    static const char* const k_points[][2] = {
        {OPENRAC_RAC1_HOOK_Pad_ProcessReport, OPENRAC_RAC1_SIG_Pad_ProcessReport},
        {OPENRAC_RAC1_HOOK_Hero_UpdateMotionInput, OPENRAC_RAC1_SIG_Hero_UpdateMotionInput},
        {OPENRAC_RAC1_HOOK_Hero_TurnToHeading, OPENRAC_RAC1_SIG_Hero_TurnToHeading},
        {OPENRAC_RAC1_HOOK_Hero_SetPlanarVelocity, OPENRAC_RAC1_SIG_Hero_SetPlanarVelocity},
        {OPENRAC_RAC1_HOOK_FollowCamera_ApplyOverrides, OPENRAC_RAC1_SIG_FollowCamera_ApplyOverrides},
        {OPENRAC_RAC1_HOOK_Hero_AdvanceAnimation, OPENRAC_RAC1_SIG_Hero_AdvanceAnimation},
    };
    for (int i = 0; i < 6; ++i) {
        CHECK(strcmp(s_hooks[i].name, k_points[i][0]) == 0 && strcmp(s_hooks[i].signature, k_points[i][1]) == 0);
    }

    /* The hero running on level ground in play, the Blaster ready, L2 held. */
    const lock_strafe_disc* d = &k_lock_strafe_discs[0];
    put_u32(d->hero + 0x2084u, RAC1_STRAFE_STATE_RUN);
    put_u32(d->hero + 0x1090u + 0x24u, RAC1_STRAFE_SLOT_READY);
    put_u32(d->hero + 0x1090u + 0x28u, 0x0F);
    put_u32(d->level_mode, 0);
    put_f32(d->camera_yaw, 1.25f);
    put_f32(d->hero + 0x180u, -0.5f);
    openrac_rac1_Pad_ProcessReport pad = (openrac_rac1_Pad_ProcessReport)hook_named("Pad_ProcessReport");
    openrac_rac1_Hero_TurnToHeading turn = (openrac_rac1_Hero_TurnToHeading)hook_named("Hero_TurnToHeading");
    CHECK(pad != NULL && turn != NULL);

    /* Out of play (no level): the game sees L2. */
    s_level = -1;
    put_u32(d->pad + 0x1A0u, RAC1_PAD_L2 | 0x4u);
    s_original_calls = 0;
    pad(d->pad, 0x00100000u, 32);
    CHECK(s_original_calls == 1 && get_u32(d->pad + 0x1A0u) == (RAC1_PAD_L2 | 0x4u));

    /* In play on foot: L2 taken out, L1 kept. */
    s_level = 0;
    pad(d->pad, 0x00100000u, 32);
    CHECK(s_original_calls == 2 && get_u32(d->pad + 0x1A0u) == 0x4u);

    /* Locked: the turn eases toward the camera's yaw, and the stick's move yaw is put back. */
    turn(0, 0.1f, 0.2f, 0.3f);
    CHECK(s_original_calls == 3 && s_move_yaw_seen == 1.25f && get_f32(d->hero + 0x180u) == -0.5f);
    /* A turn of another kind is the game's, untouched. */
    turn(1, 0.1f, 0.2f, 0.3f);
    CHECK(s_original_calls == 4 && s_move_yaw_seen == -0.5f);

    /* The Hold mode: with nothing latched and the wrench in hand, the game's turn. */
    s_setting_mode = "hold";
    s_hook_count = 0;
    CHECK(load(&api, mod) == 0);
    pad = (openrac_rac1_Pad_ProcessReport)hook_named("Pad_ProcessReport");
    turn = (openrac_rac1_Hero_TurnToHeading)hook_named("Hero_TurnToHeading");
    put_u32(d->hero + 0x1090u + 0x28u, 0x08);
    put_u32(d->pad + 0x1A0u, 0);
    pad(d->pad, 0x00100000u, 32);
    turn(0, 0.1f, 0.2f, 0.3f);
    CHECK(s_move_yaw_seen == -0.5f);

    free(s_memory);
    printf("load_test: ok\n");
    return 0;
}
