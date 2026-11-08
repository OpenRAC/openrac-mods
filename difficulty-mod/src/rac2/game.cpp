// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Going Commando: the difficulty mod's rac2 part (README.md, "In Going Commando"; src/mod.cpp loads
// it for rac2). The same sliders, presets and Chaos as Ratchet & Clank's (src/common), each changed
// where the game takes it through OpenRAC's rac2 hook points, or once a level at its source (the
// weapon table); with every slider at 1.0 each hook calls the game's own function as it was called.
//
//   damage to Ratchet   Hero_TakeDamage (func_L00_002A4EF8 on USA v1.01): the hit's points before
//                       the armour takes its share, scaled (an accumulator: 0.5 is half the points,
//                       carried over between hits)
//   damage to enemies   Moby_GetHitMessage (func_002A9640): the hit an enemy reads (a moby whose
//                       class is of the bestiary's kind, 5) has its damage (+0x2C) scaled, once a
//                       hit however often it is read
//   Ratchet's speed     Hero_ApproachSpeed (func_L00_002B41B8): his ground speed eases toward a
//                       scaled target
//   prices, max ammo    the weapon table (level 0's copy at 0x262570), rewritten from the level's
//                       own values at each level's load (Level_Load, func_L00_00303990)
//   ammo per shot       Weapon_TakeAmmo (func_L00_002D3F30), with an accumulator per item
//   bolt drops          Moby_RecordDeath (func_002A6C58): the bolts (+0xB4) a dying enemy or a
//                       broken crate drops, scaled for the call
//   max health          Hero_NanotechSteps (func_L00_002D5E30): the count the game's max health is
//                       made of (4, the nanotech levels' steps, then the boosts) scaled, so the
//                       game's own recount gives the scaled max health (and fills his health when
//                       it changes)
//   health pickups      the nanotech orb's update (func_003136D0): the health it gives, with an
//                       accumulator
//
// Going Commando has no clock its enemies run on (their code steps a 60th of a second in
// constants of its own), and no table that tells the level's free enemies from those its scripts
// need: the enemy count and the enemy speed are rac1's only (the manifest's "games"), and Chaos's
// events that are only about the enemies' speed are never picked here.
#include "game.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "discs.h"
#include "openrac/mod.hpp"
#include "rules.h"
#include "session.h"

