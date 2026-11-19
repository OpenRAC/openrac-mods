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
//   health pickups      the nanotech orb's update (func_003136D0, rac2.moby.nanotech_ball.update):
//                       the health its pieces gave this tick, scaled with an accumulator, at most
//                       the most the game allows him (his max health, 4 in a vehicle)
//
// Going Commando has no clock its enemies run on (their code steps a 60th of a second in
// constants of its own), and no table that tells the level's free enemies from those its scripts
// need: the enemy count and the enemy speed are rac1's only (the manifest's "games"), and Chaos's
// events that are only about the enemies' speed are never picked here.

#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "discs.h"
#include "openrac/mod.hpp"
#include "rules.h"
#include "session.h"

namespace difficulty::rac2 {
namespace {

using openrac::gaddr;

// ---- Constants ----

// The size of a game word: the stride of the ammo array.
constexpr std::uint32_t kWord = 4u;

// The hero record (discs.h: Disc.hero). Offsets are from its address (documented: discs.h).
constexpr std::uint32_t kHeroTargetSpeed = 0x1B0u;
constexpr std::uint32_t kHeroItemInHand = 0x1248u;
constexpr std::uint32_t kHeroMoby = 0x2290u;
constexpr std::uint32_t kHeroHealth = 0x24CCu;
// His vehicle mode, a byte: 1 in a vehicle, where the orbs fill his health to 4 at most
// (documented: rac2-decomp src/game/0030F7B0-ball-sw-g8.c, the Hero's vehicle_mode and `lim`).
constexpr std::uint32_t kHeroVehicleMode = 0x22B4u;
constexpr std::int32_t kVehicleHealth = 4;

// A moby: its class's header, a pointer (documented: rac2-decomp src/game/002A0430.c, RewardMoby's
// mclass); the bolts it drops, s16 (the same struct's bolts).
constexpr std::uint32_t kMobyClass = 0x24u;
constexpr std::uint32_t kMobyBolts = 0xB4u;
// A class header's kind, s16: 5 for the enemies the bestiary counts (documented: func_002A6C58
// counts a kill in the bestiary for that kind only).
constexpr std::uint32_t kClassKind = 0x46u;
constexpr std::int16_t kKindEnemy = 5;
// The game's memory, the most a pointer read from a moby is believed in (the EE's 32 MB).
constexpr std::uint32_t kRamSize = 0x02000000u;

// A pending hit (what Moby_GetHitMessage returns): its damage, f32 (documented: rac2-decomp
// src/game/002B00B0-damage-g8.c, F(hit, 0x2c)).
constexpr std::uint32_t kHitDamage = 0x2Cu;

// The weapon table (discs.h: Disc.weapon_table): 119 records of 0xE0 bytes, the 120th a mark
// (-2); the fields it rewrites (documented: discs.h; the records read from the USA v1.01 boot's
// copy at 0x239BA0, the same bytes as each level's).
constexpr int kRows = 119;
constexpr std::uint32_t kRowSize = 0xE0u;
constexpr std::uint32_t kRowPrice = 0x80u;
constexpr std::uint32_t kRowAmmoPrice = 0x88u;
constexpr std::uint32_t kRowMaxAmmo = 0x8Eu;
// The most a price may be (assumed), and what an ammo field, a u16, may be.
constexpr int kMaxPrice = 9999999;
constexpr int kMaxU16 = 65535;
// The most a bolt count, an s16, may be.
constexpr int kMaxS16 = 32767;

// The ammo array's slots: g_weaponAmmo is 0x1E0 bytes, g_weaponXp after it (documented:
// rac2-decomp symbol_addrs). What Weapon_TakeAmmo is given for the item in hand (documented: its
// C reads the item in hand for -1).
constexpr int kAmmoSlots = 120;
constexpr std::int32_t kItemInHand = -1;

// The nanotech boosts, a byte each (documented: discs.h), and the most health a count is made
// (assumed: the HUD's three digits).
constexpr int kBoosts = 10;
constexpr int kMaxHealthCap = 999;

// The levels there are, 0..30 (documented: rac2-decomp docs/LEVELS.md).
constexpr int kLevels = 31;

// The game's state that is play (documented: discs.h: Disc.game_state).
constexpr std::int32_t kStatePlay = 0;

// The knobs Going Commando honours: all but the enemy count and the enemy speed.
constexpr unsigned kKnobs =
    difficulty::kAllKnobs & ~(1u << difficulty::kEnemyCount) & ~(1u << difficulty::kEnemySpeed);

// The salt that makes a level's own random stream (the bolts' rounding) apart from the others.
constexpr std::uint64_t kLevelRngSalt = 0x3000u;

// ---- Hook points ----

// OpenRAC's rac2 hook points, by permanent id (docs/MULTI_GAME.md): the same <area>.<name> as
// rac1's where they do the same job.
struct TakeDamage {
    static constexpr const char* id = "rac2.player.take_damage";
    static constexpr const char* name = "Hero_TakeDamage";
    static constexpr const char* signature = "void(i32)";
    using fn = void (*)(std::int32_t);
};

struct GetHit {
    static constexpr const char* id = "rac2.combat.get_hit";
    static constexpr const char* name = "Moby_GetHitMessage";
    static constexpr const char* signature = "u32(u32,u32,i32)";
    using fn = std::uint32_t (*)(std::uint32_t, std::uint32_t, std::int32_t);
};

struct TakeAmmo {
    static constexpr const char* id = "rac2.weapon.take_ammo";
    static constexpr const char* name = "Weapon_TakeAmmo";
    static constexpr const char* signature = "i32(i32,i32)";
    using fn = std::int32_t (*)(std::int32_t, std::int32_t);
};

struct RecordDeath {
    static constexpr const char* id = "rac2.moby.record_death";
    static constexpr const char* name = "Moby_RecordDeath";
    static constexpr const char* signature = "void(u32,i32,i32)";
    using fn = void (*)(std::uint32_t, std::int32_t, std::int32_t);
};

struct LevelLoad {
    static constexpr const char* id = "rac2.level.load";
    static constexpr const char* name = "Level_Load";
    static constexpr const char* signature = "i32(i32,i32)";
    using fn = std::int32_t (*)(std::int32_t, std::int32_t);
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
    using fn = std::int32_t (*)(std::uint32_t, std::uint32_t, std::uint32_t);
};

struct NanotechBall {
    static constexpr const char* id = "rac2.moby.nanotech_ball.update";
    static constexpr const char* name = "Moby_NanotechBall_Update";
    static constexpr const char* signature = "void(u32)";
    using fn = void (*)(std::uint32_t);
};

// ---- State ----

// Everything the mod keeps while it runs in rac2: what it loaded and what it carries between
// calls. Made by load and read by every hook, all on the game's thread.
struct State {
    // The port's mod object; not owned, valid for the life of the program.
    openrac::Mod* mod = nullptr;
    // The disc's addresses; not owned, a row of kDiscs.
    const Disc* disc = nullptr;

