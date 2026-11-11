// SPDX-License-Identifier: GPL-3.0-or-later
// The cheats mod's library as the port loads it in Ratchet & Clank, against a host table and game
// memory made here (openrac_mod.h): it reports its id and interface, refuses another game and an
// unknown disc, takes its three hook points with their signatures on both discs and adds the pause
// menu's Cheats page; with every cheat off each hook calls the game's function as it was called;
// with them on (from the settings or the page) hits take nothing, shots take nothing and bolts count
// many times; the page's buttons refill the owned weapons to their max and give bolts; the HUD's
// line follows the cheats and the level loads; and with no room in the pause menu it still loads.
//
// Run with the library's path: cheats_rac1_load_test <cheats.dylib|.so|.dll>.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "discs.h"
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

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kMemory = 0x00200000u;
std::uint8_t* g_mem;
std::string g_failed;
std::vector<std::string> g_warnings;
const char* g_disc = "pal";
std::map<std::string, int> g_bools;
std::map<std::string, std::string> g_strings;

const cheats_disc& disc() {
    return std::strcmp(g_disc, "pal") == 0 ? k_cheats_discs[0] : k_cheats_discs[1];
}

template <class T>
T get(std::uint32_t at) {
    T v;
    std::memcpy(&v, g_mem + at, sizeof v);
    return v;
}

template <class T>
void put(std::uint32_t at, T v) {
    std::memcpy(g_mem + at, &v, sizeof v);
}

// ---- The game's side: the hook points' originals, recording their calls ----

int g_bolts;
int g_bolt_value = 5;
std::vector<int> g_damage_calls;
std::vector<std::pair<int, int>> g_ammo_calls;
int g_bolt_calls;

void game_take_damage(int32_t n) {
    g_damage_calls.push_back(n);
}

int32_t game_take_ammo(int32_t item, int32_t amount) {
    g_ammo_calls.push_back({item, amount});
    return 0;  // too little: only the mod's cheat can let the shot go
}

void game_collect_bolt(std::uint32_t) {
    ++g_bolt_calls;
    g_bolts += g_bolt_value;
}

// ---- The host ----

struct Taken {
    std::string name;
    std::string signature;
    void* replacement;
};

std::vector<Taken> g_hooks;
openrac_frame_fn g_frame;
void* g_frame_user;

struct Page {
    openrac_ui_fn fn;
    void* user;
};

std::vector<Page> g_pages;
std::vector<std::string> g_pause_items;
bool g_refuse_pause;
int g_sounds;

// What the last ui_set put on a page.
struct Shown {
    std::string title;
    std::vector<std::string> labels;
    std::vector<std::uint32_t> choices;
};

Shown g_shown;
openrac_play_status g_play;
int g_hud_calls;
std::string g_hud_text;

void host_log(openrac_mod*, int level, const char* text) {
    std::printf("[%d] cheats: %s\n", level, text);

    // A warning, kept for the checks.
    if (level == OPENRAC_LOG_WARN) {
        g_warnings.push_back(text);
    }
}

void host_fail(openrac_mod*, const char* reason) {
    g_failed = reason;
}

int setting_bool(openrac_mod*, const char* key) {
    const auto it = g_bools.find(key);
    return it == g_bools.end() ? 0 : it->second;
}

int64_t setting_int(openrac_mod*, const char*) {
    return 0;
}

double setting_float(openrac_mod*, const char*) {
    return 0.0;
}

const char* setting_string(openrac_mod*, const char* key) {
    const auto it = g_strings.find(key);
    return it == g_strings.end() ? "" : it->second.c_str();
}

// The pad callback the mod gave, and a report of the first pad sent to it as the port does.
openrac_pad_fn g_pad;
void* g_pad_user;

int host_on_pad(openrac_mod*, openrac_pad_fn fn, void* user) {
    g_pad = fn;
    g_pad_user = user;
    return 0;
}

// Sends the mod a report of pad 0: `held` the buttons held (the report's word has them clear) and
// the left stick at (x, y), 0x7F at rest.
void pad_report(unsigned held, int x = 0x7F, int y = 0x7F) {
    std::uint8_t report[18];
    std::memset(report, 0x7F, sizeof report);
    const unsigned word = ~held & 0xFFFFu;
    report[0] = static_cast<std::uint8_t>(word & 0xFF);
    report[1] = static_cast<std::uint8_t>(word >> 8);
    report[4] = static_cast<std::uint8_t>(x);
    report[5] = static_cast<std::uint8_t>(y);
    g_pad(g_pad_user, 0, report, static_cast<int>(sizeof report));
}