namespace {

using openrac::gaddr;

// ---- Constants ----

// The size of a game word: the stride of the ammo array.
constexpr uint32_t kWord = 4u;

// The hero record (discs.h: dm2_disc.hero). Offsets are from its address (documented: discs.h).
constexpr uint32_t kHeroTargetSpeed = 0x1B0u;
constexpr uint32_t kHeroItemInHand = 0x1248u;
constexpr uint32_t kHeroMoby = 0x2290u;
constexpr uint32_t kHeroHealth = 0x24CCu;

// A moby: its class's header, a pointer (documented: rac2-decomp src/game/002A0430.c, RewardMoby's
// mclass); the bolts it drops, s16 (the same struct's bolts).
constexpr uint32_t kMobyClass = 0x24u;
constexpr uint32_t kMobyBolts = 0xB4u;
// A class header's kind, s16: 5 for the enemies the bestiary counts (documented: func_002A6C58
// counts a kill in the bestiary for that kind only).
constexpr uint32_t kClassKind = 0x46u;
constexpr int16_t kKindEnemy = 5;
// The game's memory, the most a pointer read from a moby is believed in (the EE's 32 MB).
constexpr uint32_t kRamSize = 0x02000000u;

// A pending hit (what Moby_GetHitMessage returns): its damage, f32 (documented: rac2-decomp
// src/game/002B00B0-damage-g8.c, F(hit, 0x2c)).
constexpr uint32_t kHitDamage = 0x2Cu;

// The weapon table (discs.h: dm2_disc.weapon_table): 119 records of 0xE0 bytes, the 120th a mark
// (-2); the fields it rewrites (documented: discs.h; the records read from the USA v1.01 boot's
// copy at 0x239BA0, the same bytes as each level's).
constexpr int k_rows = 119;
constexpr uint32_t k_row_size = 0xE0u;
constexpr uint32_t kRowPrice = 0x80u;
constexpr uint32_t kRowAmmoPrice = 0x88u;
constexpr uint32_t kRowMaxAmmo = 0x8Eu;
// The most a price may be (assumed), and what an ammo field, a u16, may be.
constexpr int kMaxPrice = 9999999;
constexpr int kMaxU16 = 65535;
// The most a bolt count, an s16, may be.
constexpr int kMaxS16 = 32767;

// The ammo array's slots: g_weaponAmmo is 0x1E0 bytes, g_weaponXp after it (documented:
// rac2-decomp symbol_addrs). What Weapon_TakeAmmo is given for the item in hand (documented: its
// C reads the item in hand for -1).
constexpr int k_ammo_slots = 120;
constexpr int32_t kItemInHand = -1;

// The nanotech boosts, a byte each (documented: discs.h), and the most health a count is made
// (assumed: the HUD's three digits).
constexpr int k_boosts = 10;
constexpr int kMaxHealth = 999;

// The levels there are, 0..30 (documented: rac2-decomp docs/LEVELS.md).
constexpr int k_levels = 31;

// The game's state that is play (documented: discs.h: dm2_disc.game_state).
constexpr int32_t kStatePlay = 0;

// The knobs Going Commando honours: all but the enemy count and the enemy speed.
constexpr unsigned kKnobs = dm::k_all_knobs & ~(1u << dm::ENEMY_COUNT) & ~(1u << dm::ENEMY_SPEED);

// The salt that makes a level's own random stream (the bolts' rounding) apart from the others.
constexpr uint64_t kLevelRngSalt = 0x3000u;

// ---- Hook points ----

// OpenRAC's rac2 hook points, by permanent id (docs/MULTI_GAME.md): the same <area>.<name> as
// rac1's where they do the same job.
struct TakeDamage {
    static constexpr const char* id = "rac2.player.take_damage";
    static constexpr const char* name = "Hero_TakeDamage";
    static constexpr const char* signature = "void(i32)";
    using fn = void (*)(int32_t);
};

struct GetHit {
    static constexpr const char* id = "rac2.combat.get_hit";
    static constexpr const char* name = "Moby_GetHitMessage";
    static constexpr const char* signature = "u32(u32,u32,i32)";
    using fn = uint32_t (*)(uint32_t, uint32_t, int32_t);
};

struct TakeAmmo {
    static constexpr const char* id = "rac2.weapon.take_ammo";
    static constexpr const char* name = "Weapon_TakeAmmo";
    static constexpr const char* signature = "i32(i32,i32)";
    using fn = int32_t (*)(int32_t, int32_t);
};

struct RecordDeath {
    static constexpr const char* id = "rac2.moby.record_death";
    static constexpr const char* name = "Moby_RecordDeath";
    static constexpr const char* signature = "void(u32,i32,i32)";
    using fn = void (*)(uint32_t, int32_t, int32_t);
};

struct LevelLoad {
    static constexpr const char* id = "rac2.level.load";
    static constexpr const char* name = "Level_Load";
    static constexpr const char* signature = "i32(i32,i32)";
    using fn = int32_t (*)(int32_t, int32_t);
};

struct ApproachSpeed {
    static constexpr const char* id = "rac2.player.approach_speed";
    static constexpr const char* name = "Hero_ApproachSpeed";
    static constexpr const char* signature = "void(f32,f32)";
    using fn = void (*)(float, float);
};

struct NanotechSteps {
    static constexpr const char* id = "rac2.player.nanotech_steps";
    static constexpr const char* name = "Hero_NanotechSteps";
    static constexpr const char* signature = "i32(u32,u32,u32)";
    using fn = int32_t (*)(uint32_t, uint32_t, uint32_t);
};

// The nanotech orb's update, taken by id when the port has it (health pickups only).
constexpr const char* k_nanotech_ball = "rac2.moby.nanotech_ball.update";

// ---- State ----

openrac::Mod* s_mod;
const dm2_disc* s_disc;
dm::Session& s_session = dm::session();
dm::Rng s_rng(1);

// Per level: the weapon table as the level's program has it, read before the first change. Game
// thread only.
std::vector<uint8_t> s_rows[k_levels];
bool s_rows_read[k_levels];

// The carried-over fractions of damage taken, health pickups and ammo per shot (one per item).
// Game thread only.
dm::Accumulator s_damage;
dm::Accumulator s_pickups;
dm::Accumulator s_ammo[k_ammo_slots];

// The hits whose damage was scaled, by the hit's address: the moby it was for and the value
// written. Game thread only.
std::unordered_map<uint32_t, std::pair<uint32_t, float>> s_hits;

// The max health last logged (with the trace), so each change is one line.
int s_traced_max = -1;

// The nanotech orb's own update.
void (*s_ball_original)(uint32_t);

// ---- Game memory ----

template <class T>
T get(gaddr at) {
    return s_mod->memory().read<T>(at);
}

template <class T>
void put(gaddr at, T v) {
    s_mod->memory().write<T>(at, v);
}

// The base of the game's memory in this process: the game address a is mem() + a.
uint8_t* mem() {
    return s_mod->memory().base();
}

// A global of the boot program, the loaded level's copy.
gaddr exe(uint32_t address) {
    return s_mod->relocate(address);
}

// A global of level 0's program, the loaded level's copy.
gaddr level0(uint32_t address) {
    return s_mod->relocate_level(0, address);
}

gaddr hero() {
    return s_disc->hero;
}

// Whether a level is loaded and in play (not a menu, a scene, the vendor or a load).
bool in_play() {
    return s_mod->loaded_level() >= 0 && get<int32_t>(exe(s_disc->game_state)) == kStatePlay;
}

// Whether a moby is an enemy: its class is of the kind the bestiary counts.
bool is_enemy(uint32_t moby) {
    const uint32_t cls = get<uint32_t>(moby + kMobyClass);
    return cls != 0 && cls < kRamSize - kClassKind && get<int16_t>(cls + kClassKind) == kKindEnemy;
}

// ---- Damage, ammo, bolts, speed, health ----

// A hit on Ratchet: its points before the armour's share, scaled and carried over between hits. A
// hit scaled to nothing is not taken.
template <class Original>
void take_damage(Original original, int32_t amount) {
    const double k = s_session.active[dm::DAMAGE_TAKEN];

    // No hit to scale (0 does nothing, below 0 heals), or the slider at retail: the game's call.
    if (amount <= 0 || k == 1.0) {
        original(amount);
        return;
    }

    const int points = s_damage.take(k, amount);

    // Some points to take this hit.
    if (points > 0) {
        original(points);
    }
}

// The hit an enemy reads: its damage scaled once (the same hit read again keeps the value
// written, which is how it is told from a new one).
template <class Original>
uint32_t get_hit(Original original, uint32_t moby, uint32_t mask, int32_t keep) {
    const uint32_t hit = original(moby, mask, keep);
    const double k = s_session.active[dm::DAMAGE_DEALT];

    // Nothing was hit, the slider is at retail, it is Ratchet who reads it (his own damage is the
    // damage to Ratchet's slider's), or the moby is no enemy.
    if (hit == 0 || k == 1.0 || moby == get<uint32_t>(hero() + kHeroMoby) || !is_enemy(moby)) {
        return hit;
    }

    const float damage = get<float>(hit + kHitDamage);
    const auto it = s_hits.find(hit);

    // The value there is what the mod wrote for this moby: scaled already.
    if (it != s_hits.end() && it->second.first == moby && it->second.second == damage) {
        return hit;
    }

    const float scaled = static_cast<float>(damage * k);
    put<float>(hit + kHitDamage, scaled);
    s_hits[hit] = {moby, scaled};
    return hit;
}

// A shot's ammo: the multiplier per shot, carried over per item; never a free shot from an empty
// gun, and the last rounds still fire when a shot would want more than is left.
template <class Original>
int32_t take_ammo(Original original, int32_t item, int32_t amount) {
    const double k = s_session.active[dm::AMMO_USE];

    // The slider is at retail, or the call takes no ammo: the game's own call.
    if (k == 1.0 || amount <= 0) {
        return original(item, amount);
    }

    const int32_t id = item == kItemInHand ? get<int32_t>(hero() + kHeroItemInHand) : item;

    // Not an item the game has ammo for.
    if (id < 0 || id >= k_ammo_slots) {
        return original(item, amount);
    }

    const int32_t have = get<int32_t>(s_disc->ammo + kWord * static_cast<uint32_t>(id));
    int32_t want = s_ammo[id].take(k, amount);

    // This shot's share rounds to nothing: a free shot only from a gun that has rounds, as the game
    // would refuse an empty one.
    if (want == 0) {
        return have > 0 ? original(item, 0) : original(item, amount);
    }

    // The shot wants more than is left.
    if (want > have) {
        // Less is left than the game asks for itself: it refuses, as it would.
        if (have < amount) {
            return original(item, amount);
        }

        want = have;
    }

    return original(item, want);
}

// The bolts a dying enemy or a broken crate drops: its bolt count scaled for the call, and the
// game's own count back after it.
template <class Original>
void record_death(Original original, uint32_t moby, int32_t flags, int32_t arg) {
    const double k = s_session.active[dm::BOLTS];

    // The slider is at retail: the game's own call.
    if (k == 1.0) {
        original(moby, flags, arg);
        return;
    }

    const int16_t own = get<int16_t>(moby + kMobyBolts);
    const int n = dm::scale_count(own, k, s_rng);
    const auto scaled = static_cast<int16_t>(n > kMaxS16 ? kMaxS16 : n);
    put<int16_t>(moby + kMobyBolts, scaled);
    original(moby, flags, arg);

    // The game left the scaled count as it was: its own back, so a later death is scaled from it.
    if (get<int16_t>(moby + kMobyBolts) == scaled) {
        put<int16_t>(moby + kMobyBolts, own);
    }
}

// Ratchet's ground speed eases toward his target speed: toward the target scaled, as fast.
template <class Original>
void approach_speed(Original original, float up, float down) {
    const double k = s_session.active[dm::PLAYER_SPEED];

    // The slider is at retail: the game's own call.
    if (k == 1.0) {
        original(up, down);
        return;
    }

    const gaddr target = hero() + kHeroTargetSpeed;
    const float t = get<float>(target);
    const float scaled = static_cast<float>(t * k);
    put<float>(target, scaled);
    original(static_cast<float>(up * k), static_cast<float>(down * k));

    // The game left the scaled target as it was: give his own back, so the scale is never kept in
    // the value the game reads elsewhere.
    if (get<float>(target) == scaled) {
        put<float>(target, t);
    }
}

// How many nanotech boosts he has.
int boosts() {
    int n = 0;

    // Each boost's byte.
    for (int i = 0; i < k_boosts; ++i) {
        n += get<uint8_t>(s_disc->boosts + static_cast<uint32_t>(i)) != 0;
    }

    return n;
}

// The max health's count: the game adds the nanotech levels' steps to *count (which starts at 4),
// and then a cell for each boost. The count is made such that, with the boosts, it is the game's
// max health scaled (at least 1). The game's recount then gives the max health it shows, saves and
// fills his health to when it changes; its HUD's callers ignore the count.
template <class Original>
int32_t nanotech_steps(Original original, uint32_t a0, uint32_t a1, uint32_t count) {
    const int32_t r = original(a0, a1, count);
    const double k = s_session.active[dm::MAX_HEALTH];

    // The slider is at retail, or no count was given.
    if (k == 1.0 || count == 0) {
        return r;
    }

    const int b = boosts();
    const int retail = get<int32_t>(count) + b;
    const double want = std::round(retail * k);
    const int target = want < 1.0 ? 1 : want > kMaxHealth ? kMaxHealth : static_cast<int>(want);
    put<int32_t>(count, target - b);

    // With the trace on, one line for each change.
    if (s_session.trace && target != s_traced_max) {
        s_traced_max = target;
        s_mod->info("max health " + std::to_string(retail) + " -> " + std::to_string(target));
    }

    return r;
}

// The nanotech orb: the health it gives this tick, scaled.
void ball_update(uint32_t moby) {
    const double k = s_session.active[dm::HEALTH_PICKUPS];
    const gaddr hp = hero() + kHeroHealth;
    const int32_t before = get<int32_t>(hp);
    s_ball_original(moby);
    const int32_t gained = get<int32_t>(hp) - before;

    // The slider is at retail, or the orb gave nothing this tick.
    if (k == 1.0 || gained <= 0) {
        return;
    }

    const int32_t max = get<int32_t>(s_disc->max_health);
    const int32_t now = before + s_pickups.take(k, gained);
    put<int32_t>(hp, now > max ? max : now);
}

// ---- A level's load: its values and the weapon table ----

// The weapon table from the level's own values: prices (+0x80), ammo prices (+0x88) and max ammo
// (+0x8E), written whatever they are now (so a value is never scaled twice).
void rewrite_rows(int level) {
    const gaddr table = level0(s_disc->weapon_table);
    const uint8_t* at = mem() + table;

    // The first load of this level: keep its own table to scale from.
    if (!s_rows_read[level]) {
        s_rows[level].assign(at, at + k_rows * k_row_size);
        s_rows_read[level] = true;
    }

    const uint8_t* own = s_rows[level].data();
    const dm::Values& v = s_session.level;

    // Each record.
    for (int i = 0; i < k_rows; ++i) {
        const uint32_t r = static_cast<uint32_t>(i) * k_row_size;
        int32_t price;
        std::memcpy(&price, own + r + kRowPrice, 4);
        put<int32_t>(table + r + kRowPrice, dm::scale_amount(price, v[dm::WEAPON_COST], kMaxPrice));
        uint16_t ammo_price;
        std::memcpy(&ammo_price, own + r + kRowAmmoPrice, 2);
        put<uint16_t>(
            table + r + kRowAmmoPrice,
            static_cast<uint16_t>(dm::scale_amount(ammo_price, v[dm::AMMO_COST], kMaxU16))
        );
        uint16_t most;
        std::memcpy(&most, own + r + kRowMaxAmmo, 2);
        put<uint16_t>(
            table + r + kRowMaxAmmo,
            static_cast<uint16_t>(dm::scale_amount(most, v[dm::MAX_AMMO], kMaxU16))
        );
    }
}

// Level_Load: once the game has loaded the level (its program's data, the gameplay file, the
// mobys), this level's values are rolled (with Chaos) or taken and the weapon table rewritten. It
// runs again when the game reloads the level after a death: the same values, the same table.
template <class Original>
int32_t level_load(Original original, int32_t load, int32_t resume) {
    const int32_t next = original(load, resume);
    const int level = s_mod->loaded_level();

    // Not a level the mod knows: nothing to apply.
    if (level < 0 || level >= k_levels) {
        return next;
    }

    const uint64_t seed = dm::level_values(level);
    s_hits.clear();
    s_rng = dm::Rng(dm::mix(seed, kLevelRngSalt + static_cast<uint64_t>(level)));
    rewrite_rows(level);
    return next;
}

// ---- Load ----

// Takes a hook point, a function the host calls in place of the game's with the original to call.
template <class HookPoint, class F>
void take(openrac::Mod& mod, F&& f) {
    mod.hook<HookPoint>(std::forward<F>(f));
}

// Finds this disc in discs.h, refusing one it does not know.
void find_disc(openrac::Mod& mod) {
    s_disc = nullptr;

    // Each disc the mod knows: the one whose version this is.
    for (const auto& disc : k_dm2_discs) {
        // This is the disc's version.
        if (mod.version() == disc.version) {
            s_disc = &disc;
        }
    }

    // A disc the mod has no addresses for.
    if (s_disc == nullptr) {
        throw openrac::Error(
            "difficulty knows Going Commando's USA v1.01, PAL and USA v2.00 discs, not "
            + std::string(mod.version())
        );
    }
}

// Puts what the hooks carry between calls back as at a start.
void reset_state() {
    s_damage = {};
    s_pickups = {};

    // Each item's carried-over fraction.
    for (auto& a : s_ammo) {
        a = {};
    }

    // Each level's table, to be read again from the level's own values.
    for (bool& r : s_rows_read) {
        r = false;
    }

    s_hits.clear();
    s_traced_max = -1;
}

// Each frame: the events' clock, in play.
void on_frame() {
    // Chaos events are on and the game is in play: the clock runs on play time only.
    if (s_session.events_on && !s_session.follow && in_play()) {
        dm::advance_events(s_disc->tick_seconds);
    }
}

}  // namespace

