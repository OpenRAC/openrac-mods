// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Ratchet & Clank: the difficulty mod, a mod of OpenRAC's native port (README.md). A slider for
// each thing that makes the game hard or easy, presets that fill them (the manifest's), and Chaos,
// which rolls them for each level from a seed. Each value is changed where the game takes it, through a
// hook point (OpenRAC's rac1 hook points: docs/port/MODS.md, section 4), or once a level at its
// source (the items' table of prices and ammo), never by patching memory each frame; with every
// slider at 1.0 each hook calls the game's own function as it was called, so the game is retail.
//
//   damage to Ratchet   Hero_TakeDamage: a hit takes one cell; the multiplier is how many, on
//                       average (an accumulator: 0.5 is a cell every other hit)
//   damage to enemies   Moby_GetHitMessage: the hit an enemy reads has its damage (+0x2C) scaled,
//                       once a hit however often it is read
//   enemy speed         each enemy class's update (the behaviour catalogue's hook points) runs with
//                       the tick's time constants scaled, and its animation speed (+0x58) scaled
//   Ratchet's speed     Hero_ApproachSpeed: his ground speed eases toward a scaled target
//   prices, max ammo    the items' table (level 0's D_L00_001C43B0), rewritten from the level's
//                       own values at each level's load
//   ammo per shot       Weapon_TakeAmmo, with an accumulator per item
//   bolt drops          Moby_RecordDeath: the bolts a dying enemy or a broken crate drops
//   max health          gMaxNanotech, from the upgrades bought, kept to a count the HUD can show
//   health pickups      the nanotech orb's update: the cells it gives, with an accumulator
//   enemy count         Level_LoadGameplay: after the level's mobys are placed and before any has
//                       run, enemies are copied beside their originals (or some removed), within
//                       the spawned mobys' pool's room (pool.h, rules.h: k_pool_reserve)
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "classes.h"
#include "discs.h"
#include "openrac/mod.hpp"
#include "pool.h"
#include "rules.h"

