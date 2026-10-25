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
// The sliders (the manifest's "settings"; ranges and Chaos's ranges are in rules.cpp):
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
//
// The other settings:
//
//   chaos               each level rolls its own value for every slider left at 1.0, from the
//                       seed; a slider moved off 1.0 holds on every level
//   seed                for Chaos: an 8-hex-digit code plays the same rolls again, any other text
//                       is hashed; blank takes a new seed at each start (shown in the log)
//   chaos_events        every few minutes of play, a random event for 45 seconds (rules.h: Event)
//   chaos_minutes       the minutes of play between events
//   trace               more lines in the log: each level's values, max health changes
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

// ---- Constants ----

// Game addresses are per disc and live in discs.h (dm_disc). What is below is the layout of what
// they point at, the same on both discs.

// The size of a game word: the stride of the ammo array and of the tick constants.
constexpr uint32_t kWord = 4u;

// The hero (Hero, discs.h: dm_disc.hero). Offsets are from its address.

// The hero's target ground speed, f32 (documented: discs.h).
constexpr uint32_t kHeroTargetSpeed = 0x190u;
// The hero's moby, a u32 address, compared with the moby a hit is for to tell Ratchet's own hits
// from an enemy's (assumed).
constexpr uint32_t kHeroMoby = 0x2080u;
// A byte that is 1 when Ratchet is not on foot, where max health is left alone (assumed).
constexpr uint32_t kHeroNotOnFoot = 0x20A4u;
// The hero's health in cells, s32 (documented: discs.h).
constexpr uint32_t kHeroHealth = 0x22A8u;

// The upgrade bytes (discs.h: dm_disc.upgrades), from its address.

// Non-zero when the Premium Nanotech upgrade is bought (documented: discs.h).
constexpr uint32_t kUpgradePremium = 4u;
// Non-zero when the Ultra Nanotech upgrade is bought (documented: discs.h).
constexpr uint32_t kUpgradeUltra = 5u;
// The cells of health retail gives: none, Premium, Ultra (documented: the manifest's max_health
// text).
constexpr int kCellsBase = 4;
constexpr int kCellsPremium = 5;
constexpr int kCellsUltra = 8;

// The game mode that is play (documented: discs.h: dm_disc.level_mode).
constexpr int32_t kModePlay = 0;

// The tick constants (discs.h: dm_disc.tick): 8 floats of which this is the count, the one that is
// an int and left alone, and the seconds of the tick's duration (documented: discs.h).
constexpr int kTickConstants = 8;
constexpr int kTickIntSlot = 6;
constexpr uint32_t kTickSeconds = 12u;

// A moby (the game's 0x100-byte object: dm::k_moby_size). Offsets are from its address.

// Positions x, y, z, f32 (assumed).
constexpr uint32_t kMobyX = 0x10u;
constexpr uint32_t kMobyY = 0x14u;
constexpr uint32_t kMobyZ = 0x18u;
// Its state byte: negative as an int8 when the slot is free (documented: pool.h: 0xFE and 0xFF).
constexpr uint32_t kMobyState = 0x20u;
// Its group byte: 0xFF for none (assumed).
constexpr uint32_t kMobyGroup = 0x21u;
// Its animation speed, f32 (documented: this file's header).
constexpr uint32_t kMobyAnimSpeed = 0x58u;
// Its pvars' address, u32 (assumed).
constexpr uint32_t kMobyPvars = 0x78u;
// Its class, s16 (documented: classes.h: oclass, moby +0xA6).
constexpr uint32_t kMobyClass = 0xA6u;
// Its mission byte, then the save id's byte and word (assumed).
constexpr uint32_t kMobyMission = 0xB0u;
constexpr uint32_t kMobySaveByte = 0xB1u;
constexpr uint32_t kMobySaveWord = 0xB2u;
// The two bolt counts a death drops, s16 each (assumed).
constexpr uint32_t kMobyBoltsA = 0xB4u;
constexpr uint32_t kMobyBoltsB = 0xB6u;
// What a copy has in the save id's place: no save id (assumed).
constexpr uint8_t kNoSaveByte = 0xFE;
constexpr uint16_t kNoSaveWord = 0xFFFF;

// The fields a copy keeps of its own slot's (CreateMoby's): offset and length of each. Its state
// and group, list link, pvars, grid cells and pending hit, its slot's index and counter, its
// mission and save id, its parent (assumed).
const std::pair<uint32_t, uint32_t> k_own_fields[] =
    {{0x20, 2}, {0x28, 4}, {0x78, 4}, {0xA0, 6}, {0xA8, 8}, {0xB0, 4}, {0xB8, 8}};

// A pending hit (a record of the ring, discs.h: dm_disc.hit_ring): its damage, f32 (documented:
// discs.h).
constexpr uint32_t kHitDamage = 0x2Cu;
// The ring: 64 pending hits of 0x40 bytes (documented: discs.h).
constexpr int k_hits = 64;
constexpr uint32_t k_hit_size = 0x40;

// The items' table (discs.h: dm_disc.item_table): 37 ids, 0..0x24, of 0x18 bytes each, with the
// offset of each field it rewrites (documented: discs.h).
constexpr int k_items = 37;
constexpr uint32_t k_item_size = 0x18;
constexpr uint32_t kItemPriceA = 0x0u;
constexpr uint32_t kItemPriceB = 0x4u;
constexpr uint32_t kItemAmmoPriceA = 0x8u;
constexpr uint32_t kItemAmmoPriceB = 0xAu;
constexpr uint32_t kItemMaxAmmo = 0xEu;
// What Weapon_TakeAmmo is given for the item in hand (assumed).
constexpr int32_t kItemInHand = -1;
// The most a price may be (assumed), and what an ammo field, a u16, may be.
constexpr int kMaxPrice = 9999999;
constexpr int kMaxU16 = 65535;
// The most a bolt count, an s16, may be.
constexpr int kMaxS16 = 32767;

