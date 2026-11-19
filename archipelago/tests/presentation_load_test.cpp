// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The mod as the port loads it: the built library opened, given a host table and game memory of
// the test's own, and pointed at an Archipelago server the test runs on the loopback (plain ws://).
// It checks that an item the server sends is given, that a location had gives nothing and is sent
// to the server, that a gold bolt found is sent, and that Drek's ending is the goal.
//
//   archipelago_load_test <the built mod>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ap_gold_weapons.h"
#include "ap_ids.h"
#include "ap_npc_infobots.h"
#include "ap_skills.h"
#include "ap_visual_api.h"
#include "discs.h"
#include "hooks.h"
#include "openrac/openrac_mod.h"
#include "rando.h"
#include "websocket.hpp"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;

static void close_socket(socket_t s) {
    closesocket(s);
}

static void* open_library(const char* p) {
    return (void*)LoadLibraryA(p);
}

static void* symbol(void* lib, const char* n) {
    return (void*)GetProcAddress((HMODULE)lib, n);
}
#else
#include <dlfcn.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
using socket_t = int;

static void close_socket(socket_t s) {
    close(s);
}

static void* open_library(const char* p) {
    return dlopen(p, RTLD_NOW | RTLD_LOCAL);
}

static void* symbol(void* lib, const char* n) {
    return dlsym(lib, n);
}
#endif

using nlohmann::json;