namespace {

using openrac::gaddr;

// The hook points it takes (OpenRAC's sdk/include/openrac/games/rac1.hpp has them too from the
// SDK after 1.1.2; written here so the mod builds against the SDK it pins).
struct TakeDamage {
    static constexpr const char* id = "rac1.player.take_damage";
    static constexpr const char* name = "Hero_TakeDamage";
    static constexpr const char* signature = "void(i32)";
    using fn = void (*)(int32_t);
};

struct GetHit {
    static constexpr const char* id = "rac1.combat.get_hit";
    static constexpr const char* name = "Moby_GetHitMessage";
    static constexpr const char* signature = "u32(u32,i32,i32)";
    using fn = uint32_t (*)(uint32_t, int32_t, int32_t);
};

struct TakeAmmo {
    static constexpr const char* id = "rac1.weapon.take_ammo";
    static constexpr const char* name = "Weapon_TakeAmmo";
    static constexpr const char* signature = "i32(i32,i32)";
    using fn = int32_t (*)(int32_t, int32_t);
};

struct RecordDeath {
    static constexpr const char* id = "rac1.moby.record_death";
    static constexpr const char* name = "Moby_RecordDeath";
    static constexpr const char* signature = "void(u32,i32,i32)";
    using fn = void (*)(uint32_t, int32_t, int32_t);
};

struct LoadGameplay {
    static constexpr const char* id = "rac1.level.load_gameplay";
    static constexpr const char* name = "Level_LoadGameplay";
    static constexpr const char* signature = "u32(i32)";
    using fn = uint32_t (*)(int32_t);
};

struct ApproachSpeed {
    static constexpr const char* id = "rac1.player.approach_speed";
    static constexpr const char* name = "Hero_ApproachSpeed";
    static constexpr const char* signature = "void(f32,f32)";
    using fn = void (*)(float, float);
};

constexpr const char* k_nanotech_ball = "rac1.moby.nanotech_ball.update";

constexpr int k_items = 37;  // item ids 0..0x24
constexpr uint32_t k_item_size = 0x18;
constexpr int k_levels = 19;
constexpr int k_hits = 64;  // the pending hits' ring
constexpr uint32_t k_hit_size = 0x40;

openrac::Mod* s_mod;
const dm_disc* s_disc;

// What the player chose (load), what plays on the level loaded, and with the event on.
bool s_chaos;
dm::Values s_sliders;
uint64_t s_seed;
bool s_trace;
bool s_events_on;
dm::Values s_level;
dm::Values s_active;
dm::EventClock s_clock;
dm::Rng s_rng(1);

// Per level: the items' table as the level's program has it, read before the first change.
std::vector<uint8_t> s_items[k_levels];
bool s_items_read[k_levels];

dm::Accumulator s_damage;
dm::Accumulator s_pickups;
dm::Accumulator s_ammo[k_items];

// The hits whose damage was scaled: the value written, and the moby it was for, per ring entry.
float s_hit_value[k_hits];
uint32_t s_hit_owner[k_hits];

// The animation speed each timed moby had from the game, and what the mod wrote over it.
std::unordered_map<uint32_t, std::pair<float, float>> s_anim;
int s_timed_depth;

// The classes by id (classes.h).
std::unordered_map<int, const dm_class*> s_classes;

// The game's functions it calls.
uint32_t (*s_create_moby)(int32_t);
void (*s_delete_moby)(uint32_t);
void (*s_build_matrix)(uint32_t);
float (*s_ground)(uint32_t, int32_t, float);
int32_t (*s_line)(uint32_t, uint32_t, int32_t, uint32_t, uint32_t);

// ---- Game memory ----

template <class T>
T get(gaddr at) {
    return s_mod->memory().read<T>(at);
}

template <class T>
void put(gaddr at, T v) {
    s_mod->memory().write<T>(at, v);
}

uint8_t* mem() {
    return s_mod->memory().base();
}

gaddr exe(uint32_t address) {
    return s_mod->relocate(address);
}

gaddr level0(uint32_t address) {
    return s_mod->relocate_level(0, address);
}

gaddr hero() {
    return exe(s_disc->hero);
}

bool in_play() {
    return s_mod->loaded_level() >= 0 && get<int32_t>(level0(s_disc->level_mode)) == 0;
}

const dm_class* class_of(gaddr moby) {
    const auto it = s_classes.find(get<int16_t>(moby + 0xA6u));
    return it == s_classes.end() ? nullptr : it->second;
}

std::string fmt(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}

std::string describe(const dm::Values& v) {
    std::string out;
    for (int k = 0; k < dm::KNOB_COUNT; ++k) {
        out += std::string(k ? ", " : "") + dm::k_knobs[k].name + " " + fmt(v[k]);
    }
    return out;
}

void set_active() {
    s_active = dm::with_event(s_level, s_clock.current);
}

// ---- Damage, ammo, bolts, speed, health ----

// A hit on Ratchet takes one cell (the game clamps the amount to 1): the multiplier is how many
// hits' worth, carried over between hits.
template <class Original>
void take_damage(Original original, int32_t amount) {
    const double k = s_active[dm::DAMAGE_TAKEN];
    if (amount <= 0 || k == 1.0) {
        original(amount);
        return;
    }
    const int hits = s_damage.take(k, 1);
    for (int i = 0; i < hits; ++i) {
        original(1);
    }
}

// The hit an enemy reads: its damage scaled once (the same hit read again keeps the value
// written, which is how it is told from a new one).
template <class Original>
uint32_t get_hit(Original original, uint32_t moby, int32_t mask, int32_t keep) {
    const uint32_t hit = original(moby, mask, keep);
    const double k = s_active[dm::DAMAGE_DEALT];
    if (hit == 0 || k == 1.0 || moby == get<uint32_t>(hero() + 0x2080u)) {
        return hit;
    }
    const dm_class* c = class_of(moby);
    if (c == nullptr || c->role == DM_EXCLUDED) {
        return hit;
    }
    const gaddr ring = level0(s_disc->hit_ring);
    if (hit < ring || (hit - ring) % k_hit_size != 0 || (hit - ring) / k_hit_size >= k_hits) {
        return hit;
    }
    const uint32_t i = (hit - ring) / k_hit_size;
    const float damage = get<float>(hit + 0x2Cu);
    if (s_hit_owner[i] == moby && s_hit_value[i] == damage) {
        return hit;
    }
    const float scaled = static_cast<float>(damage * k);
    put<float>(hit + 0x2Cu, scaled);
    s_hit_owner[i] = moby;
    s_hit_value[i] = scaled;
    return hit;
}

// A shot's ammo: the multiplier per shot, carried over per item; never a free shot from an empty
// gun, and the last rounds still fire when a shot would want more than is left.
template <class Original>
int32_t take_ammo(Original original, int32_t item, int32_t amount) {
    const double k = s_active[dm::AMMO_USE];
    if (k == 1.0 || amount <= 0) {
        return original(item, amount);
    }
    const int32_t id = item == -1 ? get<int32_t>(exe(s_disc->item_in_hand)) : item;
    if (id < 0 || id >= k_items) {
        return original(item, amount);
    }
    const int32_t have = get<int32_t>(exe(s_disc->ammo) + 4u * static_cast<uint32_t>(id));
    int32_t want = s_ammo[id].take(k, amount);
    if (want == 0) {
        return have > 0 ? original(item, 0) : original(item, amount);
    }
    if (want > have) {
        if (have < amount) {
            return original(item, amount);
        }
        want = have;
    }
    return original(item, want);
}

// The bolts a dying enemy or a broken crate drops: its two bolt counts scaled before the game
// reads them.
template <class Original>
void record_death(Original original, uint32_t moby, int32_t flags, int32_t index) {
    const double k = s_active[dm::BOLTS];
    if (k != 1.0) {
        for (const uint32_t at : {0xB4u, 0xB6u}) {
            const int n = dm::scale_count(get<int16_t>(moby + at), k, s_rng);
            put<int16_t>(moby + at, static_cast<int16_t>(n > 32767 ? 32767 : n));
        }
    }
    original(moby, flags, index);
}

// Ratchet's ground speed eases toward his target speed: toward the target scaled, as fast.
template <class Original>
void approach_speed(Original original, float up, float down) {
    const double k = s_active[dm::PLAYER_SPEED];
    if (k == 1.0) {
        original(up, down);
        return;
    }
    const gaddr target = hero() + 0x190u;
    const float t = get<float>(target);
    const float scaled = static_cast<float>(t * k);
    put<float>(target, scaled);
    original(static_cast<float>(up * k), static_cast<float>(down * k));
    if (get<float>(target) == scaled) {
        put<float>(target, t);
    }
}

// An enemy's update with its time scaled: the tick's constants (seconds, and the ratio to NTSC's
// tick, with their squares and cubes as the game keeps them) for the call, and its animation speed
// after it, which the next tick's animation advance reads.
void timed_update(void (*original)(uint32_t), uint32_t moby) {
    const double k = s_active[dm::ENEMY_SPEED];
    const dm_class* c = class_of(moby);
    if (k == 1.0 || s_timed_depth > 0 || c == nullptr
        || (c->role != DM_ENEMY && c->role != DM_ROAMER && c->role != DM_TURRET)) {
        original(moby);
        return;
    }
    const gaddr anim = moby + 0x58u;
    auto it = s_anim.find(moby);
    if (it != s_anim.end() && get<float>(anim) == it->second.second) {
        put<float>(anim, it->second.first);
    }
    const gaddr tick = exe(s_disc->tick);
    float saved[8];
    for (int i = 0; i < 8; ++i) {
        saved[i] = get<float>(tick + 4u * static_cast<uint32_t>(i));
    }
    const double scale[8] = {k, k * k, 1.0 / k, k, k * k, k * k * k, 1.0, k};
    for (int i = 0; i < 8; ++i) {
        if (i != 6) {
            put<float>(
                tick + 4u * static_cast<uint32_t>(i), static_cast<float>(saved[i] * scale[i])
            );
        }
    }
    ++s_timed_depth;
    original(moby);
    --s_timed_depth;
    for (int i = 0; i < 8; ++i) {
        if (i != 6) {
            put<float>(tick + 4u * static_cast<uint32_t>(i), saved[i]);
        }
    }
    if (static_cast<int8_t>(get<uint8_t>(moby + 0x20u)) >= 0
        && get<int16_t>(moby + 0xA6u) == c->id) {
        const float base = get<float>(anim);
        const float scaled = static_cast<float>(base * k);
        put<float>(anim, scaled);
        s_anim[moby] = {base, scaled};
    } else {
        s_anim.erase(moby);
    }
}

// The update hook points' originals, one thunk each (classes.h: k_dm_update_hooks).
constexpr int k_max_updates = 96;
void (*s_update_original[k_max_updates])(uint32_t);

template <int I>
void update_thunk(uint32_t moby) {
    timed_update(s_update_original[I], moby);
}

template <int... I>
constexpr std::array<void (*)(uint32_t), sizeof...(I)> thunks(std::integer_sequence<int, I...>) {
    return {&update_thunk<I>...};
}

constexpr auto k_update_thunks = thunks(std::make_integer_sequence<int, k_max_updates>{});
static_assert(
    sizeof k_dm_update_hooks / sizeof k_dm_update_hooks[0] <= k_max_updates,
    "more update hook points than thunks"
);

// The nanotech orb: the cells it gives this tick, scaled.
void (*s_ball_original)(uint32_t);

void ball_update(uint32_t moby) {
    const double k = s_active[dm::HEALTH_PICKUPS];
    const gaddr hp = hero() + 0x22A8u;
    const int32_t before = get<int32_t>(hp);
    s_ball_original(moby);
    const int32_t gained = get<int32_t>(hp) - before;
    if (k == 1.0 || gained <= 0) {
        return;
    }
    const int32_t max = get<int32_t>(exe(s_disc->max_health));
    int32_t now = before + s_pickups.take(k, gained);
    put<int32_t>(hp, now > max ? max : now);
}

// Max health: the cells the upgrades bought give (4, Premium 5, Ultra 8), scaled to a count the
// HUD has places for. Each tick in play, on foot.
void keep_max_health() {
    if (!in_play() || get<uint8_t>(hero() + 0x20A4u) == 1) {
        return;
    }
    const gaddr upgrades = exe(s_disc->upgrades);
    const int retail = get<uint8_t>(upgrades + 5u) ? 8 : get<uint8_t>(upgrades + 4u) ? 5 : 4;
    const int target = dm::max_health(retail, s_active[dm::MAX_HEALTH]);
    const gaddr max = exe(s_disc->max_health);
    const int32_t now = get<int32_t>(max);
    if (now == target) {
        return;
    }
    const gaddr hp = hero() + 0x22A8u;
    const int32_t health = get<int32_t>(hp);
    if (health == now || health > target) {
        put<int32_t>(hp, target);
    }
    put<int32_t>(max, target);
    if (s_trace) {
        s_mod->info("max health " + std::to_string(now) + " -> " + std::to_string(target));
    }
}

// ---- A level's load: its values, the items' table, the enemies ----

// The items' table from the level's own values: weapon prices (+0, +4), ammo prices (+8, +0xA)
// and max ammo (+0xE), written whatever they are now (so a value is never scaled twice).
void rewrite_items(int level) {
    const gaddr table = level0(s_disc->item_table);
    uint8_t* at = mem() + table;
    if (!s_items_read[level]) {
        s_items[level].assign(at, at + k_items * k_item_size);
        s_items_read[level] = true;
    }
    const uint8_t* own = s_items[level].data();
    for (int id = 0; id < k_items; ++id) {
        const uint32_t r = static_cast<uint32_t>(id) * k_item_size;
        for (const uint32_t f : {0x0u, 0x4u}) {
            int32_t v;
            std::memcpy(&v, own + r + f, 4);
            v = dm::scale_amount(v, s_level[dm::WEAPON_COST], 9999999);
            put<int32_t>(table + r + f, v);
        }
        for (const uint32_t f : {0x8u, 0xAu}) {
            uint16_t v;
            std::memcpy(&v, own + r + f, 2);
            put<uint16_t>(
                table + r + f,
                static_cast<uint16_t>(dm::scale_amount(v, s_level[dm::AMMO_COST], 65535))
            );
        }
        uint16_t v;
        std::memcpy(&v, own + r + 0xE, 2);
        put<uint16_t>(
            table + r + 0xE,
            static_cast<uint16_t>(dm::scale_amount(v, s_level[dm::MAX_AMMO], 65535))
        );
    }
}

// A placed moby's record in the gameplay file, found by its pvars.
struct Placement {
    int32_t flags = 0;
    int32_t pvar = -1;  // its pvar block's index
};

// Where a copy of `orig` may stand: on the same floor (a ground probe under the spot within a
// unit of the original's), in the open (no wall on the line from the original), above the death
// plane. At the same height over the floor as the original.
bool find_spot(gaddr orig, int copy, float out[3]) {
    openrac::StackFrame frame(*s_mod, 48);
    const gaddr a = frame.address(), b = a + 16u;
    const float x = get<float>(orig + 0x10u), y = get<float>(orig + 0x14u),
                z = get<float>(orig + 0x18u);
    put<float>(a, x);
    put<float>(a + 4u, y);
    put<float>(a + 8u, z);
    put<float>(a + 12u, 0.0f);
    const float floor0 = s_ground(a, 0, 1.0f);
    if (!(floor0 > 0.0f) || z - floor0 < -0.5f) {
        return false;
    }
    const float death = get<float>(level0(s_disc->death_plane));
    const float over = z - floor0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const double angle = (copy * 3 + attempt) * 0.7853981633974483 + s_rng.unit() * 0.4;
        const double r = 2.5 + s_rng.unit() * 1.5;
        const float cx = static_cast<float>(x + r * std::cos(angle));
        const float cy = static_cast<float>(y + r * std::sin(angle));
        // The line from the original to the spot, a unit up.
        put<float>(a, x);
        put<float>(a + 4u, y);
        put<float>(a + 8u, floor0 + 1.0f);
        put<float>(a + 12u, 0.0f);
        put<float>(b, cx);
        put<float>(b + 4u, cy);
        put<float>(b + 8u, floor0 + 1.0f);
        put<float>(b + 12u, 0.0f);
        if (s_line(a, b, 0, orig, 0) != 0) {
            continue;
        }
        put<float>(b + 8u, floor0 + 0.5f);
        const float floor = s_ground(b, 0, 1.0f);
        if (!(floor > 0.0f) || std::fabs(floor - floor0) > 1.0f
            || (death != 0.0f && floor < death + 1.0f)) {
            continue;
        }
        out[0] = cx;
        out[1] = cy;
        out[2] = floor + over;
        return true;
    }
    return false;
}

