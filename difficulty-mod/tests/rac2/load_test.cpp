// SPDX-License-Identifier: GPL-3.0-or-later
// The difficulty mod's library as the port loads it in Going Commando, against a host table and
// game memory made here (openrac_mod.h), at each of the three discs' addresses (src/rac2/discs.h):
// it refuses an unknown disc and an OpenRAC without its rac2 hook points, takes the seven by their
// rac2 ids and signatures and none of rac1's; with the sliders at 1.0 each hook calls the game's
// function exactly as it was called; with them moved, each scales what it should (the hit's points,
// an enemy's damage once a hit, rounds, bolts for the call, speed, the max health's count with the
// boosts, the weapon table without compounding); the enemy count, enemy speed and health pickups
// change nothing here; and its party's rules are published as in rac1.
//
// Run with the library's path: difficulty_rac2_load_test <difficulty.dylib|.so|.dll>.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "discs.h"
#include "openrac/openrac_mod.h"
#include "rules.h"

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

constexpr std::uint32_t kMemory = 0x00800000u;
std::uint8_t* g_mem;
std::string g_failed;
std::vector<std::string> g_warnings;
int g_level = 3;
const dm2_disc* g_disc = &k_dm2_discs[0];
std::map<std::string, double> g_floats;
std::map<std::string, std::string> g_strings;
std::map<std::string, int> g_bools;
std::vector<std::string> g_refuse;  // hook points this host lacks

// The hero record's fields the mod reads and writes (discs.h).
constexpr std::uint32_t kTarget = 0x1B0u, kInHand = 0x1248u, kHeroMoby = 0x2290u, kHealth = 0x24CCu;

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

const dm2_disc& disc() {
    return *g_disc;
}

// ---- The game's side: the hook points' originals, recording their calls ----

std::vector<int> g_damage_calls;

void game_take_damage(int32_t n) {
    g_damage_calls.push_back(n);
}

std::uint32_t g_hit_returned;

std::uint32_t game_get_hit(std::uint32_t, std::uint32_t, int32_t) {
    return g_hit_returned;
}

std::vector<std::pair<int, int>> g_ammo_calls;

int32_t game_take_ammo(int32_t item, int32_t amount) {
    g_ammo_calls.push_back({item, amount});
    return 1;
}

int g_bolts_seen;

void game_record_death(std::uint32_t m, int32_t, int32_t) {
    g_bolts_seen = get<int16_t>(m + 0xB4u);
}

int g_loads;

int32_t game_level_load(int32_t, int32_t) {
    ++g_loads;
    return 0x00123450;
}

float g_approach_seen[3];

void game_approach_speed(float up, float down) {
    g_approach_seen[0] = up;
    g_approach_seen[1] = down;
    g_approach_seen[2] = get<float>(disc().hero + kTarget);
}

// The nanotech levels' steps: 3 points on top of the count given (the game's 4).
int32_t game_nanotech_steps(std::uint32_t, std::uint32_t, std::uint32_t count) {
    if (count != 0) {
        put<int32_t>(count, get<int32_t>(count) + 3);
    }

    return 0;
}

// ---- The host table ----

struct Taken {
    std::string name, signature;
    void* replacement;
};

std::vector<Taken> g_hooks;

void host_log(openrac_mod*, int level, const char* text) {
    std::printf("[%d] difficulty: %s\n", level, text);

    if (level == OPENRAC_LOG_WARN) {
        g_warnings.push_back(text);
    }
}

void host_fail(openrac_mod*, const char* reason) {
    g_failed = reason;
}

int setting_bool(openrac_mod*, const char* key) {
    return g_bools.count(key) ? g_bools[key] : 0;
}

int64_t setting_int(openrac_mod*, const char* key) {
    return std::strcmp(key, "chaos_minutes") == 0 ? 3 : 0;
}

double setting_float(openrac_mod*, const char* key) {
    return g_floats.count(key) ? g_floats[key] : 1.0;
}

const char* setting_string(openrac_mod*, const char* key) {
    static std::string out;
    out = g_strings.count(key) ? g_strings[key] : "";
    return out.c_str();
}

