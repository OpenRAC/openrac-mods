// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The multiplayer library as the port loads it in Going Commando, against a host table of the
// test's own (openrac_mod.h): it loads on rac2 with no hook point taken (Ratchet & Clank's are its
// own), its pages made, Multiplayer in the title's main menu under Load Game and in the pause menu,
// a frame callback given, and its frames run in the title with no party; a port without the play
// calls is refused, naming them, as is a game it is not for. It loads again with the launcher's
// server a hub (OPENRAC_SERVER) and a region set, and with a server setting that is not an address
// (said, and the launcher's used).
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
#else
#include <dlfcn.h>
#endif

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

// Opens the library at `path`; null when it cannot be loaded.
void* open_library(const char* path) {
#if defined(_WIN32)
    return (void*)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

// Looks up `name` in the library `lib`; null when it has no such symbol.
void* symbol(void* lib, const char* name) {
#if defined(_WIN32)
    return (void*)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}

// The reason the mod gave for failing to load.
std::string g_failed;
// The lines the mod logged, and its notices with a prefix.
std::vector<std::string> g_log;
// The hook points the mod took.
std::vector<std::string> g_hooks;
// How many pages the mod made.
int g_pages = 0;
// The main menu items as "after:label", and the pause menu's labels.
std::vector<std::string> g_main_items, g_pause_items;
// The game's memory as the table gives it: 32 MB of zeros.
std::vector<std::uint8_t> g_memory(0x2000000, 0);
// Whether this port offers the play calls.
bool g_play = true;
// The phase the play status reports.
int g_phase = OPENRAC_PLAY_FRONT_END;
// The frame callbacks the mod gave, with their user data.
std::vector<std::pair<openrac_frame_fn, void*>> g_frames;
// The mod's string settings by key.
std::map<std::string, std::string> g_strings =
    {{"server", "127.0.0.1:7770"}, {"name", "Tester"}, {"region", ""}, {"verbose_log", ""}};

// Sets the variable `name` to `value` for the mod's getenv.
void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

// Forgets what an earlier load recorded, for the next.
void reset_records() {
    g_failed.clear();
    g_log.clear();
    g_hooks.clear();
    g_pages = 0;
    g_main_items.clear();
    g_pause_items.clear();
    g_frames.clear();
}

// Keeps a line the mod logs.
void host_log(openrac_mod*, int, const char* text) {
    g_log.emplace_back(text);
}

// Keeps the reason a mod load failed.
void host_fail(openrac_mod*, const char* reason) {
    g_failed = reason;
}

// Returns false for every boolean setting.
int setting_bool(openrac_mod*, const char*) {
    return 0;
}

// Returns 0 for every integer setting.
int64_t setting_int(openrac_mod*, const char*) {
    return 0;
}

// Returns 0 for every float setting.
double setting_float(openrac_mod*, const char*) {
    return 0.0;
}

// Returns the string set for a key, or an empty string.
const char* setting_string(openrac_mod*, const char* key) {
    const auto it = g_strings.find(key);
    return it != g_strings.end() ? it->second.c_str() : "";
}

// Records a hook point the mod takes and accepts it.
int host_hook(openrac_mod*, const char* name, const char*, void*, void**) {
    g_hooks.emplace_back(name);
    return OPENRAC_HOOK_OK;
}

// Keeps a frame callback the mod registers.
int host_on_frame(openrac_mod*, openrac_frame_fn fn, void* user) {
    g_frames.emplace_back(fn, user);
    return 0;
}

// Reports the phase in `g_phase` and no level, or an unknown call when `g_play` is off.
int host_play_status(openrac_play_status* out) {
    // A port without the play calls answers that the call is unknown.
    if (!g_play) {
        return OPENRAC_HOOK_UNKNOWN;
    }

    out->phase = g_phase;
    out->level = -1;
    out->levels = 27;
    out->loads = 1;
    return OPENRAC_HOOK_OK;
}

// Reports that no scene plays.
int host_scene_playing(int* frame) {
    // The caller may pass no frame slot.
    if (frame != nullptr) {
        *frame = 0;
    }

    return -1;
}

// Writes a plugins folder path that does not exist.
int host_plugins_folder(char* out, uint32_t size) {
    std::snprintf(out, size, "%s", "/nonexistent-openrac-plugins");
    return static_cast<int>(std::strlen(out));
}

// Reports no other mods.
int host_mod_count() {
    return 0;
}

// Returns no text for the world family and the identity key.
int host_unknown_text(char*, uint32_t) {
    return 0;
}

// Reports that the packs' information is unknown.
int host_packs_info(openrac_packs_info*) {
    return OPENRAC_HOOK_UNKNOWN;
}

// Counts a page the mod makes.
int host_ui_page(openrac_mod*, uint32_t, openrac_ui_fn, void*) {
    ++g_pages;
    return 0;
}

// Records a main menu item with the item it follows.
int host_ui_main_item(openrac_mod*, const char* after, const char* label, int) {
    g_main_items.push_back(std::string(after) + ":" + label);
    return 0;
}

// Records a pause menu item.
int host_ui_pause_item(openrac_mod*, const char* label, int) {
    g_pause_items.emplace_back(label);
    return 0;
}

// Accepts a page's content.
int host_ui_set(openrac_mod*, int, const openrac_ui_content*) {
    return OPENRAC_HOOK_OK;
}

// Reports the call as unknown.
int host_ui_unknown(openrac_mod*, int) {
    return OPENRAC_HOOK_UNKNOWN;
}

// Reports the sound call as unknown.
int host_ui_sound(openrac_mod*, int) {
    return OPENRAC_HOOK_UNKNOWN;
}

// Accepts a pad callback and drops it.
int host_on_pad(openrac_mod*, openrac_pad_fn, void*) {
    return 0;
}

// Accepts a moby update callback and drops it.
int host_on_moby_update(openrac_mod*, openrac_moby_update_fn, void*) {
    return 0;
}

// Accepts a moby hit callback and drops it.
int host_on_moby_hit(openrac_mod*, openrac_hit_fn, void*) {
    return 0;
}

// Accepts an overlay callback and drops it.
int host_on_overlay(openrac_mod*, openrac_overlay_fn, void*) {
    return 0;
}

// Accepts a camera script callback and drops it.
int host_on_camera_script(openrac_mod*, openrac_camera_script_fn, void*) {
    return 0;
}

// Accepts a metal detection callback and drops it.
int host_on_metal_detect(openrac_mod*, openrac_metal_detect_fn, void*) {
    return 0;
}

// Returns the address unchanged.
openrac_gaddr host_relocate(openrac_gaddr address) {
    return address;
}

// Returns the address unchanged, whatever the level.
openrac_gaddr host_relocate_level(int, openrac_gaddr address) {
    return address;
}

// Reports that no level is loaded.
int host_loaded_level() {
    return -1;
}

// Returns 0 for every save slot.
int host_save_slot(int, openrac_save_info*) {
    return 0;
}

// Returns 0 for every named card's save slot.
int host_save_slot2(const char*, int, openrac_save_info*) {
    return 0;
}

// Returns 0 saves on the card.
uint32_t host_card_saves() {
    return 0;
}

// Returns 0 for every card note.
int host_card_note_read(const char*, int, char*, uint32_t) {
    return 0;
}

// Returns an empty level name.
const char* host_level_name(int) {
    return "";
}

// Reports that the sim digest is unknown.
int host_sim_digest(int32_t, uint32_t*) {
    return OPENRAC_HOOK_UNKNOWN;
}

// Returns 0 bolts.
int host_bolts(int) {
    return 0;
}

// Reports that the camera position is unknown.
int host_camera_position(float*) {
    return OPENRAC_HOOK_UNKNOWN;
}

// Logs a play notice with a prefix so the test can find it.
int host_notice(openrac_mod*, const char* text) {
    g_log.emplace_back(std::string("notice: ") + text);
    return OPENRAC_HOOK_OK;
}

// Returns a host table for `game` with the calls above set, and the slots the mod must not call
// left empty.
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
    CHECK(
        g_main_items.size() == 1
        && g_main_items[0] == std::string(OPENRAC_UI_LOAD_GAME) + ":Multiplayer"
    );
    CHECK(g_pause_items.size() == 1 && g_pause_items[0] == "Multiplayer");

    // Its frames in the title with no party: nothing on the banner, nothing asked of the game.
    for (int i = 0; i < 5; ++i) {
        g_frames[0].first(g_frames[0].second);
    }

    // No frame may have put a notice on the banner while there is no party.
    for (const std::string& line : g_log) {
        CHECK(line.rfind("notice: ", 0) != 0);
    }

    unload();

    // The launcher's server a hub (OPENRAC_SERVER, the mod's own setting empty), a region set:
    // loads as before, with no word about the address. A connection opens only with the pages.
    g_strings["server"] = "";
    g_strings["region"] = "Europe";
    set_env("OPENRAC_SERVER", "127.0.0.1:7177");
    reset_records();
    openrac_api hub = make_api("rac2");
    CHECK(load(&hub, handle) == 0 && g_failed.empty() && g_frames.size() == 1);

    // Its frames in the title, as before.
    for (int i = 0; i < 5; ++i) {
        g_frames[0].first(g_frames[0].second);
    }

    // Nothing said about the address.
    for (const std::string& line : g_log) {
        CHECK(line.find("is not host:port") == std::string::npos);
    }

    unload();

    // The mod's own setting not an address: said once, and the launcher's hub used instead.
    g_strings["server"] = ":7770";
    reset_records();
    openrac_api direct = make_api("rac2");
    CHECK(load(&direct, handle) == 0 && g_failed.empty());
    bool said = false;

    // The warning names the address it takes.
    for (const std::string& line : g_log) {
        said = said || line.find("using 127.0.0.1:7177") != std::string::npos;
    }

    CHECK(said);
    unload();
    std::puts("multiplayer rac2 load: all passed");
    return 0;
}