// The levels there are, 0..18 (assumed).
constexpr int k_levels = 19;

// The level's moby pool's globals (discs.h: dm_disc.pool), from its address (documented: pool.h).
constexpr uint32_t kPoolSpare = 4u;
constexpr uint32_t kPoolEnd = 8u;
constexpr uint32_t kPoolPvars = 0x10u;

// The level load's state (discs.h: dm_disc.load_state): the gameplay file's header is a u32 at +4
// (documented: discs.h).
constexpr uint32_t kLoadStateHeader = 4u;

// The gameplay file's header, from its address (assumed: read from the file's layout).
constexpr uint32_t kFileFixMoby = 0x50u;     // the fix-up list of fields that name another moby
constexpr uint32_t kFilePvarTable = 0x54u;   // the pvar table: each block's address and size
constexpr uint32_t kFilePvarFlag = 0x58u;    // non-zero when the file has pvars
constexpr uint32_t kFileFixInner = 0x5Cu;    // the fix-up list of pointers into a block itself
constexpr uint32_t kFilePlacements = 0x44u;  // the placements' section's offset
// A pvar table entry: the block's address, then its size (assumed).
constexpr uint32_t kPvarEntrySize = 8u;
constexpr uint32_t kPvarEntrySizeField = 4u;
// A fix-up list's entry: the block's index, then the offset in it; the list ends at a negative
// index (assumed).
constexpr uint32_t kFixEntrySize = 8u;
constexpr uint32_t kFixOffsetField = 4u;
// The most entries a fix-up list is read for, so a damaged file cannot loop for ever.
constexpr int kFixGuard = 0x40000;
// The placements' section: a count, then the records from +0x10; each record's size is a u32 with
// its low two bits clear, and has at least this many bytes; +8 its flags, +0x58 its pvar block's
// index (assumed).
constexpr uint32_t kSectionFirst = 0x10u;
constexpr uint32_t kRecordMinSize = 0x5Cu;
constexpr uint32_t kRecordFlags = 8u;
constexpr uint32_t kRecordPvar = 0x58u;
// The most placements believed in a file (assumed).
constexpr int kMaxPlacements = 0x8000;

// ---- Hook points ----

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

// The nanotech orb's update's hook point, by id.
constexpr const char* k_nanotech_ball = "rac1.moby.nanotech_ball.update";

// The most enemy-class update hook points there can be: one thunk each.
constexpr int k_max_updates = 96;

// ---- Tuning ----

// The salts that make the mod's random streams apart: a level's own (copies, drops, rounding), and
// the events' clock.
constexpr uint64_t kLevelRngSalt = 0x3000u;
constexpr uint64_t kEventClockSalt = 0x4000u;
// A seed is 32 bits (the code is 8 hex digits).
constexpr uint64_t kSeedMask = 0xFFFFFFFFu;
// The seconds in a minute, the least a gap between events is, and what it is when the setting is
// below that.
constexpr double kSecondsPerMinute = 60.0;
constexpr double kDefaultEventGap = 180.0;
// A frame's time (a tick's seconds) is believed under this; above it, or not positive, a 60th of a
// second is taken.
constexpr double kMaxFrameSeconds = 0.1;
constexpr double kFallbackFrameSeconds = 1.0 / 60.0;

// Where a copy of an enemy is tried: this many spots around it, at angles an eighth of a circle
// apart (and a little random), at 2.5 to 4 units; the line to a spot is tested a unit up.
constexpr int kSpotAttempts = 8;
constexpr double kSpotAngleStep = 0.7853981633974483;
constexpr double kSpotAngleJitter = 0.4;
constexpr double kSpotRadiusMin = 2.5;
constexpr double kSpotRadiusRange = 1.5;
constexpr float kSpotLineHeight = 1.0f;
// A spot's ground is probed from half a unit up, and must be within a unit of the original's.
constexpr float kSpotProbeHeight = 0.5f;
constexpr float kSpotFloorTolerance = 1.0f;
// An original must not stand more than half a unit below its own ground probe.
constexpr float kSpotBelowFloor = -0.5f;
// A spot must be a unit above the level's death plane.
constexpr float kSpotAboveDeath = 1.0f;
// The scratch room on the game's stack for the two 16-byte vectors the probes read (a position and
// a w of 0).
constexpr int kSpotFrameBytes = 48;
constexpr uint32_t kVecY = 4u;
constexpr uint32_t kVecZ = 8u;
constexpr uint32_t kVecW = 12u;
constexpr uint32_t kVecSize = 16u;

// ---- State ----

openrac::Mod* s_mod;
const dm_disc* s_disc;

// What the player chose (load), what plays on the level loaded, and with the event on. Written at
// load and at each level's load, read by every hook: all on the game's thread.
bool s_chaos;
dm::Values s_sliders;
uint64_t s_seed;
bool s_trace;
bool s_events_on;
dm::Values s_level;
dm::Values s_active;
dm::EventClock s_clock;
dm::Rng s_rng(1);

// Per level: the items' table as the level's program has it, read before the first change. Game
// thread only.
std::vector<uint8_t> s_items[k_levels];
bool s_items_read[k_levels];

// The carried-over fractions of damage taken, health pickups and ammo per shot (one per item).
// Game thread only.
dm::Accumulator s_damage;
dm::Accumulator s_pickups;
dm::Accumulator s_ammo[k_items];