void* original_for(const std::string& name) {
    if (name == "rac2.player.take_damage") {
        return (void*)game_take_damage;
    }

    if (name == "rac2.combat.get_hit") {
        return (void*)game_get_hit;
    }

    if (name == "rac2.weapon.take_ammo") {
        return (void*)game_take_ammo;
    }

    if (name == "rac2.moby.record_death") {
        return (void*)game_record_death;
    }

    if (name == "rac2.level.load") {
        return (void*)game_level_load;
    }

    if (name == "rac2.player.approach_speed") {
        return (void*)game_approach_speed;
    }

    if (name == "rac2.player.nanotech_steps") {
        return (void*)game_nanotech_steps;
    }

    return nullptr;
}

int host_hook(
    openrac_mod*, const char* name, const char* signature, void* replacement, void** original
) {
    for (const std::string& r : g_refuse) {
        if (r == name) {
            return OPENRAC_HOOK_UNKNOWN;
        }
    }

    // A hook point this game's port does not have (rac1's, say).
    if (original_for(name) == nullptr) {
        return OPENRAC_HOOK_UNKNOWN;
    }

    g_hooks.push_back({name, signature, replacement});
    *original = original_for(name);
    return OPENRAC_HOOK_OK;
}

openrac_frame_fn g_frame;
void* g_frame_user;

int host_on_frame(openrac_mod*, openrac_frame_fn fn, void* user) {
    g_frame = fn;
    g_frame_user = user;
    return 0;
}

int host_on_pad(openrac_mod*, openrac_pad_fn, void*) {
    return 0;
}

// The rac2 part calls none of the game's functions by name.
void* host_function(const char*) {
    return nullptr;
}

void* host_function_at(openrac_gaddr) {
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

openrac_gaddr g_sp = 0x007F0000u;

openrac_gaddr stack_push(uint32_t size, openrac_gaddr* saved) {
    *saved = g_sp;
    g_sp -= (size + 15u) & ~15u;
    return g_sp;
}

void stack_pop(openrac_gaddr saved) {
    g_sp = saved;
}

std::vector<std::uint8_t> g_published;
std::string g_published_text;
int g_publishes;
openrac_party_rules_fn g_rules_fn;

int host_mod_count() {
    return 0;
}

int host_mod_entry(int, openrac_mod_entry*) {
    return OPENRAC_HOOK_UNKNOWN;
}

int host_mod_setting(int, int, openrac_mod_setting*) {
    return OPENRAC_HOOK_UNKNOWN;
}

int host_rules_publish(openrac_mod*, const void* data, uint32_t size, const char* text) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    g_published.assign(bytes, bytes + size);
    g_published_text = text;
    ++g_publishes;
    return OPENRAC_HOOK_OK;
}

int host_rules_read(int, void*, uint32_t, uint32_t*) {
    return -1;
}

const char* host_rules_text(int) {
    return "";
}

int host_on_rules(openrac_mod*, openrac_party_rules_fn fn, void*) {
    g_rules_fn = fn;
    return 0;
}

int host_rules_deliver(int, const void*, uint32_t) {
    return OPENRAC_HOOK_UNKNOWN;
}

void host_replay(int) {}

int host_replaying() {
    return 0;
}

openrac_api make_api(const char* version) {
    openrac_api api;
    std::memset(&api, 0, sizeof api);
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = "rac2";
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
    api.on_pad = host_on_pad;
    api.function = host_function;
    api.function_at = host_function_at;
    api.relocate_data = identity;
    api.relocate_level_data = identity_level;
    api.loaded_level = loaded_level;
    api.stack_push = stack_push;
    api.stack_pop = stack_pop;
    api.mod_count = host_mod_count;
    api.mod_entry = host_mod_entry;
    api.mod_setting = host_mod_setting;
    api.party_rules_publish = host_rules_publish;
    api.party_rules_read = host_rules_read;
    api.party_rules_text = host_rules_text;
    api.on_party_rules = host_on_rules;
    api.party_rules_deliver = host_rules_deliver;
    api.party_replay = host_replay;
    api.party_replaying = host_replaying;
    return api;
}

