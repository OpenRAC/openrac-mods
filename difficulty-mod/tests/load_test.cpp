// SPDX-License-Identifier: GPL-3.0-or-later
// The difficulty mod's library as the port loads it, against a host table and game memory made
// here (openrac_mod.h): it reports its id, refuses another game, an unknown disc and an OpenRAC
// without its hook points, takes them with their signatures on both discs; with the sliders at
// 1.0 each hook calls the game's function exactly as it was called; with them moved, each scales
// what it should (hits, damage once a hit, rounds, bolts, speed, prices without compounding); and
// on a level laid out here, the enemy count copies enemies only within the pool's budget, leaves
// the scripted ones alone and keeps the slots the game needs free.
//
// Run with the library's path: difficulty_load_test <difficulty.dylib|.so|.dll>.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "classes.h"
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
int g_level = 3;
const char* g_disc = "pal";
std::map<std::string, double> g_floats;
std::map<std::string, std::string> g_strings;
std::map<std::string, int> g_bools;
std::string g_refuse_hook;  // a hook point this host lacks

const dm_disc& disc() {
    return std::strcmp(g_disc, "pal") == 0 ? k_dm_discs[0] : k_dm_discs[1];
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

std::vector<int> g_damage_calls;

void game_take_damage(int32_t n) {
    g_damage_calls.push_back(n);
}

std::uint32_t g_hit_returned;

std::uint32_t game_get_hit(std::uint32_t, int32_t, int32_t) {
    return g_hit_returned;
}

std::vector<std::pair<int, int>> g_ammo_calls;

int32_t game_take_ammo(int32_t item, int32_t amount) {
    g_ammo_calls.push_back({item, amount});
    return 1;
}

int g_bolts_seen[2];

void game_record_death(std::uint32_t m, int32_t, int32_t) {
    g_bolts_seen[0] = get<int16_t>(m + 0xB4u);
    g_bolts_seen[1] = get<int16_t>(m + 0xB6u);
}

int g_loads;

std::uint32_t game_load_gameplay(int32_t) {
    ++g_loads;
    return 0x00123450u;
}

float g_approach_seen[3];

void game_approach_speed(float up, float down) {
    g_approach_seen[0] = up;
    g_approach_seen[1] = down;
    g_approach_seen[2] = get<float>(disc().hero + 0x190u);
}

float g_tick_seen;

void game_update(std::uint32_t) {
    g_tick_seen = get<float>(disc().tick + 12u);
}

void game_ball(std::uint32_t) {
    put<int32_t>(disc().hero + 0x22A8u, get<int32_t>(disc().hero + 0x22A8u) + 1);
}

// The game's functions the mod calls: a CreateMoby and DeleteMoby over the pool laid out below, a
// floor everywhere at 0.5 and no walls.
int g_created, g_deleted, g_built;

std::uint32_t pool_word(int i) {
    return get<std::uint32_t>(disc().pool + 4u * static_cast<std::uint32_t>(i));
}

std::uint32_t game_create(int32_t cls) {
    const std::uint32_t spare = pool_word(1), end = pool_word(2),
                        pvars = get<std::uint32_t>(disc().pool + 0x10u);

    for (std::uint32_t m = spare; m < end; m += 0x100u) {
        if (g_mem[m + 0x20] >= 0xFE) {
            if (g_mem[m + 0x20] == 0xFF) {
                g_mem[m + 0x120] = 0xFF;
            }

            std::memset(g_mem + m, 0, 0x100);
            put<int16_t>(m + 0xA6u, static_cast<int16_t>(cls));
            g_mem[m + 0x21] = 0xFF;
            g_mem[m + 0xA4] = 0xFF;
            put<std::uint32_t>(m + 0x78u, pvars + (m - spare) / 2u);
            ++g_created;
            return m;
        }
    }

    return 0;
}

void game_delete(std::uint32_t m) {
    g_mem[m + 0x20] = m < pool_word(1) ? 0xFD : 0xFE;
    ++g_deleted;
}

void game_build(std::uint32_t) {
    ++g_built;
}

float game_ground(std::uint32_t, int32_t, float) {
    return 0.5f;
}

int32_t game_line(std::uint32_t, std::uint32_t, int32_t, std::uint32_t, std::uint32_t) {
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
    if (name == "rac1.player.take_damage") {
        return (void*)game_take_damage;
    }

    if (name == "rac1.combat.get_hit") {
        return (void*)game_get_hit;
    }

    if (name == "rac1.weapon.take_ammo") {
        return (void*)game_take_ammo;
    }

    if (name == "rac1.moby.record_death") {
        return (void*)game_record_death;
    }

    if (name == "rac1.level.load_gameplay") {
        return (void*)game_load_gameplay;
    }

    if (name == "rac1.player.approach_speed") {
        return (void*)game_approach_speed;
    }

    if (name == "rac1.moby.nanotech_ball.update") {
        return (void*)game_ball;
    }

    return (void*)game_update;
}

int host_hook(
    openrac_mod*, const char* name, const char* signature, void* replacement, void** original
) {
    if (g_refuse_hook == name) {
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

void* host_function(const char* name) {
    const dm_disc& d = disc();

    if (std::strcmp(name, d.create_moby) == 0) {
        return (void*)game_create;
    }

    if (std::strcmp(name, d.delete_moby) == 0) {
        return (void*)game_delete;
    }

    if (std::strcmp(name, d.build_matrix) == 0) {
        return (void*)game_build;
    }

    if (std::strcmp(name, d.ground) == 0) {
        return (void*)game_ground;
    }

    if (std::strcmp(name, d.line) == 0) {
        return (void*)game_line;
    }

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
    g_disc = version;
    openrac_api api = make_api("rac1", version);
    static openrac_api kept;
    kept = api;
    return g_load(&kept, g_handle) == 0;
}

// ---- A level laid out: placed enemies, a gameplay file's moby section and pvar table ----

constexpr std::uint32_t kHeader = 0x00400000u;  // the gameplay file
constexpr std::uint32_t kBase = 0x00500000u;    // the moby array
constexpr std::uint32_t kPlacedPvars = 0x00480000u;

struct Placed {
    int16_t cls;
    std::uint8_t group;
    int32_t flags;
    std::uint32_t pvar_size;
};

// The gameplay file's moby links (+0x50): {pvar block, offset in it} and the moby index the field
// holds after the load (-1: none).
struct Link {
    int32_t block;
    std::uint32_t offset;
    int32_t target;
};

void lay_level(const std::vector<Placed>& placed, int n2, const std::vector<Link>& links = {}) {
    const dm_disc& d = disc();
    std::memset(g_mem + kHeader, 0, 0x00180000u);
    const std::uint32_t n = static_cast<std::uint32_t>(placed.size());
    const std::uint32_t spare = kBase + n * 0x100u;
    const std::uint32_t end = kBase + (n + static_cast<std::uint32_t>(n2)) * 0x100u - 0x100u;
    put<std::uint32_t>(d.pool, kBase);
    put<std::uint32_t>(d.pool + 4u, spare);
    put<std::uint32_t>(d.pool + 8u, end);
    put<std::uint32_t>(d.pool + 0x10u, end + 0x100u);
    put<std::uint32_t>(d.load_state + 4u, kHeader);
    // The header: the moby section at +0x100, the pvar table at +0x40000, no fix-ups.
    put<std::uint32_t>(kHeader + 0x44u, 0x100u);
    put<std::uint32_t>(kHeader + 0x54u, 0x40000u);
    put<std::uint32_t>(kHeader + 0x58u, 0x50000u);
    const std::uint32_t section = kHeader + 0x100u;
    put<std::uint32_t>(section, n);
    put<std::uint32_t>(section + 4u, static_cast<std::uint32_t>(n2));
    std::uint32_t pv = kPlacedPvars;

    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t rec = section + 0x10u + i * 0x88u;
        put<std::uint32_t>(rec, 0x88u);
        put<int32_t>(rec + 8u, placed[i].flags);
        put<int32_t>(rec + 0x58u, static_cast<int32_t>(i));
        put<std::uint32_t>(kHeader + 0x40000u + 8u * i, pv);
        put<std::uint32_t>(kHeader + 0x40000u + 8u * i + 4u, placed[i].pvar_size);
        const std::uint32_t m = kBase + i * 0x100u;
        put<int16_t>(m + 0xA6u, placed[i].cls);
        g_mem[m + 0x21] = placed[i].group;
        put<std::uint32_t>(m + 0x78u, pv);
        put<float>(m + 0x10u, 10.0f + static_cast<float>(i) * 20.0f);
        put<float>(m + 0x14u, 5.0f);
        put<float>(m + 0x18u, 1.5f);
        put<int16_t>(m + 0xB2u, static_cast<int16_t>(i));
        g_mem[m + 0x20] = 0;

        for (std::uint32_t b = 0; b < placed[i].pvar_size; ++b) {
            g_mem[pv + b] = static_cast<std::uint8_t>(i + b);
        }

        pv += (placed[i].pvar_size + 0xFu) & ~0xFu;
    }

    if (!links.empty()) {
        put<std::uint32_t>(kHeader + 0x50u, 0x60000u);
        std::uint32_t q = kHeader + 0x60000u;

        for (const Link& l : links) {
            put<int32_t>(q, l.block);
            put<std::uint32_t>(q + 4u, l.offset);
            const std::uint32_t block = get<std::uint32_t>(kHeader + 0x40000u + 8u * l.block);
            put<int32_t>(block + l.offset, l.target);
            q += 8u;
        }

        put<int32_t>(q, -1);
    }

    g_mem[spare + 0x20] = 0xFF;
    g_created = g_deleted = g_built = 0;
}

int free_slots() {
    const std::uint32_t spare = pool_word(1), end = pool_word(2);
    int free = 0;

    for (std::uint32_t m = spare; m < end; m += 0x100u) {
        if (g_mem[m + 0x20] == 0xFF) {
            return free + static_cast<int>((end - m) / 0x100u);
        }

        free += g_mem[m + 0x20] >= 0xFE;
    }

    return free;
}

using LoadFn = std::uint32_t (*)(int32_t);

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
    // The interface its manifest names (1.1), whatever the SDK it was built with.
    CHECK(info.api_major == OPENRAC_MOD_API_MAJOR && info.api_minor == 1);
    CHECK(std::strcmp(info.id, "difficulty") == 0 && std::strcmp(info.version, "1.2.0") == 0);
    g_mem = static_cast<std::uint8_t*>(std::calloc(1, kMemory));
    CHECK(g_mem != nullptr);
    g_handle = reinterpret_cast<openrac_mod*>(&info);

    // Another game, an unknown disc: refused with a reason, before any hook.
    openrac_api api = make_api("rac2", "ntsc");
    CHECK(
        g_load(&api, g_handle) != 0 && g_failed.find("rac1") != std::string::npos && g_hooks.empty()
    );
    CHECK(!load_with("jp") && g_failed.find("jp") != std::string::npos && g_hooks.empty());

    // An OpenRAC without the level load's hook point: refused, naming it.
    g_refuse_hook = "rac1.level.load_gameplay";
    CHECK(!load_with("pal") && g_failed.find("Level_LoadGameplay") != std::string::npos);
    // Without an enemy's update: loads, that class at its own speed.
    g_refuse_hook = k_dm_update_hooks[0];
    CHECK(load_with("ntsc") && g_failed.empty());
    g_refuse_hook.clear();

    // Both discs: the six hook points with their signatures, each enemy update and the orb's.
    static const char* const k_points[][2] = {
        {"rac1.player.take_damage", "void(i32)"},
        {"rac1.combat.get_hit", "u32(u32,i32,i32)"},
        {"rac1.weapon.take_ammo", "i32(i32,i32)"},
        {"rac1.moby.record_death", "void(u32,i32,i32)"},
        {"rac1.level.load_gameplay", "u32(i32)"},
        {"rac1.player.approach_speed", "void(f32,f32)"},
        {"rac1.moby.nanotech_ball.update", "void(u32)"},
    };
    const std::size_t updates = sizeof k_dm_update_hooks / sizeof k_dm_update_hooks[0];

    for (const char* version : {"ntsc", "pal"}) {
        CHECK(load_with(version) && g_failed.empty());
        CHECK(g_hooks.size() == 7 + updates);

        for (const auto& p : k_points) {
            bool found = false;

            for (const Taken& t : g_hooks) {
                found |= t.name == p[0] && t.signature == p[1];
            }

            CHECK(found);
        }
    }

    const dm_disc& d = disc();
    auto take_damage = (void (*)(int32_t))hook("rac1.player.take_damage");
    auto get_hit = (std::uint32_t (*)(std::uint32_t, int32_t, int32_t))hook("rac1.combat.get_hit");
    auto take_ammo = (int32_t (*)(int32_t, int32_t))hook("rac1.weapon.take_ammo");
    auto record_death = (void (*)(std::uint32_t, int32_t, int32_t))hook("rac1.moby.record_death");
    auto load_gameplay = (LoadFn)hook("rac1.level.load_gameplay");
    auto approach = (void (*)(float, float))hook("rac1.player.approach_speed");
    auto update = (void (*)(std::uint32_t))hook(k_dm_update_hooks[0]);
    CHECK(
        take_damage && get_hit && take_ammo && record_death && load_gameplay && approach && update
    );

    // ---- Every slider at 1.0: the game's calls, as they were ----
    g_damage_calls.clear();

    for (int i = 0; i < 5; ++i) {
        take_damage(1);
    }

    take_damage(0);
    CHECK(g_damage_calls == (std::vector<int>{1, 1, 1, 1, 1, 0}));
    const std::uint32_t ring = d.hit_ring, enemy = 0x00600000u;
    put<int16_t>(enemy + 0xA6u, 427);  // a Blarg trooper
    put<float>(ring + 3u * 0x40u + 0x2Cu, 1.0f);
    g_hit_returned = ring + 3u * 0x40u;
    CHECK(
        get_hit(enemy, 0x330000, 0) == g_hit_returned && get<float>(g_hit_returned + 0x2Cu) == 1.0f
    );
    g_ammo_calls.clear();
    CHECK(take_ammo(-1, 1) == 1 && take_ammo(5, 1) == 1);
    CHECK(g_ammo_calls == (std::vector<std::pair<int, int>>{{-1, 1}, {5, 1}}));
    const std::uint32_t crate = 0x00610000u;
    put<int16_t>(crate + 0xB4u, 12);
    put<int16_t>(crate + 0xB6u, 7);
    record_death(crate, 0x100, 0);
    CHECK(g_bolts_seen[0] == 12 && g_bolts_seen[1] == 7);
    put<float>(d.hero + 0x190u, 0.1f);
    approach(0.25f, 0.5f);
    CHECK(g_approach_seen[0] == 0.25f && g_approach_seen[1] == 0.5f && g_approach_seen[2] == 0.1f);
    put<float>(d.tick + 12u, 0.02f);
    update(enemy);
    CHECK(g_tick_seen == 0.02f);

    // The items' table and the level: as they were.
    for (std::uint32_t id = 0; id < 37; ++id) {
        put<int32_t>(d.item_table + id * 0x18u, 1000 + static_cast<int32_t>(id));
        put<uint16_t>(d.item_table + id * 0x18u + 8u, id % 3 ? 4 : 0);
        put<uint16_t>(d.item_table + id * 0x18u + 0xEu, id % 3 ? 40 : 0);
    }

    lay_level(std::vector<Placed>(50, Placed{427, 0xFF, 0, 0x40}), 256);
    CHECK(load_gameplay(1) == 0x00123450u && g_created == 0 && g_deleted == 0);
    CHECK(
        get<int32_t>(d.item_table + 2u * 0x18u) == 1002
        && get<uint16_t>(d.item_table + 2u * 0x18u + 0xEu) == 40
    );

    // ---- The sliders moved ----
    g_floats =
        {{"damage_taken", 2.0},
         {"damage_dealt", 2.0},
         {"ammo_use", 2.0},
         {"bolts", 2.0},
         {"player_speed", 1.5},
         {"enemy_speed", 2.0},
         {"weapon_cost", 2.0},
         {"max_ammo", 0.5},
         {"ammo_cost", 3.0}};
    CHECK(load_with("pal"));
    take_damage = (void (*)(int32_t))hook("rac1.player.take_damage");
    get_hit = (std::uint32_t (*)(std::uint32_t, int32_t, int32_t))hook("rac1.combat.get_hit");
    take_ammo = (int32_t (*)(int32_t, int32_t))hook("rac1.weapon.take_ammo");
    record_death = (void (*)(std::uint32_t, int32_t, int32_t))hook("rac1.moby.record_death");
    load_gameplay = (LoadFn)hook("rac1.level.load_gameplay");
    approach = (void (*)(float, float))hook("rac1.player.approach_speed");
    update = (void (*)(std::uint32_t))hook(k_dm_update_hooks[0]);
    // Two cells a hit.
    g_damage_calls.clear();
    take_damage(1);
    CHECK(g_damage_calls == (std::vector<int>{1, 1}));
    // Twice the damage, once however often the hit is read; a new hit is scaled again.
    put<float>(g_hit_returned + 0x2Cu, 1.5f);
    get_hit(enemy, 0x330000, 1);
    get_hit(enemy, 0x330000, 1);
    CHECK(get<float>(g_hit_returned + 0x2Cu) == 3.0f);
    put<float>(g_hit_returned + 0x2Cu, 1.5f);
    get_hit(enemy, 0x330000, 0);
    CHECK(get<float>(g_hit_returned + 0x2Cu) == 3.0f);
    // The hero's hits and an excluded class's are not this slider's.
    const std::uint32_t hero_moby = 0x00620000u, mine = 0x00630000u;
    put<std::uint32_t>(d.hero + 0x2080u, hero_moby);
    put<float>(g_hit_returned + 0x2Cu, 1.0f);
    get_hit(hero_moby, 1, 0);
    CHECK(get<float>(g_hit_returned + 0x2Cu) == 1.0f);
    put<int16_t>(mine + 0xA6u, 74);
    get_hit(mine, 0x330000, 0);
    CHECK(get<float>(g_hit_returned + 0x2Cu) == 1.0f);
    // Two rounds a shot; the last round still fires; an empty gun gets the game's answer.
    put<int32_t>(d.item_in_hand, 6);
    put<int32_t>(d.ammo + 6u * 4u, 5);
    g_ammo_calls.clear();
    take_ammo(-1, 1);
    put<int32_t>(d.ammo + 6u * 4u, 1);
    take_ammo(-1, 1);
    put<int32_t>(d.ammo + 6u * 4u, 0);
    take_ammo(-1, 1);
    CHECK(g_ammo_calls == (std::vector<std::pair<int, int>>{{-1, 2}, {-1, 1}, {-1, 1}}));
    // Twice the bolts.
    put<int16_t>(crate + 0xB4u, 12);
    put<int16_t>(crate + 0xB6u, 7);
    record_death(crate, 0x100, 0);
    CHECK(g_bolts_seen[0] == 24 && g_bolts_seen[1] == 14);
    // Ratchet toward one and a half times his target, as fast; the target put back.
    approach(0.25f, 0.5f);
    CHECK(std::fabs(g_approach_seen[2] - 0.15f) < 1e-6f && g_approach_seen[0] == 0.375f);
    CHECK(get<float>(d.hero + 0x190u) == 0.1f);
    // An enemy's update on twice the clock, the clock put back after; its animation twice as fast.
    put<float>(enemy + 0x58u, 1.0f);
    update(enemy);
    CHECK(std::fabs(g_tick_seen - 0.04f) < 1e-7f && get<float>(d.tick + 12u) == 0.02f);
    CHECK(get<float>(enemy + 0x58u) == 2.0f);
    update(enemy);
    CHECK(get<float>(enemy + 0x58u) == 2.0f);  // from the game's 1.0 again, not 4.0
    // The items' table: from the level's own values at each load, never twice.
    lay_level(std::vector<Placed>(10, Placed{427, 0xFF, 0, 0x40}), 256);
    load_gameplay(1);
    load_gameplay(1);
    CHECK(get<int32_t>(d.item_table + 2u * 0x18u) == 2004);
    CHECK(
        get<uint16_t>(d.item_table + 2u * 0x18u + 8u) == 12
        && get<uint16_t>(d.item_table + 2u * 0x18u + 0xEu) == 20
    );
    CHECK(
        get<uint16_t>(d.item_table + 3u * 0x18u + 8u) == 0
        && get<uint16_t>(d.item_table + 3u * 0x18u + 0xEu) == 0
    );

    // ---- The enemy count: within the pool's budget ----
    g_floats = {{"enemy_count", 4.0}};
    CHECK(load_with("pal"));
    load_gameplay = (LoadFn)hook("rac1.level.load_gameplay");
    // 120 troopers asking three copies each: 95 slots of 255 may go, 160 stay free.
    lay_level(std::vector<Placed>(120, Placed{427, 0xFF, 0, 0x60}), 256);
    load_gameplay(1);
    CHECK(g_created == 255 - dm::k_pool_reserve && g_built == g_created);
    CHECK(free_slots() >= dm::k_pool_reserve);
    // A copy: the original's class and pvars (its own slot's), beside it, no save id, no bolts.
    const std::uint32_t spare = pool_word(1);
    CHECK(
        get<int16_t>(spare + 0xA6u) == 427 && get<uint16_t>(spare + 0xB2u) == 0xFFFF
        && g_mem[spare + 0xB1] == 0xFE
    );
    CHECK(get<std::uint32_t>(spare + 0x78u) == get<std::uint32_t>(d.pool + 0x10u));
    CHECK(std::memcmp(g_mem + get<std::uint32_t>(spare + 0x78u), g_mem + kPlacedPvars, 0x60) == 0);
    const float dx = get<float>(spare + 0x10u) - 10.0f, dy = get<float>(spare + 0x14u) - 5.0f;
    CHECK(std::sqrt(dx * dx + dy * dy) >= 2.4f && std::sqrt(dx * dx + dy * dy) <= 4.1f);
    CHECK(get<float>(spare + 0x18u) == 1.5f);  // the original's height over the floor
    // Large pvars: room from the pool's tail, which CreateMoby then never hands out.
    lay_level(std::vector<Placed>(40, Placed{427, 0xFF, 0, 0x210}), 256);
    const std::uint32_t end_before = pool_word(2);
    load_gameplay(1);
    CHECK(g_created > 0 && pool_word(2) < end_before);
    CHECK(free_slots() >= dm::k_pool_reserve);
    const std::uint32_t spare40 = pool_word(1);
    const std::uint32_t pv = get<std::uint32_t>(spare40 + 0x78u);
    CHECK(pv >= get<std::uint32_t>(d.pool + 0x10u) + (pool_word(2) - spare40) / 2u);
    CHECK(std::memcmp(g_mem + pv, g_mem + kPlacedPvars, 0x210) == 0);
    // The flags of the discs' own records: nearly every enemy is in play before its mission is
    // done (1), after it (2) or both (3), and is copied all the same, with its mission.
    lay_level(
        {Placed{427, 0xFF, 1, 0x40}, Placed{427, 0xFF, 2, 0x40}, Placed{427, 0xFF, 3, 0x40}}, 256
    );
    g_mem[kBase + 0xB0] = 4;
    load_gameplay(1);
    CHECK(g_created == 9);
    {
        const std::uint32_t first = pool_word(1);
        CHECK(g_mem[first + 0xB0] == 4 && g_mem[first + 0xB1] == 0xFE);
    }
    // A link field left at -1 names no moby: copied. One naming a moby: neither is copied.
    lay_level(
        {Placed{427, 0xFF, 3, 0x40}, Placed{427, 0xFF, 3, 0x40}, Placed{427, 0xFF, 3, 0x40}},
        256,
        {Link{0, 0x10, -1}, Link{1, 0x10, 2}}
    );
    load_gameplay(1);
    CHECK(g_created == 3);
    CHECK(get<int16_t>(pool_word(1) + 0xA6u) == 427);
    {
        // All three copies are of the first: the same pvars.
        bool all_first = true;

        for (std::uint32_t m = pool_word(1); m < pool_word(1) + 3u * 0x100u; m += 0x100u) {
            const std::uint8_t* pv = g_mem + get<std::uint32_t>(m + 0x78u);
            all_first = all_first && std::memcmp(pv, g_mem + kPlacedPvars, 0x40) == 0;
        }

        CHECK(all_first);
    }
    // Waves, bosses, turrets and mines: never copied.
    lay_level(
        {Placed{427, 3, 0, 0x40},
         Placed{1130, 0xFF, 0, 0x40},
         Placed{29, 0xFF, 0, 0x40},
         Placed{74, 0xFF, 0, 0x40}},
        256
    );
    load_gameplay(1);
    CHECK(g_created == 0);
    // Gaspar: none at all.
    g_level = 9;
    lay_level(std::vector<Placed>(20, Placed{427, 0xFF, 0, 0x40}), 256);
    load_gameplay(1);
    CHECK(g_created == 0);
    g_level = 3;

    // Below 1: about that share of the free-roaming enemies left out, the same each time.
    g_floats = {{"enemy_count", 0.5}};
    CHECK(load_with("pal"));
    load_gameplay = (LoadFn)hook("rac1.level.load_gameplay");
    std::vector<Placed> mix(200, Placed{427, 0xFF, 0, 0x40});
    mix[0] = Placed{427, 2, 0, 0x40};
    mix[1] = Placed{1130, 0xFF, 0, 0x40};
    lay_level(mix, 256);
    load_gameplay(1);
    const int first = g_deleted;
    CHECK(first > 60 && first < 140 && g_mem[kBase + 0x20] == 0 && g_mem[kBase + 0x120] == 0);
    lay_level(mix, 256);
    load_gameplay(1);
    CHECK(g_deleted == first);

    // Chaos: each level's values from the seed, the same for the same seed.
    g_floats.clear();
    g_strings = {{"seed", "0A1B2C3D"}};
    g_bools["chaos"] = 1;
    CHECK(load_with("ntsc") && g_failed.empty());

    std::free(g_mem);
    std::printf("difficulty_load_test: ok\n");
    return 0;
}