// The hits whose damage was scaled: the value written, and the moby it was for, per ring entry.
// Game thread only.
float s_hit_value[k_hits];
uint32_t s_hit_owner[k_hits];

// The animation speed each timed moby had from the game, and what the mod wrote over it. Game
// thread only.
std::unordered_map<uint32_t, std::pair<float, float>> s_anim;

// How deep an enemy's update called from another's is, so that the time is scaled only once.
int s_timed_depth;

// The classes by id (classes.h).
std::unordered_map<int, const dm_class*> s_classes;

// The game's functions it calls.
uint32_t (*s_create_moby)(int32_t);
void (*s_delete_moby)(uint32_t);
void (*s_build_matrix)(uint32_t);
float (*s_ground)(uint32_t, int32_t, float);
int32_t (*s_line)(uint32_t, uint32_t, int32_t, uint32_t, uint32_t);

// The update hook points' originals, one thunk each (classes.h: k_dm_update_hooks).
void (*s_update_original[k_max_updates])(uint32_t);

// The nanotech orb's own update.
void (*s_ball_original)(uint32_t);

// ---- Types ----

// A placed moby's record in the gameplay file, found by its pvars.
struct Placement {
    int32_t flags = 0;
    int32_t pvar = -1;  // its pvar block's index
};

// What the gameplay file's two fix-up lists say of the pvar blocks.
struct Fixups {
    std::unordered_set<int> names_moby;                    // pvar blocks with a field naming a moby
    std::unordered_set<uint32_t> named;                    // mobys another names
    std::unordered_map<int, std::vector<uint32_t>> inner;  // pointers into a block
};

// An enemy to copy: where it is, its class, its pvars' size and block.
struct Wanted {
    gaddr moby;
    int16_t cls;
    uint32_t size;
    int block;
};

// ---- Game memory ----

// Reads a T from the game's memory.
template <class T>
T get(gaddr at) {
    return s_mod->memory().read<T>(at);
}

// Writes a T to the game's memory.
template <class T>
void put(gaddr at, T v) {
    s_mod->memory().write<T>(at, v);
}

// The base of the game's memory in this process: the game address a is mem() + a.
uint8_t* mem() {
    return s_mod->memory().base();
}

// An address in the executable, relocated.
gaddr exe(uint32_t address) {
    return s_mod->relocate(address);
}

// An address in level 0's program, relocated.
gaddr level0(uint32_t address) {
    return s_mod->relocate_level(0, address);
}

// The hero's address.
gaddr hero() {
    return exe(s_disc->hero);
}

// Whether a level is loaded and in play (not a menu or a cutscene).
bool in_play() {
    return s_mod->loaded_level() >= 0 && get<int32_t>(level0(s_disc->level_mode)) == kModePlay;
}

// The class table's entry for a moby, null for a class the mod does not know.
const dm_class* class_of(gaddr moby) {
    const auto it = s_classes.find(get<int16_t>(moby + kMobyClass));
    return it == s_classes.end() ? nullptr : it->second;
}

// ---- Text ----

// A value as the log shows it: two decimals.
std::string fmt(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}

// The values as one log line: each slider's name and value.
std::string describe(const dm::Values& v) {
    std::string out;

    // Each slider in turn, in the manifest's order.
    for (int k = 0; k < dm::KNOB_COUNT; ++k) {
        out += std::string(k ? ", " : "") + dm::k_knobs[k].name + " " + fmt(v[k]);
    }

    return out;
}

// What plays now: the level's values with the event on, if any.
void set_active() {
    s_active = dm::with_event(s_level, s_clock.current);
}

// ---- Damage, ammo, bolts, speed, health ----

// A hit on Ratchet takes one cell (the game clamps the amount to 1): the multiplier is how many
// hits' worth, carried over between hits.
template <class Original>
void take_damage(Original original, int32_t amount) {
    const double k = s_active[dm::DAMAGE_TAKEN];

    // No hit to scale, or the slider at retail: the game's own call.
    if (amount <= 0 || k == 1.0) {
        original(amount);
        return;
    }

    const int hits = s_damage.take(k, 1);

    // One call of the game's own hit for each whole hit the accumulator gives.
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

    // Nothing was hit, the slider is at retail, or it is Ratchet who reads it: his own damage is
    // the damage to Ratchet's slider's.
    if (hit == 0 || k == 1.0 || moby == get<uint32_t>(hero() + kHeroMoby)) {
        return hit;
    }

    const dm_class* c = class_of(moby);

    // A class the mod does not know, or one it leaves as it is (bosses and the like).
    if (c == nullptr || c->role == DM_EXCLUDED) {
        return hit;
    }

    const gaddr ring = level0(s_disc->hit_ring);

    // The hit is not one of the ring's records: not one to write to.
    if (hit < ring || (hit - ring) % k_hit_size != 0 || (hit - ring) / k_hit_size >= k_hits) {
        return hit;
    }

    const uint32_t i = (hit - ring) / k_hit_size;
    const float damage = get<float>(hit + kHitDamage);

    // The value there is what the mod wrote for this moby: scaled already.
    if (s_hit_owner[i] == moby && s_hit_value[i] == damage) {
        return hit;
    }

    const float scaled = static_cast<float>(damage * k);
    put<float>(hit + kHitDamage, scaled);
    s_hit_owner[i] = moby;
    s_hit_value[i] = scaled;
    return hit;
}