void* hook(const std::string& name) {
    for (const Taken& t : g_hooks) {
        if (t.name == name) {
            return t.replacement;
        }
    }

    return nullptr;
}

openrac_mod_load_fn g_load;
openrac_mod* g_handle;

// Loads the mod on a disc with the settings given; true when it loaded.
bool load_with(const char* version) {
    g_hooks.clear();
    g_failed.clear();
    g_warnings.clear();

    for (const dm2_disc& d : k_dm2_discs) {
        if (std::strcmp(d.version, version) == 0) {
            g_disc = &d;
        }
    }

    static openrac_api kept;
    kept = make_api(version);
    return g_load(&kept, g_handle) == 0;
}

// The hook points, as the mod's replacements.
struct Hooks {
    void (*take_damage)(int32_t);
    std::uint32_t (*get_hit)(std::uint32_t, std::uint32_t, int32_t);
    int32_t (*take_ammo)(int32_t, int32_t);
    void (*record_death)(std::uint32_t, int32_t, int32_t);
    int32_t (*level_load)(int32_t, int32_t);
    void (*approach)(float, float);
    int32_t (*steps)(std::uint32_t, std::uint32_t, std::uint32_t);
};

Hooks hooks() {
    Hooks h;
    h.take_damage = (void (*)(int32_t))hook("rac2.player.take_damage");
    h.get_hit =
        (std::uint32_t (*)(std::uint32_t, std::uint32_t, int32_t))hook("rac2.combat.get_hit");
    h.take_ammo = (int32_t (*)(int32_t, int32_t))hook("rac2.weapon.take_ammo");
    h.record_death = (void (*)(std::uint32_t, int32_t, int32_t))hook("rac2.moby.record_death");
    h.level_load = (int32_t (*)(int32_t, int32_t))hook("rac2.level.load");
    h.approach = (void (*)(float, float))hook("rac2.player.approach_speed");
    h.steps = (int32_t (*)(std::uint32_t, std::uint32_t, std::uint32_t))hook(
        "rac2.player.nanotech_steps"
    );
    CHECK(
        h.take_damage && h.get_hit && h.take_ammo && h.record_death && h.level_load && h.approach
        && h.steps
    );
    return h;
}

// Game memory for a disc: the weapon table's 119 records (a price, an ammo price and the most ammo
// on each), a class header of each kind, an enemy and a crate, the hero's moby and health.
constexpr std::uint32_t kEnemyClass = 0x00580000u, kCrateClass = 0x00580100u;
constexpr std::uint32_t kEnemy = 0x00600000u, kCrate = 0x00600100u, kHero = 0x00600200u;
constexpr std::uint32_t kHit = 0x00610000u;

void lay_game() {
    const dm2_disc& d = disc();
    std::memset(g_mem, 0, kMemory);

    for (std::uint32_t i = 0; i < 119; ++i) {
        const std::uint32_t r = d.weapon_table + i * 0xE0u;
        put<int32_t>(r + 0x80u, i % 4 ? 1000 + static_cast<int32_t>(i) : 0);
        put<uint16_t>(r + 0x88u, i % 3 ? 4 : 0);
        put<uint16_t>(r + 0x8Eu, i % 3 ? 40 : 0);
    }

    put<int16_t>(kEnemyClass + 0x46u, 5);
    put<int16_t>(kCrateClass + 0x46u, 2);
    put<std::uint32_t>(kEnemy + 0x24u, kEnemyClass);
    put<std::uint32_t>(kCrate + 0x24u, kCrateClass);
    put<std::uint32_t>(kHero + 0x24u, kEnemyClass);
    put<std::uint32_t>(d.hero + kHeroMoby, kHero);
    put<int32_t>(d.hero + kHealth, 10);
    put<int32_t>(d.max_health, 20);
    g_hit_returned = kHit;
}