constexpr unsigned kRight = 1u << 5;
constexpr unsigned kLeft = 1u << 7;
constexpr unsigned kCross = 1u << 14;

int host_hook(
    openrac_mod*, const char* name, const char* signature, void* replacement, void** original
) {
    g_hooks.push_back({name, signature, replacement});
    const std::string n = name;

    // Each hook point the mod may take: the game's function behind it.
    if (n == "rac1.player.take_damage") {
        *original = reinterpret_cast<void*>(&game_take_damage);
    } else if (n == "rac1.weapon.take_ammo") {
        *original = reinterpret_cast<void*>(&game_take_ammo);
    } else if (n == "rac1.moby.collect_bolt") {
        *original = reinterpret_cast<void*>(&game_collect_bolt);
    } else {
        return OPENRAC_HOOK_UNKNOWN;
    }

    return OPENRAC_HOOK_OK;
}

int host_on_frame(openrac_mod*, openrac_frame_fn fn, void* user) {
    g_frame = fn;
    g_frame_user = user;
    return OPENRAC_HOOK_OK;
}

openrac_gaddr identity(openrac_gaddr a) {
    return a;
}

openrac_gaddr identity_level(int, openrac_gaddr a) {
    return a;
}

int host_ui_page(openrac_mod*, uint32_t, openrac_ui_fn fn, void* user) {
    g_pages.push_back({fn, user});
    return static_cast<int>(g_pages.size()) - 1;
}

int host_ui_main_item(openrac_mod*, const char*, const char*, int) {
    return OPENRAC_HOOK_OK;
}

int host_ui_set(openrac_mod*, int, const openrac_ui_content* content) {
    g_shown = {};
    g_shown.title = content->title;

    // Each item: its label and the choice it shows.
    for (std::uint32_t i = 0; i < content->item_count; ++i) {
        g_shown.labels.push_back(content->items[i].label);
        g_shown.choices.push_back(content->items[i].choice);
    }

    return OPENRAC_HOOK_OK;
}

int host_ui_open(openrac_mod*, int) {
    return OPENRAC_HOOK_OK;
}

int host_ui_confirm(openrac_mod*, int, const char*, uint32_t) {
    return OPENRAC_HOOK_OK;
}

int host_ui_cursor(openrac_mod*, int, int) {
    return 0;
}

int host_ui_sound(openrac_mod*, int) {
    ++g_sounds;
    return OPENRAC_HOOK_OK;
}

int host_ui_pause_item(openrac_mod*, const char* label, int) {
    // A host whose pause menu other mods filled.
    if (g_refuse_pause) {
        return OPENRAC_HOOK_UNKNOWN;
    }

    g_pause_items.push_back(label);
    return OPENRAC_HOOK_OK;
}

int host_play_status(openrac_play_status* out) {
    const std::uint32_t size = out->size;
    *out = g_play;
    out->size = size;
    return OPENRAC_HOOK_OK;
}

int host_bolts(int add) {
    g_bolts += add;
    return g_bolts;
}

// The folder the port says the mods are in (interface 1.14), "" for none.
std::string g_plugins_dir;

int host_plugins_folder(char* out, uint32_t size) {
    // No folder, or one that does not fit.
    if (g_plugins_dir.empty() || g_plugins_dir.size() + 1 > size) {
        return 0;
    }

    std::memcpy(out, g_plugins_dir.c_str(), g_plugins_dir.size() + 1);
    return static_cast<int>(g_plugins_dir.size());
}

int host_hud_text_set(openrac_mod*, const openrac_hud_text* lines, int count) {
    // Outside a level the port refuses.
    if (g_play.level < 0) {
        return OPENRAC_HOOK_UNKNOWN;
    }

    ++g_hud_calls;
    g_hud_text = count > 0 ? lines[0].text : "";
    return OPENRAC_HOOK_OK;
}