// A shot's ammo: the multiplier per shot, carried over per item; never a free shot from an empty
// gun, and the last rounds still fire when a shot would want more than is left.
template <class Original>
int32_t take_ammo(Original original, int32_t item, int32_t amount) {
    const double k = s_active[dm::AMMO_USE];

    // The slider is at retail, or the call takes no ammo: the game's own call.
    if (k == 1.0 || amount <= 0) {
        return original(item, amount);
    }

    const int32_t id = item == kItemInHand ? get<int32_t>(exe(s_disc->item_in_hand)) : item;

    // Not an item the game has ammo for.
    if (id < 0 || id >= k_items) {
        return original(item, amount);
    }

    const int32_t have = get<int32_t>(exe(s_disc->ammo) + kWord * static_cast<uint32_t>(id));
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

// The bolts a dying enemy or a broken crate drops: its two bolt counts scaled before the game
// reads them.
template <class Original>
void record_death(Original original, uint32_t moby, int32_t flags, int32_t index) {
    const double k = s_active[dm::BOLTS];

    // The slider is off retail: scale each count, to at most what an s16 holds.
    if (k != 1.0) {
        // The two bolt counts of the moby.
        for (const uint32_t at : {kMobyBoltsA, kMobyBoltsB}) {
            const int n = dm::scale_count(get<int16_t>(moby + at), k, s_rng);
            put<int16_t>(moby + at, static_cast<int16_t>(n > kMaxS16 ? kMaxS16 : n));
        }
    }

    original(moby, flags, index);
}

// Ratchet's ground speed eases toward his target speed: toward the target scaled, as fast.
template <class Original>
void approach_speed(Original original, float up, float down) {
    const double k = s_active[dm::PLAYER_SPEED];

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
    // the value the game saves or reads elsewhere.
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

    // The slider is at retail, this is an update inside another's (the time is scaled already), or
    // the moby is not an enemy, a roamer or a turret: the game's own call.
    if (k == 1.0 || s_timed_depth > 0 || c == nullptr
        || (c->role != DM_ENEMY && c->role != DM_ROAMER && c->role != DM_TURRET)) {
        original(moby);
        return;
    }

    const gaddr anim = moby + kMobyAnimSpeed;
    auto it = s_anim.find(moby);

    // Its animation speed is what the mod wrote last tick: give back the game's own, which the
    // update may read or advance from.
    if (it != s_anim.end() && get<float>(anim) == it->second.second) {
        put<float>(anim, it->second.first);
    }

    const gaddr tick = exe(s_disc->tick);
    float saved[kTickConstants];

    // Every one of the tick's constants, as it was before the update.
    for (int i = 0; i < kTickConstants; ++i) {
        saved[i] = get<float>(tick + kWord * static_cast<uint32_t>(i));
    }

    const double scale[kTickConstants] = {k, k * k, 1.0 / k, k, k * k, k * k * k, 1.0, k};

    // Every constant but the int one scaled by its own power of the multiplier.
    for (int i = 0; i < kTickConstants; ++i) {
        // All but the int slot, which is not a time and stays as it is.
        if (i != kTickIntSlot) {
            put<float>(
                tick + kWord * static_cast<uint32_t>(i), static_cast<float>(saved[i] * scale[i])
            );
        }
    }

    ++s_timed_depth;
    original(moby);
    --s_timed_depth;

    // The game's own constants back, for everything else that runs this tick.
    for (int i = 0; i < kTickConstants; ++i) {
        // All but the int slot, as when they were scaled.
        if (i != kTickIntSlot) {
            put<float>(tick + kWord * static_cast<uint32_t>(i), saved[i]);
        }
    }

    // The moby is still alive and still the same class (its update may have freed or reused the
    // slot): scale its animation speed, and remember what it was.
    if (static_cast<int8_t>(get<uint8_t>(moby + kMobyState)) >= 0
        && get<int16_t>(moby + kMobyClass) == c->id) {
        const float base = get<float>(anim);
        const float scaled = static_cast<float>(base * k);
        put<float>(anim, scaled);
        s_anim[moby] = {base, scaled};
    } else {
        s_anim.erase(moby);
    }
}

// One update hook point's replacement: the originals are kept by index in s_update_original, and
// the hook gets a plain function for each index.
template <int I>
void update_thunk(uint32_t moby) {
    timed_update(s_update_original[I], moby);
}

// The thunks for indices 0 to N-1, as an array.
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
void ball_update(uint32_t moby) {
    const double k = s_active[dm::HEALTH_PICKUPS];
    const gaddr hp = hero() + kHeroHealth;
    const int32_t before = get<int32_t>(hp);
    s_ball_original(moby);
    const int32_t gained = get<int32_t>(hp) - before;

    // The slider is at retail, or the orb gave nothing this tick.
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
    // Not in play, or Ratchet is not on foot: left alone.
    if (!in_play() || get<uint8_t>(hero() + kHeroNotOnFoot) == 1) {
        return;
    }

    const gaddr upgrades = exe(s_disc->upgrades);
    const int retail = get<uint8_t>(upgrades + kUpgradeUltra)     ? kCellsUltra
                       : get<uint8_t>(upgrades + kUpgradePremium) ? kCellsPremium
                                                                  : kCellsBase;
    const int target = dm::max_health(retail, s_active[dm::MAX_HEALTH]);
    const gaddr max = exe(s_disc->max_health);
    const int32_t now = get<int32_t>(max);

    // Already the count asked for.
    if (now == target) {
        return;
    }

    const gaddr hp = hero() + kHeroHealth;
    const int32_t health = get<int32_t>(hp);

    // Health was full, or is over the new count: it follows the count.
    if (health == now || health > target) {
        put<int32_t>(hp, target);
    }

    put<int32_t>(max, target);

    // With the trace on, one line for each change.
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

    // The first load of this level: keep its own table to scale from.
    if (!s_items_read[level]) {
        s_items[level].assign(at, at + k_items * k_item_size);
        s_items_read[level] = true;
    }

    const uint8_t* own = s_items[level].data();

    // Each item id's record.
    for (int id = 0; id < k_items; ++id) {
        const uint32_t r = static_cast<uint32_t>(id) * k_item_size;

        // The two weapon prices.
        for (const uint32_t f : {kItemPriceA, kItemPriceB}) {
            int32_t v;
            std::memcpy(&v, own + r + f, 4);
            v = dm::scale_amount(v, s_level[dm::WEAPON_COST], kMaxPrice);
            put<int32_t>(table + r + f, v);
        }

        // The two ammo prices.
        for (const uint32_t f : {kItemAmmoPriceA, kItemAmmoPriceB}) {
            uint16_t v;
            std::memcpy(&v, own + r + f, 2);
            put<uint16_t>(
                table + r + f,
                static_cast<uint16_t>(dm::scale_amount(v, s_level[dm::AMMO_COST], kMaxU16))
            );
        }

        uint16_t v;
        std::memcpy(&v, own + r + kItemMaxAmmo, 2);
        put<uint16_t>(
            table + r + kItemMaxAmmo,
            static_cast<uint16_t>(dm::scale_amount(v, s_level[dm::MAX_AMMO], kMaxU16))
        );
    }
}

// Where a copy of `orig` may stand: on the same floor (a ground probe under the spot within a
// unit of the original's), in the open (no wall on the line from the original), above the death
// plane. At the same height over the floor as the original. Returns false when no spot of the
// tries will do; `out` is the spot otherwise.
bool find_spot(gaddr orig, int copy, float out[3]) {
    openrac::StackFrame frame(*s_mod, kSpotFrameBytes);
    const gaddr a = frame.address(), b = a + kVecSize;
    const float x = get<float>(orig + kMobyX), y = get<float>(orig + kMobyY),
                z = get<float>(orig + kMobyZ);
    put<float>(a, x);
    put<float>(a + kVecY, y);
    put<float>(a + kVecZ, z);
    put<float>(a + kVecW, 0.0f);
    const float floor0 = s_ground(a, 0, 1.0f);

    // The original has no ground under it, or stands below it: no copy is placed by it.
    if (!(floor0 > 0.0f) || z - floor0 < kSpotBelowFloor) {
        return false;
    }

    const float death = get<float>(level0(s_disc->death_plane));
    const float over = z - floor0;

    // Spots around the original in turn, until one is clear.
    for (int attempt = 0; attempt < kSpotAttempts; ++attempt) {
        const double angle =
            (copy * 3 + attempt) * kSpotAngleStep + s_rng.unit() * kSpotAngleJitter;
        const double r = kSpotRadiusMin + s_rng.unit() * kSpotRadiusRange;
        const float cx = static_cast<float>(x + r * std::cos(angle));
        const float cy = static_cast<float>(y + r * std::sin(angle));
        // The line from the original to the spot, a unit up.
        put<float>(a, x);
        put<float>(a + kVecY, y);
        put<float>(a + kVecZ, floor0 + kSpotLineHeight);
        put<float>(a + kVecW, 0.0f);
        put<float>(b, cx);
        put<float>(b + kVecY, cy);
        put<float>(b + kVecZ, floor0 + kSpotLineHeight);
        put<float>(b + kVecW, 0.0f);

        // A wall on the way: try the next angle.
        if (s_line(a, b, 0, orig, 0) != 0) {
            continue;
        }

        put<float>(b + kVecZ, floor0 + kSpotProbeHeight);
        const float floor = s_ground(b, 0, 1.0f);

        // No ground there, ground of another height, or too near the death plane.
        if (!(floor > 0.0f) || std::fabs(floor - floor0) > kSpotFloorTolerance
            || (death != 0.0f && floor < death + kSpotAboveDeath)) {
            continue;
        }

        out[0] = cx;
        out[1] = cy;
        out[2] = floor + over;
        return true;
    }

    return false;
}

// Makes one copy of the placed moby `orig` of class `cls` at `spot`: a new moby from the game,
// given the original's fields but the ones its slot keeps, and its pvars (`pvar_size` bytes, with
// the offsets of the pointers into them in `inner`) in its own slot or in the room the pool's tail
// gave (*room, with *room_left bytes left, both advanced). Returns the copy, or 0 when the game
// had no slot or the room is short (nothing is left behind then).
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

    // The pool has no slot: no copy.
    if (m == 0) {
        return 0;
    }

    uint8_t own[dm::k_moby_size];
    std::memcpy(own, mem() + m, dm::k_moby_size);
    std::memcpy(mem() + m, mem() + orig, dm::k_moby_size);

    // The fields the copy keeps of its own slot's.
    for (const auto& f : k_own_fields) {
        std::memcpy(mem() + m + f.first, own + f.first, f.second);
    }

    put<float>(m + kMobyX, spot[0]);
    put<float>(m + kMobyY, spot[1]);
    put<float>(m + kMobyZ, spot[2]);
    // Not a placed moby: no save id (its death marks nothing killed, SetDeathBits), and no bolts.
    // Its mission the original's, for the classes whose update reads it.
    put<uint8_t>(m + kMobyMission, get<uint8_t>(orig + kMobyMission));
    put<uint8_t>(m + kMobySaveByte, kNoSaveByte);
    put<uint16_t>(m + kMobySaveWord, kNoSaveWord);
    put<int16_t>(m + kMobyBoltsA, 0);
    put<int16_t>(m + kMobyBoltsB, 0);
    // Its pvars: the original's, in its own slot's 0x80 bytes or in the room the pool's tail gave.
    const gaddr from = get<uint32_t>(orig + kMobyPvars);
    gaddr to = get<uint32_t>(m + kMobyPvars);

    // The pvars are larger than a spawned moby's own: they go in the room the tail gave.
    if (pvar_size > dm::k_spare_pvars) {
        const uint32_t need =
            static_cast<uint32_t>(dm::pvar_slots(static_cast<int>(pvar_size))) * dm::k_spare_pvars;

        // No room, or too little: give the slot back.
        if (*room == 0 || *room_left < need) {
            s_delete_moby(m);
            return 0;
        }

        to = *room;
        *room += need;
        *room_left -= need;
        put<uint32_t>(m + kMobyPvars, to);
    }

    // There are pvars to copy.
    if (from != 0 && to != 0 && pvar_size != 0) {
        std::memcpy(mem() + to, mem() + from, pvar_size);

        // Pointers into its own pvars (the load made them absolute) point into the copy's.
        for (const uint32_t off : inner) {
            // The pointer lies inside the pvars.
            if (off + 4u <= pvar_size) {
                const uint32_t v = get<uint32_t>(to + off);

                // It points into the original's pvars: move it to the same place in the copy's.
                if (v >= from && v < from + pvar_size) {
                    put<uint32_t>(to + off, v - from + to);
                }
            }
        }
    }

    s_build_matrix(m);
    return m;
}