    // What the player chose, what plays on the level loaded and with the event on, and a party's
    // host's rules: the session every game shares (session.h).
    difficulty::Session session;
    // The level's own random stream (copies, drops, rounding).
    difficulty::Rng rng{1};

    // Per level: the weapon table as the level's program has it, read before the first change.
    std::vector<std::uint8_t> rows[kLevels];
    bool rows_read[kLevels] = {};

    // The carried-over fractions of damage taken, health pickups and ammo per shot (one per item).
    difficulty::Accumulator damage;
    difficulty::Accumulator pickups;
    difficulty::Accumulator ammo[kAmmoSlots];

    // The hits whose damage was scaled, by the hit's address: the moby it was for and the value
    // written.
    std::unordered_map<std::uint32_t, std::pair<std::uint32_t, float>> hits;

    // The max health last logged (with the trace), so each change is one line.
    int traced_max = -1;
};

// The mod's state, made at load. The hooks are plain functions the port calls with no user
// pointer, so they reach the state through this one pointer.
std::unique_ptr<State> g_state;

// Returns the state; only valid between load's start and the end of the program.
State& state() {
    return *g_state;
}

// ---- Game memory ----

template <class T>
T get(gaddr at) {
    return state().mod->memory().read<T>(at);
}

template <class T>
void put(gaddr at, T v) {
    state().mod->memory().write<T>(at, v);
}

// The base of the game's memory in this process: the game address a is mem() + a.
std::uint8_t* mem() {
    return state().mod->memory().base();
}

// A global of the boot program, the loaded level's copy.
gaddr exe(std::uint32_t address) {
    return state().mod->relocate(address);
}

// A global of level 0's program, the loaded level's copy.
gaddr level0(std::uint32_t address) {
    return state().mod->relocate_level(0, address);
}

gaddr hero() {
    return state().disc->hero;
}

// Whether a level is loaded and in play (not a menu, a scene, the vendor or a load).
bool in_play() {
    return state().mod->loaded_level() >= 0
           && get<std::int32_t>(exe(state().disc->game_state)) == kStatePlay;
}

// Whether a moby is an enemy: its class is of the kind the bestiary counts.
bool is_enemy(std::uint32_t moby) {
    const std::uint32_t cls = get<std::uint32_t>(moby + kMobyClass);
    return cls != 0 && cls < kRamSize - kClassKind
           && get<std::int16_t>(cls + kClassKind) == kKindEnemy;
}

// ---- Damage, ammo, bolts, speed, health ----

// A hit on Ratchet: its points before the armour's share, scaled and carried over between hits. A
// hit scaled to nothing is not taken.
template <class Original>
void take_damage(Original original, std::int32_t amount) {
    const double k = state().session.active[difficulty::kDamageTaken];

    // No hit to scale (0 does nothing, below 0 heals), or the slider at retail: the game's call.
    if (amount <= 0 || k == 1.0) {
        original(amount);
        return;
    }

    const int points = state().damage.take(k, amount);

    // Some points to take this hit.
    if (points > 0) {
        original(points);
    }
}

// The hit an enemy reads: its damage scaled once (the same hit read again keeps the value
// written, which is how it is told from a new one).
template <class Original>
std::uint32_t get_hit(
    Original original, std::uint32_t moby, std::uint32_t mask, std::int32_t keep
) {
    const std::uint32_t hit = original(moby, mask, keep);
    const double k = state().session.active[difficulty::kDamageDealt];

    // Nothing was hit, the slider is at retail, it is Ratchet who reads it (his own damage is the
    // damage to Ratchet's slider's), or the moby is no enemy.
    if (hit == 0 || k == 1.0 || moby == get<std::uint32_t>(hero() + kHeroMoby) || !is_enemy(moby)) {
        return hit;
    }

    const float damage = get<float>(hit + kHitDamage);
    const auto it = state().hits.find(hit);

    // The value there is what the mod wrote for this moby: scaled already.
    if (it != state().hits.end() && it->second.first == moby && it->second.second == damage) {
        return hit;
    }

    const float scaled = static_cast<float>(damage * k);
    put<float>(hit + kHitDamage, scaled);
    state().hits[hit] = {moby, scaled};
    return hit;
}

// A shot's ammo: the multiplier per shot, carried over per item; never a free shot from an empty
// gun, and the last rounds still fire when a shot would want more than is left.
template <class Original>
std::int32_t take_ammo(Original original, std::int32_t item, std::int32_t amount) {
    const double k = state().session.active[difficulty::kAmmoUse];

    // The slider is at retail, or the call takes no ammo: the game's own call.
    if (k == 1.0 || amount <= 0) {
        return original(item, amount);
    }

    const std::int32_t id =
        item == kItemInHand ? get<std::int32_t>(hero() + kHeroItemInHand) : item;

    // Not an item the game has ammo for.
    if (id < 0 || id >= kAmmoSlots) {
        return original(item, amount);
    }

    const std::int32_t have =
        get<std::int32_t>(state().disc->ammo + kWord * static_cast<std::uint32_t>(id));
    std::int32_t want = state().ammo[id].take(k, amount);

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
void record_death(Original original, std::uint32_t moby, std::int32_t flags, std::int32_t arg) {
    const double k = state().session.active[difficulty::kBolts];

    // The slider is at retail: the game's own call.
    if (k == 1.0) {
        original(moby, flags, arg);
        return;
    }

    const std::int16_t own = get<std::int16_t>(moby + kMobyBolts);
    const int n = difficulty::scale_count(own, k, state().rng);
    const auto scaled = static_cast<std::int16_t>(n > kMaxS16 ? kMaxS16 : n);
    put<std::int16_t>(moby + kMobyBolts, scaled);
    original(moby, flags, arg);

    // The game left the scaled count as it was: its own back, so a later death is scaled from it.
    if (get<std::int16_t>(moby + kMobyBolts) == scaled) {
        put<std::int16_t>(moby + kMobyBolts, own);
    }
}

// Ratchet's ground speed eases toward his target speed: toward the target scaled, as fast.
template <class Original>
void approach_speed(Original original, float up, float down) {
    const double k = state().session.active[difficulty::kPlayerSpeed];

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
    for (int i = 0; i < kBoosts; ++i) {
        n += get<std::uint8_t>(state().disc->boosts + static_cast<std::uint32_t>(i)) != 0;
    }

    return n;
}

// The max health's count: the game adds the nanotech levels' steps to *count (which starts at 4),
// and then a cell for each boost. The count is made such that, with the boosts, it is the game's
// max health scaled (at least 1). The game's recount then gives the max health it shows, saves and
// fills his health to when it changes; its HUD's callers ignore the count.
template <class Original>
std::int32_t nanotech_steps(
    Original original, std::uint32_t a0, std::uint32_t a1, std::uint32_t count
) {
    const std::int32_t r = original(a0, a1, count);
    const double k = state().session.active[difficulty::kMaxHealth];

    // The slider is at retail, or no count was given.
    if (k == 1.0 || count == 0) {
        return r;
    }

    const int b = boosts();
    const int retail = get<std::int32_t>(count) + b;
    const double want = std::round(retail * k);
    int target = 0;

    // At least one cell.
    if (want < 1.0) {
        target = 1;
    } else if (want > kMaxHealthCap) {
        // The most the game's field can hold.
        target = kMaxHealthCap;
    } else {
        target = static_cast<int>(want);
    }

    put<std::int32_t>(count, target - b);

    // With the trace on, one line for each change.
    if (state().session.trace && target != state().traced_max) {
        state().traced_max = target;
        state().mod->info("max health " + std::to_string(retail) + " -> " + std::to_string(target));
    }

    return r;
}

// The nanotech orb: the health its pieces gave him this tick, scaled (carried over, so 1.5 is one
// and then two), at most the most the game fills him to.
template <class Original>
void ball_update(Original original, std::uint32_t moby) {
    const double k = state().session.active[difficulty::kHealthPickups];
    const gaddr hp = hero() + kHeroHealth;
    const std::int32_t before = get<std::int32_t>(hp);
    original(moby);
    const std::int32_t gained = get<std::int32_t>(hp) - before;

    // The slider is at retail, or the orb gave nothing this tick.
    if (k == 1.0 || gained <= 0) {
        return;
    }

    const std::int32_t most = get<std::uint8_t>(hero() + kHeroVehicleMode) == 1
                                  ? kVehicleHealth
                                  : get<std::int32_t>(state().disc->max_health);
    const std::int32_t now = before + state().pickups.take(k, gained);
    std::int32_t filled = now;

    // The pieces would fill him past the most: stop at the most, or where he was if that is more.
    if (now > most) {
        filled = std::max(most, before);
    }

    put<std::int32_t>(hp, filled);
}

// ---- A level's load: its values and the weapon table ----

// The weapon table from the level's own values: prices (+0x80), ammo prices (+0x88) and max ammo
// (+0x8E), written whatever they are now (so a value is never scaled twice).
void rewrite_rows(int level) {
    const gaddr table = level0(state().disc->weapon_table);
    const std::uint8_t* at = mem() + table;

    // The first load of this level: keep its own table to scale from.
    if (!state().rows_read[level]) {
        state().rows[level].assign(at, at + kRows * kRowSize);
        state().rows_read[level] = true;
    }

    const std::uint8_t* own = state().rows[level].data();
    const difficulty::Values& v = state().session.level;

    // Each record.
    for (int i = 0; i < kRows; ++i) {
        const std::uint32_t r = static_cast<std::uint32_t>(i) * kRowSize;
        std::int32_t price;
        std::memcpy(&price, own + r + kRowPrice, sizeof price);
        put<std::int32_t>(
            table + r + kRowPrice,
            difficulty::scale_amount(price, v[difficulty::kWeaponCost], kMaxPrice)
        );
        std::uint16_t ammo_price;
        std::memcpy(&ammo_price, own + r + kRowAmmoPrice, sizeof ammo_price);
        put<std::uint16_t>(
            table + r + kRowAmmoPrice,
            static_cast<std::uint16_t>(
                difficulty::scale_amount(ammo_price, v[difficulty::kAmmoCost], kMaxU16)
            )
        );
        std::uint16_t most;
        std::memcpy(&most, own + r + kRowMaxAmmo, sizeof most);
        put<std::uint16_t>(
            table + r + kRowMaxAmmo,
            static_cast<std::uint16_t>(
                difficulty::scale_amount(most, v[difficulty::kMaxAmmo], kMaxU16)
            )
        );
    }
}

// Level_Load: once the game has loaded the level (its program's data, the gameplay file, the
// mobys), this level's values are rolled (with Chaos) or taken and the weapon table rewritten. It
// runs again when the game reloads the level after a death: the same values, the same table.
template <class Original>
std::int32_t level_load(Original original, std::int32_t load, std::int32_t resume) {
    const std::int32_t next = original(load, resume);
    const int level = state().mod->loaded_level();

    // Not a level the mod knows: nothing to apply.
    if (level < 0 || level >= kLevels) {
        return next;
    }

    const std::uint64_t seed = difficulty::level_values(state().session, level);
    state().hits.clear();
    state().rng =
        difficulty::Rng(difficulty::mix(seed, kLevelRngSalt + static_cast<std::uint64_t>(level)));
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
    state().disc = nullptr;

    // Each disc the mod knows: the one whose version this is.
    for (const auto& disc : kDiscs) {
        // This is the disc's version.
        if (mod.version() == disc.version) {
            state().disc = &disc;
        }
    }

    // A disc the mod has no addresses for.
    if (state().disc == nullptr) {
        throw openrac::Error(
            "difficulty knows Going Commando's USA v1.01, PAL and USA v2.00 discs, not "
            + std::string(mod.version())
        );
    }
}

// Each frame: the events' clock, in play.
void on_frame() {
    // Chaos events are on (or a party's host's event may be on) and the game is in play: the clock
    // runs on play time only.
    if ((state().session.events_on || state().session.follow) && in_play()) {
        difficulty::advance_events(state().session, state().disc->tick_seconds);
    }
}

}  // namespace

// The rac2 part's entry (game.h): checks the disc, reads the settings, takes the hook points it
// needs (one this OpenRAC lacks refuses the mod, naming it), and logs how it plays.
void load(openrac::Mod& mod) {
    // A load starts afresh: nothing carries over from an earlier one.
    g_state = std::make_unique<State>();
    state().mod = &mod;
    find_disc(mod);
    difficulty::start(state().session, mod, kKnobs);

    // The hook points every slider needs: one this OpenRAC lacks refuses the mod, naming it.
    take<TakeDamage>(mod, [](auto original, std::int32_t amount) {
        take_damage(original, amount);
    });
    take<GetHit>(mod, [](auto original, std::uint32_t m, std::uint32_t mask, std::int32_t keep) {
        return get_hit(original, m, mask, keep);
    });
    take<TakeAmmo>(mod, [](auto original, std::int32_t item, std::int32_t amount) {
        return take_ammo(original, item, amount);
    });
    take<RecordDeath>(
        mod, [](auto original, std::uint32_t m, std::int32_t flags, std::int32_t arg) {
            record_death(original, m, flags, arg);
        }
    );
    take<LevelLoad>(mod, [](auto original, std::int32_t load, std::int32_t resume) {
        return level_load(original, load, resume);
    });
    take<ApproachSpeed>(mod, [](auto original, float up, float down) {
        approach_speed(original, up, down);
    });
    take<NanotechSteps>(
        mod, [](auto original, std::uint32_t a0, std::uint32_t a1, std::uint32_t count) {
            return nanotech_steps(original, a0, a1, count);
        }
    );
    take<NanotechBall>(mod, [](auto original, std::uint32_t moby) { ball_update(original, moby); });

    mod.on_frame(on_frame);
    difficulty::log_start(
        state().session, "enemy count and enemy speed are Ratchet & Clank's only"
    );
}

}  // namespace difficulty::rac2