// The rac2 part's entry (game.h): checks the disc, reads the settings, takes the hook points it
// needs (one this OpenRAC lacks refuses the mod, naming it), and logs how it plays.
void rac2_load(openrac::Mod& mod) {
    s_mod = &mod;
    find_disc(mod);
    dm::start(mod, kKnobs);
    reset_state();

    // The hook points every slider needs: one this OpenRAC lacks refuses the mod, naming it.
    take<TakeDamage>(mod, [](auto original, int32_t amount) { take_damage(original, amount); });
    take<GetHit>(mod, [](auto original, uint32_t m, uint32_t mask, int32_t keep) {
        return get_hit(original, m, mask, keep);
    });
    take<TakeAmmo>(mod, [](auto original, int32_t item, int32_t amount) {
        return take_ammo(original, item, amount);
    });
    take<RecordDeath>(mod, [](auto original, uint32_t m, int32_t flags, int32_t arg) {
        record_death(original, m, flags, arg);
    });
    take<LevelLoad>(mod, [](auto original, int32_t load, int32_t resume) {
        return level_load(original, load, resume);
    });
    take<ApproachSpeed>(mod, [](auto original, float up, float down) {
        approach_speed(original, up, down);
    });
    take<NanotechSteps>(mod, [](auto original, uint32_t a0, uint32_t a1, uint32_t count) {
        return nanotech_steps(original, a0, a1, count);
    });

    // The nanotech orb's update (health pickups): one this OpenRAC lacks leaves the pickups as the
    // game has them, logged, not refused.
    void* ball = nullptr;
    std::string missing;

    // Taken by id, as the update hook points of rac1 are.
    if (mod.api().hook(
            mod.handle(), k_nanotech_ball, "void(u32)", reinterpret_cast<void*>(&ball_update), &ball
        )
        == OPENRAC_HOOK_OK) {
        s_ball_original = reinterpret_cast<void (*)(uint32_t)>(ball);
    } else {
        s_ball_original = nullptr;
        missing = k_nanotech_ball;
        mod.warn(
            "no hook point on this disc for " + missing
            + ": health pickups stay as the game has them"
        );
    }

    mod.on_frame(on_frame);
    dm::log_start(
        "enemy count and enemy speed are Ratchet & Clank's only"
        + std::string(missing.empty() ? "" : "; health pickups as the game has them")
    );
}