// The gameplay file, still in memory: each placement's flags and pvar block, by the block's
// address. `section` is the placements' section, `table` the pvar table (each block's address and
// size). Reads nothing when the file has no pvars or its count is not believable.
std::unordered_map<uint32_t, Placement> read_placements(gaddr hdr, gaddr section, gaddr table) {
    const int placed = get<int32_t>(section);
    const bool has_pvars = get<uint32_t>(hdr + kFilePvarFlag) != 0;
    std::unordered_map<uint32_t, Placement> by_pvars;

    // No pvars in the file, or a count that is not one: nothing is eligible.
    if (!has_pvars || placed <= 0 || placed >= kMaxPlacements) {
        return by_pvars;
    }

    gaddr p = section + kSectionFirst;

    // Each placement's record, one after the other.
    for (int i = 0; i < placed; ++i) {
        const uint32_t size = get<uint32_t>(p) & ~3u;

        // A record too short to be one: the file is not as expected, stop here.
        if (size < kRecordMinSize) {
            break;
        }

        Placement pl;
        pl.flags = get<int32_t>(p + kRecordFlags);
        pl.pvar = get<int32_t>(p + kRecordPvar);

        // It has a pvar block: find it by the block's address.
        if (pl.pvar >= 0) {
            by_pvars[get<uint32_t>(table + kPvarEntrySize * static_cast<uint32_t>(pl.pvar))] = pl;
        }

        p += size;
    }

    return by_pvars;
}