// The moby's fields a copy keeps of its own (CreateMoby's): its state, group, list link, pvars,
// grid cells and pending hit, its slot's index and counter, its mission and save id, its parent.
const std::pair<uint32_t, uint32_t> k_own_fields[] =
    {{0x20, 2}, {0x28, 4}, {0x78, 4}, {0xA0, 6}, {0xA8, 8}, {0xB0, 4}, {0xB8, 8}};

gaddr copy_moby(
    gaddr orig,
    int16_t cls,
    const float spot[3],
    uint32_t pvar_size,
    const std::vector<uint32_t>& inner,
    gaddr* room,
    uint32_t* room_left
) {
    const gaddr m = s_create_moby(cls);
    if (m == 0) {
        return 0;
    }
    uint8_t own[0x100];
    std::memcpy(own, mem() + m, 0x100);
    std::memcpy(mem() + m, mem() + orig, 0x100);
    for (const auto& f : k_own_fields) {
        std::memcpy(mem() + m + f.first, own + f.first, f.second);
    }
    put<float>(m + 0x10u, spot[0]);
    put<float>(m + 0x14u, spot[1]);
    put<float>(m + 0x18u, spot[2]);
    // Not a placed moby: no save id (its death marks nothing killed, SetDeathBits), and no bolts.
    put<uint8_t>(m + 0xB1u, 0xFE);
    put<uint16_t>(m + 0xB2u, 0xFFFF);
    put<int16_t>(m + 0xB4u, 0);
    put<int16_t>(m + 0xB6u, 0);
    // Its pvars: the original's, in its own slot's 0x80 bytes or in the room the pool's tail gave.
    const gaddr from = get<uint32_t>(orig + 0x78u);
    gaddr to = get<uint32_t>(m + 0x78u);
    if (pvar_size > dm::k_spare_pvars) {
        const uint32_t need =
            static_cast<uint32_t>(dm::pvar_slots(static_cast<int>(pvar_size))) * dm::k_spare_pvars;
        if (*room == 0 || *room_left < need) {
            s_delete_moby(m);
            return 0;
        }
        to = *room;
        *room += need;
        *room_left -= need;
        put<uint32_t>(m + 0x78u, to);
    }
    if (from != 0 && to != 0 && pvar_size != 0) {
        std::memcpy(mem() + to, mem() + from, pvar_size);
        // Pointers into its own pvars (the load made them absolute) point into the copy's.
        for (const uint32_t off : inner) {
            if (off + 4u <= pvar_size) {
                const uint32_t v = get<uint32_t>(to + off);
                if (v >= from && v < from + pvar_size) {
                    put<uint32_t>(to + off, v - from + to);
                }
            }
        }
    }
    s_build_matrix(m);
    return m;
}

