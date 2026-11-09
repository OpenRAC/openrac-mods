// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The multiplayer library as the port loads it in Going Commando, against a host table of the
// test's own (openrac_mod.h): it loads on rac2 with no hook point taken (Ratchet & Clank's are its
// own), its pages made, Multiplayer in the title's main menu under Load Game and in the pause menu,
// a frame callback given, and its frames run in the title with no party; a port without the play
// calls is refused, naming them, as is a game it is not for.
// Slots the table leaves empty are ones the mod must not call here: a call would crash the test.
//
// Run with the library's path: multiplayer_rac2_load_test <multiplayer.dylib|.so|.dll>.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

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

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

std::string g_failed;
std::vector<std::string> g_log;
std::vector<std::string> g_hooks;
int g_pages = 0;
std::vector<std::string> g_main_items, g_pause_items;
std::vector<std::uint8_t> g_memory(0x2000000, 0);
bool g_play = true;
int g_phase = OPENRAC_PLAY_FRONT_END;
std::vector<std::pair<openrac_frame_fn, void*>> g_frames;
std::map<std::string, std::string> g_strings =
    {{"server", "127.0.0.1:7770"},
     {"name", "Tester"},
     {"verbose_log", ""}};

void host_log(openrac_mod*, int, const char* text) {
    g_log.emplace_back(text);
}

void host_fail(openrac_mod*, const char* reason) {
    g_failed = reason;
}

int setting_bool(openrac_mod*, const char*) {
    return 0;
}

int64_t setting_int(openrac_mod*, const char*) {
    return 0;
}

double setting_float(openrac_mod*, const char*) {
    return 0.0;
}

const char* setting_string(openrac_mod*, const char* key) {
    const auto it = g_strings.find(key);
    return it != g_strings.end() ? it->second.c_str() : "";
}

int host_hook(openrac_mod*, const char* name, const char*, void*, void**) {
    g_hooks.emplace_back(name);
    return OPENRAC_HOOK_OK;
}

int host_on_frame(openrac_mod*, openrac_frame_fn fn, void* user) {
    g_frames.emplace_back(fn, user);
    return 0;
}

int host_play_status(openrac_play_status* out) {
    if (!g_play) {
        return OPENRAC_HOOK_UNKNOWN;
    }

    out->phase = g_phase;
    out->level = -1;
    out->levels = 27;
    out->loads = 1;
    return OPENRAC_HOOK_OK;
}

int host_scene_playing(int* frame) {
    if (frame != nullptr) {
        *frame = 0;
    }

    return -1;
}

int host_plugins_folder(char* out, uint32_t size) {
    std::snprintf(out, size, "%s", "/nonexistent-openrac-plugins");
    return static_cast<int>(std::strlen(out));
}

int host_mod_count() {
    return 0;
}

int host_unknown_text(char*, uint32_t) {
    return 0;
}

int host_packs_info(openrac_packs_info*) {
    return OPENRAC_HOOK_UNKNOWN;
}

int host_ui_page(openrac_mod*, uint32_t, openrac_ui_fn, void*) {
    ++g_pages;
    return 0;
}

int host_ui_main_item(openrac_mod*, const char* after, const char* label, int) {
    g_main_items.push_back(std::string(after) + ":" + label);
    return 0;
}

int host_ui_pause_item(openrac_mod*, const char* label, int) {
    g_pause_items.emplace_back(label);
    return 0;
}

int host_ui_set(openrac_mod*, int, const openrac_ui_content*) {
    return OPENRAC_HOOK_OK;
}

int host_ui_unknown(openrac_mod*, int) {
    return OPENRAC_HOOK_UNKNOWN;
}

int host_ui_sound(openrac_mod*, int) {
    return OPENRAC_HOOK_UNKNOWN;
}

int host_on_pad(openrac_mod*, openrac_pad_fn, void*) {
    return 0;
}

int host_on_moby_update(openrac_mod*, openrac_moby_update_fn, void*) {
    return 0;
}

int host_on_moby_hit(openrac_mod*, openrac_hit_fn, void*) {
    return 0;
}

int host_on_overlay(openrac_mod*, openrac_overlay_fn, void*) {
    return 0;
}

int host_on_camera_script(openrac_mod*, openrac_camera_script_fn, void*) {
    return 0;
}

int host_on_metal_detect(openrac_mod*, openrac_metal_detect_fn, void*) {
    return 0;
}

openrac_gaddr host_relocate(openrac_gaddr address) {
    return address;
}

openrac_gaddr host_relocate_level(int, openrac_gaddr address) {
    return address;
}

int host_loaded_level() {
    return -1;
}

int host_save_slot(int, openrac_save_info*) {
    return 0;
}

int host_save_slot2(const char*, int, openrac_save_info*) {
    return 0;
}

uint32_t host_card_saves() {
    return 0;
}

int host_card_note_read(const char*, int, char*, uint32_t) {
    return 0;
}