// The gameplay file's two fix-up lists (+0x50: fields that name another moby; +0x5C: pointers into
// the block itself), as which blocks name a moby, which mobys are named, and each block's inner
// pointers. `pool` gives the mobys' addresses.
Fixups read_fixups(gaddr hdr, gaddr table, const dm::Pool& pool) {
    Fixups out;

    // The list of fields that name a moby first, then the list of pointers into a block.
    for (const uint32_t list : {kFileFixMoby, kFileFixInner}) {
        // The file has no such list.
        if (get<uint32_t>(hdr + list) == 0) {
            continue;
        }

        gaddr q = hdr + get<uint32_t>(hdr + list);

        // Each entry of the list, up to the end mark (a negative block index) or the guard.
        for (int guard = 0; guard < kFixGuard && get<int32_t>(q) >= 0;
             ++guard, q += kFixEntrySize) {
            const int blk = get<int32_t>(q);
            const uint32_t off = get<uint32_t>(q + kFixOffsetField);
            const uint32_t x = get<uint32_t>(table + kPvarEntrySize * static_cast<uint32_t>(blk));

            // The block lies before the file: not one of its own.
            if (x < hdr) {
                continue;
            }

            // The list of fields that name a moby; the other is of pointers into the block.
            if (list == kFileFixMoby) {
                // A field left at -1 names no moby: a copy of it names none either.
                const int32_t target = get<int32_t>(x + off);

                // It names one: that block must not be copied, and that moby is not eligible.
                if (target >= 0) {
                    out.names_moby.insert(blk);
                    out.named.insert(pool.base + static_cast<uint32_t>(target) * dm::k_moby_size);
                }
            } else {
                out.inner[blk].push_back(off);
            }
        }
    }

    return out;
}