void adjust_enemies(int level) {
    const double k = s_level[dm::ENEMY_COUNT];
    if (k == 1.0) {
        return;
    }
    const dm::PoolGlobals at =
        {level0(s_disc->pool),
         level0(s_disc->pool + 4u),
         level0(s_disc->pool + 8u),
         level0(s_disc->pool + 0x10u)};
    const dm::Pool pool = dm::Pool::read(mem(), at);
    const gaddr hdr = get<uint32_t>(level0(s_disc->load_state) + 4u);
    if (!pool.sane() || hdr == 0) {
        s_mod->warn("enemy count: the level's moby pool is not as expected; left as it is");
        return;
    }
    // The gameplay file, still in memory: each placement's flags and pvar block, the pvar table
    // (+0x54: each block's address and size) and its two fix-up lists (+0x50: fields that name
    // another moby; +0x5C: pointers into the block itself).
    const gaddr section = hdr + get<uint32_t>(hdr + 0x44u);
    const int placed = get<int32_t>(section);
    const bool has_pvars = get<uint32_t>(hdr + 0x58u) != 0;
    const gaddr table = hdr + get<uint32_t>(hdr + 0x54u);
    std::unordered_map<uint32_t, Placement> by_pvars;
    if (has_pvars && placed > 0 && placed < 0x8000) {
        gaddr p = section + 0x10u;
        for (int i = 0; i < placed; ++i) {
            const uint32_t size = get<uint32_t>(p) & ~3u;
            if (size < 0x5Cu) {
                break;
            }
            Placement pl;
            pl.flags = get<int32_t>(p + 8u);
            pl.pvar = get<int32_t>(p + 0x58u);
            if (pl.pvar >= 0) {
                by_pvars[get<uint32_t>(table + 8u * static_cast<uint32_t>(pl.pvar))] = pl;
            }
            p += size;
        }
    }
    std::unordered_set<int> names_moby;                    // pvar blocks with a field naming a moby
    std::unordered_set<uint32_t> named;                    // mobys another names
    std::unordered_map<int, std::vector<uint32_t>> inner;  // pointers into a block
    for (const uint32_t list : {0x50u, 0x5Cu}) {
        if (get<uint32_t>(hdr + list) == 0) {
            continue;
        }
        gaddr q = hdr + get<uint32_t>(hdr + list);
        for (int guard = 0; guard < 0x40000 && get<int32_t>(q) >= 0; ++guard, q += 8u) {
            const int blk = get<int32_t>(q);
            const uint32_t off = get<uint32_t>(q + 4u);
            const uint32_t x = get<uint32_t>(table + 8u * static_cast<uint32_t>(blk));
            if (x < hdr) {
                continue;
            }
            if (list == 0x50u) {
                names_moby.insert(blk);
                const int32_t target = get<int32_t>(x + off);
                if (target >= 0) {
                    named.insert(pool.base + static_cast<uint32_t>(target) * dm::k_moby_size);
                }
            } else {
                inner[blk].push_back(off);
            }
        }
    }

    struct Wanted {
        gaddr moby;
        int16_t cls;
        uint32_t size;
        int block;
    };

    std::vector<Wanted> wanted;
    std::vector<dm::CloneAsk> asks;
    int dropped = 0, eligible = 0;
    for (gaddr m = pool.base; m < pool.spare; m += dm::k_moby_size) {
        if (static_cast<int8_t>(get<uint8_t>(m + 0x20u)) < 0) {
            continue;
        }
        const dm_class* c = class_of(m);
        if (c == nullptr || (c->role != DM_ENEMY && c->role != DM_ROAMER)) {
            continue;
        }
        const auto pl = by_pvars.find(get<uint32_t>(m + 0x78u));
        // Only a moby free of the level's scripts: no group (the waves, the "defeat them all"
        // doors), no mission, no other moby naming it, and found in the file.
        if (pl == by_pvars.end() || get<uint8_t>(m + 0x21u) != 0xFF || (pl->second.flags & 3) != 0
            || named.count(m)) {
            continue;
        }
        ++eligible;
        if (k < 1.0) {
            if (s_rng.unit() < 1.0 - k) {
                s_delete_moby(m);
                ++dropped;
            }
            continue;
        }
        if (c->role != DM_ENEMY || names_moby.count(pl->second.pvar)) {
            continue;
        }
        const uint32_t size =
            get<uint32_t>(table + 8u * static_cast<uint32_t>(pl->second.pvar) + 4u);
        wanted.push_back({m, c->id, size, pl->second.pvar});
        asks.push_back(
            {dm::scale_count(1, k - 1.0, s_rng), dm::pvar_slots(static_cast<int>(size))}
        );
    }
    if (k < 1.0) {
        s_mod->info(
            "level " + std::to_string(level) + ": " + std::to_string(dropped) + " of "
            + std::to_string(eligible) + " enemies left out"
        );
        return;
    }
    const int free = dm::count_free(mem(), pool);
    const int budget = dm::clone_budget(free, level);
    dm::ClonePlan plan = dm::plan_clones(asks, budget);
    gaddr room = 0;
    uint32_t room_left = 0;
    if (plan.pvar_slots > 0 && !dm::reserve_tail(mem(), at, plan.pvar_slots, &room, &room_left)) {
        // No room for large pvars: only the copies whose pvars fit their own slot.
        for (std::size_t i = 0; i < asks.size(); ++i) {
            asks[i].want = asks[i].pvslots > 0 ? 0 : asks[i].want;
        }
        plan = dm::plan_clones(asks, budget);
        room = 0;
        room_left = 0;
    }
    int made = 0, no_spot = 0;
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        for (int c = 0; c < plan.copies[i]; ++c) {
            float spot[3];
            if (!find_spot(wanted[i].moby, c, spot)) {
                ++no_spot;
                continue;
            }
            const auto in = inner.find(wanted[i].block);
            static const std::vector<uint32_t> none;
            if (copy_moby(
                    wanted[i].moby,
                    wanted[i].cls,
                    spot,
                    wanted[i].size,
                    in == inner.end() ? none : in->second,
                    &room,
                    &room_left
                )
                == 0) {
                break;
            }
            ++made;
        }
    }
    s_mod->info(
        "level " + std::to_string(level) + ": " + std::to_string(made) + " copies of "
        + std::to_string(eligible) + " enemies (" + std::to_string(free) + " free slots, room for "
        + std::to_string(budget) + " beside the " + std::to_string(dm::k_pool_reserve)
        + " kept for the game; " + std::to_string(plan.pvar_slots) + " reserved for pvars; "
        + std::to_string(no_spot) + " without a clear spot)"
    );
}

