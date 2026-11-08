// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The multiplayer library as the port loads it in Going Commando, against a host table of the
// test's own (openrac_mod.h): it loads on rac2 with no hook point taken (neither game's) and no page
// or menu item made (rac2's title has none), a frame callback given, and its frames run in the title
// with no party; a port without the play calls is refused, naming them, as is a game it is not for.
// Slots the table leaves empty are ones the mod must not call here: a call would crash the test.
//
// Run with the library's path: multiplayer_rac2_load_test <multiplayer.dylib|.so|.dll>.
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
bool g_play = true;
int g_phase = OPENRAC_PLAY_FRONT_END;
std::vector<std::pair<openrac_frame_fn, void*>> g_frames;
std::map<std::string, std::string> g_strings =
    {{"server", "127.0.0.1:7770"},
     {"name", "Tester"},
     {"party", "off"},
     {"code", ""},
     {"address", ""}};

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

int host_ui_main_item(openrac_mod*, const char*, const char*, int) {
    ++g_pages;
    return 0;
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
    CHECK(g_hooks.empty() && g_frames.empty() && g_pages == 0);

    // Going Commando on a port without its play calls: refused, saying so.
    g_failed.clear();
    g_play = false;
    openrac_api old = make_api("rac2");
    CHECK(load(&old, handle) != 0 && g_failed.find("play calls") != std::string::npos);
    CHECK(g_hooks.empty() && g_frames.empty() && g_pages == 0);

    // Going Commando: loads, takes no hook point and makes no page, gives a frame callback.
    g_failed.clear();
    g_play = true;
    openrac_api api = make_api("rac2");
    CHECK(load(&api, handle) == 0 && g_failed.empty());
    CHECK(g_hooks.empty() && g_pages == 0 && g_frames.size() == 1);

    // Its frames in the title with no party: nothing on the banner, nothing asked of the game.
    for (int i = 0; i < 5; ++i) {
        g_frames[0].first(g_frames[0].second);
    }

    bool said_rac2 = false;

    for (const std::string& line : g_log) {
        said_rac2 = said_rac2 || line.find("Going Commando") != std::string::npos;
        CHECK(line.rfind("notice: ", 0) != 0);
    }

    CHECK(said_rac2);
    unload();
    std::puts("multiplayer rac2 load: all passed");
    return 0;
}
