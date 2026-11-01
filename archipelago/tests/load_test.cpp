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
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ap_ids.h"
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
int g_gives;
std::string g_notice;
std::string g_server;

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

void game_void() {}

void game_i32(int32_t) {}

int32_t game_count() {
    return 0;
}

void game_moby(openrac_gaddr) {}

void* original_for(const std::string& name) {
    if (name == OPENRAC_RAC1_HOOK_Item_Give) {
        return (void*)game_give;
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

int setting_bool(openrac_mod*, const char*) {
    return 0;
}

int64_t setting_int(openrac_mod*, const char*) {
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
    s->phase = OPENRAC_PLAY_PLAYING;
    s->level = g_level;
    s->loads = 1;
    return OPENRAC_HOOK_OK;
}

int scene_playing(int*) {
    return g_scene;
}

int moby_list(openrac_moby*, int) {
    return 0;
}

int moby_deletion(int, openrac_moby_deletion*) {
    return OPENRAC_HOOK_UNKNOWN;
}

int play_notice(openrac_mod*, const char* text) {
    g_notice = text != nullptr ? text : "";
    return OPENRAC_HOOK_OK;
}

openrac_api make_api() {
    openrac_api api;
    std::memset(&api, 0, sizeof api);
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = "rac1";
    api.version = "pal";
    api.memory = g_memory.data();
    api.log = host_log;
    api.fail = host_fail;
    api.setting_bool = setting_bool;
    api.setting_int = setting_int;
    api.setting_float = setting_float;
    api.setting_string = setting_string;
    api.hook = host_hook;
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
    return api;
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
    send_all(c, server_frame(R"([{"cmd":"RoomInfo","games":["Ratchet & Clank"]}])"));

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
                    const json connected = json::array({
                        {{"cmd", "Connected"},
                         {"team", 0},
                         {"slot", 1},
                         {"players", json::array()},
                         {"missing_locations", json::array()},
                         {"checked_locations", json::array()},
                         {"slot_info", json::object()}},
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
                    send_all(c, server_frame(connected.dump()));
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
    const openrac_api api = make_api();
    CHECK(load(&api, reinterpret_cast<openrac_mod*>(1)) == 0);
    CHECK(g_hooks == 14 && g_frame != nullptr);

    // The server's Heli-Pack is given, its obtained byte left to its own location.
    CHECK(frames_until([] { return g_connected; }));
    CHECK(frames_until([] { return *at(D->owned + ITEM_HELI_PACK) != 0; }));
    CHECK(*at(D->obtained + ITEM_HELI_PACK) == 0);

    // Its banner, in another level's text, is not left on screen.
    CHECK(g_gives > 0 && std::strcmp(reinterpret_cast<char*>(at(D->banner_text)), "Paradox") != 0);

    // A tutorial whose place is not known yet stays the game's.
    auto help = (openrac_rac1_Help_Request)replacement(OPENRAC_RAC1_HOOK_Help_Request);
    CHECK(help(rando_item_helps[0].message, 0) == 0 && g_helps == 1);

    // Orxon: no O2 Mask shown until Clank's part is done; the real byte back; then as it is.
    auto ship = (openrac_rac1_Ship_InitTransition)replacement(OPENRAC_RAC1_HOOK_Ship_InitTransition);
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

    // Drek's ending scene on Veldin (return): the goal.
    set_level(LEVEL_VELDIN2);
    g_scene = 4;
    CHECK(frames_until([] { return g_goal; }));

    unload();
    g_stop = true;
    server.join();
    close_socket(listener);
    std::printf(g_failures == 0 ? "load: ok\n" : "load: %d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