template <class Original>
uint32_t load_gameplay(Original original, int32_t full) {
    const uint32_t cursor = original(full);
    const int level = s_mod->loaded_level();
    if (level < 0 || level >= k_levels) {
        return cursor;
    }
    s_level = dm::effective(s_chaos, s_sliders, s_seed, level);
    set_active();
    s_anim.clear();
    std::memset(s_hit_owner, 0, sizeof s_hit_owner);
    s_rng = dm::Rng(dm::mix(s_seed, 0x3000u + static_cast<uint64_t>(level)));
    if (s_chaos || s_trace) {
        s_mod->info("level " + std::to_string(level) + ": " + describe(s_level));
    }
    rewrite_items(level);
    adjust_enemies(level);
    return cursor;
}

// ---- Load ----

template <class HookPoint, class F>
void take(openrac::Mod& mod, F&& f) {
    mod.hook<HookPoint>(std::forward<F>(f));
}

void load(openrac::Mod& mod) {
    s_mod = &mod;
    s_disc = nullptr;
    if (mod.game() != "rac1") {
        throw openrac::Error("difficulty is for Ratchet & Clank (rac1)");
    }
    for (const auto& disc : k_dm_discs) {
        if (mod.version() == disc.version) {
            s_disc = &disc;
        }
    }
    if (s_disc == nullptr) {
        throw openrac::Error(
            "difficulty knows the PAL and NTSC-U discs, not " + std::string(mod.version())
        );
    }
    s_classes.clear();
    for (const auto& c : k_dm_classes) {
        s_classes[c.id] = &c;
    }

    s_chaos = mod.setting<bool>("chaos");
    for (int k = 0; k < dm::KNOB_COUNT; ++k) {
        s_sliders[k] = dm::clamp_knob(k, mod.setting<double>(dm::k_knobs[k].key));
    }
    const std::string seed_text = mod.setting<std::string>("seed");
    if (seed_text.empty()) {
        std::random_device rd;
        s_seed =
            (static_cast<uint64_t>(rd()) ^ static_cast<uint64_t>(std::time(nullptr))) & 0xFFFFFFFFu;
    } else {
        s_seed = dm::seed_from_text(seed_text);
    }
    s_trace = mod.setting<bool>("trace");
    s_events_on = mod.setting<bool>("chaos_events");
    s_clock = dm::EventClock{};
    s_clock.seed = dm::mix(s_seed, 0x4000u);
    s_clock.every = 60.0 * static_cast<double>(mod.setting<int>("chaos_minutes"));
    if (!(s_clock.every >= 60.0)) {
        s_clock.every = 180.0;
    }
    s_level = dm::effective(s_chaos, s_sliders, s_seed, -1);
    set_active();
    s_damage = {};
    s_pickups = {};
    for (auto& a : s_ammo) {
        a = {};
    }
    for (bool& r : s_items_read) {
        r = false;
    }
    s_timed_depth = 0;

    s_create_moby = mod.required_function<uint32_t (*)(int32_t)>(s_disc->create_moby);
    s_delete_moby = mod.required_function<void (*)(uint32_t)>(s_disc->delete_moby);
    s_build_matrix = mod.required_function<void (*)(uint32_t)>(s_disc->build_matrix);
    s_ground = mod.required_function<float (*)(uint32_t, int32_t, float)>(s_disc->ground);
    s_line = mod.required_function<int32_t (*)(uint32_t, uint32_t, int32_t, uint32_t, uint32_t)>(
        s_disc->line
    );

    // The hook points every slider needs: one this OpenRAC lacks refuses the mod, naming it.
    take<TakeDamage>(mod, [](auto original, int32_t amount) { take_damage(original, amount); });
    take<GetHit>(mod, [](auto original, uint32_t m, int32_t mask, int32_t keep) {
        return get_hit(original, m, mask, keep);
    });
    take<TakeAmmo>(mod, [](auto original, int32_t item, int32_t amount) {
        return take_ammo(original, item, amount);
    });
    take<RecordDeath>(mod, [](auto original, uint32_t m, int32_t flags, int32_t index) {
        record_death(original, m, flags, index);
    });
    take<LoadGameplay>(mod, [](auto original, int32_t full) {
        return load_gameplay(original, full);
    });
    take<ApproachSpeed>(mod, [](auto original, float up, float down) {
        approach_speed(original, up, down);
    });

    // The catalogue's updates: the enemies' (their speed) and the nanotech orb's (health pickups).
    // One a disc lacks only leaves that class at its own speed: logged, not refused.
    const openrac_api& api = mod.api();
    int timed = 0;
    std::string missing;
    const int count = static_cast<int>(sizeof k_dm_update_hooks / sizeof k_dm_update_hooks[0]);
    for (int i = 0; i < count; ++i) {
        void* original = nullptr;
        const int r = api.hook(
            mod.handle(),
            k_dm_update_hooks[i],
            "void(u32)",
            reinterpret_cast<void*>(k_update_thunks[static_cast<std::size_t>(i)]),
            &original
        );
        if (r == OPENRAC_HOOK_OK) {
            s_update_original[i] = reinterpret_cast<void (*)(uint32_t)>(original);
            ++timed;
        } else {
            missing += std::string(missing.empty() ? "" : ", ") + k_dm_update_hooks[i];
        }
    }
    void* ball = nullptr;
    if (api.hook(
            mod.handle(), k_nanotech_ball, "void(u32)", reinterpret_cast<void*>(&ball_update), &ball
        )
        == OPENRAC_HOOK_OK) {
        s_ball_original = reinterpret_cast<void (*)(uint32_t)>(ball);
    } else {
        missing += std::string(missing.empty() ? "" : ", ") + k_nanotech_ball;
    }
    if (!missing.empty()) {
        mod.warn("no hook point on this disc for " + missing + ": those stay as the game has them");
    }

    mod.on_frame([] {
        if (s_events_on && in_play()) {
            const double dt = get<float>(exe(s_disc->tick) + 12u);
            const int change = s_clock.advance(dt > 0.0 && dt < 0.1 ? dt : 1.0 / 60.0);
            if (change != 0) {
                set_active();
                s_mod->info(
                    change > 0 ? std::string("chaos: ") + dm::k_events[s_clock.current].name
                               : std::string("chaos: back to normal")
                );
            }
        }
        keep_max_health();
    });

    std::string mode = s_chaos ? "chaos" : dm::sliders_moved(s_sliders) ? "sliders" : "retail";
    if (s_chaos && dm::sliders_moved(s_sliders)) {
        mode += ", with sliders moved off 1.0 holding";
    }
    mod.info(
        mode + " on the " + std::string(mod.version()) + " disc; seed "
        + dm::seed_code(s_seed) + " (put it in Seed to play the same rolls again); "
        + std::to_string(timed) + " enemy updates timed"
        + (s_events_on ? "; chaos events every " + fmt(s_clock.every / 60.0) + " min" : "")
    );
    if (!s_chaos) {
        mod.info(describe(s_level));
    }
}

}  // namespace

OPENRAC_MOD_CPP("difficulty", "1.1.0", load)