openrac_api make_api(const char* game, const char* version) {
    openrac_api api;
    std::memset(&api, 0, sizeof api);
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = game;
    api.version = version;
    api.openrac_version = "test";
    api.platform = "test";
    api.memory = g_mem;
    api.log = host_log;
    api.fail = host_fail;
    api.setting_bool = setting_bool;
    api.setting_int = setting_int;
    api.setting_float = setting_float;
    api.setting_string = setting_string;
    api.hook = host_hook;
    api.on_frame = host_on_frame;
    api.relocate_data = identity;
    api.relocate_level_data = identity_level;
    api.ui_page = host_ui_page;
    api.ui_main_item = host_ui_main_item;
    api.ui_set = host_ui_set;
    api.ui_open = host_ui_open;
    api.ui_confirm = host_ui_confirm;
    api.ui_cursor = host_ui_cursor;
    api.ui_sound = host_ui_sound;
    api.ui_pause_item = host_ui_pause_item;
    api.play_status = host_play_status;
    api.bolts = host_bolts;
    api.hud_text_set = host_hud_text_set;
    api.on_pad = host_on_pad;
    api.plugins_folder = host_plugins_folder;
    return api;
}

void* hook(const std::string& name) {
    // The replacement the mod gave for `name`.
    for (const Taken& t : g_hooks) {
        // This one.
        if (t.name == name) {
            return t.replacement;
        }
    }

    return nullptr;
}

openrac_mod_load_fn g_load;
openrac_mod* g_handle;

// Loads the mod on a disc with the settings given; true when it loaded.
bool load_with(const char* game, const char* version) {
    g_hooks.clear();
    g_pages.clear();
    g_pause_items.clear();
    g_warnings.clear();
    g_failed.clear();
    g_frame = nullptr;
    g_shown = {};
    g_disc = version;
    static openrac_api kept;
    kept = make_api(game, version);
    return g_load(&kept, g_handle) == 0;
}

// Sends the pause page an event.
void page_event(std::uint32_t kind, int item, int value) {
    CHECK(!g_pages.empty());
    openrac_ui_event e{};
    e.size = sizeof e;
    e.kind = kind;
    e.item = item;
    e.value = value;
    g_pages[0].fn(g_pages[0].user, 0, &e);
}

using DamageFn = void (*)(int32_t);
using AmmoFn = int32_t (*)(int32_t, int32_t);
using BoltFn = void (*)(std::uint32_t);

// Lays out the owned weapons, their ammo and the items' table's max on the disc chosen.
void lay_weapons() {
    const cheats_disc& d = disc();
    std::memset(g_mem, 0, kMemory);
    // Item 2: owned, 3 of 40. Item 5: owned, a gadget (no max). Item 7: not owned, 0 of 30.
    g_mem[d.owned + 2] = 1;
    g_mem[d.owned + 5] = 1;
    put<std::uint16_t>(d.item_table + 2 * CHEATS_ITEM_SIZE + CHEATS_ITEM_MAX_AMMO, 40);
    put<std::uint16_t>(d.item_table + 7 * CHEATS_ITEM_SIZE + CHEATS_ITEM_MAX_AMMO, 30);
    put<int32_t>(d.ammo + 2 * 4, 3);
}

int ammo(int id) {
    return get<int32_t>(disc().ammo + 4u * static_cast<std::uint32_t>(id));
}

int air() {
    return get<int32_t>(disc().hero + CHEATS_HERO_AIR);
}