std::uint32_t row(int i) {
    return disc().weapon_table + static_cast<std::uint32_t>(i) * 0xE0u;
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
    CHECK(info.api_major == OPENRAC_MOD_API_MAJOR && info.api_minor == 12);
    CHECK(std::strcmp(info.id, "difficulty") == 0 && std::strcmp(info.version, "1.4.0") == 0);
    g_mem = static_cast<std::uint8_t*>(std::calloc(1, kMemory));
    CHECK(g_mem != nullptr);
    g_handle = reinterpret_cast<openrac_mod*>(&info);

    // An unknown disc: refused, naming it, before any hook.
    CHECK(!load_with("jp") && g_failed.find("jp") != std::string::npos && g_hooks.empty());
    g_disc = &k_dm2_discs[0];

    // An OpenRAC without the level load's hook point: refused, naming it.
    g_refuse = {"rac2.level.load"};
    CHECK(!load_with("ntsc") && g_failed.find("Level_Load") != std::string::npos);
    g_refuse.clear();

    // Each disc: the seven hook points with their signatures, none of rac1's.
    static const char* const k_points[][2] = {
        {"rac2.player.take_damage", "void(i32)"},
        {"rac2.combat.get_hit", "u32(u32,u32,i32)"},
        {"rac2.weapon.take_ammo", "i32(i32,i32)"},
        {"rac2.moby.record_death", "void(u32,i32,i32)"},
        {"rac2.level.load", "i32(i32,i32)"},
        {"rac2.player.approach_speed", "void(f32,f32)"},
        {"rac2.player.nanotech_steps", "i32(u32,u32,u32)"},
    };

    for (const char* version : {"ntsc", "pal", "ntsc2"}) {
        CHECK(load_with(version) && g_failed.empty() && g_warnings.empty());
        CHECK(g_hooks.size() == 7);

        for (const auto& p : k_points) {
            bool found = false;

            for (const Taken& t : g_hooks) {
                found |= t.name == p[0] && t.signature == p[1];
            }

            CHECK(found);
        }
    }

    // Each disc, at its own addresses.
    for (const char* version : {"ntsc", "pal", "ntsc2"}) {
        // ---- Every slider at 1.0: the game's calls, as they were ----
        g_floats.clear();
        CHECK(load_with(version));
        const dm2_disc& d = disc();
        lay_game();
        Hooks h = hooks();
        g_damage_calls.clear();
        h.take_damage(3);
        h.take_damage(0);
        h.take_damage(-2);
        CHECK(g_damage_calls == (std::vector<int>{3, 0, -2}));
        put<float>(kHit + 0x2Cu, 1.5f);
        CHECK(h.get_hit(kEnemy, 0x10000, 0) == kHit && get<float>(kHit + 0x2Cu) == 1.5f);
        g_ammo_calls.clear();
        CHECK(h.take_ammo(-1, 1) == 1 && h.take_ammo(5, 2) == 1);
        CHECK(g_ammo_calls == (std::vector<std::pair<int, int>>{{-1, 1}, {5, 2}}));
        put<int16_t>(kCrate + 0xB4u, 12);
        h.record_death(kCrate, 0x100, 0);
        CHECK(g_bolts_seen == 12 && get<int16_t>(kCrate + 0xB4u) == 12);
        put<float>(d.hero + kTarget, 0.1f);
        h.approach(0.25f, 0.5f);
        CHECK(
            g_approach_seen[0] == 0.25f && g_approach_seen[1] == 0.5f && g_approach_seen[2] == 0.1f
        );
        const std::uint32_t count = 0x00620000u;
        put<int32_t>(count, 4);
        h.steps(0x00620004u, 0x00620008u, count);
        CHECK(get<int32_t>(count) == 7);
        g_loads = 0;
        CHECK(h.level_load(1, 0) == 0x00123450 && g_loads == 1);
        CHECK(get<int32_t>(row(1) + 0x80u) == 1001 && get<uint16_t>(row(1) + 0x8Eu) == 40);

        // ---- The sliders moved ----
        g_floats =
            {{"damage_taken", 2.0},
             {"damage_dealt", 2.0},
             {"ammo_use", 2.0},
             {"bolts", 2.0},
             {"player_speed", 1.5},
             {"weapon_cost", 2.0},
             {"max_ammo", 0.5},
             {"ammo_cost", 3.0},
             {"max_health", 2.0},
             {"health_pickups", 1.5},
             {"enemy_count", 4.0},
             {"enemy_speed", 2.0}};
        CHECK(load_with(version) && g_failed.empty());
        // The enemy count and enemy speed are not Going Commando's: nothing more is hooked for them.
        CHECK(g_hooks.size() == 7);
        lay_game();
        h = hooks();
        // Twice the points; a hit that heals or does nothing is the game's.
        g_damage_calls.clear();
        h.take_damage(3);
        h.take_damage(0);
        CHECK(g_damage_calls == (std::vector<int>{6, 0}));
        // Half: a point every other hit, carried over.
        g_floats = {{"damage_taken", 0.5}};
        CHECK(load_with(version));
        h = hooks();
        g_damage_calls.clear();
        h.take_damage(1);
        h.take_damage(1);
        h.take_damage(1);
        CHECK(g_damage_calls == (std::vector<int>{1}));
        g_floats =
            {{"damage_dealt", 2.0},
             {"ammo_use", 2.0},
             {"bolts", 2.0},
             {"player_speed", 1.5},
             {"weapon_cost", 2.0},
             {"max_ammo", 0.5},
             {"ammo_cost", 3.0},
             {"max_health", 2.0},
             {"health_pickups", 1.5}};
        CHECK(load_with(version));
        h = hooks();
        // An enemy's hit twice as hard, once however often it is read; a new hit scaled again.
        put<float>(kHit + 0x2Cu, 1.5f);
        h.get_hit(kEnemy, 0x10000, 1);
        h.get_hit(kEnemy, 0x10000, 1);
        CHECK(get<float>(kHit + 0x2Cu) == 3.0f);
        put<float>(kHit + 0x2Cu, 1.5f);
        h.get_hit(kEnemy, 0x10000, 0);
        CHECK(get<float>(kHit + 0x2Cu) == 3.0f);
        // Ratchet's own hits and a crate's are not this slider's.
        put<float>(kHit + 0x2Cu, 1.0f);
        h.get_hit(kHero, 1, 0);
        CHECK(get<float>(kHit + 0x2Cu) == 1.0f);
        h.get_hit(kCrate, 0x10000, 0);
        CHECK(get<float>(kHit + 0x2Cu) == 1.0f);
        // Two rounds a shot from the item in hand; the last round still fires; an empty gun gets
        // the game's answer.
        put<int32_t>(d.hero + kInHand, 6);
        put<int32_t>(d.ammo + 6u * 4u, 5);
        g_ammo_calls.clear();
        h.take_ammo(-1, 1);
        put<int32_t>(d.ammo + 6u * 4u, 1);
        h.take_ammo(-1, 1);
        put<int32_t>(d.ammo + 6u * 4u, 0);
        h.take_ammo(-1, 1);
        CHECK(g_ammo_calls == (std::vector<std::pair<int, int>>{{-1, 2}, {-1, 1}, {-1, 1}}));
        // Twice the bolts for the call, the moby's own count back after it.
        put<int16_t>(kCrate + 0xB4u, 12);
        h.record_death(kCrate, 0x100, 0);
        CHECK(g_bolts_seen == 24 && get<int16_t>(kCrate + 0xB4u) == 12);
        // Ratchet toward one and a half times his target, as fast; the target put back.
        put<float>(d.hero + kTarget, 0.1f);
        h.approach(0.25f, 0.5f);
        CHECK(std::fabs(g_approach_seen[2] - 0.15f) < 1e-6f && g_approach_seen[0] == 0.375f);
        CHECK(get<float>(d.hero + kTarget) == 0.1f);
        // Max health: 4 + 3 steps + 2 boosts = 9, twice that 18; the count holds 18 less the
        // boosts, which the game adds after.
        put<uint8_t>(d.boosts + 1u, 1);
        put<uint8_t>(d.boosts + 7u, 1);
        put<int32_t>(count, 4);
        h.steps(0x00620004u, 0x00620008u, count);
        CHECK(get<int32_t>(count) == 16);
        // A count the HUD asks for with none given: left alone.
        h.steps(0x00620004u, 0x00620008u, 0);
        // The weapon table: from the level's own values at each load, never twice.
        h.level_load(1, 0);
        h.level_load(0, 1);
        CHECK(get<int32_t>(row(1) + 0x80u) == 2002 && get<int32_t>(row(4) + 0x80u) == 0);
        CHECK(get<uint16_t>(row(1) + 0x88u) == 12 && get<uint16_t>(row(1) + 0x8Eu) == 20);
        CHECK(get<uint16_t>(row(3) + 0x88u) == 0 && get<uint16_t>(row(3) + 0x8Eu) == 0);
        CHECK(get<int32_t>(row(118) + 0x80u) == 2 * (1000 + 118));
        // The record past the table (its mark) is not the mod's.
        CHECK(get<int32_t>(row(119) + 0x80u) == 0);
    }

    // Chaos: each level's values from the seed; loads on every disc.
    g_floats.clear();
    g_strings = {{"seed", "0A1B2C3D"}};
    g_bools = {{"chaos", 1}, {"chaos_events", 1}};
    CHECK(load_with("ntsc2") && g_failed.empty());
    {
        lay_game();
        Hooks h = hooks();
        h.level_load(1, 0);
        // The events' clock runs on play (the game's state 0), a tick a frame, and never picks an
        // event about the enemies' speed alone.
        CHECK(g_frame != nullptr);
        int publishes = g_publishes, started = 0;

        // Forty minutes of play, an event every three.
        for (int i = 0; i < 60 * 60 * 40; ++i) {
            g_frame(g_frame_user);

            // An event started or ended: the rules published say which is on.
            if (g_publishes != publishes) {
                publishes = g_publishes;
                dm::PartyRules now;
                CHECK(
                    dm::decode_party_rules(
                        g_published.data(), static_cast<std::uint32_t>(g_published.size()), now
                    )
                );

                // One started: never Slow motion, Frenzy or Double time.
                if (now.event >= 0) {
                    ++started;
                    CHECK(dm::k_events[now.event].knob[0] != dm::ENEMY_SPEED);
                    CHECK(dm::k_events[now.event].knob[1] != dm::ENEMY_SPEED);
                }
            }
        }

        CHECK(started >= 10);
    }

    // A party: the host's game publishes what it plays by, its blank seed resolved.
    g_bools.clear();
    g_strings.clear();
    g_floats = {{"bolts", 3.0}};
    g_publishes = 0;
    CHECK(load_with("pal") && g_publishes == 1);
    dm::PartyRules host;
    CHECK(
        dm::decode_party_rules(
            g_published.data(), static_cast<std::uint32_t>(g_published.size()), host
        )
    );
    CHECK(host.seed != 0 && host.sliders[dm::BOLTS] == 3.0 && !host.chaos);
    CHECK(g_published_text == "Bolts x3");
    // A member with its sliders at 1.0 plays by the host's bolts.
    const std::vector<std::uint8_t> host_bytes = g_published;
    g_floats.clear();
    CHECK(load_with("pal"));
    lay_game();
    {
        Hooks h = hooks();
        g_rules_fn(nullptr, host_bytes.data(), static_cast<std::uint32_t>(host_bytes.size()));
        h.level_load(1, 0);
        put<int16_t>(kCrate + 0xB4u, 10);
        h.record_death(kCrate, 0, 0);
        CHECK(g_bolts_seen == 30);
        // The party gone: this game's own, retail, from the next level loaded.
        g_rules_fn(nullptr, nullptr, 0);
        h.level_load(1, 0);
        h.record_death(kCrate, 0, 0);
        CHECK(g_bolts_seen == 10);
    }

    std::free(g_mem);
    std::printf("difficulty_rac2_load_test: ok\n");
    return 0;
}