namespace {

int g_failures = 0;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                               \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

// ---- the game ----

constexpr std::size_t kMemory = 32u << 20;
std::vector<std::uint8_t> g_memory(kMemory);
const rando_disc* D = &k_rando_discs[0];
openrac_frame_fn g_frame;
void* g_frame_user;
int g_hooks;
std::vector<std::pair<std::string, void*>> g_replacements;
int g_level = LEVEL_NOVALIS;
int g_scene = -1;
int g_play_phase = OPENRAC_PLAY_PLAYING;
int g_gives;
std::string g_notice;
std::string g_server;
bool g_new_game_plus;
bool g_gold_mode;
bool g_missing_gold_hook;
bool g_visual_mode;
bool g_acquisition_mode;
std::vector<std::pair<int, int>> g_acquisitions;
int g_case_demos;
std::vector<openrac_moby_visual> g_visuals;
std::vector<openrac_moby> g_test_mobys;
std::string g_rendered_name;
int g_rendered_icon;
int g_source_icon = 50;
int g_ammo_icon;
int g_vendor_initialized_item = -1;
bool g_dialog_starts = true;
std::string g_rendered_prompt;
std::int64_t location_id(int kind, int level, int key);
std::string g_data_dir;
int g_save_updates;
int g_npc_purchases;
uint8_t g_serialized_planets[LEVEL_COUNT];
uint8_t g_serialized_owned[RANDO_ITEM_COUNT];
uint32_t g_serialized_checks;
int g_bolts = 2000;
int32_t (*g_unspent)();

std::uint8_t* at(std::uint32_t a) {
    return g_memory.data() + a;
}

void set_level(int level) {
    std::memcpy(at(D->level), &level, 4);
    g_level = level;
}

// The game's banner: what GiveItem and UnlockPlanet write in another level's text.
void game_banner() {
    std::memcpy(at(D->banner_text), "Paradox", 8);
    const int32_t ticks = 99;
    std::memcpy(at(D->banner_ticks), &ticks, 4);
}

void game_give(int32_t id, int32_t) {
    if (id == ITEM_DEVASTATOR) {
        const std::uint32_t gold = std::strcmp(D->version, "pal") == 0 ? 0x13E620 : 0x13E520;
        CHECK(*at(gold + id) == 1);  // variant is set before initial ammo/model setup
    }
    *at(D->owned + id) = 1;
    *at(D->obtained + id) = 1;
    ++g_gives;
    game_banner();
}

void game_unlock(int32_t planet) {
    *at(D->planets + planet) = 1;
    game_banner();
}

int g_ship_saw = -1;
int g_class22_saw = -1;
int g_helps;

void game_ship(int32_t) {
    g_ship_saw = *at(D->owned + ITEM_O2_MASK);
}

void game_class22(openrac_gaddr) {
    g_class22_saw = *at(D->owned + ITEM_O2_MASK);
}

int32_t game_help(int32_t, int32_t) {
    ++g_helps;
    return 0;
}

std::vector<uint8_t> g_drop_stock;
openrac_gaddr g_drop_moby;

int32_t game_drop(openrac_gaddr moby, openrac_gaddr out_item) {
    g_drop_moby = moby;
    g_drop_stock.assign(at(D->vendor_stock), at(D->vendor_stock) + VENDOR_STOCK_SIZE);
    int32_t item;
    std::memcpy(&item, at(out_item), 4);
    if (item <= 0) {
        item = g_drop_stock[0] == 0xff ? ITEM_BOMB_GLOVE : g_drop_stock[0] & 0x3f;
        std::memcpy(at(out_item), &item, 4);
    }
    return 2;
}

void game_void() {}

void game_i32(int32_t) {}

void game_new_game_plus(int32_t) {
    const int zero = 0;
    std::memcpy(at(D->save_tag), &zero, 4);
}

int32_t game_count() {
    return 0;
}

void game_moby(openrac_gaddr) {}

void* replacement(const char* name);

void game_case_demo(uint32_t, int32_t) {
    ++g_case_demos;
}

void game_gold_case(openrac_gaddr moby) {
    int16_t cls;
    std::memcpy(&cls, at(moby + 0xA6), 2);
    if (cls >= 0x5B0 && cls < 0x5B0 + AP_GOLD_WEAPON_COUNT) {
        const auto flags = std::strcmp(D->version, "pal") == 0 ? 0x13E620u : 0x13E520u;
        if (*at(flags + k_ap_gold_bases[cls - 0x5B0])) {
            *at(moby + 0x20) = 0x80;
        }
        return;
    }

    std::uint32_t pvars;
    std::memcpy(&pvars, at(moby + MOBY_PVARS), 4);
    int slot, base;
    std::memcpy(&slot, at(pvars + 0x40), 4);
    const uint32_t entries = std::strcmp(D->version, "pal") == 0 ? 0x1DEDE0u : 0x1DEB10u;
    std::memcpy(&base, at(entries + slot * 4), 4);
    const std::uint32_t gold = std::strcmp(D->version, "pal") == 0 ? 0x13E620 : 0x13E520;
    if (*at(pvars + 4) == 2 && *at(D->owned + base) && !*at(gold + base) && g_unspent() >= 4
        && g_bolts >= 1000) {
        const int before = g_unspent();
        g_bolts -= 1000;
        *at(gold + base) = 1;
        ((void (*)(uint32_t))replacement("rac1.save.serialize"))(0);
        CHECK(*at(D->owned + base) == 1);  // temporary transaction view resumes after a save
        CHECK(g_unspent() == before - 4);  // a purchase in flight spends currency immediately
        if (g_acquisition_mode) {
            const uint32_t context = std::strcmp(D->version, "pal") == 0 ? 0x179200u : 0x179100u;
            std::memcpy(at(context + 8), &moby, 4);
            std::memcpy(at(context + 12), &pvars, 4);
            ((void (*)(uint32_t, int32_t))replacement("GoldCase_StartDemo"))(moby, base);
        }
    }
}

void game_save_update() {
    ++g_save_updates;
    const auto mc = std::strcmp(D->version, "pal") == 0 ? 0x13D390u : 0x13D290u;
    int state, sub, busy;
    std::memcpy(&state, at(mc + 0xDC), 4);
    std::memcpy(&sub, at(mc + 0xE0), 4);
    std::memcpy(&busy, at(mc + 0xD4), 4);
    if (state == 20 && sub == 3 && !busy) {
        state = 0;
        std::memcpy(at(mc + 0xDC), &state, 4);
    }
}

int32_t game_restore_data(uint32_t, int32_t, uint32_t) {
    return 1;
}

const char* data_dir(openrac_mod*) {
    return g_data_dir.c_str();
}

std::string saved_book() {
    std::ifstream in(std::filesystem::path(g_data_dir) / "save-slots.tsv");
    return std::string(std::istreambuf_iterator<char>(in), {});
}

uint32_t sprite_tint(openrac_mod*, uint32_t color) {
    return UINT32_MAX;
}

int visual_submit(openrac_mod*, const openrac_moby_visual* v, int n) {
    g_visuals.assign(v, v + n);
    return OPENRAC_HOOK_OK;
}

uint32_t guest_text(openrac_mod*, const char* text) {
    static uint32_t next = 0x1E00000;
    const auto address = next;
    next += 1024;
    std::memcpy(at(address), text, std::strlen(text) + 1);
    return address;
}

int game_icon(int icon, int frame) {
    return icon * 10 + frame;
}

uint32_t game_message(int id) {
    const uint32_t address = 0x1D00000;
    std::strcpy(
        reinterpret_cast<char*>(at(address)), id == 257 ? "Buy Infobot for 500 bolts" : "Pyrocitor"
    );
    return address;
}

void* replacement(const char* name);

void game_draw_details() {
    auto message = (uint32_t (*)(int))replacement("rac1.text.msg_string");
    g_rendered_name = reinterpret_cast<const char*>(at(message(256)));
}

void game_draw_icons() {
    auto icon = (int (*)(int, int))replacement("rac1.ui.icon_frame");
    g_rendered_icon = icon(g_source_icon, 0);
    g_ammo_icon = icon(g_source_icon, 2);
}

int game_try_dialog(uint32_t, uint32_t) {
    auto message = (uint32_t (*)(int))replacement("rac1.text.msg_string");
    g_rendered_prompt = reinterpret_cast<const char*>(at(message(257)));
    return g_dialog_starts ? 1 : 0;
}

void game_serialize(uint32_t) {
    std::memcpy(g_serialized_planets, at(D->planets), LEVEL_COUNT);
    std::memcpy(g_serialized_owned, at(D->owned), RANDO_ITEM_COUNT);
    std::memcpy(&g_serialized_checks, at(D->save_tag), 4);
}

void game_infobot(uint32_t moby) {
    uint32_t vars;
    int planet;
    std::memcpy(&vars, at(moby + MOBY_PVARS), 4);
    std::memcpy(&planet, at(vars + INFOBOT_PLANET), 4);
    if (!*at(D->planets + planet)) {
        ((void (*)(int))replacement(OPENRAC_RAC1_HOOK_Planet_Unlock))(planet);
        *at(D->triggers + g_level * 16 + *at(moby + MOBY_MISSION)) = 0xFF;
    }
}

void game_npc(uint32_t moby) {
    int16_t cls;
    std::memcpy(&cls, at(moby + 0xA6), 2);
    for (const auto& npc : k_ap_npcs) {
        if (npc.oclass == cls && npc.level == g_level && !*at(D->planets + npc.planet)) {
            ++g_npc_purchases;
            ((void (*)(int))replacement(OPENRAC_RAC1_HOOK_Planet_Unlock))(npc.planet);
            ((void (*)(uint32_t))replacement("rac1.save.serialize"))(0);
            break;
        }
    }
}

void game_vendor_initialize() {
    int cursor, objects, item;
    std::memcpy(&cursor, at(D->vendor_menu + VENDOR_CURSOR), 4);
    std::memcpy(&objects, at(D->vendor_menu + 0x20), 4);
    std::memcpy(&item, at(D->vendor_menu + VENDOR_ENTRIES + cursor * VENDOR_ENTRY_SIZE), 4);
    g_vendor_initialized_item = item;
    const uint32_t definitions = std::strcmp(D->version, "pal") == 0 ? 0x179BC0u : 0x179AC0u;
    int cls;
    std::memcpy(&cls, at(definitions + item * 0x4C + 0x10), 4);
    const int16_t short_class = int16_t(cls);
    if (objects) {
        for (const int offset : {0x100, 0x300}) {
            std::memcpy(at(objects + offset + 0xA6), &short_class, 2);
        }
    }
}

void* original_for(const std::string& name) {
    if (name == OPENRAC_RAC1_HOOK_Ammo_ChooseDrop) {
        return (void*)game_drop;
    }
    if (name == "GoldCase_StartDemo") {
        return (void*)game_case_demo;
    }
    if (name == "rac1.save.serialize") {
        return (void*)game_serialize;
    }
    for (const auto& npc : k_ap_npcs) {
        if (name == npc.hook) {
            return (void*)game_npc;
        }
    }
    if (name == "rac1.vendor.initialize_selected") {
        return (void*)game_vendor_initialize;
    }
    if (name == "rac1.vendor.draw_icons") {
        return (void*)game_draw_icons;
    }
    if (name == "rac1.vendor.draw_details") {
        return (void*)game_draw_details;
    }
    if (name == "rac1.ui.sprite.draw") {
        return (void*)game_moby;
    }
    if (name == "rac1.ui.icon_frame") {
        return (void*)game_icon;
    }
    if (name == "rac1.npc.try_dialog") {
        return (void*)game_try_dialog;
    }
    if (name == "rac1.text.msg_string") {
        return (void*)game_message;
    }
    if (name == ITEM_RANDOMIZER_HOOK_Save_NewGame) {
        return (void*)game_void;
    }
    if (name == "rac1.save.update_state") {
        return (void*)game_save_update;
    }
    if (name == "rac1.save.restore_data") {
        return (void*)game_restore_data;
    }
    if (name == ITEM_RANDOMIZER_HOOK_Save_NewGamePlus) {
        return (void*)game_new_game_plus;
    }
    if (name == OPENRAC_RAC1_HOOK_GoldBolts_CountUnspent) {
        return (void*)game_count;
    }
    if (name == OPENRAC_RAC1_HOOK_Moby_GoldWeaponCase_Update) {
        return (void*)game_gold_case;
    }

    if (name == OPENRAC_RAC1_HOOK_Item_Give) {
        return (void*)game_give;
    }
    if (name == OPENRAC_RAC1_HOOK_Moby_Infobot_Update) {
        return (void*)game_infobot;
    }
    if (name == OPENRAC_RAC1_HOOK_Planet_Unlock) {
        return (void*)game_unlock;
    }
    if (name == OPENRAC_RAC1_HOOK_Vendor_InitLevelStock) {
        return (void*)game_void;
    }
    if (name == OPENRAC_RAC1_HOOK_Vendor_BuildItemList) {
        return (void*)game_i32;
    }
    if (name == OPENRAC_RAC1_HOOK_GoldBolts_CountCollected) {
        return (void*)game_count;
    }
    if (name == OPENRAC_RAC1_HOOK_Ship_InitTransition) {
        return (void*)game_ship;
    }
    if (name == OPENRAC_RAC1_HOOK_Moby_Class22_Update) {
        return (void*)game_class22;
    }
    if (name == OPENRAC_RAC1_HOOK_Help_Request) {
        return (void*)game_help;
    }
    return (void*)game_moby;
}

void* replacement(const char* name) {
    for (const auto& [n, fn] : g_replacements) {
        if (n == name) {
            return fn;
        }
    }
    return nullptr;
}

// ---- the host ----

void host_log(openrac_mod*, int, const char* text) {
    std::printf("  mod: %s\n", text);
}

void host_fail(openrac_mod*, const char* reason) {
    std::printf("  mod failed: %s\n", reason);
}

int setting_bool(openrac_mod*, const char* key) {
    return std::strcmp(key, "reward_models") == 0 || std::strcmp(key, "reward_text") == 0;
}

int64_t setting_int(openrac_mod*, const char* key) {
    (void)key;
    return 0;
}

double setting_float(openrac_mod*, const char*) {
    return 0;
}

const char* setting_string(openrac_mod*, const char* key) {
    if (std::strcmp(key, "server") == 0) {
        return g_server.c_str();
    }
    if (std::strcmp(key, "slot") == 0) {
        return "Ratchet";
    }
    return "";
}

int host_hook(openrac_mod*, const char* name, const char*, void* fn, void** original) {
    ++g_hooks;
    if (g_missing_gold_hook && std::strcmp(name, OPENRAC_RAC1_HOOK_GoldBolts_CountUnspent) == 0) {
        return OPENRAC_HOOK_UNKNOWN;
    }
    g_replacements.emplace_back(name, fn);
    *original = original_for(name);
    return OPENRAC_HOOK_OK;
}

int host_on_frame(openrac_mod*, openrac_frame_fn fn, void* user) {
    g_frame = fn;
    g_frame_user = user;
    return OPENRAC_HOOK_OK;
}

void* host_function(const char*) {
    return nullptr;
}

openrac_gaddr identity(openrac_gaddr a) {
    return a;
}

openrac_gaddr identity_level(int, openrac_gaddr a) {
    return a;
}

int loaded_level() {
    return g_level;
}

int play_status(openrac_play_status* s) {
    s->phase = g_play_phase;
    s->level = g_level;
    s->loads = 1;
    return OPENRAC_HOOK_OK;
}

int scene_playing(int*) {
    return g_scene;
}

int moby_list(openrac_moby* out, int max) {
    const int n = std::min(max, int(g_test_mobys.size()));
    std::copy_n(g_test_mobys.begin(), n, out);
    return int(g_test_mobys.size());
}

int moby_deletion(int, openrac_moby_deletion*) {
    return OPENRAC_HOOK_UNKNOWN;
}

float acquisition_position[3] = {};
float acquisition_yaw = 0;
float acquisition_target[3] = {};
float acquisition_target_yaw = 0;
int acquisition_places = 0, acquisition_busy = 0;

int acquisition_place(openrac_mod*, const float* p, float yaw, uint32_t) {
    std::copy_n(p, 3, acquisition_target);
    acquisition_target_yaw = yaw;
    ++acquisition_places;
    return OPENRAC_HOOK_OK;
}

int acquisition_pose(openrac_pose* out) {
    out->level = g_level;
    out->health = 3;
    std::copy_n(acquisition_position, 3, out->position);
    out->rotation[2] = acquisition_yaw;
    return OPENRAC_HOOK_OK;
}

int acquisition_request(openrac_mod*, int kind, int item) {
    if (kind < 0) {
        return acquisition_busy;
    }
    g_acquisitions.emplace_back(kind, item);
    return kind ? 1 : 0;
}

int play_notice(openrac_mod*, const char* text) {
    g_notice = text != nullptr ? text : "";
    return OPENRAC_HOOK_OK;
}

ap_visual_api make_api() {
    ap_visual_api result{};
    auto& api = *ap_visual_base(&result);
    api.size = g_visual_mode ? sizeof result : sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = g_visual_mode ? 22 : 14;
    api.game = "rac1";
    api.version = D->version;
    api.memory = g_memory.data();
    api.log = host_log;
    api.fail = host_fail;
    api.setting_bool = setting_bool;
    api.setting_int = setting_int;
    api.setting_float = setting_float;
    api.setting_string = setting_string;
    api.hook = host_hook;
    api.data_dir = data_dir;
    api.on_frame = host_on_frame;
    api.function = host_function;
    api.relocate_data = identity;
    api.relocate_level_data = identity_level;
    api.loaded_level = loaded_level;
    api.play_status = play_status;
    api.scene_playing = scene_playing;
    api.moby_list = moby_list;
    api.moby_deletion = moby_deletion;
    api.play_notice = play_notice;
    api.hero_pose = acquisition_pose;
    api.hero_place = acquisition_place;
    result.moby_visuals = visual_submit;
    result.guest_text = guest_text;
    result.sprite_tint = sprite_tint;
    return result;
}

// ---- the server ----

std::mutex g_mutex;
std::set<std::int64_t> g_checked;
bool g_goal = false;
bool g_connected = false;
std::atomic<bool> g_stop{false};

// Frames to the client: a server's are not masked.
std::string server_frame(const std::string& text) {
    std::string f;
    f += char(0x81);
    if (text.size() < 126) {
        f += char(text.size());
    } else {
        f += char(126);
        f += char((text.size() >> 8) & 0xFF);
        f += char(text.size() & 0xFF);
    }
    return f + text;
}

bool send_all(socket_t s, const std::string& data) {
    return ::send(s, data.data(), int(data.size()), 0) == int(data.size());
}

void serve(socket_t listener) {
    socket_t c = accept(listener, nullptr, nullptr);
    std::string buffer;
    char chunk[4096];

    // The handshake.
    while (buffer.find("\r\n\r\n") == std::string::npos) {
        const long n = recv(c, chunk, sizeof chunk, 0);
        if (n <= 0) {
            return;
        }
        buffer.append(chunk, std::size_t(n));
    }

    const std::size_t k = buffer.find("Sec-WebSocket-Key: ") + 19;
    const std::string key = buffer.substr(k, buffer.find("\r\n", k) - k);
    buffer.erase(0, buffer.find("\r\n\r\n") + 4);
    send_all(
        c,
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Accept: "
            + ap::ws::accept_for(key) + "\r\n\r\n"
    );
    send_all(
        c,
        server_frame(R"([{"cmd":"RoomInfo","seed_name":"test-seed","games":["Ratchet & Clank"]}])")
    );

    while (!g_stop) {
        ap::ws::Opcode op;
        bool fin = false, bad = false;
        std::string payload;

        while (ap::ws::decode(buffer, op, fin, payload, bad)) {
            for (const json& p : json::parse(payload)) {
                const std::string cmd = p.at("cmd");
                std::lock_guard<std::mutex> lock(g_mutex);

                if (cmd == "Connect") {
                    CHECK(p.at("name") == "Ratchet" && p.at("game") == AP_GAME);
                    CHECK(p.at("slot_data") == true);
                    json connected = json::array({
                        {{"cmd", "Connected"},
                         {"team", 0},
                         {"slot", 1},
                         {"slot_data",
                          {{"start_in_new_game_plus", int(g_new_game_plus)},
                           {"shuffle_gold_weapons", int(g_gold_mode)},
                           {"npc_infobot_checks", int(g_visual_mode)}}},
                         {"players", json::array()},
                         {"missing_locations", json::array()},
                         {"checked_locations", json::array()},
                         {"slot_info",
                          {{"1", {{"name", "Ratchet"}, {"game", AP_GAME}, {"type", 1}}},
                           {"2", {{"name", "Link"}, {"game", "Other"}, {"type", 1}}}}}},
                        {{"cmd", "ReceivedItems"},
                         {"index", 0},
                         {"items",
                          json::array(
                              {{{"item", AP_BASE + ITEM_HELI_PACK},
                                {"location", 1},
                                {"player", 2},
                                {"flags", 1}}}
                          )}},
                    });
                    connected[1]["items"].push_back(
                        {{"item", AP_BASE + AP_GOLD_WEAPON_OFFSET + ITEM_DEVASTATOR},
                         {"location", 2},
                         {"player", 2},
                         {"flags", 1}}
                    );
                    for (int i = 0; i < (g_gold_mode ? 48 : 8); ++i) {
                        connected[1]["items"].push_back(
                            {{"item", AP_BASE + AP_GOLD_BOLT_OFFSET},
                             {"location", 3 + i},
                             {"player", 2},
                             {"flags", 0}}
                        );
                    }
                    if (g_visual_mode) {
                        for (int i = 0; i < AP_NPC_COUNT; ++i) {
                            connected[0]["missing_locations"].push_back(ap_npc_id(i));
                        }
                    }
                    send_all(c, server_frame(connected.dump()));
                    if (g_visual_mode) {
                        const auto pyro = location_id(LOC_VENDOR, LEVEL_NOVALIS, ITEM_PYROCITOR);
                        const auto bolt = location_id(LOC_GOLD, LEVEL_NOVALIS, 0);
                        const auto weapon_bolt = location_id(LOC_GOLD, LEVEL_NOVALIS, 1);
                        const auto retail_weapon =
                            location_id(LOC_VENDOR, LEVEL_KERWAN, ITEM_BLASTER);
                        const auto gadget_vendor =
                            location_id(LOC_VENDOR, LEVEL_BLARG, ITEM_TAUNTER);
                        const auto pickup_weapon_vendor =
                            location_id(LOC_VENDOR, LEVEL_BATALIA, ITEM_DEVASTATOR);
                        const auto foreign_bolt = location_id(LOC_GOLD, LEVEL_NOVALIS, 2);
                        const auto vanilla_bolt = location_id(LOC_GOLD, LEVEL_KERWAN, 0);
                        const auto helga = location_id(LOC_GIVE, LEVEL_KERWAN, ITEM_SWINGSHOT);
                        const auto al = location_id(LOC_GIVE, LEVEL_KERWAN, ITEM_HELI_PACK);
                        json info = json::array(
                            {{{"cmd", "DataPackage"},
                              {"data",
                               {{"games",
                                 {{AP_GAME,
                                   {{"item_name_to_id",
                                     {{"Gold Bolt", AP_BASE + AP_GOLD_BOLT_OFFSET},
                                      {"Aridia Coordinates",
                                       AP_BASE + AP_PLANET_OFFSET + LEVEL_ARIDIA}}},
                                    {"location_name_to_id", json::object()}}}}}}}},
                             {{"cmd", "LocationInfo"},
                              {"locations",
                               json::array(
                                   {{{"location", pyro},
                                     {"item", AP_BASE + AP_GOLD_BOLT_OFFSET},
                                     {"player", 1}},
                                    {{"location", bolt},
                                     {"item", AP_BASE + AP_PLANET_OFFSET + LEVEL_ARIDIA},
                                     {"player", 1}},
                                    {{"location", weapon_bolt},
                                     {"item", AP_BASE + ITEM_BLASTER},
                                     {"player", 1}},
                                    {{"location", retail_weapon},
                                     {"item", AP_BASE + ITEM_DEVASTATOR},
                                     {"player", 1}},
                                    {{"location", location_id(LOC_VENDOR, LEVEL_OLTANIS, ITEM_TESLA_CLAW)},
                                     {"item", AP_BASE + AP_GOLD_WEAPON_OFFSET + ITEM_DECOY_GLOVE},
                                     {"player", 1}},
                                    {{"location", gadget_vendor},
                                     {"item", AP_BASE + ITEM_HELI_PACK},
                                     {"player", 1}},
                                    {{"location", pickup_weapon_vendor},
                                     {"item", AP_BASE + ITEM_MORPH_O_RAY},
                                     {"player", 1}},
                                    {{"location", foreign_bolt},
                                     {"item", AP_BASE + ITEM_BLASTER},
                                     {"player", 2}},
                                    {{"location", vanilla_bolt},
                                     {"item", AP_BASE + AP_GOLD_BOLT_OFFSET},
                                     {"player", 1}},
                                    {{"location", helga},
                                     {"item", AP_BASE + AP_GOLD_BOLT_OFFSET},
                                     {"player", 1}},
                                    {{"location", al},
                                     {"item", AP_BASE + AP_PLANET_OFFSET + LEVEL_ARIDIA},
                                     {"player", 1}},
                                    {{"location",
                                      location_id(LOC_GIVE, LEVEL_EUDORA, ITEM_SUCK_CANNON)},
                                     {"item", AP_BASE + ITEM_DEVASTATOR},
                                     {"player", 1}},
                                    {{"location", location_id(LOC_INFOBOT, LEVEL_KERWAN, 3)},
                                     {"item", AP_BASE + ITEM_DEVASTATOR},
                                     {"player", 1}},
                                    {{"location", ap_gold_case_id(ITEM_PYROCITOR)},
                                     {"item", AP_BASE + AP_GOLD_BOLT_OFFSET},
                                     {"player", 1}},
                                    {{"location", ap_gold_case_id(ITEM_BLASTER)},
                                     {"item", AP_BASE + AP_PLANET_OFFSET + LEVEL_ARIDIA},
                                     {"player", 1}},
                                    {{"location", ap_gold_case_id(ITEM_DEVASTATOR)},
                                     {"item", AP_BASE + ITEM_BLASTER},
                                     {"player", 1}},
                                    {{"location", ap_gold_case_id(ITEM_BOMB_GLOVE)},
                                     {"item", 7},
                                     {"player", 2}}}
                               )}}}
                        );
                        send_all(c, server_frame(info.dump()));
                    }
                    g_connected = true;
                } else if (cmd == "LocationChecks") {
                    for (const json& id : p.at("locations")) {
                        g_checked.insert(id.get<std::int64_t>());
                    }
                } else if (cmd == "StatusUpdate") {
                    g_goal = p.at("status") == 30;
                }
            }
        }

        timeval tv{0, 20000};
        fd_set set;
        FD_ZERO(&set);
        FD_SET(c, &set);

        if (select(int(c) + 1, &set, nullptr, nullptr, &tv) > 0) {
            const long n = recv(c, chunk, sizeof chunk, 0);
            if (n <= 0) {
                break;
            }
            buffer.append(chunk, std::size_t(n));
        }
    }

    close_socket(c);
}

// Runs the mod's frames until cond holds (or a few seconds pass).
template <class F>
bool frames_until(F cond) {
    for (int i = 0; i < 400; ++i) {
        g_frame(g_frame_user);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (cond()) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

std::int64_t location_id(int kind, int level, int key) {
    for (std::size_t i = 0; i < rando_location_count; ++i) {
        const rando_location& l = rando_locations[i];
        if (l.kind == kind && l.level == level && l.key == key) {
            return ap_location_id(&l);
        }
    }
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: archipelago_load_test <mod>\n");
        return 2;
    }

    if (argc > 2 && std::strcmp(argv[2], "ntsc") == 0) {
        D = &k_rando_discs[1];
    }
    g_new_game_plus = argc > 3 && std::strcmp(argv[3], "ngplus") == 0;
    g_gold_mode = argc > 4 && std::strcmp(argv[4], "gold") == 0;
    g_missing_gold_hook = argc > 5 && std::strcmp(argv[5], "missing") == 0;
    g_visual_mode = argc > 6;
    g_acquisition_mode = argc > 7;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    // The server on a free port of the loopback.
    socket_t listener = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    listen(listener, 1);
    socklen_t len = sizeof addr;
    getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
    g_server = "ws://127.0.0.1:" + std::to_string(ntohs(addr.sin_port));
    std::thread server(serve, listener);

    void* lib = open_library(argv[1]);
    CHECK(lib != nullptr);
    if (lib == nullptr) {
        return 1;
    }
    auto query = (openrac_mod_query_fn)symbol(lib, OPENRAC_MOD_QUERY);
    auto load = (openrac_mod_load_fn)symbol(lib, OPENRAC_MOD_LOAD);
    auto unload = (openrac_mod_unload_fn)symbol(lib, OPENRAC_MOD_UNLOAD);
    openrac_mod_info info{};
    query(&info);
    CHECK(std::strcmp(info.id, "archipelago") == 0);

    set_level(LEVEL_NOVALIS);
    *at(D->planets + LEVEL_NOVALIS) = 1;
    const auto dir =
        std::filesystem::current_path()
        / ("ap-save-test-"
           + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    g_data_dir = dir.string();
    {
        std::ofstream book(dir / "save-slots.tsv");
        book << "4\told-seed\tRatchet\n";
    }
    auto api = make_api();
    if (g_acquisition_mode) {
        api.pickup_animation = acquisition_request;
    }
    CHECK(load(ap_visual_base(&api), reinterpret_cast<openrac_mod*>(1)) == 0);
    CHECK(g_hooks == (g_acquisition_mode ? 55 : g_visual_mode ? 47 : 40) && g_frame != nullptr);
    CHECK(replacement("rac1.moby.moby743.update") != nullptr);
    CHECK(replacement("rac1.moby.moby744.update") != nullptr);
    const std::uint32_t challenge = std::strcmp(D->version, "pal") == 0 ? 0x15EFA0 : 0x15EEA0;
    CHECK(*at(challenge) == 0);

    // A new save can remember an unowned Bomb Glove in both equip shortcuts. Stock cleanup
    // must not be required: this reproduces the start with no newly inserted ammunition row.
    const std::uint32_t hero = std::strcmp(D->version, "pal") == 0 ? 0x13F450 : 0x13F350;
    const auto put = [](std::uint32_t address, int value) {
        std::memcpy(at(address), &value, 4);
    };
    const auto get = [](std::uint32_t address) {
        int value;
        std::memcpy(&value, at(address), 4);
        return value;
    };
    auto init_stock =
        (openrac_rac1_Vendor_InitLevelStock)replacement(OPENRAC_RAC1_HOOK_Vendor_InitLevelStock);
    set_level(LEVEL_VELDIN);
    put(D->last_weapon, ITEM_BOMB_GLOVE);
    put(D->level + 8, ITEM_BOMB_GLOVE);
    put(hero + 0x10B8, ITEM_WRENCH);
    put(hero + 0x20B8, ITEM_BOMB_GLOVE);
    put(hero + 0x20F0, ITEM_BOMB_GLOVE);
    init_stock();
    CHECK(get(D->last_weapon) == ITEM_WRENCH);
    CHECK(get(D->level + 8) == ITEM_WRENCH);
    CHECK(get(hero + 0x20B8) == ITEM_WRENCH);
    CHECK(get(hero + 0x20F0) == ITEM_WRENCH);
    CHECK(*at(D->owned + ITEM_BOMB_GLOVE) == 0);

    // A late initialization, death or loaded save can restore those words. The frame guard
    // queues the normal wrench switch for a live Bomb Glove, without rewriting the live slot.
    put(D->last_weapon, ITEM_BOMB_GLOVE);
    put(D->level + 8, ITEM_BOMB_GLOVE);
    put(hero + 0x10B8, ITEM_BOMB_GLOVE);
    put(hero + 0x20D4, ITEM_BOMB_GLOVE);
    put(hero + 0x20B8, 0);
    g_frame(g_frame_user);
    CHECK(get(D->last_weapon) == ITEM_WRENCH && get(D->level + 8) == ITEM_WRENCH);
    CHECK(get(hero + 0x20B8) == ITEM_WRENCH);
    CHECK(get(hero + 0x10B8) == ITEM_BOMB_GLOVE);  // game owns the moby switch

    // A valid weapon selection is preserved, and an owned Bomb Glove remains usable.
    *at(D->owned + ITEM_TESLA_CLAW) = 1;
    put(D->last_weapon, ITEM_TESLA_CLAW);
    put(D->level + 8, ITEM_TESLA_CLAW);
    put(hero + 0x20B8, ITEM_TESLA_CLAW);
    g_frame(g_frame_user);
    CHECK(get(D->last_weapon) == ITEM_TESLA_CLAW && get(D->level + 8) == ITEM_TESLA_CLAW);
    CHECK(get(hero + 0x20B8) == ITEM_TESLA_CLAW);
    // Run on both discs: an out-of-order starting weapon must reach the selector.
    auto drop = (openrac_rac1_Ammo_ChooseDrop)replacement(OPENRAC_RAC1_HOOK_Ammo_ChooseDrop);
    constexpr uint32_t output = 0x120000, dropper = 0x121000;
    std::vector<uint8_t> saved_owned(at(D->owned), at(D->owned) + RANDO_ITEM_COUNT);
    std::vector<uint8_t> saved_prices(
        at(D->item_price_table), at(D->item_price_table) + VENDOR_ITEM_COUNT * 0x18
    );
    std::vector<uint8_t> saved_stock(at(D->vendor_stock), at(D->vendor_stock) + VENDOR_STOCK_SIZE);
    std::memset(at(D->owned), 0, RANDO_ITEM_COUNT);
    std::memset(at(D->item_price_table), 0, VENDOR_ITEM_COUNT * 0x18);
    std::memset(at(D->vendor_stock), 0xff, VENDOR_STOCK_SIZE);
    *at(D->vendor_stock) = ITEM_BOMB_GLOVE | 0x40;
    std::vector<uint8_t> real_stock(at(D->vendor_stock), at(D->vendor_stock) + VENDOR_STOCK_SIZE);
    *at(D->owned + ITEM_DEVASTATOR) = 1;
    *at(D->owned + ITEM_HELI_PACK) = 1;  // owned non-ammo gadget must be excluded
    *at(D->item_price_table + ITEM_DEVASTATOR * 0x18 + 8) = 1;
    *at(D->item_price_table + ITEM_BOMB_GLOVE * 0x18 + 8) = 1;  // unowned must be excluded
    put(output, -1);
    CHECK(drop(dropper, output) == 2);
    CHECK(g_drop_moby == dropper && get(output) == ITEM_DEVASTATOR);
    CHECK(g_drop_stock[0] == (ITEM_DEVASTATOR | 0x40));
    for (int i = 1; i < VENDOR_STOCK_SIZE; ++i) {
        CHECK(g_drop_stock[i] == 0xff);
    }
    CHECK(std::memcmp(at(D->vendor_stock), real_stock.data(), VENDOR_STOCK_SIZE) == 0);
    // Assigned first-break ammo must pass through, even if that weapon is unowned.
    put(output, ITEM_BOMB_GLOVE);
    CHECK(drop(dropper, output) == 2 && get(output) == ITEM_BOMB_GLOVE);
    // No candidates: delegate the retail fallback with an empty, terminated list.
    *at(D->owned + ITEM_DEVASTATOR) = 0;
    put(output, -1);
    CHECK(drop(0, output) == 2 && get(output) == ITEM_BOMB_GLOVE);
    CHECK(g_drop_stock[0] == 0xff && g_drop_moby == 0);
    CHECK(std::memcmp(at(D->vendor_stock), real_stock.data(), VENDOR_STOCK_SIZE) == 0);
    std::memcpy(at(D->owned), saved_owned.data(), saved_owned.size());
    std::memcpy(at(D->item_price_table), saved_prices.data(), saved_prices.size());
    std::memcpy(at(D->vendor_stock), saved_stock.data(), saved_stock.size());

    *at(D->owned + ITEM_BOMB_GLOVE) = 1;
    put(D->last_weapon, ITEM_BOMB_GLOVE);
    put(D->level + 8, ITEM_BOMB_GLOVE);
    put(hero + 0x20B8, ITEM_BOMB_GLOVE);
    g_frame(g_frame_user);
    CHECK(get(D->last_weapon) == ITEM_BOMB_GLOVE && get(D->level + 8) == ITEM_BOMB_GLOVE);
    CHECK(get(hero + 0x20B8) == ITEM_BOMB_GLOVE);
    *at(D->owned + ITEM_BOMB_GLOVE) = 0;
    set_level(LEVEL_NOVALIS);

    // The server's Heli-Pack is given, its obtained byte left to its own location.
    CHECK(frames_until([] { return g_connected; }));
    CHECK(frames_until([] { return *at(D->owned + ITEM_HELI_PACK) != 0; }));
    CHECK(*at(D->obtained + ITEM_HELI_PACK) == 0);
    // The last shuffled vendor reward can be gone while an out-of-stock starting
    // weapon still needs ammunition. Keep the vendor usable in that case, and
    // restore it if ammunition becomes available after an empty-stock guard.
    {
        uint8_t saved_owned[RANDO_ITEM_COUNT], saved_stock[VENDOR_STOCK_SIZE];
        std::memcpy(saved_owned, at(D->owned), sizeof saved_owned);
        std::memcpy(saved_stock, at(D->vendor_stock), sizeof saved_stock);
        std::memset(at(D->owned), 0, sizeof saved_owned);
        std::memset(at(D->vendor_stock), 0xFF, sizeof saved_stock);
        const uint32_t pool = std::strcmp(D->version, "pal") == 0 ? 0x160098u : 0x15FFD8u;
        put(pool, 0x1800000);
        const int16_t vendor_class = 0xB;
        std::memcpy(at(0x1800000 + 0xA6), &vendor_class, 2);
        *at(0x1800000 + 0x52) = 7;
        openrac_moby vendor{};
        vendor.slot = 0;
        vendor.oclass = vendor_class;
        g_test_mobys = {vendor};
        uint16_t saved_price;
        std::memcpy(&saved_price, at(D->item_price_table + ITEM_GLOVE_OF_DOOM * 0x18 + 8), 2);
        const uint16_t ammo_price = 1;
        std::memcpy(at(D->item_price_table + ITEM_GLOVE_OF_DOOM * 0x18 + 8), &ammo_price, 2);
        g_play_phase = OPENRAC_PLAY_MENU;  // do not apply server gifts during the fixture
        *at(D->owned + ITEM_GLOVE_OF_DOOM) = 1;
        g_frame(g_frame_user);
        CHECK(*at(0x1800000 + 0x52) == 7);
        *at(D->owned + ITEM_GLOVE_OF_DOOM) = 0;
        *at(D->owned + ITEM_WALLOPER) = 1;  // no ammunition to sell
        g_frame(g_frame_user);
        CHECK(*at(0x1800000 + 0x52) == 0);
        *at(D->owned + ITEM_GLOVE_OF_DOOM) = 1;
        g_frame(g_frame_user);
        CHECK(*at(0x1800000 + 0x52) == 7);
        std::memcpy(at(D->item_price_table + ITEM_GLOVE_OF_DOOM * 0x18 + 8), &saved_price, 2);
        g_test_mobys.clear();
        put(pool, 0);
        std::memcpy(at(D->owned), saved_owned, sizeof saved_owned);
        std::memcpy(at(D->vendor_stock), saved_stock, sizeof saved_stock);
        g_play_phase = OPENRAC_PLAY_PLAYING;
    }
    CHECK(frames_until([challenge] { return *at(challenge) == int(g_new_game_plus); }));
    if (g_new_game_plus) {
        set_level(-1);
        *at(challenge) = 0;
        g_frame(g_frame_user);
        CHECK(*at(challenge) == 0);
        set_level(LEVEL_NOVALIS);
        g_frame(g_frame_user);
        CHECK(*at(challenge) == 1);
        CHECK(*at(D->owned + ITEM_RYNO) == 0);  // no completed-save inventory imported
    } else {
        *at(challenge) = 1;
        g_frame(g_frame_user);
        CHECK(*at(challenge) == 1);  // false does not downgrade an earned NG+ save
        *at(challenge) = 0;
    }

    // Gold items grant their base weapon and the real retail upgrade without buying a case.
    const std::uint32_t gold = std::strcmp(D->version, "pal") == 0 ? 0x13E620 : 0x13E520;
    CHECK(frames_until([gold] { return *at(gold + ITEM_DEVASTATOR) != 0; }));
    CHECK(*at(D->owned + ITEM_DEVASTATOR) == 1);
    CHECK(*at(D->obtained + ITEM_DEVASTATOR) == 0);
    if (g_gold_mode && !g_missing_gold_hook) {
        g_unspent = (int32_t (*)())replacement(OPENRAC_RAC1_HOOK_GoldBolts_CountUnspent);
        CHECK(g_unspent() == 40);  // gold drops cap at 40; the received upgrade spends nothing
        auto update_case = (openrac_rac1_Moby_GoldWeaponCase_Update)replacement(
            OPENRAC_RAC1_HOOK_Moby_GoldWeaponCase_Update
        );
        const std::uint32_t moby = 0x100000, vars = 0x110000;
        put(moby + MOBY_PVARS, vars);
        const int16_t cls = 304;
        std::memcpy(at(moby + 0xA6), &cls, 2);
        put(vars + 0x40, 0);
        put(std::strcmp(D->version, "pal") == 0 ? 0x1DEDE0 : 0x1DEB10, ITEM_BOMB_GLOVE);
        *at(vars + 4) = 2;
        const int before = g_bolts;
        g_bolts = 999;
        update_case(moby);
        CHECK(g_bolts == 999 && g_unspent() == 40);  // unaffordable cases do not check or charge
        g_bolts = before;
        *at(D->owned + ITEM_BOMB_GLOVE) =
            1; /* retail prerequisite; removed by separate gold-case PR */
        update_case(moby);
        CHECK(g_bolts == before - 1000 && g_unspent() == 36);
        CHECK(*at(gold + ITEM_BOMB_GLOVE) == 0);  // the original case did not grant its gold weapon
        const std::int64_t check = ap_gold_case_id(ITEM_BOMB_GLOVE);
        CHECK(frames_until([check] { return g_checked.count(check) != 0; }));
        const int after = g_bolts;
        update_case(moby);
        CHECK(
            g_bolts == after && g_unspent() == 36
        );  // duplicate case, across either shop, costs nothing
        auto restart =
            (item_randomizer_Save_NewGamePlus)replacement(ITEM_RANDOMIZER_HOOK_Save_NewGamePlus);
        restart(0);
        CHECK(g_unspent() == 36);  // completed-save restart keeps purchased checks/currency
        update_case(moby);
        CHECK(g_bolts == after);
    }

    // Its banner, in another level's text, is not left on screen.
    CHECK(g_gives > 0 && std::strcmp(reinterpret_cast<char*>(at(D->banner_text)), "Paradox") != 0);

    // A tutorial whose place is not known yet stays the game's.
    auto help = (openrac_rac1_Help_Request)replacement(OPENRAC_RAC1_HOOK_Help_Request);
    CHECK(help(rando_item_helps[0].message, 0) == 0 && g_helps == 1);

    // Orxon: no O2 Mask shown until Clank's part is done; the real byte back; then as it is.
    auto ship =
        (openrac_rac1_Ship_InitTransition)replacement(OPENRAC_RAC1_HOOK_Ship_InitTransition);
    auto class22 =
        (openrac_rac1_Moby_Class22_Update)replacement(OPENRAC_RAC1_HOOK_Moby_Class22_Update);
    set_level(LEVEL_ORXON);
    *at(D->owned + ITEM_O2_MASK) = 1;
    ship(0);
    class22(0x00100000u);
    CHECK(g_ship_saw == 0 && g_class22_saw == 0 && *at(D->owned + ITEM_O2_MASK) == 1);
    *at(D->triggers + LEVEL_ORXON * 16 + ORXON_CLANK_MISSION) = 0xFF;
    ship(0);
    class22(0x00100000u);
    CHECK(g_ship_saw == 1 && g_class22_saw == 1);
    *at(D->triggers + LEVEL_ORXON * 16 + ORXON_CLANK_MISSION) = 0;
    *at(D->owned + ITEM_O2_MASK) = 0;
    set_level(LEVEL_NOVALIS);

    if (g_visual_mode) {
        const uint32_t definitions = std::strcmp(D->version, "pal") == 0 ? 0x1864D0u : 0x1863D0u;
        put(definitions + ITEM_PYROCITOR * 0x4C, 256);
        const uint16_t pyro_icon = 50;
        std::memcpy(at(definitions + ITEM_PYROCITOR * 0x4C + 0x38), &pyro_icon, 2);
        put(D->vendor_menu + VENDOR_CURSOR, 0);
        put(D->vendor_menu + VENDOR_COUNT, 1);
        put(D->vendor_menu + VENDOR_ENTRIES, ITEM_PYROCITOR);
        put(D->vendor_menu + VENDOR_ENTRIES + 4, 0);
        put(D->vendor_menu + 0x20, 0x1A00000);
        // Verified NTSC level mode: D_L00_0015F5C4. The nearby 15F5A8 word
        // stays zero, reproducing the real vendor that exposed the bad address.
        if (std::strcmp(D->version, "ntsc") == 0) put(0x15F5A8u, 0);
        const uint32_t game_mode = std::strcmp(D->version, "pal") == 0 ? 0x15F6A8u : 0x15F5C4u;
        put(game_mode, 5);
        g_play_phase = OPENRAC_PLAY_MENU;
        auto initialize = (void (*)())replacement("rac1.vendor.initialize_selected");
        auto details = (void (*)())replacement("rac1.vendor.draw_details");
        const uint32_t offsets[] = {0x1BC0, 0x14D0, 0xB90};
        const int sizes[] = {0x30, 0x40, 0x30};
        for (int i = 0; i < 3; ++i) {
            std::memset(
                at(D->vendor_menu - offsets[i] + ITEM_BLASTER * sizes[i]), 0x11 + i, sizes[i]
            );
            std::memset(
                at(D->vendor_menu - offsets[i] + ITEM_DEVASTATOR * sizes[i]), 0x66 + i, sizes[i]
            );
        }

        initialize();
        CHECK(frames_until([&] {
            details();
            return g_rendered_name == "Gold Bolt";
        }));
        CHECK(std::any_of(g_visuals.begin(), g_visuals.end(), [](const auto& v) {
            return v.moby == 0x1A00100 && v.oclass == 0x46E;
        }));
        // The scrolling banner asks outside the details pass, using its own text id.
        const uint32_t banner_table = std::strcmp(D->version, "pal") == 0 ? 0x1C2488u : 0x1C2108u;
        put(banner_table + ITEM_PYROCITOR * 0x14 + 0x10, 258);
        auto vendor_message = (uint32_t (*)(int))replacement("rac1.text.msg_string");
        CHECK(std::string(reinterpret_cast<const char*>(at(vendor_message(258)))) == "Gold Bolt");
        auto icons = (void (*)())replacement("rac1.vendor.draw_icons");
        icons();
        CHECK(g_rendered_icon == 0x754F * 10 && g_ammo_icon == 502);
        // Inventory uses the same level and can reuse the preview's addresses.
        put(game_mode, 3);
        g_frame(g_frame_user);
        CHECK(g_visuals.empty());
        // A vendor weapon uses its own retail display rows, not the source slot's rows.
        put(game_mode, 5);
        const uint32_t retail_models = std::strcmp(D->version, "pal") == 0 ? 0x179BC0u : 0x179AC0u;
        put(retail_models + ITEM_DEVASTATOR * 0x4C + 0x10, 0x601);
        put(D->vendor_menu + VENDOR_ENTRIES, ITEM_BLASTER);
        initialize();
        CHECK(g_vendor_initialized_item == ITEM_DEVASTATOR);
        CHECK(
            get(D->vendor_menu + VENDOR_ENTRIES) == ITEM_BLASTER
        );  // actual purchase id unchanged
        for (int i = 0; i < 3; ++i) {
            CHECK(*at(D->vendor_menu - offsets[i] + ITEM_BLASTER * sizes[i]) == 0x66 + i);
        }
        g_frame(g_frame_user);
        CHECK(std::none_of(g_visuals.begin(), g_visuals.end(), [](const auto& v) {
            return v.moby == 0x1A00200 && v.oclass == -1;
        }));  // keep any native secondary model
        put(game_mode, 0);
        g_frame(g_frame_user);
        for (int i = 0; i < 3; ++i) {
            CHECK(*at(D->vendor_menu - offsets[i] + ITEM_BLASTER * sizes[i]) == 0x11 + i);
        }
        // Devastator's own row can have been borrowed by another randomized reward.
        // Its next appearance must still use the original retail setup in all three tables.
        for (int i = 0; i < 3; ++i) {
            std::memset(
                at(D->vendor_menu - offsets[i] + ITEM_DEVASTATOR * sizes[i]), 0x99, sizes[i]
            );
        }
        put(game_mode, 5);
        initialize();
        CHECK(g_vendor_initialized_item == ITEM_DEVASTATOR);
        CHECK(get(D->vendor_menu + VENDOR_ENTRIES) == ITEM_BLASTER);
        for (int i = 0; i < 3; ++i) {
            for (int byte = 0; byte < sizes[i]; ++byte) {
                CHECK(
                    *at(D->vendor_menu - offsets[i] + ITEM_DEVASTATOR * sizes[i] + byte) == 0x66 + i
                );
                CHECK(
                    *at(D->vendor_menu - offsets[i] + ITEM_BLASTER * sizes[i] + byte) == 0x66 + i
                );
            }
        }
        put(game_mode, 0);
        g_frame(g_frame_user);
        // A gadget or pickup-only weapon gets the same native spinning display,
        // with its real geometry present in BOTH the inset and center hologram.
        for (const auto item : {ITEM_HELI_PACK, ITEM_MORPH_O_RAY}) {
            const int source = item == ITEM_HELI_PACK ? ITEM_TAUNTER : ITEM_DEVASTATOR;
            const int model = item == ITEM_HELI_PACK ? 607 : 0x603;
            for (int i = 0; i < 3; ++i) {
                std::memset(at(D->vendor_menu - offsets[i] + item * sizes[i]), 0x33, sizes[i]);
            }
            put(retail_models + item * 0x4C + 0x10, item == ITEM_HELI_PACK ? 0 : model);
            put(D->vendor_menu + VENDOR_ENTRIES, source);
            put(game_mode, 5);
            initialize();
            CHECK(g_vendor_initialized_item == ITEM_DEVASTATOR);
            CHECK(get(D->vendor_menu + VENDOR_ENTRIES) == source);
            CHECK(*at(D->vendor_menu - offsets[0] + ITEM_DEVASTATOR * sizes[0]) == 0x66);
            if (item == ITEM_HELI_PACK) {
                CHECK(get(D->vendor_menu - offsets[0] + source * sizes[0] + 0x14) == 0);
                CHECK(get(D->vendor_menu - offsets[2] + source * sizes[2] + 0x14) == 0);
            }
            g_frame(g_frame_user);
            for (const uint32_t address : {0x1A00100u, 0x1A00300u}) {
                CHECK(std::any_of(g_visuals.begin(), g_visuals.end(), [&](const auto& v) {
                    return v.moby == address && v.oclass == model;
                }));
            }
            put(game_mode, 0);
            g_frame(g_frame_user);
        }
        // A gold vendor reward must submit the real gold mesh for both monitors,
        // even when retail initialized a normal Decoy Glove to provide its pose.
        put(retail_models + ITEM_DECOY_GLOVE * 0x4C + 0x10, 562);
        put(D->vendor_menu + VENDOR_ENTRIES, ITEM_TESLA_CLAW);
        put(game_mode, 5);
        initialize();
        g_frame(g_frame_user);
        for (const uint32_t address : {0x1A00100u, 0x1A00300u}) {
            CHECK(std::any_of(g_visuals.begin(), g_visuals.end(), [&](const auto& v) {
                return v.moby == address && v.oclass == 1462;
            }));
        }
        put(D->vendor_menu + VENDOR_ENTRIES, ITEM_PYROCITOR);
        put(game_mode, 0);
        g_play_phase = OPENRAC_PLAY_PLAYING;
        const uint32_t pool_pointer = std::strcmp(D->version, "pal") == 0 ? 0x160098u : 0x15FFD8u;
        put(pool_pointer, 0x1800000);
        put(0x1800000 + 0x78, 0x1900000);
        put(0x1900000, 0);
        const int16_t bolt_class = 0x46E;
        std::memcpy(at(0x1800000 + 0xA6), &bolt_class, 2);
        openrac_moby bolt_moby{};
        bolt_moby.slot = 0;
        bolt_moby.oclass = bolt_class;
        g_test_mobys = {bolt_moby};
        g_frame(g_frame_user);
        CHECK(std::any_of(g_visuals.begin(), g_visuals.end(), [](const auto& v) {
            return v.moby == 0x1800000 && v.oclass == 0x2EE;
        }));
        // Real Novalis Gold Bolts occupy slots 958-960, beyond the old 512 cap.
        const uint32_t model_table = std::strcmp(D->version, "pal") == 0 ? 0x179BC0u : 0x179AC0u;
        put(model_table + ITEM_BLASTER * 0x4C + 0x10, 0x600);
        g_test_mobys.assign(960, {});
        for (auto& moby : g_test_mobys) {
            moby.slot = -1;
            moby.flags = OPENRAC_MOBY_DEAD;
        }
        for (int key = 0; key < 3; ++key) {
            const int slot = 958 + key;
            const uint32_t address = 0x1800000 + slot * 0x100;
            const uint32_t vars = 0x1910000 + key * 0x100;
            std::memcpy(at(address + 0xA6), &bolt_class, 2);
            put(address + MOBY_PVARS, vars);
            put(vars, key);
            auto& moby = g_test_mobys[957 + key];
            moby = {};
            moby.slot = slot;
            moby.oclass = bolt_class;
        }
        g_frame(g_frame_user);
        for (int key = 0; key < 2; ++key) {
            const int expected[] = {0x2EE, 0x600, 0x46E};
            CHECK(std::any_of(g_visuals.begin(), g_visuals.end(), [&](const auto& v) {
                return v.moby == 0x1800000 + (958 + key) * 0x100 && v.oclass == expected[key];
            }));
        }
        CHECK(std::none_of(g_visuals.begin(), g_visuals.end(), [](const auto& v) {
            return v.moby == 0x1800000 + 960 * 0x100;
        }));  // foreign-game fallback leaves the original Gold Bolt, including its size
        set_level(LEVEL_KERWAN);
        g_frame(g_frame_user);
        CHECK(std::none_of(g_visuals.begin(), g_visuals.end(), [](const auto& v) {
            return v.moby == 0x1800000 + 958 * 0x100;
        }));  // a real Gold Bolt reward keeps its model
        set_level(LEVEL_NOVALIS);
        g_test_mobys = {bolt_moby};
        g_play_phase = OPENRAC_PLAY_MENU;
        put(game_mode, 3);
        g_frame(g_frame_user);
        CHECK(g_visuals.empty());  // no world-class replacement in the gadgets menu
        g_play_phase = OPENRAC_PLAY_SCENE;
        put(game_mode, 2);
        g_frame(g_frame_user);
        CHECK(g_visuals.empty());  // NPC handoff scenes keep their original props
        g_play_phase = OPENRAC_PLAY_PLAYING;
        put(game_mode, 0);
        const int16_t case_class = 0x5B0;
        std::memcpy(at(0x1800000 + 0xA6), &case_class, 2);
        g_frame(g_frame_user);
        CHECK(std::any_of(g_visuals.begin(), g_visuals.end(), [](const auto& v) {
            return v.moby == 0x1800000 && v.oclass == 0x46E;
        }));
        CHECK(get(0x1900000) == 0);  // visual swap leaves collection state untouched
        const auto upgrades = std::strcmp(D->version, "pal") == 0 ? 0x13E620u : 0x13E520u;
        *at(upgrades + ITEM_BOMB_GLOVE) = 1;
        *at(0x1800000 + 0x20) = 0;
        auto gold_update =
            (void (*)(uint32_t))replacement(OPENRAC_RAC1_HOOK_Moby_GoldWeaponCase_Update);
        // Remove earlier test purchases so this display is still for sale.
        put(D->save_tag, 0);
        gold_update(0x1800000);
        CHECK(*at(0x1800000 + 0x20) == 0 && *at(upgrades + ITEM_BOMB_GLOVE) == 1);

        // Al's scouted reward is an Infobot, but his Heli-Pack prop stays original.
        set_level(LEVEL_KERWAN);
        const uint32_t class_table = std::strcmp(D->version, "pal") == 0 ? 0x179BC0u : 0x179AC0u;
        const int16_t pack_class = 0x555;
        put(class_table + ITEM_HELI_PACK * 0x4C + 0x10, pack_class);
        std::memcpy(at(0x1800000 + 0xA6), &pack_class, 2);
        *at(0x1800000 + 0x34) = 0;
        g_frame(g_frame_user);
        CHECK(g_visuals.empty());

        // Eudora uses class 1120 for the pickup, not the inventory Suck Cannon class.
        set_level(LEVEL_EUDORA);
        const int16_t suck_pickup_class = 1120;
        std::memcpy(at(0x1800000 + 0xA6), &suck_pickup_class, 2);
        *at(D->obtained + ITEM_SUCK_CANNON) = 0;
        g_frame(g_frame_user);
        CHECK(std::any_of(g_visuals.begin(), g_visuals.end(), [](const auto& v) {
            return v.moby == 0x1800000 && v.oclass == 0x601 && v.height == 1;
        }));
        g_play_phase = OPENRAC_PLAY_SCENE;
        g_frame(g_frame_user);
        CHECK(g_visuals.empty());  // the handoff/cutscene remains native
        g_play_phase = OPENRAC_PLAY_PLAYING;

        g_test_mobys.clear();
        put(pool_pointer, 0);
        auto dialog = (int (*)(uint32_t, uint32_t))replacement("rac1.npc.try_dialog");
        auto message = (uint32_t (*)(int))replacement("rac1.text.msg_string");
        const int16_t helga_class = 0x37A;
        std::memcpy(at(0x1800000 + 0xA6), &helga_class, 2);
        set_level(LEVEL_KERWAN);
        put(std::strcmp(D->version, "pal") == 0 ? 0x15F6A8u : 0x15F5C4u, 2);
        put(std::strcmp(D->version, "pal") == 0 ? 0x15F6A8u : 0x15F5C4u, 0);
        g_dialog_starts = false;
        CHECK(dialog(0x1800000, 0) == 0 && g_rendered_prompt == "Buy Gold Bolt for 500 bolts");
        g_dialog_starts = true;
        put(std::strcmp(D->version, "pal") == 0 ? 0x15F6A8u : 0x15F5C4u, 2);
        CHECK(dialog(0x1800000, 0) == 1);
        CHECK(
            std::strcmp(reinterpret_cast<char*>(at(message(257))), "Buy Gold Bolt for 500 bolts")
            == 0
        );
        {
            // Helga is the NPC reward text's: one owner names her selected prompt, and any other
            // text of her interaction (speech, another entry) is not rewritten here either.
            const uint32_t request = 0x1803000, entries = 0x1804000;
            const int16_t index = 0;
            uint16_t entry_msg = 257;
            put(request + 0x3C, entries);
            std::memcpy(at(request + 0x36), &index, 2);
            std::memcpy(at(entries), &entry_msg, 2);
            set_level(LEVEL_KERWAN);
            g_dialog_starts = false;
            CHECK(dialog(0x1800000, request) == 0);
            CHECK(g_rendered_prompt == "Buy Gold Bolt for 500 bolts");
            entry_msg = 258;
            std::memcpy(at(entries), &entry_msg, 2);
            CHECK(dialog(0x1800000, request) == 0);
            CHECK(g_rendered_prompt == "Buy Infobot for 500 bolts");
            g_dialog_starts = true;
        }
        set_level(LEVEL_NOVALIS);
        if (g_gold_mode) {
            const uint32_t moby = 0x1800000, request = 0x1801000, entries = 0x1802000;
            const int16_t cls = 304, msg = 257, index = 0, cost_kind = 6;
            std::memcpy(at(moby + 0xA6), &cls, 2);
            put(moby + MOBY_PVARS, request);
            put(request + 0x3C, entries);
            std::memcpy(at(request + 0x36), &index, 2);
            put(request + 0x40, 0);
            const uint32_t table = std::strcmp(D->version, "pal") == 0 ? 0x1DEDE0u : 0x1DEB10u;
            put(table, ITEM_PYROCITOR);  // scouted Gold Bolt
            std::memcpy(at(entries), &msg, 2);
            std::memcpy(at(entries + 8), &cost_kind, 2);
            g_dialog_starts = false;
            CHECK(dialog(moby, request) == 0);
            CHECK(g_rendered_prompt == "Buy Gold Bolt for %d bolts + 4 Gold Bolts");
            g_dialog_starts = true;
            CHECK(dialog(moby, request) == 1);
            CHECK(g_rendered_prompt == "Buy Gold Bolt for %d bolts + 4 Gold Bolts");
            const int16_t plain_kind = 0;
            std::memcpy(at(entries + 8), &plain_kind, 2);
            const uint32_t prices = std::strcmp(D->version, "pal") == 0 ? 0x1C47B0u : 0x1C4530u;
            const uint16_t price = 2500;
            std::memcpy(at(prices + ITEM_PYROCITOR * 0x18 + 0x14), &price, 2);
            dialog(moby, request);
            CHECK(g_rendered_prompt == "Buy Gold Bolt for 2,500 bolts + 4 Gold Bolts");
        }
    }

    if (g_acquisition_mode) {
        g_play_phase = OPENRAC_PLAY_PLAYING;
        set_level(LEVEL_NOVALIS);
        const uint32_t moby = 0x1800000, vars = 0x1900000;
        const int16_t bolt_class = 1134;
        std::memcpy(at(moby + 0xA6), &bolt_class, 2);
        put(moby + 0x78, vars);
        put(vars, 1);
        std::memset(at(moby + 0x10), 0, 12);
        *at(moby + 0x20) = 1;
        *at(D->gold_bolts + LEVEL_NOVALIS * 4 + 1) = 0;
        *at(D->owned + ITEM_BLASTER) = 1;
        ((void (*)(uint32_t))replacement("rac1.moby.gold_bolt.update"))(moby);
        g_frame(g_frame_user);
        CHECK(*at(D->gold_bolts + LEVEL_NOVALIS * 4 + 1) == 1 && *at(moby + 0x20) == 0xFD);
        CHECK(
            !g_acquisitions.empty() && g_acquisitions.back() == std::make_pair(2, int(ITEM_BLASTER))
        );
        // A foreign reward collects immediately, with no Ratchet-specific sequence.
        put(vars, 2);
        *at(moby + 0x20) = 1;
        ((void (*)(uint32_t))replacement("rac1.moby.gold_bolt.update"))(moby);
        g_frame(g_frame_user);
        CHECK(g_acquisitions.back().first == 0);
        put(vars, 0);
        *at(moby + 0x20) = 1;
        ((void (*)(uint32_t))replacement("rac1.moby.gold_bolt.update"))(moby);
        g_frame(g_frame_user);
        CHECK(g_acquisitions.back() == std::make_pair(3, int(LEVEL_ARIDIA)));
        set_level(LEVEL_KERWAN);
        const int16_t bot_class = 750;
        std::memcpy(at(moby + 0xA6), &bot_class, 2);
        put(vars + 4, LEVEL_EUDORA);
        *at(moby + 0xB0) = 3;
        *at(moby + 0x20) = 8;
        *at(D->triggers + LEVEL_KERWAN * 16 + 3) = 0;
        *at(D->owned + ITEM_DEVASTATOR) = 1;
        ((void (*)(uint32_t))replacement(OPENRAC_RAC1_HOOK_Moby_Infobot_Update))(moby);
        g_frame(g_frame_user);
        CHECK(*at(D->triggers + LEVEL_KERWAN * 16 + 3) == 0xFF && *at(moby + 0x20) == 0xFD);
        CHECK(g_acquisitions.back() == std::make_pair(2, int(ITEM_DEVASTATOR)));
        set_level(LEVEL_NOVALIS);
        // Vendor presentations wait for exit and only the final purchase survives.
        auto buy = (openrac_rac1_Item_Give)replacement(OPENRAC_RAC1_HOOK_Item_Give);
        auto purchase = [&](int item) {
            put(D->vendor_menu + VENDOR_DIALOG, VENDOR_DIALOG_BUY);
            put(D->vendor_menu + VENDOR_CURSOR, 0);
            put(D->vendor_menu + VENDOR_COUNT, 1);
            put(D->vendor_menu + VENDOR_ENTRIES, item);
            put(D->vendor_menu + VENDOR_ENTRIES + 4, 0);
            buy(item, 1);
            CHECK(get(D->vendor_menu + VENDOR_ENTRIES) == -1);
            CHECK(get(D->vendor_menu + VENDOR_LAST_BOUGHT) == -1);
        };
        const auto before_vendor = g_acquisitions.size();
        g_play_phase = OPENRAC_PLAY_MENU;
        purchase(ITEM_BLASTER);    // Devastator reward
        purchase(ITEM_PYROCITOR);  // Gold Bolt reward replaces it
        g_frame(g_frame_user);
        CHECK(g_acquisitions.size() == before_vendor);
        g_play_phase = OPENRAC_PLAY_PLAYING;
        g_frame(g_frame_user);
        CHECK(g_acquisitions.size() == before_vendor + 1 && g_acquisitions.back().first == 1);
        g_frame(g_frame_user);
        CHECK(g_acquisitions.size() == before_vendor + 1);
        g_play_phase = OPENRAC_PLAY_MENU;
        purchase(ITEM_PYROCITOR);
        purchase(ITEM_BLASTER);  // reversing the order plays the weapon instead
        g_play_phase = OPENRAC_PLAY_PLAYING;
        g_frame(g_frame_user);
        CHECK(g_acquisitions.back() == std::make_pair(2, int(ITEM_DEVASTATOR)));
        g_play_phase = OPENRAC_PLAY_MENU;
        purchase(ITEM_PYROCITOR);
        purchase(ITEM_TAUNTER);  // Helipack has no sequence: discard the earlier Gold Bolt
        g_play_phase = OPENRAC_PLAY_PLAYING;
        g_frame(g_frame_user);
        CHECK(g_acquisitions.back().first == 0);
        const auto after_vendor = g_acquisitions.size();
        g_play_phase = OPENRAC_PLAY_MENU;
        g_frame(g_frame_user);  // opening/exiting without buying must not repeat it
        g_play_phase = OPENRAC_PLAY_PLAYING;
        g_frame(g_frame_user);
        CHECK(g_acquisitions.size() == after_vendor);
        put(D->vendor_menu + VENDOR_DIALOG, 0);
        // A case keeps its costs/checks, suppresses the source gold-weapon demo,
        // and presents exactly the scouted reward after the transaction.
        set_level(LEVEL_NOVALIS);
        const uint32_t case_moby = 0x1820000, case_vars = 0x1920000;
        const uint32_t case_entries = std::strcmp(D->version, "pal") == 0 ? 0x1DEDE0u : 0x1DEB10u;
        const int16_t case_class = 304;
        std::memcpy(at(case_moby + 0xA6), &case_class, 2);
        put(case_moby + MOBY_PVARS, case_vars);
        put(case_vars + 0x40, 0);
        put(case_vars + 4, 2);
        g_bolts = 10000;
        auto case_update =
            (void (*)(uint32_t))replacement(OPENRAC_RAC1_HOOK_Moby_GoldWeaponCase_Update);
        const uint32_t pool_address = std::strcmp(D->version, "pal") == 0 ? 0x160098u : 0x15FFD8u;
        const uint32_t end_address = std::strcmp(D->version, "pal") == 0 ? 0x1600A0u : 0x15FFE0u;
        put(pool_address, case_moby);
        put(end_address, case_moby + 0x300);
        for (int i = 0; i < 3; ++i) {
            const uint32_t m = case_moby + i * 0x100;
            std::memcpy(at(m + 0xA6), &case_class, 2);
            const float p[3] = {float(10 + i * 3), 20, 30};
            std::memcpy(at(m + 0x10), p, sizeof p);
        }
        for (const auto expected :
             {std::make_pair(int(ITEM_PYROCITOR), 1),
              std::make_pair(int(ITEM_BLASTER), 3),
              std::make_pair(int(ITEM_DEVASTATOR), 2)}) {
            put(case_entries, expected.first);
            *at(D->owned + expected.first) = 1;  // retail prerequisite in the presentation-only PR
            const auto count = g_acquisitions.size();
            const int bolts = g_bolts, unspent = g_unspent();
            acquisition_position[0] = 9;
            acquisition_position[1] = 18;
            acquisition_position[2] = 30;
            acquisition_yaw = 1.25f;
            const int places = acquisition_places;
            case_update(case_moby);
            const uint32_t case_context =
                std::strcmp(D->version, "pal") == 0 ? 0x179200u : 0x179100u;
            CHECK(get(case_context + 8) == 0 && get(case_context + 12) == 0);
            CHECK(*at(D->owned + expected.first) == 1 && g_serialized_owned[expected.first] == 1);
            if (expected.second == 2) {
                *at(D->owned + ITEM_BLASTER) = 1;  // actual reward delivery
            }
            g_frame(g_frame_user);
            CHECK(acquisition_places == places + 1 && g_acquisitions.size() == count);
            CHECK(
                acquisition_target[0] == 13 && acquisition_target[1] == 20
                && acquisition_target[2] == 30.25f
            );
            g_frame(g_frame_user);  // queued placement has not happened yet
            CHECK(g_acquisitions.size() == count);
            std::copy_n(acquisition_target, 3, acquisition_position);
            g_frame(g_frame_user);
            CHECK(g_bolts == bolts - 1000 && g_unspent() == unspent - 4);
            CHECK(g_case_demos == 0 && g_acquisitions.size() == count + 1);
            CHECK(g_acquisitions.back().first == expected.second);
            if (expected.second == 3) {
                CHECK(g_acquisitions.back().second == LEVEL_ARIDIA);
            }
            if (expected.second == 2) {
                CHECK(g_acquisitions.back().second == ITEM_BLASTER);
            }
            acquisition_busy = 1;
            const int centered_places = acquisition_places;
            g_frame(g_frame_user);
            CHECK(acquisition_places == centered_places);
            acquisition_busy = 0;
            g_frame(g_frame_user);
            CHECK(acquisition_places == centered_places + 1);
            CHECK(
                acquisition_target[0] == 9 && acquisition_target[1] == 18
                && acquisition_target[2] == 30
            );
            CHECK(acquisition_target_yaw == 1.25f);
            std::copy_n(acquisition_target, 3, acquisition_position);
            case_update(case_moby);
            g_frame(g_frame_user);
            CHECK(g_bolts == bolts - 1000 && g_acquisitions.size() == count + 1);
        }
    }

    std::fill_n(acquisition_position, 3, 0.f);

    // Helga's Swingshot: nothing given, the location had and sent.
    const int gives = g_gives;
    auto give = (openrac_rac1_Item_Give)replacement(OPENRAC_RAC1_HOOK_Item_Give);
    give(ITEM_SWINGSHOT, 1);
    CHECK(
        g_gives == gives && *at(D->owned + ITEM_SWINGSHOT) == 0
        && *at(D->obtained + ITEM_SWINGSHOT) == 1
    );
    const std::int64_t helga = location_id(LOC_GIVE, LEVEL_KERWAN, ITEM_SWINGSHOT);
    CHECK(frames_until([helga] { return g_checked.count(helga) != 0; }));

    // A gold bolt on Novalis, found by the tick's check.
    *at(D->gold_bolts + LEVEL_NOVALIS * 4 + 0) = 1;
    const std::int64_t bolt = location_id(LOC_GOLD, LEVEL_NOVALIS, 0);
    CHECK(bolt >= 0 && frames_until([bolt] { return g_checked.count(bolt) != 0; }));

    // A skill point the game sets, on either kind of disc's address: found by the tick's check.
    const std::uint32_t skills = ap_skill_flags_address(D->version);
    CHECK(skills != 0);
    *at(skills + 0x08) = 1;
    const std::int64_t skill = ap_skill_location_id(LEVEL_RILGAR, 0x08);
    CHECK(frames_until([skill] { return g_checked.count(skill) != 0; }));

    // Drek's ending scene on Veldin (return): the goal.
    set_level(LEVEL_VELDIN2);
    g_scene = 4;
    CHECK(frames_until([] { return g_goal; }));

    if (g_visual_mode) {
        const uint32_t npc_moby = 0x1800000;
        int16_t cls = 1144;
        std::memcpy(at(npc_moby + 0xA6), &cls, 2);
        set_level(LEVEL_BATALIA);
        *at(D->planets + LEVEL_GASPAR) =
            1;  // acquired from elsewhere: NPC must still be purchasable
        auto deserter = (void (*)(uint32_t))replacement("Moby_BataliaDeserter_Update");
        const int purchases = g_npc_purchases;
        deserter(npc_moby);
        CHECK(g_npc_purchases == purchases + 1 && *at(D->planets + LEVEL_GASPAR) == 1);
        CHECK(g_serialized_planets[LEVEL_GASPAR] == 1);
        CHECK((g_serialized_checks & (1u << (10 + 7))) != 0);
        CHECK(frames_until([] { return g_checked.count(ap_npc_id(7)) != 0; }));
        deserter(npc_moby);
        CHECK(g_npc_purchases == purchases + 1);  // receiving its coordinates never buys it twice
        cls = 774;
        std::memcpy(at(npc_moby + 0xA6), &cls, 2);
        set_level(LEVEL_NOVALIS);
        *at(D->planets + LEVEL_ARIDIA) = 0;
        ((void (*)(uint32_t))replacement("Moby_ThePlumber_Update"))(npc_moby);
        CHECK(*at(D->planets + LEVEL_ARIDIA) == 0 && g_serialized_planets[LEVEL_ARIDIA] == 0);
        CHECK(frames_until([] { return g_checked.count(ap_npc_id(0)) != 0; }));
        // NPC-first and infobot-first both spend exactly one canonical check.
        for (int index : {3, 5}) {
            const auto& npc = k_ap_npcs[index];
            const uint32_t bot = 0x1820000, vars = 0x1821000;
            set_level(npc.level);
            std::memcpy(at(npc_moby + 0xA6), &npc.oclass, 2);
            put(bot + MOBY_PVARS, vars);
            put(vars + INFOBOT_PLANET, npc.planet);
            *at(bot + MOBY_MISSION) = index == 3 ? 0 : 3;
            *at(bot + 0x20) = 8;
            auto npc_update = (void (*)(uint32_t))replacement(npc.hook);
            auto bot_update = (void (*)(uint32_t))replacement("Moby_Infobot_Update");
            const int before = g_npc_purchases;
            if (index == 3) npc_update(npc_moby);
            bot_update(bot);
            CHECK(frames_until([index] { return g_checked.count(ap_npc_id(index)) != 0; }));
            npc_update(npc_moby);
            bot_update(bot);
            CHECK(g_npc_purchases == before + (index == 3 ? 1 : 0));
            CHECK(*at(bot + 0x20) == 0xFD);
        }
        const int checks = get(D->save_tag);
        ((void (*)(int))replacement(ITEM_RANDOMIZER_HOOK_Save_NewGamePlus))(0);
        CHECK(get(D->save_tag) == checks);  // challenge restart preserves NPC and gold-case checks
    } else {
        // Older APWorlds still give the direct NPC coordinates normally.
        const uint32_t npc_moby = 0x1800000;
        const int16_t cls = 774;
        std::memcpy(at(npc_moby + 0xA6), &cls, 2);
        set_level(LEVEL_NOVALIS);
        *at(D->planets + LEVEL_ARIDIA) = 0;
        ((void (*)(uint32_t))replacement("Moby_ThePlumber_Update"))(npc_moby);
        CHECK(*at(D->planets + LEVEL_ARIDIA) == 1);
    }

    // Same-slot overwrite: a new game may replace an old seed only after a successful close.
    auto new_game = (void (*)())replacement(ITEM_RANDOMIZER_HOOK_Save_NewGame);
    auto update = (void (*)())replacement("rac1.save.update_state");
    new_game();
    const std::uint32_t mc = std::strcmp(D->version, "pal") == 0 ? 0x13D390u : 0x13D290u;
    const auto prepare_close = [&](int busy, int result) {
        put(mc + 0x14, 4);
        put(mc + 0xDC, 20);
        put(mc + 0xE0, 3);
        put(mc + 0xD4, busy);
        put(mc + 0xC4, result);
        put(mc + 0xCC, 0);
    };
    prepare_close(1, 0);
    update();
    CHECK(saved_book().find("old-seed") != std::string::npos);  // polling is not success
    prepare_close(0, -1);
    update();
    CHECK(saved_book().find("old-seed") != std::string::npos);  // failed close
    prepare_close(0, 0);
    update();
    CHECK(saved_book().find("4\ttest-seed\tRatchet") != std::string::npos);
    CHECK(g_save_updates == 3);  // all paths still call the original

    unload();
    std::filesystem::remove(dir / "save-slots.tsv");
    std::filesystem::remove(dir);
    g_stop = true;
    server.join();
    close_socket(listener);
    std::printf(g_failures == 0 ? "load: ok\n" : "load: %d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