// The enemy count slider, at a level's load: with k below 1 some enemies are deleted, with k above
// 1 copies are made beside the enemies that are free of the level's scripts, within the pool's
// budget. Leaves the level as it is when the pool or the file is not as expected.
void adjust_enemies(int level) {
    const double k = s_level[dm::ENEMY_COUNT];

    // The slider is at retail.
    if (k == 1.0) {
        return;
    }

    const dm::PoolGlobals at =
        {level0(s_disc->pool),
         level0(s_disc->pool + kPoolSpare),
         level0(s_disc->pool + kPoolEnd),
         level0(s_disc->pool + kPoolPvars)};
    const dm::Pool pool = dm::Pool::read(mem(), at);
    const gaddr hdr = get<uint32_t>(level0(s_disc->load_state) + kLoadStateHeader);

    // The pool or the gameplay file is not as laid out: a wrong guess here would corrupt the level.
    if (!pool.sane() || hdr == 0) {
        s_mod->warn("enemy count: the level's moby pool is not as expected; left as it is");
        return;
    }

    // The gameplay file, still in memory: each placement's flags and pvar block, the pvar table
    // (+0x54: each block's address and size) and its two fix-up lists.
    const gaddr section = hdr + get<uint32_t>(hdr + kFilePlacements);
    const gaddr table = hdr + get<uint32_t>(hdr + kFilePvarTable);
    const std::unordered_map<uint32_t, Placement> by_pvars = read_placements(hdr, section, table);
    const Fixups fix = read_fixups(hdr, table, pool);

    std::vector<Wanted> wanted;
    std::vector<dm::CloneAsk> asks;
    int dropped = 0, eligible = 0;

    // Each placed moby of the pool, up to the first spawned one.
    for (gaddr m = pool.base; m < pool.spare; m += dm::k_moby_size) {
        // A free slot.
        if (static_cast<int8_t>(get<uint8_t>(m + kMobyState)) < 0) {
            continue;
        }

        const dm_class* c = class_of(m);

        // Not an enemy or a roamer: turrets, bosses and the rest are never copied or left out.
        if (c == nullptr || (c->role != DM_ENEMY && c->role != DM_ROAMER)) {
            continue;
        }

        const auto pl = by_pvars.find(get<uint32_t>(m + kMobyPvars));

        // Only a moby free of the level's scripts: no group (the waves, the "defeat them all"
        // doors), no other moby naming it, and found in the file. Its record's flags 1 and 2 (in
        // play before its mission is done, after it, or both) do not count: nearly every enemy has
        // them, and the load has already left out those of the other part of the story.
        if (pl == by_pvars.end() || get<uint8_t>(m + kMobyGroup) != 0xFF || fix.named.count(m)) {
            continue;
        }

        ++eligible;

        // Fewer enemies: each eligible one is left out by chance.
        if (k < 1.0) {
            // This one is left out.
            if (s_rng.unit() < 1.0 - k) {
                s_delete_moby(m);
                ++dropped;
            }

            continue;
        }

        // A roamer is not copied, and neither is an enemy whose block has a field naming another
        // moby (a copy would name the same one).
        if (c->role != DM_ENEMY || fix.names_moby.count(pl->second.pvar)) {
            continue;
        }

        const uint32_t size = get<uint32_t>(
            table + kPvarEntrySize * static_cast<uint32_t>(pl->second.pvar) + kPvarEntrySizeField
        );
        wanted.push_back({m, c->id, size, pl->second.pvar});
        asks.push_back(
            {dm::scale_count(1, k - 1.0, s_rng), dm::pvar_slots(static_cast<int>(size))}
        );
    }

    // Fewer enemies is done: nothing is copied.
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

    // Pvars larger than a slot's are asked for, and the pool's tail cannot give the room.
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

    // Each enemy that is to be copied.
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        // Each copy the plan gives it.
        for (int c = 0; c < plan.copies[i]; ++c) {
            float spot[3];

            // No clear spot beside it for this copy: the next one.
            if (!find_spot(wanted[i].moby, c, spot)) {
                ++no_spot;
                continue;
            }

            const auto in = fix.inner.find(wanted[i].block);
            static const std::vector<uint32_t> none;

            // The game or the pvar room is out of space: no more copies of this enemy.
            if (copy_moby(
                    wanted[i].moby,
                    wanted[i].cls,
                    spot,
                    wanted[i].size,
                    in == fix.inner.end() ? none : in->second,
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

// Level_LoadGameplay: once the game has laid the level's mobys out, this level's values are
// rolled (with Chaos) or taken, the items' table rewritten and the enemy count applied. Returns
// the game's own cursor.
template <class Original>
uint32_t load_gameplay(Original original, int32_t full) {
    const uint32_t cursor = original(full);
    const int level = s_mod->loaded_level();

    // Not a level the mod knows (a menu, say): nothing to apply.
    if (level < 0 || level >= k_levels) {
        return cursor;
    }

    s_level = dm::effective(s_chaos, s_sliders, s_seed, level);
    set_active();
    s_anim.clear();
    std::memset(s_hit_owner, 0, sizeof s_hit_owner);
    s_rng = dm::Rng(dm::mix(s_seed, kLevelRngSalt + static_cast<uint64_t>(level)));

    // Chaos rolls new values each level, and the trace asks for them: say what plays.
    if (s_chaos || s_trace) {
        s_mod->info("level " + std::to_string(level) + ": " + describe(s_level));
    }

    rewrite_items(level);
    adjust_enemies(level);
    return cursor;
}

// ---- Load ----

// Takes a hook point, a function the host calls in place of the game's with the original to call.
template <class HookPoint, class F>
void take(openrac::Mod& mod, F&& f) {
    mod.hook<HookPoint>(std::forward<F>(f));
}

// Finds this disc in discs.h, refusing another game or a disc it does not know.
void find_disc(openrac::Mod& mod) {
    s_disc = nullptr;

    // Not Ratchet & Clank.
    if (mod.game() != "rac1") {
        throw openrac::Error("difficulty is for Ratchet & Clank (rac1)");
    }

    // Each disc the mod knows: the one whose version this is.
    for (const auto& disc : k_dm_discs) {
        // This is the disc's version.
        if (mod.version() == disc.version) {
            s_disc = &disc;
        }
    }

    // A disc the mod has no addresses for.
    if (s_disc == nullptr) {
        throw openrac::Error(
            "difficulty knows the PAL and NTSC-U discs, not " + std::string(mod.version())
        );
    }
}

// Reads the settings: Chaos, the sliders (clamped), the seed (a new one when blank), the trace
// and the events' clock.
void read_settings(openrac::Mod& mod) {
    s_chaos = mod.setting<bool>("chaos");

    // Each slider's setting.
    for (int k = 0; k < dm::KNOB_COUNT; ++k) {
        s_sliders[k] = dm::clamp_knob(k, mod.setting<double>(dm::k_knobs[k].key));
    }

    const std::string seed_text = mod.setting<std::string>("seed");

    // Blank: a new seed from the machine and the time, shown in the log to play it again.
    if (seed_text.empty()) {
        std::random_device rd;
        s_seed =
            (static_cast<uint64_t>(rd()) ^ static_cast<uint64_t>(std::time(nullptr))) & kSeedMask;
    } else {
        s_seed = dm::seed_from_text(seed_text);
    }

    s_trace = mod.setting<bool>("trace");
    s_events_on = mod.setting<bool>("chaos_events");
    s_clock = dm::EventClock{};
    s_clock.seed = dm::mix(s_seed, kEventClockSalt);
    s_clock.every = kSecondsPerMinute * static_cast<double>(mod.setting<int>("chaos_minutes"));

    // Less than a minute (or not a number): the default gap.
    if (!(s_clock.every >= kSecondsPerMinute)) {
        s_clock.every = kDefaultEventGap;
    }
}

// Puts what the hooks carry between calls back as at a start.
void reset_state() {
    s_level = dm::effective(s_chaos, s_sliders, s_seed, -1);
    set_active();
    s_damage = {};
    s_pickups = {};

    // Each item's carried-over fraction.
    for (auto& a : s_ammo) {
        a = {};
    }

    // Each level's table, to be read again from the level's own values.
    for (bool& r : s_items_read) {
        r = false;
    }

    s_timed_depth = 0;
}

// Hooks the enemies' updates (their speed) and the nanotech orb's (health pickups). One a disc
// lacks only leaves that class at its own speed: it is added to `missing`, not refused. Returns
// how many enemy updates were hooked.
int hook_updates(openrac::Mod& mod, std::string& missing) {
    const openrac_api& api = mod.api();
    int timed = 0;
    const int count = static_cast<int>(sizeof k_dm_update_hooks / sizeof k_dm_update_hooks[0]);

    // Each enemy class's update hook point.
    for (int i = 0; i < count; ++i) {
        void* original = nullptr;
        const int r = api.hook(
            mod.handle(),
            k_dm_update_hooks[i],
            "void(u32)",
            reinterpret_cast<void*>(k_update_thunks[static_cast<std::size_t>(i)]),
            &original
        );

        // The disc has this hook point: keep its original; else say so.
        if (r == OPENRAC_HOOK_OK) {
            s_update_original[i] = reinterpret_cast<void (*)(uint32_t)>(original);
            ++timed;
        } else {
            missing += std::string(missing.empty() ? "" : ", ") + k_dm_update_hooks[i];
        }
    }

    void* ball = nullptr;

    // The nanotech orb's update, hooked the same way.
    if (api.hook(
            mod.handle(), k_nanotech_ball, "void(u32)", reinterpret_cast<void*>(&ball_update), &ball
        )
        == OPENRAC_HOOK_OK) {
        s_ball_original = reinterpret_cast<void (*)(uint32_t)>(ball);
    } else {
        missing += std::string(missing.empty() ? "" : ", ") + k_nanotech_ball;
    }

    return timed;
}

// Each frame: the events' clock (in play, with the events on), then max health.
void on_frame() {
    // Chaos events are on and the game is in play: the clock runs on play time only.
    if (s_events_on && in_play()) {
        const double dt = get<float>(exe(s_disc->tick) + kTickSeconds);
        const int change =
            s_clock.advance(dt > 0.0 && dt < kMaxFrameSeconds ? dt : kFallbackFrameSeconds);

        // An event started or ended.
        if (change != 0) {
            set_active();
            s_mod->info(
                change > 0 ? std::string("chaos: ") + dm::k_events[s_clock.current].name
                           : std::string("chaos: back to normal")
            );
        }
    }

    keep_max_health();
}

// The mod's entry: checks the game and the disc, reads the settings, takes the functions and the
// hook points it needs (one this OpenRAC lacks refuses the mod, naming it), and logs how it plays.
void load(openrac::Mod& mod) {
    s_mod = &mod;
    find_disc(mod);

    s_classes.clear();

    // Each class the mod knows, by its id.
    for (const auto& c : k_dm_classes) {
        s_classes[c.id] = &c;
    }

    read_settings(mod);
    reset_state();

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
    std::string missing;
    const int timed = hook_updates(mod, missing);

    // Some hook point is not on this disc.
    if (!missing.empty()) {
        mod.warn("no hook point on this disc for " + missing + ": those stay as the game has them");
    }

    mod.on_frame(on_frame);

    std::string mode = s_chaos ? "chaos" : dm::sliders_moved(s_sliders) ? "sliders" : "retail";

    // Chaos with sliders held off 1.0: say that they hold.
    if (s_chaos && dm::sliders_moved(s_sliders)) {
        mode += ", with sliders moved off 1.0 holding";
    }

    mod.info(
        mode + " on the " + std::string(mod.version()) + " disc; seed " + dm::seed_code(s_seed)
        + " (put it in Seed to play the same rolls again); " + std::to_string(timed)
        + " enemy updates timed"
        + (s_events_on ? "; chaos events every " + fmt(s_clock.every / 60.0) + " min" : "")
    );

    // Without Chaos the values are the sliders': one line says them.
    if (!s_chaos) {
        mod.info(describe(s_level));
    }
}

}  // namespace

OPENRAC_MOD_CPP("difficulty", "1.2.2", load)
