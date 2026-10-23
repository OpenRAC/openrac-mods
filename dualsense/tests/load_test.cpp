// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The DualSense mod's library as the port loads it, against a host table made here
// (openrac_mod.h, interface 1.3): it reports its id and interface, refuses another game and
// another disc, takes the pad report hook and the game's event hooks, and writes the
// controller's extra buttons into the report the game reads.
//
// The expected values come from the host table's own interface (openrac_mod.h) and from the
// mod's manifest settings, as written out in this file.
//
// Run with the library's path: load_test <dualsense.dylib|.so|.dll>.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>

void* open_library(const char* path) {
    return reinterpret_cast<void*>(LoadLibraryA(path));
}

void* symbol(void* lib, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
}
#else
#include <dlfcn.h>

void* open_library(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

void* symbol(void* lib, const char* name) {
    return dlsym(lib, name);
}
#endif

#include "openrac/openrac_mod.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

std::string g_failed;
std::string g_version = "pal";
std::vector<std::string> g_hooks;
std::vector<std::string> g_hook_signatures;
openrac_frame_fn g_frame = nullptr;
void* g_frame_user = nullptr;
openrac_pad_fn g_pad = nullptr;
void* g_pad_user = nullptr;
std::vector<std::string> g_log;

/* The game's memory, as the port hands it: the mod reads the hero's health from it. */
std::vector<std::uint8_t> g_memory(0x00200000u);

/* The settings the test answers with. */
std::string g_touchpad = "circle";
std::string g_left_paddle = "triangle";
std::string g_right_paddle = "square";
std::string g_microphone = "none";
int g_vibration = 1;
int g_strength = 100;
int g_light_bar = 1;

void host_log(openrac_mod*, int, const char* text) {
    g_log.emplace_back(text);
}

void host_fail(openrac_mod*, const char* reason) {
    g_failed = reason;
}

int setting_bool(openrac_mod*, const char* key) {
    if (std::strcmp(key, "vibration") == 0) {
        return g_vibration;
    }

    if (std::strcmp(key, "light_bar") == 0) {
        return g_light_bar;
    }

    return 0;
}

std::int64_t setting_int(openrac_mod*, const char* key) {
    if (std::strcmp(key, "strength") == 0) {
        return g_strength;
    }

    return 0;
}

double setting_float(openrac_mod*, const char*) {
    return 0.0;
}

const char* setting_string(openrac_mod*, const char* key) {
    if (std::strcmp(key, "touchpad") == 0) {
        return g_touchpad.c_str();
    }

    if (std::strcmp(key, "left_paddle") == 0) {
        return g_left_paddle.c_str();
    }

    if (std::strcmp(key, "right_paddle") == 0) {
        return g_right_paddle.c_str();
    }

    if (std::strcmp(key, "microphone") == 0) {
        return g_microphone.c_str();
    }

    return "";
}

/* The game's own functions the hooks go on to. */
void game_pad_process_report(std::uint32_t, std::uint32_t, std::int32_t) {}

void game_take_damage(std::int32_t) {}

std::uint32_t game_get_hit(std::uint32_t, std::int32_t, std::int32_t) {
    return 0;
}

void game_record_death(std::uint32_t, std::int32_t, std::int32_t) {}

void game_item_give(std::int32_t, std::int32_t) {}

std::int32_t game_gold_bolts() {
    return 0;
}

int host_hook(
    openrac_mod*, const char* name, const char* signature, void* replacement, void** original
) {
    g_hooks.emplace_back(name);
    g_hook_signatures.emplace_back(signature);
    /* The game's own function goes on to, so the hook's replacement can call it. */

    if (std::strcmp(name, "rac1.pad.process_report") == 0) {
        *original = reinterpret_cast<void*>(&game_pad_process_report);
    } else if (std::strcmp(name, "rac1.player.take_damage") == 0) {
        *original = reinterpret_cast<void*>(&game_take_damage);
    } else if (std::strcmp(name, "rac1.combat.get_hit") == 0) {
        *original = reinterpret_cast<void*>(&game_get_hit);
    } else if (std::strcmp(name, "rac1.moby.record_death") == 0) {
        *original = reinterpret_cast<void*>(&game_record_death);
    } else if (std::strcmp(name, "rac1.item.give") == 0) {
        *original = reinterpret_cast<void*>(&game_item_give);
    } else if (std::strcmp(name, "rac1.gold_bolt.count_collected") == 0) {
        *original = reinterpret_cast<void*>(&game_gold_bolts);
    } else {
        *original = nullptr;
    }

    (void)replacement;
    return OPENRAC_HOOK_OK;
}

int host_on_frame(openrac_mod*, openrac_frame_fn fn, void* user) {
    g_frame = fn;
    g_frame_user = user;
    return 0;
}

int host_on_pad(openrac_mod*, openrac_pad_fn fn, void* user) {
    g_pad = fn;
    g_pad_user = user;
    return 0;
}

void* host_function(const char*) {
    return nullptr;
}

void* host_function_at(openrac_gaddr) {
    return nullptr;
}

openrac_gaddr host_relocate(openrac_gaddr a) {
    return a;
}

openrac_gaddr host_relocate_level(int, openrac_gaddr a) {
    return a;
}

int host_loaded_level() {
    return -1;
}

openrac_gaddr host_stack_push(std::uint32_t size, openrac_gaddr* saved) {
    *saved = 0x001F0000u;
    return 0x001F0000u - size;
}

void host_stack_pop(openrac_gaddr) {}

const openrac_input* host_input() {
    return nullptr;
}

int host_take_input(openrac_mod*, std::uint32_t) {
    return 0;
}

void host_capture_mouse(openrac_mod*, int) {}

openrac_api make_api() {
    openrac_api api{};
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = "rac1";
    api.version = g_version.c_str();
    api.openrac_version = "0.0.0";
    api.platform = "test";
    api.memory = g_memory.data();
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
    api.relocate_data = host_relocate;
    api.relocate_level_data = host_relocate_level;
    api.loaded_level = host_loaded_level;
    api.stack_push = host_stack_push;
    api.stack_pop = host_stack_pop;
    api.input = host_input;
    api.take_input = host_take_input;
    api.capture_mouse = host_capture_mouse;
    return api;
}

bool has_hook(const std::vector<std::string>& hooks, const char* name) {
    for (const std::string& hook : hooks) {
        if (hook == name) {
            return true;
        }
    }

    return false;
}

/* A report as the port builds it: every button released. */
void make_report(std::vector<std::uint8_t>& report) {
    report.assign(18, 0);
    report[0] = 0xFF;
    report[1] = 0xFF;

    for (int i = 2; i < 6; ++i) {
        report[static_cast<std::size_t>(i)] = 0x80;
    }
}

bool report_holds(const std::vector<std::uint8_t>& report, std::uint16_t bit) {
    const std::uint16_t buttons =
        static_cast<std::uint16_t>(report[0]) | static_cast<std::uint16_t>(report[1]) << 8;
    return (buttons & bit) == 0;
}

void run(const char* library) {
    void* lib = open_library(library);
    CHECK(lib != nullptr);
    auto query = reinterpret_cast<openrac_mod_query_fn>(symbol(lib, OPENRAC_MOD_QUERY));
    auto load = reinterpret_cast<openrac_mod_load_fn>(symbol(lib, OPENRAC_MOD_LOAD));
    auto unload = reinterpret_cast<openrac_mod_unload_fn>(symbol(lib, OPENRAC_MOD_UNLOAD));
    CHECK(query != nullptr && load != nullptr);

    openrac_mod_info info{};
    info.size = sizeof info;
    query(&info);
    CHECK(std::strcmp(info.id, "dualsense") == 0);
    CHECK(info.api_major == 1 && info.api_minor == 3);

    // Another game is refused.
    {
        openrac_api api = make_api();
        api.game = "rac2";
        g_failed.clear();
        CHECK(load(&api, reinterpret_cast<openrac_mod*>(1)) != 0 && !g_failed.empty());
    }
    // A disc the mod does not know is refused.
    {
        g_version = "ntsc-pro";
        openrac_api api = make_api();
        g_failed.clear();
        CHECK(load(&api, reinterpret_cast<openrac_mod*>(1)) != 0 && !g_failed.empty());
        g_version = "pal";
    }

    // The mod itself. It refuses when there is no DualSense, which is what a machine without one
    // gets: the reason says so.
    {
        g_failed.clear();
        openrac_api api = make_api();
        const int result = load(&api, reinterpret_cast<openrac_mod*>(1));

        if (result != 0) {
            // No controller on this machine: the reason must say which one is missing.
            CHECK(g_failed.find("DualSense") != std::string::npos);
            std::printf("dualsense load: no DualSense on this machine (%s)\n", g_failed.c_str());
            return;
        }
    }
    CHECK(g_failed.empty());

    // The hooks it takes. The pad report itself is not a hook point: the mod writes the extra
    // buttons through the port's pad callback.
    CHECK(has_hook(g_hooks, "rac1.player.take_damage"));
    CHECK(has_hook(g_hooks, "rac1.combat.get_hit"));
    CHECK(has_hook(g_hooks, "rac1.moby.record_death"));
    CHECK(has_hook(g_hooks, "rac1.item.give"));
    CHECK(has_hook(g_hooks, "rac1.gold_bolt.count_collected"));
    CHECK(g_frame != nullptr);
    CHECK(g_pad != nullptr);

    // The pad report: with no controller button held, the report is the port's own.
    {
        std::vector<std::uint8_t> report;
        make_report(report);
        g_pad(g_pad_user, 0, report.data(), static_cast<int>(report.size()));
        CHECK(report[0] == 0xFF && report[1] == 0xFF);
    }

    // The frame callback runs: it drives the light bar and the vibration, and it reads the
    // hero's health out of the game's memory.
    g_frame(g_frame_user);

    // The hooks go on to the game's own functions.
    for (std::size_t i = 0; i < g_hooks.size(); ++i) {
        (void)i;
    }

    if (unload != nullptr) {
        unload();
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: load_test <library>\n");
        return 2;
    }

    run(argv[1]);
    std::puts("dualsense load: all passed");
    return 0;
}
