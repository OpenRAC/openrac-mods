// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The shoulder camera's library as the port loads it, against a host table made here
// (openrac_mod.h): it refuses another game and an unknown disc, and takes the follow camera's stick
// read with its signature, pad 0's reports and the overlay. Then, on a game memory laid out at the
// PAL disc's addresses (src/rac2/discs.hpp): the aim key held on foot holds L2 and brings the eye in
// to 0.6 of its distance along its view line over eight ticks, its goals with it, and draws the
// vignette and the crosshair; let go, the eye goes back out and the game's own goals are left
// alone; out of play, or swimming, the key does nothing; the camera distance scales the game's
// goal; the pad's L2 aims when its setting is on.
//
// Run with the library's path: load_test <shoulder_camera.dylib|.so|.dll>.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "openrac/openrac_mod.h"
#include "rac2/discs.hpp"

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

// Stops the test at the first failed condition and says where. It is a macro for the line number
// and the text of the condition, which a function cannot see.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

using namespace shoulder_camera::rac2;

namespace {

// The test's game memory, and where it puts the follow camera and its data.
std::vector<std::uint8_t> g_memory(32u << 20);
constexpr std::uint32_t kCamera = 0x00150000u;
constexpr std::uint32_t kData = 0x00160000u;

// The PAL disc's addresses, as the mod reads them.
const Disc& kPal = kDiscs[1];

// The key the test binds to the aim: G (its HID usage).
constexpr int kKeyG = 10;
// The pad report's L2 (bit 8, clear while held) and its pressure byte.
constexpr std::uint16_t kL2 = 1u << 8;
constexpr int kL2Pressure = 16;

std::string g_failed;
std::string g_game = "rac2";
std::string g_version = "pal";
std::map<std::string, std::string> g_settings;

// The hook the mod took: its name, signature and replacement; what the game's read does (nothing).
std::string g_hook_name;
std::string g_hook_signature;
void (*g_read_stick)(std::uint32_t, std::uint32_t, std::uint32_t) = nullptr;
int g_original_calls = 0;

openrac_pad_fn g_pad = nullptr;
void* g_pad_user = nullptr;
openrac_overlay_fn g_overlay = nullptr;
void* g_overlay_user = nullptr;
openrac_frame_fn g_settings_changed = nullptr;
void* g_settings_user = nullptr;
openrac_input g_input{};

// What the overlay drew: how many rectangles, and the nearest one's middle to the projected point.
int g_rects = 0;
float g_nearest = 1e9f;

void set_float(std::uint32_t a, float v) {
    std::memcpy(g_memory.data() + a, &v, sizeof v);
}

float mem_float(std::uint32_t a) {
    float v = 0.0f;
    std::memcpy(&v, g_memory.data() + a, sizeof v);
    return v;
}

void set_word(std::uint32_t a, std::uint32_t v) {
    std::memcpy(g_memory.data() + a, &v, sizeof v);
}

const std::string* setting(const char* key) {
    const auto it = g_settings.find(key);
    return it == g_settings.end() ? nullptr : &it->second;
}

void host_log(openrac_mod*, int, const char*) {}

void host_fail(openrac_mod*, const char* reason) {
    g_failed = reason;
}

int setting_bool(openrac_mod*, const char* key) {
    if (const std::string* s = setting(key)) {
        return *s == "true" ? 1 : 0;
    }

    return std::strcmp(key, "crosshair") == 0 || std::strcmp(key, "vignette") == 0 ? 1 : 0;
}

std::int64_t setting_int(openrac_mod*, const char*) {
    return 0;
}

double setting_float(openrac_mod*, const char* key) {
    if (const std::string* s = setting(key)) {
        return std::stod(*s);
    }

    return std::strcmp(key, "shoulder_distance") == 0 ? 0.6 : 1.0;
}

const char* setting_string(openrac_mod*, const char* key) {
    const std::string* s = setting(key);
    return s != nullptr ? s->c_str() : "";
}

void game_read_stick(std::uint32_t, std::uint32_t, std::uint32_t) {
    ++g_original_calls;
}

int host_hook(
    openrac_mod*, const char* name, const char* signature, void* replacement, void** original
) {
    g_hook_name = name;
    g_hook_signature = signature;
    g_read_stick =
        reinterpret_cast<void (*)(std::uint32_t, std::uint32_t, std::uint32_t)>(replacement);
    *original = reinterpret_cast<void*>(&game_read_stick);
    return OPENRAC_HOOK_OK;
}

int host_on_pad(openrac_mod*, openrac_pad_fn fn, void* user) {
    g_pad = fn;
    g_pad_user = user;
    return 0;
}

int host_on_overlay(openrac_mod*, openrac_overlay_fn fn, void* user) {
    g_overlay = fn;
    g_overlay_user = user;
    return OPENRAC_HOOK_OK;
}

int host_on_settings(openrac_mod*, openrac_frame_fn fn, void* user) {
    g_settings_changed = fn;
    g_settings_user = user;
    return OPENRAC_HOOK_OK;
}

int host_play_status(openrac_play_status*) {
    return OPENRAC_HOOK_UNKNOWN;
}

openrac_gaddr host_relocate(openrac_gaddr a) {
    return a;
}

const openrac_input* host_input() {
    return &g_input;
}

openrac_api make_api() {
    openrac_api api{};
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = g_game.c_str();
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
    api.on_pad = host_on_pad;
    api.on_overlay = host_on_overlay;
    api.on_settings = host_on_settings;
    api.play_status = host_play_status;
    api.relocate_data = host_relocate;
    api.input = host_input;
    return api;
}

// The overlay drawn on a picture of the test's own, where every point projects to (300, 200).
void draw_overlay() {
    g_rects = 0;
    g_nearest = 1e9f;
    openrac_overlay o{};
    o.size = sizeof o;
    o.project = [](void*, const float[3], float* sx, float* sy) {
        *sx = 300.0f;
        *sy = 200.0f;
        return 1;
    };
    o.rect = [](void*, float x, float y, float w, float h, std::uint32_t) {
        ++g_rects;
        g_nearest = std::fmin(g_nearest, std::hypot(x + w * 0.5f - 300.0f, y + h * 0.5f - 200.0f));
    };
    g_overlay(g_overlay_user, &o);
}

// One tick: the pad report (every button up, unless `l2`), with G held or not, then the follow
// camera's read with the game's goals set as the level sets them each tick. Returns whether the
// report the game reads holds L2.
bool tick(bool g, bool pressed = false, bool l2 = false) {
    g_input.tick += 1;
    g_input.keys[kKeyG] = static_cast<std::uint8_t>(
        (g ? OPENRAC_KEY_HELD : 0u) | (pressed ? OPENRAC_KEY_PRESSED : 0u)
    );
    std::uint8_t report[18] = {0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80};

    if (l2) {
        report[1] = static_cast<std::uint8_t>(report[1] & ~(kL2 >> 8));
    }

    g_pad(g_pad_user, 0, report, static_cast<int>(sizeof report));
    set_float(kData + kDistanceGoal, 10.0f);
    set_float(kData + kHeightGoal, 4.0f);
    g_read_stick(kCamera, 0x00170000u, 0x00170004u);
    const auto buttons = static_cast<std::uint16_t>(report[0] | (report[1] << 8));
    return (buttons & kL2) == 0 && (l2 || report[kL2Pressure] == 0xFF);
}

// The PAL disc's play on foot: the game playing, a hero with health standing, the follow camera's
// data with its eye 10 away and 4 up, and the view 0.07 below level.
void play_on_foot() {
    std::fill(g_memory.begin(), g_memory.end(), 0);
    set_word(kPal.game_state, 0);
    set_word(kPal.hero_moby, 0x00180000u);
    set_word(kPal.health, 8);
    set_word(kPal.hero_state, 0);
    set_word(kCamera + kCameraData, kData);
    set_float(kData + kDistance, 10.0f);
    set_float(kData + kHeight, 4.0f);
    set_float(kPal.camera_matrix + kForwardZ, std::sin(-0.07f));
}

int load(void* lib) {
    auto entry = reinterpret_cast<openrac_mod_load_fn>(symbol(lib, OPENRAC_MOD_LOAD));
    static openrac_api api;
    api = make_api();
    g_failed.clear();
    return entry(&api, reinterpret_cast<openrac_mod*>(1));
}

void run(const char* library) {
    void* lib = open_library(library);
    CHECK(lib != nullptr);
    auto query = reinterpret_cast<openrac_mod_query_fn>(symbol(lib, OPENRAC_MOD_QUERY));
    auto unload = reinterpret_cast<openrac_mod_unload_fn>(symbol(lib, OPENRAC_MOD_UNLOAD));
    CHECK(query != nullptr);
    openrac_mod_info info{};
    info.size = sizeof info;
    query(&info);
    CHECK(std::strcmp(info.id, "shoulder-camera") == 0 && std::strcmp(info.version, "1.0.0") == 0);

    // Another game, an unknown disc: refused, with a reason.
    g_game = "rac1";
    CHECK(load(lib) != 0 && g_failed.find("Going Commando") != std::string::npos);
    g_game = "rac2";
    g_version = "ntsc-pro";
    CHECK(load(lib) != 0 && g_failed.find("ntsc-pro") != std::string::npos);
    g_version = "pal";

    // The PAL disc with G bound, the window focused: the stick read with its signature, the pad,
    // the overlay.
    g_input.size = sizeof g_input;
    g_input.focused = 1;
    g_settings = {{"aim", "KeyG"}};
    CHECK(load(lib) == 0 && g_failed.empty());
    CHECK(
        g_hook_name == "rac2.camera.follow.read_stick" && g_hook_signature == "void(u32,u32,u32)"
    );
    CHECK(g_pad != nullptr && g_overlay != nullptr && g_settings_changed != nullptr);

    // G held on foot: L2 held; over eight ticks the eye comes in to 6 and lower by the pitch,
    // its goals with it; the game's read runs each tick.
    play_on_foot();
    CHECK(!tick(false));
    CHECK(tick(true, true));
    const int calls = g_original_calls;

    for (int i = 0; i < 8; ++i) {
        CHECK(tick(true));
    }

    CHECK(g_original_calls == calls + 8);
    const float height = 4.0f + std::tan(-0.07f) * 4.0f;
    CHECK(std::fabs(mem_float(kData + kDistance) - 6.0f) < 1e-3f);
    CHECK(std::fabs(mem_float(kData + kDistanceGoal) - 6.0f) < 1e-3f);
    CHECK(std::fabs(mem_float(kData + kHeight) - height) < 1e-3f);
    CHECK(std::fabs(mem_float(kData + kHeightGoal) - height) < 1e-3f);

    // The vignette's 48 bands and the crosshair's ten bars, about the projected point.
    draw_overlay();
    CHECK(g_rects == 4 * 12 + 10 && g_nearest < 5.0f);

    // Let go: L2 up, the look goes over eight ticks, then the game's goals are left as it set them.
    CHECK(!tick(false));

    for (int i = 0; i < 8; ++i) {
        tick(false);
    }

    tick(false);
    CHECK(mem_float(kData + kDistanceGoal) == 10.0f && mem_float(kData + kHeightGoal) == 4.0f);
    draw_overlay();
    CHECK(g_rects == 0);

    // Paused (the game's state not play), or swimming: no aim.
    set_word(kPal.game_state, 3);
    CHECK(!tick(true, true));
    set_word(kPal.game_state, 0);
    set_word(kPal.hero_state, 0x34);
    CHECK(!tick(true, true));
    set_word(kPal.hero_state, 0);

    // The camera's distance at 0.8: the game's goal of 10 becomes 8.
    g_settings = {{"aim", "KeyG"}, {"camera_distance", "0.8"}};
    g_settings_changed(g_settings_user);
    tick(false);
    CHECK(std::fabs(mem_float(kData + kDistanceGoal) - 8.0f) < 1e-4f);

    // Toggled, at 0.5: a press aims, it stays without the key, another press stops it.
    g_settings = {{"aim", "KeyG"}, {"aim_toggle", "true"}, {"shoulder_distance", "0.5"}};
    g_settings_changed(g_settings_user);
    set_float(kData + kDistance, 10.0f);
    CHECK(tick(true, true));

    for (int i = 0; i < 8; ++i) {
        CHECK(tick(false));
    }

    CHECK(std::fabs(mem_float(kData + kDistance) - 5.0f) < 1e-3f);
    CHECK(tick(true, true) == false);

    // No key, the pad's L2 on: L2 held on the pad brings the camera in too.
    g_settings = {{"pad_l2", "true"}};
    g_settings_changed(g_settings_user);

    for (int i = 0; i < 10; ++i) {
        tick(false);
    }

    set_float(kData + kDistance, 10.0f);

    for (int i = 0; i < 9; ++i) {
        CHECK(tick(false, false, true));
    }

    CHECK(std::fabs(mem_float(kData + kDistance) - 6.0f) < 1e-3f);

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
    std::puts("shoulder-camera load: all passed");
    return 0;
}