const char* host_level_name(int) {
    return "";
}

int host_sim_digest(int32_t, uint32_t*) {
    return OPENRAC_HOOK_UNKNOWN;
}

int host_bolts(int) {
    return 0;
}

int host_camera_position(float*) {
    return OPENRAC_HOOK_UNKNOWN;
}

int host_notice(openrac_mod*, const char* text) {
    g_log.emplace_back(std::string("notice: ") + text);
    return OPENRAC_HOOK_OK;
}

openrac_api make_api(const char* game) {
    openrac_api api;
    std::memset(&api, 0, sizeof api);
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = game;
    api.version = "ntsc";
    api.openrac_version = "test";
    api.platform = "test";
    api.log = host_log;
    api.fail = host_fail;
    api.setting_bool = setting_bool;
    api.setting_int = setting_int;
    api.setting_float = setting_float;
    api.setting_string = setting_string;
    api.hook = host_hook;
    api.on_frame = host_on_frame;
    api.play_status = host_play_status;
    api.scene_playing = host_scene_playing;
    api.plugins_folder = host_plugins_folder;
    api.mod_count = host_mod_count;
    api.packs_info = host_packs_info;
    api.world_family = host_unknown_text;
    api.identity_key = host_unknown_text;
    api.ui_page = host_ui_page;
    api.ui_main_item = host_ui_main_item;
    api.ui_pause_item = host_ui_pause_item;
    api.ui_set = host_ui_set;
    api.ui_open = host_ui_unknown;
    api.ui_sound = host_ui_sound;
    api.on_pad = host_on_pad;
    api.on_moby_update = host_on_moby_update;
    api.on_moby_hit = host_on_moby_hit;
    api.on_overlay = host_on_overlay;
    api.on_camera_script = host_on_camera_script;
    api.on_metal_detect = host_on_metal_detect;
    api.memory = g_memory.data();
    api.relocate_data = host_relocate;
    api.relocate_level_data = host_relocate_level;
    api.loaded_level = host_loaded_level;
    api.save_slot = host_save_slot;
    api.save_slot2 = host_save_slot2;
    api.card_saves = host_card_saves;
    api.card_note_read = host_card_note_read;
    api.level_name = host_level_name;
    api.sim_digest = host_sim_digest;
    api.bolts = host_bolts;
    api.camera_position = host_camera_position;
    api.play_notice = host_notice;
    return api;
}

}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    void* lib = open_library(argv[1]);
    CHECK(lib != nullptr);
    auto query = (openrac_mod_query_fn)symbol(lib, OPENRAC_MOD_QUERY);
    auto load = (openrac_mod_load_fn)symbol(lib, OPENRAC_MOD_LOAD);
    auto unload = (openrac_mod_unload_fn)symbol(lib, OPENRAC_MOD_UNLOAD);
    CHECK(query != nullptr && load != nullptr && unload != nullptr);
    openrac_mod_info info;
    std::memset(&info, 0, sizeof info);
    info.size = sizeof info;
    query(&info);
    CHECK(std::strcmp(info.id, "multiplayer") == 0);
    auto* handle = reinterpret_cast<openrac_mod*>(&info);

    // A game it is not for: refused, naming it, before anything is taken.
    openrac_api rac3 = make_api("rac3");
    CHECK(load(&rac3, handle) != 0 && g_failed.find("rac3") != std::string::npos);
    CHECK(g_hooks.empty() && g_frames.empty() && g_pages == 0 && g_main_items.empty());

    // Going Commando on a port without its play calls: refused, saying so.
    g_failed.clear();
    g_play = false;
    openrac_api old = make_api("rac2");
    CHECK(load(&old, handle) != 0 && g_failed.find("play calls") != std::string::npos);
    CHECK(g_hooks.empty() && g_frames.empty() && g_pages == 0 && g_main_items.empty());

    // Going Commando: loads, takes no hook point, makes its pages, Multiplayer under Load Game and
    // in the pause menu, and gives a frame callback.
    g_failed.clear();
    g_play = true;
    openrac_api api = make_api("rac2");
    CHECK(load(&api, handle) == 0 && g_failed.empty());
    CHECK(g_hooks.empty() && g_pages >= 10 && g_frames.size() == 1);
    CHECK(g_main_items.size() == 1 && g_main_items[0] == std::string(OPENRAC_UI_LOAD_GAME) + ":Multiplayer");
    CHECK(g_pause_items.size() == 1 && g_pause_items[0] == "Multiplayer");

    // Its frames in the title with no party: nothing on the banner, nothing asked of the game.
    for (int i = 0; i < 5; ++i) {
        g_frames[0].first(g_frames[0].second);
    }

    for (const std::string& line : g_log) {
        CHECK(line.rfind("notice: ", 0) != 0);
    }

    unload();
    std::puts("multiplayer rac2 load: all passed");
    return 0;
}