void write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    void* lib = open_library(argv[1]);
    CHECK(lib != nullptr);
    auto query = (openrac_mod_query_fn)symbol(lib, OPENRAC_MOD_QUERY);
    g_load = (openrac_mod_load_fn)symbol(lib, OPENRAC_MOD_LOAD);
    CHECK(query != nullptr && g_load != nullptr);
    openrac_mod_info info;
    std::memset(&info, 0, sizeof info);
    info.size = sizeof info;
    query(&info);
    // The interface its manifest names (1.12), whatever the SDK it was built with.
    CHECK(info.api_major == OPENRAC_MOD_API_MAJOR && info.api_minor == 12);
    CHECK(std::strcmp(info.id, "cheats") == 0 && std::strcmp(info.version, "0.3.1") == 0);
    g_mem = static_cast<std::uint8_t*>(std::calloc(1, kMemory));
    CHECK(g_mem != nullptr);
    g_handle = reinterpret_cast<openrac_mod*>(&info);
    g_play.size = sizeof g_play;
    g_play.phase = OPENRAC_PLAY_PLAYING;
    g_play.level = 3;
    g_play.loads = 1;

    // Another game, an unknown disc: refused with a reason, before any hook.
    CHECK(
        !load_with("rac2", "ntsc") && g_failed.find("rac2") != std::string::npos && g_hooks.empty()
    );
    CHECK(!load_with("rac1", "jp") && g_failed.find("jp") != std::string::npos && g_hooks.empty());

    // Both discs: the three hook points with their signatures, and the Cheats page in the pause
    // menu, every cheat off.
    static const char* const k_points[][2] = {
        {"rac1.player.take_damage", "void(i32)"},
        {"rac1.weapon.take_ammo", "i32(i32,i32)"},
        {"rac1.moby.collect_bolt", "void(u32)"},
    };

    // Each disc the mod knows.
    for (const char* version : {"ntsc", "pal"}) {
        CHECK(load_with("rac1", version) && g_failed.empty());
        CHECK(g_hooks.size() == 3);

        // Each hook point, taken with its signature.
        for (const auto& p : k_points) {
            bool found = false;

            // The one taken under this name.
            for (const Taken& t : g_hooks) {
                found |= t.name == p[0] && t.signature == p[1];
            }

            CHECK(found);
        }

        CHECK(g_pause_items.size() == 1 && g_pause_items[0] == "Cheats");
        CHECK(g_shown.title == "Cheats" && g_shown.labels.size() == 6);
        CHECK(g_shown.labels[2] == "Infinite oxygen");
        CHECK(g_shown.choices[0] == 0 && g_shown.choices[1] == 0 && g_shown.choices[2] == 0);
        CHECK(g_shown.choices[3] == 0);
        CHECK(g_frame != nullptr);
    }

    // Every cheat off: each hook calls the game's own function as it was called.
    auto damage = reinterpret_cast<DamageFn>(hook("rac1.player.take_damage"));
    auto take = reinterpret_cast<AmmoFn>(hook("rac1.weapon.take_ammo"));
    auto bolt = reinterpret_cast<BoltFn>(hook("rac1.moby.collect_bolt"));
    g_damage_calls.clear();
    g_ammo_calls.clear();
    g_bolts = 100;
    damage(1);
    CHECK(g_damage_calls.size() == 1 && g_damage_calls[0] == 1);
    CHECK(take(-1, 2) == 0 && g_ammo_calls.size() == 1 && g_ammo_calls[0].second == 2);
    bolt(0x1000u);
    CHECK(g_bolts == 105 && g_bolt_calls == 1);
    // Nothing on: no HUD line.
    g_frame(g_frame_user);
    CHECK(g_hud_text.empty());

    // From the settings: everything on, bolts x10.
    g_bools["invincible"] = 1;
    g_bools["infinite_ammo"] = 1;
    g_bools["infinite_air"] = 1;
    g_bools["show_status"] = 1;
    g_strings["bolt_multiplier"] = "10";
    CHECK(load_with("rac1", "ntsc") && g_failed.empty());
    CHECK(g_shown.choices[0] == 1 && g_shown.choices[1] == 1 && g_shown.choices[2] == 1);
    CHECK(g_shown.choices[3] == 4);
    damage = reinterpret_cast<DamageFn>(hook("rac1.player.take_damage"));
    take = reinterpret_cast<AmmoFn>(hook("rac1.weapon.take_ammo"));
    bolt = reinterpret_cast<BoltFn>(hook("rac1.moby.collect_bolt"));
    g_damage_calls.clear();
    g_ammo_calls.clear();
    // A hit takes nothing; a hit of 0 is the game's still.
    damage(1);
    CHECK(g_damage_calls.empty());
    damage(0);
    CHECK(g_damage_calls.size() == 1 && g_damage_calls[0] == 0);
    // A shot is allowed and takes nothing; a call that takes no rounds is the game's still.
    CHECK(take(-1, 2) == 1 && g_ammo_calls.empty());
    CHECK(take(4, 0) == 0 && g_ammo_calls.size() == 1);
    // A 5-bolt pickup counts 50.
    g_bolts = 100;
    bolt(0x1000u);
    CHECK(g_bolts == 150);

    // Air taken under water is put back to a full breath after the tick.
    put<int32_t>(disc().hero + CHEATS_HERO_AIR, 300);
    g_frame(g_frame_user);
    CHECK(air() == CHEATS_FULL_AIR);

    // The HUD's line: shown once in a level, again after a load.
    g_hud_calls = 0;
    g_play.loads = 5;
    g_frame(g_frame_user);
    CHECK(g_hud_text == "Cheats: invincible, infinite ammo, infinite oxygen, bolts x10");
    CHECK(g_hud_calls == 1);
    g_frame(g_frame_user);
    CHECK(g_hud_calls == 1);
    g_play.loads = 6;
    g_frame(g_frame_user);
    CHECK(g_hud_calls == 2);

    // Without the HUD setting: no line.
    g_bools["show_status"] = 0;
    CHECK(load_with("rac1", "ntsc"));
    g_frame(g_frame_user);
    CHECK(g_hud_text.empty());

    // From the page, everything off at the start.
    g_bools.clear();
    g_strings.clear();
    g_bools["show_status"] = 1;

    // Each disc, switched from its page.
    for (const char* version : {"pal", "ntsc"}) {
        CHECK(load_with("rac1", version));
        lay_weapons();
        damage = reinterpret_cast<DamageFn>(hook("rac1.player.take_damage"));
        take = reinterpret_cast<AmmoFn>(hook("rac1.weapon.take_ammo"));
        bolt = reinterpret_cast<BoltFn>(hook("rac1.moby.collect_bolt"));

        // Invincibility on, then off.
        page_event(OPENRAC_UI_CHANGE, 0, 1);
        g_damage_calls.clear();
        damage(1);
        CHECK(g_damage_calls.empty());
        page_event(OPENRAC_UI_CHANGE, 0, 0);
        damage(1);
        CHECK(g_damage_calls.size() == 1);

        // Infinite ammo on: the owned weapon with a max is filled, the others are left.
        page_event(OPENRAC_UI_CHANGE, 1, 1);
        CHECK(ammo(2) == 40 && ammo(5) == 0 && ammo(7) == 0);
        g_ammo_calls.clear();
        CHECK(take(-1, 1) == 1 && g_ammo_calls.empty());

        // Infinite oxygen: off, the air the game took stays taken; on, it is full again.
        put<int32_t>(disc().hero + CHEATS_HERO_AIR, 300);
        g_frame(g_frame_user);
        CHECK(air() == 300);
        page_event(OPENRAC_UI_CHANGE, 2, 1);
        g_frame(g_frame_user);
        CHECK(air() == CHEATS_FULL_AIR);

        // The refill button, once more after a shot's worth went.
        put<int32_t>(disc().ammo + 8u, 1);
        g_sounds = 0;
        page_event(OPENRAC_UI_CHOOSE, 4, 0);
        CHECK(ammo(2) == 40 && g_sounds == 1);

        // The gift of bolts.
        g_bolts = 7;
        page_event(OPENRAC_UI_CHOOSE, 5, 0);
        CHECK(g_bolts == 10007);

        // The multiplier at x100: a 1-bolt pickup counts 100.
        page_event(OPENRAC_UI_CHANGE, 3, 5);
        g_bolt_value = 1;
        g_bolts = 0;
        bolt(0x1000u);
        CHECK(g_bolts == 100);
        g_bolt_value = 5;

        // The HUD's line follows the page.
        g_frame(g_frame_user);
        CHECK(g_hud_text == "Cheats: infinite ammo, infinite oxygen, bolts x100");
    }

    // Outside a level neither the refill nor the air touches anything, and the HUD waits for a
    // level.
    g_play.level = -1;
    g_play.phase = OPENRAC_PLAY_FRONT_END;
    put<int32_t>(disc().ammo + 8u, 1);
    page_event(OPENRAC_UI_CHOOSE, 4, 0);
    CHECK(ammo(2) == 1);
    put<int32_t>(disc().hero + CHEATS_HERO_AIR, 300);
    g_frame(g_frame_user);
    CHECK(air() == 300);
    g_play.level = 3;
    g_play.phase = OPENRAC_PLAY_PLAYING;

    // No room left in the pause menu: it loads all the same, with a warning, the cheats from the
    // settings.
    g_refuse_pause = true;
    g_bools["invincible"] = 1;
    CHECK(load_with("rac1", "pal") && g_failed.empty());
    CHECK(g_pause_items.empty() && !g_warnings.empty());
    damage = reinterpret_cast<DamageFn>(hook("rac1.player.take_damage"));
    g_damage_calls.clear();
    damage(1);
    CHECK(g_damage_calls.empty());

    // In step with the launcher's list, in the folder the port says the mods are in.
    g_refuse_pause = false;
    g_bools.clear();
    g_strings.clear();
    g_bools["show_status"] = 1;
    std::random_device random;
    const fs::path dir = fs::temp_directory_path() / ("cheats-load-" + std::to_string(random()));
    fs::create_directories(dir);
    const fs::path list = dir / "plugins.json";
    write_text(
        list,
        R"({"format": 1, "mods": [{"id": "other", "enabled": true, "settings": {"x": 1}},)"
        R"( {"id": "cheats", "enabled": true, "settings": {"trace": true}}]})"
    );
    g_plugins_dir = dir.string();
    CHECK(load_with("rac1", "pal") && g_failed.empty());
    lay_weapons();
    damage = reinterpret_cast<DamageFn>(hook("rac1.player.take_damage"));

    // A cheat switched on the page is written into the list, the other mod's entry kept.
    page_event(OPENRAC_UI_CHANGE, 0, 1);
    std::string text = read_text(list);
    CHECK(text.find("\"invincible\": true") != std::string::npos);
    CHECK(
        text.find("\"other\"") != std::string::npos && text.find("\"x\": 1") != std::string::npos
    );
    CHECK(text.find("\"trace\": true") != std::string::npos);

    // The launcher changes them while the game runs: taken within a second of ticks, the page
    // showing them, invincibility off and infinite ammo filling the weapons.
    text.replace(text.find("\"invincible\": true"), 18, "\"invincible\": false");
    text.replace(text.find("\"infinite_ammo\": false"), 22, "\"infinite_ammo\": true");
    text.replace(text.find("\"bolt_multiplier\": \"1\""), 22, "\"bolt_multiplier\": \"5\"");
    write_text(list, text);
    fs::last_write_time(list, fs::last_write_time(list) + std::chrono::seconds(5));

    // About a second of ticks.
    for (int i = 0; i < 60; ++i) {
        g_frame(g_frame_user);
    }

    CHECK(g_shown.choices[0] == 0 && g_shown.choices[1] == 1 && g_shown.choices[3] == 3);
    CHECK(ammo(2) == 40);
    g_damage_calls.clear();
    damage(1);
    CHECK(g_damage_calls.size() == 1);
    CHECK(g_hud_text == "Cheats: infinite ammo, bolts x5");

    // The page and the stick: the port steps the row the cursor arrives at when the stick pushed
    // down leans a little sideways (the game makes it Right too). Pushed more down than sideways,
    // nothing changes and the page shows the cheat as it is.
    CHECK(load_with("rac1", "pal") && g_pad != nullptr);
    lay_weapons();
    pad_report(0, 0xB0, 0xFF);
    page_event(OPENRAC_UI_CHANGE, 0, 1);
    CHECK(g_shown.choices[0] == 0);
    damage = reinterpret_cast<DamageFn>(hook("rac1.player.take_damage"));
    g_damage_calls.clear();
    damage(1);
    CHECK(g_damage_calls.size() == 1);
    // A button the cursor arrives at that way is not chosen either.
    g_bolts = 0;
    page_event(OPENRAC_UI_CHOOSE, 5, 0);
    CHECK(g_bolts == 0);

    // Cross steps it, and chooses a button.
    pad_report(kCross, 0xB0, 0xFF);
    page_event(OPENRAC_UI_CHANGE, 0, 1);
    CHECK(g_shown.choices[0] == 1);
    page_event(OPENRAC_UI_CHOOSE, 5, 0);
    CHECK(g_bolts == 10000);

    // Right and Left, on the d-pad or the stick pushed sideways, step the way they point (the port
    // says forward for both).
    pad_report(kLeft);
    page_event(OPENRAC_UI_CHANGE, 3, 1);
    CHECK(g_shown.choices[3] == 5);
    pad_report(0, 0x10, 0x90);
    page_event(OPENRAC_UI_CHANGE, 3, 0);
    CHECK(g_shown.choices[3] == 4);
    pad_report(kRight);
    page_event(OPENRAC_UI_CHANGE, 3, 5);
    CHECK(g_shown.choices[3] == 5);
    // Left or Right do not choose a button.
    g_bolts = 0;
    page_event(OPENRAC_UI_CHOOSE, 5, 0);
    CHECK(g_bolts == 0);

    // Nothing on the pad (a mouse, a touch): the port's word holds.
    pad_report(0);
    page_event(OPENRAC_UI_CHANGE, 3, 1);
    CHECK(g_shown.choices[3] == 1);
    page_event(OPENRAC_UI_CHOOSE, 5, 0);
    CHECK(g_bolts == 10000);

    // Without a list (no folder said, and the library not in a mod's folder): it loads, the cheats
    // this run's.
    g_plugins_dir.clear();
    CHECK(load_with("rac1", "ntsc") && g_failed.empty());
    page_event(OPENRAC_UI_CHANGE, 0, 1);
    fs::remove_all(dir);

    std::puts("cheats rac1 load: ok");
    return 0;
}
