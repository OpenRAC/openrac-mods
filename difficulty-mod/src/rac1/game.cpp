// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Ratchet & Clank: the difficulty mod's rac1 part (README.md; src/mod.cpp loads it for rac1). A
// slider for each thing that makes the game hard or easy, presets that fill them (the manifest's),
// and Chaos, which rolls them for each level from a seed (src/common). Each value is changed where
// the game takes it, through a hook point (OpenRAC's rac1 hook points: docs/port/MODS.md, section
// 4), or once a level at its source (the items' table of prices and ammo), never by patching memory
// each frame; with every slider at 1.0 each hook calls the game's own function as it was called,
// so the game is retail.
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
//                       the spawned mobys' pool's room (pool.h, rules.h: kPoolReserve)
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
//
// In a party (multiplayer's README, Mods in a party) the host's rules count: this game publishes
// what it plays by (rules.h: PartyRules), and the same mod in a member's game takes them
// (on_party_rules): the sliders, Chaos and the resolved seed, so each level rolls the same, and
// which event is on, the host deciding the events. A member makes no copies and leaves no enemy
// out: the host's world brings its copies (SPAWN) and its removals (dead placed mobys). With no
// party nothing of this runs, and every slider at 1.0 is still the retail game.

#include "game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
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
#include "session.h"

namespace difficulty::rac1 {
namespace {

using openrac::gaddr;

// ---- Constants ----

// Game addresses are per disc and live in discs.h (Disc). What is below is the layout of what
// they point at, the same on both discs.

// The size of a game word: the stride of the ammo array and of the tick constants.
constexpr std::uint32_t kWord = 4u;

// The hero (Hero, discs.h: Disc.hero). Offsets are from its address.

// The hero's target ground speed, f32 (documented: discs.h).
constexpr std::uint32_t kHeroTargetSpeed = 0x190u;
// The hero's moby, a u32 address, compared with the moby a hit is for to tell Ratchet's own hits
// from an enemy's (assumed).
constexpr std::uint32_t kHeroMoby = 0x2080u;
// A byte that is 1 when Ratchet is not on foot, where max health is left alone (assumed).
constexpr std::uint32_t kHeroNotOnFoot = 0x20A4u;
// The hero's health in cells, s32 (documented: discs.h).
constexpr std::uint32_t kHeroHealth = 0x22A8u;

// The upgrade bytes (discs.h: Disc.upgrades), from its address.

// Non-zero when the Premium Nanotech upgrade is bought (documented: discs.h).
constexpr std::uint32_t kUpgradePremium = 4u;
// Non-zero when the Ultra Nanotech upgrade is bought (documented: discs.h).
constexpr std::uint32_t kUpgradeUltra = 5u;
// The cells of health retail gives: none, Premium, Ultra (documented: the manifest's max_health
// text).
constexpr int kCellsBase = 4;
constexpr int kCellsPremium = 5;
constexpr int kCellsUltra = 8;

// The game mode that is play (documented: discs.h: Disc.level_mode).
constexpr std::int32_t kModePlay = 0;

// The tick constants (discs.h: Disc.tick): 8 floats of which this is the count, the one that is
// an int and left alone, and the seconds of the tick's duration (documented: discs.h).
constexpr int kTickConstants = 8;
constexpr int kTickIntSlot = 6;
constexpr std::uint32_t kTickSeconds = 12u;

// A moby (the game's 0x100-byte object: difficulty::kMobySize). Offsets are from its address.

// Positions x, y, z, f32 (assumed).
constexpr std::uint32_t kMobyX = 0x10u;
constexpr std::uint32_t kMobyY = 0x14u;
constexpr std::uint32_t kMobyZ = 0x18u;
// Its state byte: negative as an int8 when the slot is free (documented: pool.h: 0xFE and 0xFF).
constexpr std::uint32_t kMobyState = 0x20u;
// Its group byte: 0xFF for none (assumed).
constexpr std::uint32_t kMobyGroup = 0x21u;
constexpr std::uint8_t kNoGroup = 0xFF;
// Its animation speed, f32 (documented: this file's header).
constexpr std::uint32_t kMobyAnimSpeed = 0x58u;
// Its pvars' address, u32 (assumed).
constexpr std::uint32_t kMobyPvars = 0x78u;
// Its class, s16 (documented: classes.h: oclass, moby +0xA6).
constexpr std::uint32_t kMobyClass = 0xA6u;
// Its mission byte, then the save id's byte and word (assumed).
constexpr std::uint32_t kMobyMission = 0xB0u;
constexpr std::uint32_t kMobySaveByte = 0xB1u;
constexpr std::uint32_t kMobySaveWord = 0xB2u;
// The two bolt counts a death drops, s16 each (assumed).
constexpr std::uint32_t kMobyBoltsA = 0xB4u;
constexpr std::uint32_t kMobyBoltsB = 0xB6u;
// What a copy has in the save id's place: no save id (assumed).
constexpr std::uint8_t kNoSaveByte = 0xFE;
constexpr std::uint16_t kNoSaveWord = 0xFFFF;

// The fields a copy keeps of its own slot's (CreateMoby's): offset and length of each. Its state
// and group, list link, pvars, grid cells and pending hit, its slot's index and counter, its
// mission and save id, its parent (assumed).
const std::pair<std::uint32_t, std::uint32_t> kOwnFields[] =
    {{0x20, 2}, {0x28, 4}, {0x78, 4}, {0xA0, 6}, {0xA8, 8}, {0xB0, 4}, {0xB8, 8}};

// A pending hit (a record of the ring, discs.h: Disc.hit_ring): its damage, f32 (documented:
// discs.h).
constexpr std::uint32_t kHitDamage = 0x2Cu;
// The ring: 64 pending hits of 0x40 bytes (documented: discs.h).
constexpr int kHits = 64;
constexpr std::uint32_t kHitSize = 0x40;

// The items' table (discs.h: Disc.item_table): 37 ids, 0..0x24, of 0x18 bytes each, with the
// offset of each field it rewrites (documented: discs.h).
constexpr int kItems = 37;
constexpr std::uint32_t kItemSize = 0x18;
constexpr std::uint32_t kItemPriceA = 0x0u;
constexpr std::uint32_t kItemPriceB = 0x4u;
constexpr std::uint32_t kItemAmmoPriceA = 0x8u;
constexpr std::uint32_t kItemAmmoPriceB = 0xAu;
constexpr std::uint32_t kItemMaxAmmo = 0xEu;
// What Weapon_TakeAmmo is given for the item in hand (assumed).
constexpr std::int32_t kItemInHand = -1;
// The most a price may be (assumed), and what an ammo field, a u16, may be.
constexpr int kMaxPrice = 9999999;
constexpr int kMaxU16 = 65535;
// The most a bolt count, an s16, may be.
constexpr int kMaxS16 = 32767;

// The levels there are, 0..18 (assumed).
constexpr int kLevels = 19;

// The level's moby pool's globals (discs.h: Disc.pool), from its address (documented: pool.h).
constexpr std::uint32_t kPoolSpare = 4u;
constexpr std::uint32_t kPoolEnd = 8u;
constexpr std::uint32_t kPoolPvars = 0x10u;

// The level load's state (discs.h: Disc.load_state): the gameplay file's header is a u32 at +4
// (documented: discs.h).
constexpr std::uint32_t kLoadStateHeader = 4u;

// The gameplay file's header, from its address (assumed: read from the file's layout).
constexpr std::uint32_t kFileFixMoby = 0x50u;    // the fix-up list of fields that name another moby
constexpr std::uint32_t kFilePvarTable = 0x54u;  // the pvar table: each block's address and size
constexpr std::uint32_t kFilePvarFlag = 0x58u;   // non-zero when the file has pvars
constexpr std::uint32_t kFileFixInner = 0x5Cu;   // the fix-up list of pointers into a block itself
constexpr std::uint32_t kFilePlacements = 0x44u;  // the placements' section's offset
// A pvar table entry: the block's address, then its size (assumed).
constexpr std::uint32_t kPvarEntrySize = 8u;
constexpr std::uint32_t kPvarEntrySizeField = 4u;
// A fix-up list's entry: the block's index, then the offset in it; the list ends at a negative
// index (assumed).
constexpr std::uint32_t kFixEntrySize = 8u;
constexpr std::uint32_t kFixOffsetField = 4u;
// The most entries a fix-up list is read for, so a damaged file cannot loop for ever.
constexpr int kFixGuard = 0x40000;
// The placements' section: a count, then the records from +0x10; each record's size is a u32 with
// its low two bits clear, and has at least this many bytes; +8 its flags, +0x58 its pvar block's
// index (assumed).
constexpr std::uint32_t kSectionFirst = 0x10u;
constexpr std::uint32_t kRecordMinSize = 0x5Cu;
constexpr std::uint32_t kRecordFlags = 8u;
constexpr std::uint32_t kRecordPvar = 0x58u;
// The most placements believed in a file (assumed).
constexpr int kMaxPlacements = 0x8000;

// ---- Hook points ----

// The hook points it takes (OpenRAC's sdk/include/openrac/games/rac1.hpp has them too from the
// SDK after 1.1.2; written here so the mod builds against the SDK it pins).
struct TakeDamage {
    static constexpr const char* id = "rac1.player.take_damage";
    static constexpr const char* name = "Hero_TakeDamage";
    static constexpr const char* signature = "void(i32)";
    using fn = void (*)(std::int32_t);
};

struct GetHit {
    static constexpr const char* id = "rac1.combat.get_hit";
    static constexpr const char* name = "Moby_GetHitMessage";
    static constexpr const char* signature = "u32(u32,i32,i32)";
    using fn = std::uint32_t (*)(std::uint32_t, std::int32_t, std::int32_t);
};

struct TakeAmmo {
    static constexpr const char* id = "rac1.weapon.take_ammo";
    static constexpr const char* name = "Weapon_TakeAmmo";
    static constexpr const char* signature = "i32(i32,i32)";
    using fn = std::int32_t (*)(std::int32_t, std::int32_t);
};

struct RecordDeath {
    static constexpr const char* id = "rac1.moby.record_death";
    static constexpr const char* name = "Moby_RecordDeath";
    static constexpr const char* signature = "void(u32,i32,i32)";
    using fn = void (*)(std::uint32_t, std::int32_t, std::int32_t);
};

struct LoadGameplay {
    static constexpr const char* id = "rac1.level.load_gameplay";
    static constexpr const char* name = "Level_LoadGameplay";
    static constexpr const char* signature = "u32(i32)";
    using fn = std::uint32_t (*)(std::int32_t);
};

struct ApproachSpeed {
    static constexpr const char* id = "rac1.player.approach_speed";
    static constexpr const char* name = "Hero_ApproachSpeed";
    static constexpr const char* signature = "void(f32,f32)";
    using fn = void (*)(float, float);
};

// The nanotech orb's update's hook point, by id.
constexpr const char* kNanotechBall = "rac1.moby.nanotech_ball.update";

// The most enemy-class update hook points there can be: one thunk each.
constexpr int kMaxUpdates = 96;

// ---- Tuning ----

// The salt that makes a level's own random stream (copies, drops, rounding) apart from the others.
constexpr std::uint64_t kLevelRngSalt = 0x3000u;
// A frame's time (a tick's seconds) is believed under this; above it, or not positive, a 60th of a
// second is taken.
constexpr double kMaxFrameSeconds = 0.1;
constexpr double kFallbackFrameSeconds = 1.0 / 60.0;

// Where a copy of an enemy is tried: this many spots around it, at angles an eighth of a circle
// apart (and a little random), at 2.5 to 4 units; the line to a spot is tested a unit up.
constexpr int kSpotAttempts = 8;
// Each further copy starts this many attempts round the circle, so copies do not share a spot.
constexpr int kSpotCopyStride = 3;
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
constexpr std::uint32_t kVecY = 4u;
constexpr std::uint32_t kVecZ = 8u;
constexpr std::uint32_t kVecW = 12u;
constexpr std::uint32_t kVecSize = 16u;
// A copy whose own start puts it within this of its original (a class that begins at its path's
// first point, the birdbot's state 0) stands on it: it is taken out. A copy this far from its spot
// moves on its own and is no longer watched.
constexpr float kStackedDistance = 0.25f;
constexpr float kAwakeDistance = 1.0f;
// Copies of one enemy that come within kApartGap units (or a big one's radius: apart_gap) of each
// other or of their original (their AI takes them to the same points: a path's, a cover's) are
// moved apart by up to kApartStep units a tick, if no wall is in the way. Stacked, they hid each
// other and caught each other's shots.
constexpr float kApartGap = 2.0f;
// A big enemy keeps further: its bounding sphere's radius (+0xC, in 1024ths of a unit), to at most
// kApartGapMost units.
constexpr std::uint32_t kMobyRadius = 0x0Cu;
constexpr float kRadiusUnit = 1.0f / 1024.0f;
constexpr float kApartGapMost = 4.0f;
constexpr float kApartStep = 0.1f;
// Nearer than this is on top of each other; the ground at the new place may differ by this much.
constexpr float kApartSame = 1e-3f;
constexpr float kApartFloorTolerance = 0.5f;
// Over its ground by more than this, an enemy flies (a leap is lower).
constexpr float kApartFlyHeight = 3.0f;
constexpr float kRadiansPerDegree = 0.017453292f;
constexpr std::uint32_t kDegreesPerCircle = 360u;
// A moby's address over this is the angle a copy exactly on another is pushed along.
constexpr std::uint32_t kSlotShift = 8u;

// ---- State ----

// A copy made at this level's load, watched until its first moves: the copy, its original, its
// class and the spot it was put at.
struct Watched {
    gaddr copy;
    gaddr orig;
    std::int16_t cls;
    float spot[3];
};

// Everything the mod keeps while it runs in rac1: what it loaded, what it carries between calls,
// and the game's functions it calls. Made by load and read by every hook, all on the game's
// thread.
struct State {
    // The port's mod object; not owned, valid for the life of the program.
    openrac::Mod* mod = nullptr;
    // The disc's addresses; not owned, a row of kDiscs.
    const Disc* disc = nullptr;

    // What the player chose, what plays on the level loaded and with the event on, and a party's
    // host's rules: the session every game shares (session.h). Written at load and at each
    // level's load.
    difficulty::Session session;
    // The level's own random stream (copies, drops, rounding).
    difficulty::Rng rng{1};

    // Per level: the items' table as the level's program has it, read before the first change.
    std::vector<std::uint8_t> items[kLevels];
    bool items_read[kLevels] = {};

    // The carried-over fractions of damage taken, health pickups and ammo per shot (one per item).
    difficulty::Accumulator damage;
    difficulty::Accumulator pickups;
    difficulty::Accumulator ammo[kItems];

    // The hits whose damage was scaled: the value written, and the moby it was for, per ring entry.
    float hit_value[kHits] = {};
    std::uint32_t hit_owner[kHits] = {};

    // The animation speed each timed moby had from the game, and what the mod wrote over it.
    std::unordered_map<std::uint32_t, std::pair<float, float>> anim;

    // The copies made at this level's load, watched until their first moves.
    std::vector<Watched> watch;

    // This level's copies (each copy's class), and every enemy of a copied class (the level's own
    // and the copies), which a copy keeps apart from.
    std::unordered_map<std::uint32_t, std::int16_t> copy_class;
    std::unordered_map<std::int16_t, std::vector<std::uint32_t>> class_members;
    // Every moby in class_members, so that one the game spawns later (a spawner's) is added once.
    std::unordered_set<std::uint32_t> member_set;

    // How deep an enemy's update called from another's is, so that the time is scaled only once.
    int timed_depth = 0;

    // The classes by id (classes.h); the values point into kClasses.
    std::unordered_map<int, const ClassInfo*> classes;

    // The game's functions it calls, found by name at load.
    std::uint32_t (*create_moby)(std::int32_t) = nullptr;
    void (*delete_moby)(std::uint32_t) = nullptr;
    void (*build_matrix)(std::uint32_t) = nullptr;
    float (*ground)(std::uint32_t, std::int32_t, float) = nullptr;
    std::int32_t (*line)(std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t, std::uint32_t) =
        nullptr;

    // The update hook points' originals, one thunk each (classes.h: kUpdateHooks).
    void (*update_original[kMaxUpdates])(std::uint32_t) = {};

    // The nanotech orb's own update.
    void (*ball_original)(std::uint32_t) = nullptr;
};

// The mod's state, made at load. The hooks and the update thunks are plain functions the port
// calls with no user pointer, so they reach the state through this one pointer.
std::unique_ptr<State> g_state;

// Returns the state; only valid between load's start and the end of the program.
State& state() {
    return *g_state;
}

// ---- Types ----

// A placed moby's record in the gameplay file, found by its pvars.
struct Placement {
    std::int32_t flags = 0;
    std::int32_t pvar = -1;  // its pvar block's index
};

// What the gameplay file's two fix-up lists say of the pvar blocks.
struct Fixups {
    std::unordered_set<int> names_moby;       // pvar blocks with a field naming a moby
    std::unordered_set<std::uint32_t> named;  // mobys another names
    std::unordered_map<int, std::vector<std::uint32_t>> inner;  // pointers into a block
};

// An enemy to copy: where it is, its class, its pvars' size and block.
struct Wanted {
    gaddr moby;
    std::int16_t cls;
    std::uint32_t size;
    int block;
};

// ---- Game memory ----

// Reads a T from the game's memory.
template <class T>
T get(gaddr at) {
    return state().mod->memory().read<T>(at);
}

// Writes a T to the game's memory.
template <class T>
void put(gaddr at, T v) {
    state().mod->memory().write<T>(at, v);
}

// The base of the game's memory in this process: the game address a is mem() + a.
std::uint8_t* mem() {
    return state().mod->memory().base();
}

// An address in the executable, relocated.
gaddr exe(std::uint32_t address) {
    return state().mod->relocate(address);
}

// An address in level 0's program, relocated.
gaddr level0(std::uint32_t address) {
    return state().mod->relocate_level(0, address);
}

// The hero's address.
gaddr hero() {
    return exe(state().disc->hero);
}

// Whether a level is loaded and in play (not a menu or a cutscene).
bool in_play() {
    return state().mod->loaded_level() >= 0
           && get<std::int32_t>(level0(state().disc->level_mode)) == kModePlay;
}

// The class table's entry for a moby, null for a class the mod does not know.
const ClassInfo* class_of(gaddr moby) {
    const auto it = state().classes.find(get<std::int16_t>(moby + kMobyClass));
    return it == state().classes.end() ? nullptr : it->second;
}

// ---- Damage, ammo, bolts, speed, health ----

// A hit on Ratchet takes one cell (the game clamps the amount to 1): the multiplier is how many
// hits' worth, carried over between hits.
template <class Original>
void take_damage(Original original, std::int32_t amount) {
    const double k = state().session.active[difficulty::kDamageTaken];

    // No hit to scale, or the slider at retail: the game's own call.
    if (amount <= 0 || k == 1.0) {
        original(amount);
        return;
    }

    const int hits = state().damage.take(k, 1);

    // One call of the game's own hit for each whole hit the accumulator gives.
    for (int i = 0; i < hits; ++i) {
        original(1);
    }
}

// The hit an enemy reads: its damage scaled once (the same hit read again keeps the value
// written, which is how it is told from a new one).
template <class Original>
std::uint32_t get_hit(Original original, std::uint32_t moby, std::int32_t mask, std::int32_t keep) {
    const std::uint32_t hit = original(moby, mask, keep);
    const double k = state().session.active[difficulty::kDamageDealt];

    // Nothing was hit, the slider is at retail, or it is Ratchet who reads it: his own damage is
    // the damage to Ratchet's slider's.
    if (hit == 0 || k == 1.0 || moby == get<std::uint32_t>(hero() + kHeroMoby)) {
        return hit;
    }

    const ClassInfo* c = class_of(moby);

    // A class the mod does not know, or one it leaves as it is (bosses and the like).
    if (c == nullptr || c->role == ClassRole::Excluded) {
        return hit;
    }

    const gaddr ring = level0(state().disc->hit_ring);

    // The hit is not one of the ring's records: not one to write to.
    if (hit < ring || (hit - ring) % kHitSize != 0 || (hit - ring) / kHitSize >= kHits) {
        return hit;
    }

    const std::uint32_t i = (hit - ring) / kHitSize;
    const float damage = get<float>(hit + kHitDamage);

    // The value there is what the mod wrote for this moby: scaled already.
    if (state().hit_owner[i] == moby && state().hit_value[i] == damage) {
        return hit;
    }

    const float scaled = static_cast<float>(damage * k);
    put<float>(hit + kHitDamage, scaled);
    state().hit_owner[i] = moby;
    state().hit_value[i] = scaled;
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
        item == kItemInHand ? get<std::int32_t>(exe(state().disc->item_in_hand)) : item;

    // Not an item the game has ammo for.
    if (id < 0 || id >= kItems) {
        return original(item, amount);
    }

    const std::int32_t have =
        get<std::int32_t>(exe(state().disc->ammo) + kWord * static_cast<std::uint32_t>(id));
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

// The bolts a dying enemy or a broken crate drops: its two bolt counts scaled for the call, and
// the game's own counts back after it, so a moby recorded twice is not scaled twice.
template <class Original>
void record_death(Original original, std::uint32_t moby, std::int32_t flags, std::int32_t index) {
    const double k = state().session.active[difficulty::kBolts];

    // The slider is at retail: the game's own call.
    if (k == 1.0) {
        original(moby, flags, index);
        return;
    }

    const std::array<std::uint32_t, 2> fields = {kMobyBoltsA, kMobyBoltsB};
    std::array<std::int16_t, 2> own{};
    std::array<std::int16_t, 2> scaled{};

    // Each count scaled, to at most what an s16 holds.
    for (std::size_t i = 0; i < fields.size(); ++i) {
        own[i] = get<std::int16_t>(moby + fields[i]);
        const int n = difficulty::scale_count(own[i], k, state().rng);
        scaled[i] = static_cast<std::int16_t>(n > kMaxS16 ? kMaxS16 : n);
        put<std::int16_t>(moby + fields[i], scaled[i]);
    }

    original(moby, flags, index);

    // Each count the game left as the mod wrote it: its own back.
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (get<std::int16_t>(moby + fields[i]) == scaled[i]) {
            put<std::int16_t>(moby + fields[i], own[i]);
        }
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
    // the value the game saves or reads elsewhere.
    if (get<float>(target) == scaled) {
        put<float>(target, t);
    }
}

// Runs the game's `original(moby)` with the tick's time constants scaled by `k`, each by its own
// power of it, then puts the game's own constants back for everything else that runs this tick.
void run_with_scaled_tick(void (*original)(std::uint32_t), std::uint32_t moby, double k) {
    const gaddr tick = exe(state().disc->tick);
    float saved[kTickConstants];

    // Every one of the tick's constants, as it was before the update.
    for (int i = 0; i < kTickConstants; ++i) {
        saved[i] = get<float>(tick + kWord * static_cast<std::uint32_t>(i));
    }

    const double scale[kTickConstants] = {k, k * k, 1.0 / k, k, k * k, k * k * k, 1.0, k};

    // Every constant but the int one scaled by its own power of the multiplier.
    for (int i = 0; i < kTickConstants; ++i) {
        // All but the int slot, which is not a time and stays as it is.
        if (i != kTickIntSlot) {
            put<float>(
                tick + kWord * static_cast<std::uint32_t>(i),
                static_cast<float>(saved[i] * scale[i])
            );
        }
    }

    ++state().timed_depth;
    original(moby);
    --state().timed_depth;

    // The game's own constants back, for everything else that runs this tick.
    for (int i = 0; i < kTickConstants; ++i) {
        // All but the int slot, as when they were scaled.
        if (i != kTickIntSlot) {
            put<float>(tick + kWord * static_cast<std::uint32_t>(i), saved[i]);
        }
    }
}

// An enemy's update with its time scaled: the tick's constants (seconds, and the ratio to NTSC's
// tick, with their squares and cubes as the game keeps them) for the call, and its animation speed
// after it, which the next tick's animation advance reads.
void timed_update(void (*original)(std::uint32_t), std::uint32_t moby) {
    const double k = state().session.active[difficulty::kEnemySpeed];
    const ClassInfo* c = class_of(moby);

    // The slider is at retail, this is an update inside another's (the time is scaled already), or
    // the moby is not an enemy, a roamer or a turret: the game's own call.
    if (k == 1.0 || state().timed_depth > 0 || c == nullptr
        || (c->role != ClassRole::Enemy && c->role != ClassRole::Roamer
            && c->role != ClassRole::Turret)) {
        original(moby);
        return;
    }

    const gaddr anim = moby + kMobyAnimSpeed;
    auto it = state().anim.find(moby);

    // Its animation speed is what the mod wrote last tick: give back the game's own, which the
    // update may read or advance from.
    if (it != state().anim.end() && get<float>(anim) == it->second.second) {
        put<float>(anim, it->second.first);
    }

    run_with_scaled_tick(original, moby, k);

    // The moby is still alive and still the same class (its update may have freed or reused the
    // slot): scale its animation speed, and remember what it was.
    if (static_cast<std::int8_t>(get<std::uint8_t>(moby + kMobyState)) >= 0
        && get<std::int16_t>(moby + kMobyClass) == c->id) {
        const float base = get<float>(anim);
        const float scaled = static_cast<float>(base * k);
        put<float>(anim, scaled);
        state().anim[moby] = {base, scaled};
    } else {
        state().anim.erase(moby);
    }
}

// A moby that is alive and of class `cls`.
bool alive_as(std::uint32_t moby, std::int16_t cls) {
    return static_cast<std::int8_t>(get<std::uint8_t>(moby + kMobyState)) >= 0
           && get<std::int16_t>(moby + kMobyClass) == cls;
}

// A copy the mod made, still in its slot: alive, of its class, with the copy's empty save id (a
// moby the game puts in a freed slot gets its own).
bool still_copy(std::uint32_t moby, std::int16_t cls) {
    return alive_as(moby, cls) && get<std::uint8_t>(moby + kMobySaveByte) == kNoSaveByte
           && get<std::uint16_t>(moby + kMobySaveWord) == kNoSaveWord;
}

// The ground under (x, y) from half a unit over z, read with the frame's vector at `at`.
float ground_at(gaddr at, float x, float y, float z) {
    put<float>(at, x);
    put<float>(at + kVecY, y);
    put<float>(at + kVecZ, z + kSpotProbeHeight);
    put<float>(at + kVecW, 0.0f);
    return state().ground(at, 0, 1.0f);
}

// How far apart two enemies of a class keep: kApartGap, or the larger one's radius, to at most
// kApartGapMost.
float apart_gap(std::uint32_t a, std::uint32_t b) {
    const float r =
        std::max(get<float>(a + kMobyRadius), get<float>(b + kMobyRadius)) * kRadiusUnit;
    // A radius that is not one (not a number, not positive): the plain gap.
    return r > kApartGap ? std::min(r, kApartGapMost) : kApartGap;
}

// The push that takes a copy at (x, y, z) away from the enemies of class `cls` within their gap
// of it, more the nearer; `out` is the push in x and y.
void apart_push(std::uint32_t moby, std::int16_t cls, const float at[3], float out[2]) {
    out[0] = 0.0f;
    out[1] = 0.0f;

    // Each other enemy of the class within the gap: away from it, more the nearer.
    for (const std::uint32_t other : state().class_members[cls]) {
        // Itself, or one that is gone.
        if (other == moby || !alive_as(other, cls)) {
            continue;
        }

        const float dx = at[0] - get<float>(other + kMobyX);
        const float dy = at[1] - get<float>(other + kMobyY);
        const float d2 = dx * dx + dy * dy;
        const float gap = apart_gap(moby, other);

        // Far enough apart, or on another floor.
        if (d2 >= gap * gap || std::fabs(at[2] - get<float>(other + kMobyZ)) >= gap) {
            continue;
        }

        const float d = std::sqrt(d2);

        // Exactly on top: away along a direction of its own (its slot's, so it holds every tick).
        if (d < kApartSame) {
            const float angle =
                static_cast<float>((moby >> kSlotShift) % kDegreesPerCircle) * kRadiansPerDegree;
            out[0] += std::cos(angle) * gap;
            out[1] += std::sin(angle) * gap;
        } else {
            out[0] += dx / d * (gap - d);
            out[1] += dy / d * (gap - d);
        }
    }
}

// After a copy's update: moved a little away from the enemies of its class (originals and copies)
// that stand within kApartGap of it, when no wall is in the way and the ground there is the
// ground it stands over. Originals are never moved, so the game's own enemies stay as they were.
void keep_apart(std::uint32_t moby) {
    const auto of = state().copy_class.find(moby);

    // Not a copy, or a party member's game, where the host's world places the enemies.
    if (of == state().copy_class.end() || state().session.follow) {
        return;
    }

    const std::int16_t cls = of->second;

    // Its slot was freed or given to another moby: no longer a copy.
    if (!still_copy(moby, cls)) {
        state().copy_class.erase(of);
        return;
    }

    const float x = get<float>(moby + kMobyX), y = get<float>(moby + kMobyY),
                z = get<float>(moby + kMobyZ);
    const float at[3] = {x, y, z};
    float push[2];
    apart_push(moby, cls, at, push);
    const float length = std::sqrt(push[0] * push[0] + push[1] * push[1]);

    // Nothing near.
    if (!(length > kApartSame)) {
        return;
    }

    const float step = std::min(length, kApartStep) / length;
    const float nx = x + push[0] * step, ny = y + push[1] * step;
    openrac::StackFrame frame(*state().mod, kSpotFrameBytes);
    const gaddr a = frame.address(), b = a + kVecSize;
    const float here = ground_at(a, x, y, z), there = ground_at(b, nx, ny, z);

    // One that flies (well over its ground) may cross a ledge: only ground under it is needed.
    const bool flies = here > 0.0f && z - here > kApartFlyHeight;

    // No ground under the new place, or a walker's ground of another height (a ledge, a step): it
    // stays.
    if (!(there > 0.0f) || !(here > 0.0f)
        || (!flies && std::fabs(there - here) > kApartFloorTolerance)) {
        return;
    }

    put<float>(a, x);
    put<float>(a + kVecY, y);
    put<float>(a + kVecZ, z + kSpotLineHeight);
    put<float>(b, nx);
    put<float>(b + kVecY, ny);
    put<float>(b + kVecZ, z + kSpotLineHeight);

    // A wall on the way: it stays.
    if (state().line(a, b, 0, moby, 0) != 0) {
        return;
    }

    put<float>(moby + kMobyX, nx);
    put<float>(moby + kMobyY, ny);
}

// An enemy of a copied class that the game spawned after the load (a spawner's, a wave's) is one
// the copies keep apart from too, from its first update on.
void note_member(std::uint32_t moby) {
    // Nothing is copied on this level, or it is known already.
    if (state().class_members.empty() || state().member_set.count(moby) != 0) {
        return;
    }

    const auto members = state().class_members.find(get<std::int16_t>(moby + kMobyClass));

    // A class with copies: one more of it.
    if (members != state().class_members.end()) {
        members->second.push_back(moby);
        state().member_set.insert(moby);
    }
}

// One update hook point's replacement: the originals are kept by index in
// state().update_original, and the hook gets a plain function for each index. After the update,
// a copy is kept apart from the enemies of its class.
template <int I>
void update_thunk(std::uint32_t moby) {
    timed_update(state().update_original[I], moby);
    note_member(moby);
    keep_apart(moby);
}

// The thunks for indices 0 to N-1, as an array.
template <int... I>
constexpr std::array<void (*)(std::uint32_t), sizeof...(I)> thunks(
    std::integer_sequence<int, I...>
) {
    return {&update_thunk<I>...};
}

constexpr auto kUpdateThunks = thunks(std::make_integer_sequence<int, kMaxUpdates>{});
static_assert(
    sizeof kUpdateHooks / sizeof kUpdateHooks[0] <= kMaxUpdates,
    "more update hook points than thunks"
);

// The nanotech orb: the cells it gives this tick, scaled.
void ball_update(std::uint32_t moby) {
    const double k = state().session.active[difficulty::kHealthPickups];
    const gaddr hp = hero() + kHeroHealth;
    const std::int32_t before = get<std::int32_t>(hp);
    state().ball_original(moby);
    const std::int32_t gained = get<std::int32_t>(hp) - before;

    // The slider is at retail, or the orb gave nothing this tick.
    if (k == 1.0 || gained <= 0) {
        return;
    }

    const std::int32_t max = get<std::int32_t>(exe(state().disc->max_health));
    const std::int32_t now = before + state().pickups.take(k, gained);
    // Never above max health, and never below what he had before the orb.
    put<std::int32_t>(hp, now > max ? (max > before ? max : before) : now);
}

// Max health: the cells the upgrades bought give (4, Premium 5, Ultra 8), scaled to a count the
// HUD has places for. Each tick in play, on foot.
void keep_max_health() {
    // Not in play, or Ratchet is not on foot: left alone.
    if (!in_play() || get<std::uint8_t>(hero() + kHeroNotOnFoot) == 1) {
        return;
    }

    const gaddr upgrades = exe(state().disc->upgrades);
    int retail = kCellsBase;

    // Premium Nanotech is bought.
    if (get<std::uint8_t>(upgrades + kUpgradePremium) != 0) {
        retail = kCellsPremium;
    }

    // Ultra Nanotech is bought: it is the better of the two.
    if (get<std::uint8_t>(upgrades + kUpgradeUltra) != 0) {
        retail = kCellsUltra;
    }

    const int target =
        difficulty::max_health(retail, state().session.active[difficulty::kMaxHealth]);
    const gaddr max = exe(state().disc->max_health);
    const std::int32_t now = get<std::int32_t>(max);

    // Already the count asked for.
    if (now == target) {
        return;
    }

    const gaddr hp = hero() + kHeroHealth;
    const std::int32_t health = get<std::int32_t>(hp);

    // Health was full, or is over the new count: it follows the count.
    if (health == now || health > target) {
        put<std::int32_t>(hp, target);
    }

    put<std::int32_t>(max, target);

    // With the trace on, one line for each change.
    if (state().session.trace) {
        state().mod->info("max health " + std::to_string(now) + " -> " + std::to_string(target));
    }
}

// ---- A level's load: its values, the items' table, the enemies ----

// The items' table from the level's own values: weapon prices (+0, +4), ammo prices (+8, +0xA)
// and max ammo (+0xE), written whatever they are now (so a value is never scaled twice).
void rewrite_items(int level) {
    const gaddr table = level0(state().disc->item_table);
    std::uint8_t* at = mem() + table;

    // The first load of this level: keep its own table to scale from.
    if (!state().items_read[level]) {
        state().items[level].assign(at, at + kItems * kItemSize);
        state().items_read[level] = true;
    }

    const std::uint8_t* own = state().items[level].data();

    // Each item id's record.
    for (int id = 0; id < kItems; ++id) {
        const std::uint32_t r = static_cast<std::uint32_t>(id) * kItemSize;

        // The two weapon prices.
        for (const std::uint32_t f : {kItemPriceA, kItemPriceB}) {
            std::int32_t v;
            std::memcpy(&v, own + r + f, sizeof v);
            v = difficulty::scale_amount(
                v, state().session.level[difficulty::kWeaponCost], kMaxPrice
            );
            put<std::int32_t>(table + r + f, v);
        }

        // The two ammo prices.
        for (const std::uint32_t f : {kItemAmmoPriceA, kItemAmmoPriceB}) {
            std::uint16_t v;
            std::memcpy(&v, own + r + f, sizeof v);
            put<std::uint16_t>(
                table + r + f,
                static_cast<std::uint16_t>(difficulty::scale_amount(
                    v, state().session.level[difficulty::kAmmoCost], kMaxU16
                ))
            );
        }

        std::uint16_t v;
        std::memcpy(&v, own + r + kItemMaxAmmo, sizeof v);
        put<std::uint16_t>(
            table + r + kItemMaxAmmo,
            static_cast<std::uint16_t>(
                difficulty::scale_amount(v, state().session.level[difficulty::kMaxAmmo], kMaxU16)
            )
        );
    }
}

// A spot at (x, y, z) for a copy of `orig` that is nearer than their gap to an enemy of its class
// (one of the level's, or a copy already made).
bool crowded(gaddr orig, float x, float y, float z) {
    // Each enemy of its class.
    for (const std::uint32_t made : state().class_members[get<std::int16_t>(orig + kMobyClass)]) {
        const float dx = x - get<float>(made + kMobyX), dy = y - get<float>(made + kMobyY);
        const float gap = apart_gap(orig, made);

        // Within the gap, on the same floor.
        if (dx * dx + dy * dy < gap * gap && std::fabs(z - get<float>(made + kMobyZ)) < gap) {
            return true;
        }
    }

    return false;
}

// Where a copy of `orig` may stand: on the same floor (a ground probe under the spot within a
// unit of the original's), in the open (no wall on the line from the original), above the death
// plane. At the same height over the floor as the original. Returns false when no spot of the
// tries will do; `out` is the spot otherwise.
bool find_spot(gaddr orig, int copy, float out[3]) {
    openrac::StackFrame frame(*state().mod, kSpotFrameBytes);
    const gaddr a = frame.address(), b = a + kVecSize;
    const float x = get<float>(orig + kMobyX), y = get<float>(orig + kMobyY),
                z = get<float>(orig + kMobyZ);
    put<float>(a, x);
    put<float>(a + kVecY, y);
    put<float>(a + kVecZ, z);
    put<float>(a + kVecW, 0.0f);
    const float floor0 = state().ground(a, 0, 1.0f);

    // The original has no ground under it, or stands below it: no copy is placed by it.
    if (!(floor0 > 0.0f) || z - floor0 < kSpotBelowFloor) {
        return false;
    }

    const float death = get<float>(level0(state().disc->death_plane));
    const float over = z - floor0;

    // Spots around the original in turn, until one is clear.
    for (int attempt = 0; attempt < kSpotAttempts; ++attempt) {
        const double angle = (copy * kSpotCopyStride + attempt) * kSpotAngleStep
                             + state().rng.unit() * kSpotAngleJitter;
        const double r = kSpotRadiusMin + state().rng.unit() * kSpotRadiusRange;
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
        if (state().line(a, b, 0, orig, 0) != 0) {
            continue;
        }

        put<float>(b + kVecZ, floor0 + kSpotProbeHeight);
        const float floor = state().ground(b, 0, 1.0f);

        // No ground there, ground of another height, or too near the death plane.
        if (!(floor > 0.0f) || std::fabs(floor - floor0) > kSpotFloorTolerance
            || (death != 0.0f && floor < death + kSpotAboveDeath)) {
            continue;
        }

        // Too near an enemy of its class (one of the level's, or a copy already made): the next
        // angle.
        if (crowded(orig, cx, cy, floor + over)) {
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
    std::int16_t cls,
    const float spot[3],
    std::uint32_t pvar_size,
    const std::vector<std::uint32_t>& inner,
    gaddr* room,
    std::uint32_t* room_left
) {
    const gaddr m = state().create_moby(cls);

    // The pool has no slot: no copy.
    if (m == 0) {
        return 0;
    }

    std::uint8_t own[difficulty::kMobySize];
    std::memcpy(own, mem() + m, difficulty::kMobySize);
    std::memcpy(mem() + m, mem() + orig, difficulty::kMobySize);

    // The fields the copy keeps of its own slot's.
    for (const auto& f : kOwnFields) {
        std::memcpy(mem() + m + f.first, own + f.first, f.second);
    }

    put<float>(m + kMobyX, spot[0]);
    put<float>(m + kMobyY, spot[1]);
    put<float>(m + kMobyZ, spot[2]);
    // Not a placed moby: no save id (its death marks nothing killed, SetDeathBits), and no bolts.
    // Its mission the original's, for the classes whose update reads it.
    put<std::uint8_t>(m + kMobyMission, get<std::uint8_t>(orig + kMobyMission));
    put<std::uint8_t>(m + kMobySaveByte, kNoSaveByte);
    put<std::uint16_t>(m + kMobySaveWord, kNoSaveWord);
    put<std::int16_t>(m + kMobyBoltsA, 0);
    put<std::int16_t>(m + kMobyBoltsB, 0);
    // Its pvars: the original's, in its own slot's 0x80 bytes or in the room the pool's tail gave.
    const gaddr from = get<std::uint32_t>(orig + kMobyPvars);
    gaddr to = get<std::uint32_t>(m + kMobyPvars);

    // The pvars are larger than a spawned moby's own: they go in the room the tail gave.
    if (pvar_size > difficulty::kSparePvars) {
        const std::uint32_t need =
            static_cast<std::uint32_t>(difficulty::pvar_slots(static_cast<int>(pvar_size)))
            * difficulty::kSparePvars;

        // No room, or too little: give the slot back.
        if (*room == 0 || *room_left < need) {
            state().delete_moby(m);
            return 0;
        }

        to = *room;
        *room += need;
        *room_left -= need;
        put<std::uint32_t>(m + kMobyPvars, to);
    }

    // There are pvars to copy.
    if (from != 0 && to != 0 && pvar_size != 0) {
        std::memcpy(mem() + to, mem() + from, pvar_size);

        // Pointers into its own pvars (the load made them absolute) point into the copy's.
        for (const std::uint32_t off : inner) {
            // The pointer lies inside the pvars.
            if (off + 4u <= pvar_size) {
                const std::uint32_t v = get<std::uint32_t>(to + off);

                // It points into the original's pvars: move it to the same place in the copy's.
                if (v >= from && v < from + pvar_size) {
                    put<std::uint32_t>(to + off, v - from + to);
                }
            }
        }
    }

    state().build_matrix(m);
    return m;
}

// The gameplay file, still in memory: each placement's flags and pvar block, by the block's
// address. `section` is the placements' section, `table` the pvar table (each block's address and
// size). Reads nothing when the file has no pvars or its count is not believable.
std::unordered_map<std::uint32_t, Placement> read_placements(
    gaddr hdr, gaddr section, gaddr table
) {
    const int placed = get<std::int32_t>(section);
    const bool has_pvars = get<std::uint32_t>(hdr + kFilePvarFlag) != 0;
    std::unordered_map<std::uint32_t, Placement> by_pvars;

    // No pvars in the file, or a count that is not one: nothing is eligible.
    if (!has_pvars || placed <= 0 || placed >= kMaxPlacements) {
        return by_pvars;
    }

    gaddr p = section + kSectionFirst;

    // Each placement's record, one after the other.
    for (int i = 0; i < placed; ++i) {
        const std::uint32_t size = get<std::uint32_t>(p) & ~3u;

        // A record too short to be one: the file is not as expected, stop here.
        if (size < kRecordMinSize) {
            break;
        }

        Placement pl;
        pl.flags = get<std::int32_t>(p + kRecordFlags);
        pl.pvar = get<std::int32_t>(p + kRecordPvar);

        // It has a pvar block: find it by the block's address.
        if (pl.pvar >= 0) {
            by_pvars[get<std::uint32_t>(
                table + kPvarEntrySize * static_cast<std::uint32_t>(pl.pvar)
            )] = pl;
        }

        p += size;
    }

    return by_pvars;
}

// The gameplay file's two fix-up lists (+0x50: fields that name another moby; +0x5C: pointers into
// the block itself), as which blocks name a moby, which mobys are named, and each block's inner
// pointers. `pool` gives the mobys' addresses.
Fixups read_fixups(gaddr hdr, gaddr table, const difficulty::Pool& pool) {
    Fixups out;

    // The list of fields that name a moby first, then the list of pointers into a block.
    for (const std::uint32_t list : {kFileFixMoby, kFileFixInner}) {
        // The file has no such list.
        if (get<std::uint32_t>(hdr + list) == 0) {
            continue;
        }

        gaddr q = hdr + get<std::uint32_t>(hdr + list);

        // Each entry of the list, up to the end mark (a negative block index) or the guard.
        for (int guard = 0; guard < kFixGuard && get<std::int32_t>(q) >= 0;
             ++guard, q += kFixEntrySize) {
            const int blk = get<std::int32_t>(q);
            const std::uint32_t off = get<std::uint32_t>(q + kFixOffsetField);
            const std::uint32_t x =
                get<std::uint32_t>(table + kPvarEntrySize * static_cast<std::uint32_t>(blk));

            // The block lies before the file: not one of its own.
            if (x < hdr) {
                continue;
            }

            // The list of fields that name a moby; the other is of pointers into the block.
            if (list == kFileFixMoby) {
                // A field left at -1 names no moby: a copy of it names none either.
                const std::int32_t target = get<std::int32_t>(x + off);

                // It names one: that block must not be copied, and that moby is not eligible.
                if (target >= 0) {
                    out.names_moby.insert(blk);
                    out.named.insert(
                        pool.base + static_cast<std::uint32_t>(target) * difficulty::kMobySize
                    );
                }
            } else {
                out.inner[blk].push_back(off);
            }
        }
    }

    return out;
}

// What the enemy count slider found among the level's placed mobys: the enemies to copy and what
// each asks for, and how many were left out and how many were free of the level's scripts.
struct Candidates {
    // The enemies to copy, with the copies each asks for in `asks` (the same order).
    std::vector<Wanted> wanted;
    std::vector<difficulty::CloneAsk> asks;
    // Enemies deleted (the slider is below 1) and enemies free of the level's scripts.
    int dropped = 0;
    int eligible = 0;
};

// Goes through the placed mobys of `pool` once: with `k` below 1 each eligible enemy is left out by
// chance, with `k` above 1 the eligible enemies are listed with the copies they ask for. `table` is
// the gameplay file's pvar table.
Candidates pick_candidates(
    double k,
    const difficulty::Pool& pool,
    const std::unordered_map<std::uint32_t, Placement>& by_pvars,
    const Fixups& fix,
    gaddr table
) {
    Candidates found;
    std::vector<Wanted>& wanted = found.wanted;
    std::vector<difficulty::CloneAsk>& asks = found.asks;
    int& dropped = found.dropped;
    int& eligible = found.eligible;

    // Each placed moby of the pool, up to the first spawned one.
    for (gaddr m = pool.base; m < pool.spare; m += difficulty::kMobySize) {
        // A free slot.
        if (static_cast<std::int8_t>(get<std::uint8_t>(m + kMobyState)) < 0) {
            continue;
        }

        const ClassInfo* c = class_of(m);

        // Not an enemy or a roamer: turrets, bosses and the rest are never copied or left out.
        if (c == nullptr || (c->role != ClassRole::Enemy && c->role != ClassRole::Roamer)) {
            continue;
        }

        const auto pl = by_pvars.find(get<std::uint32_t>(m + kMobyPvars));

        // Only a moby free of the level's scripts: no group (the waves, the "defeat them all"
        // doors), no other moby naming it, and found in the file. Its record's flags 1 and 2 (in
        // play before its mission is done, after it, or both) do not count: nearly every enemy has
        // them, and the load has already left out those of the other part of the story.
        if (pl == by_pvars.end() || get<std::uint8_t>(m + kMobyGroup) != kNoGroup
            || fix.named.count(m)) {
            continue;
        }

        ++eligible;

        // Fewer enemies: each eligible one is left out by chance.
        if (k < 1.0) {
            // This one is left out.
            if (state().rng.unit() < 1.0 - k) {
                state().delete_moby(m);
                ++dropped;
            }

            continue;
        }

        // A roamer is not copied, and neither is an enemy whose block has a field naming another
        // moby (a copy would name the same one).
        if (c->role != ClassRole::Enemy || fix.names_moby.count(pl->second.pvar)) {
            continue;
        }

        const std::uint32_t size = get<std::uint32_t>(
            table + kPvarEntrySize * static_cast<std::uint32_t>(pl->second.pvar)
            + kPvarEntrySizeField
        );
        wanted.push_back({m, c->id, size, pl->second.pvar});
        asks.push_back(
            {difficulty::scale_count(1, k - 1.0, state().rng),
             difficulty::pvar_slots(static_cast<int>(size))}
        );
    }

    return found;
}

// How many copies were made and how many had no clear spot.
struct Placed {
    int made = 0;
    int no_spot = 0;
};

// Every enemy of a class to be copied (`wanted`), wherever it stands in `pool` and whether or not
// it is copied: the copies keep apart from all of them.
void note_class_members(const std::vector<Wanted>& wanted, const difficulty::Pool& pool) {
    // Each class to be copied, with no members yet.
    for (const Wanted& w : wanted) {
        state().class_members.try_emplace(w.cls);
    }

    // Each moby of the pool.
    for (gaddr m = pool.base; m < pool.spare; m += difficulty::kMobySize) {
        const auto members = state().class_members.find(get<std::int16_t>(m + kMobyClass));

        // A live moby of a class that is copied.
        if (members != state().class_members.end()
            && static_cast<std::int8_t>(get<std::uint8_t>(m + kMobyState)) >= 0) {
            members->second.push_back(m);
            state().member_set.insert(m);
        }
    }
}

// Makes the copies `plan` gives each enemy of `wanted`, each at a clear spot beside it, and
// watches them (state().watch). `room` and `room_left` are the pvar room from reserve_tail.
Placed place_copies(
    const std::vector<Wanted>& wanted,
    const difficulty::ClonePlan& plan,
    const Fixups& fix,
    gaddr* room,
    std::uint32_t* room_left
) {
    Placed placed;
    int& made = placed.made;
    int& no_spot = placed.no_spot;

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
            static const std::vector<std::uint32_t> none;

            const gaddr m = copy_moby(
                wanted[i].moby,
                wanted[i].cls,
                spot,
                wanted[i].size,
                in == fix.inner.end() ? none : in->second,
                room,
                room_left
            );

            // The game or the pvar room is out of space: no more copies of this enemy.
            if (m == 0) {
                break;
            }

            state().watch.push_back(
                {m, wanted[i].moby, wanted[i].cls, {spot[0], spot[1], spot[2]}}
            );
            state().copy_class[m] = wanted[i].cls;
            state().class_members[wanted[i].cls].push_back(m);
            state().member_set.insert(m);
            ++made;
        }
    }

    return placed;
}

// Plans the copies within `budget` slots. Pvars larger than a slot's take room from the pool's
// tail (`room`, `room_left`); when it cannot give it, only the copies whose pvars fit their own
// slot are planned, and `asks` is changed to say so.
difficulty::ClonePlan plan_with_room(
    std::vector<difficulty::CloneAsk>& asks,
    int budget,
    const difficulty::PoolGlobals& at,
    gaddr* room,
    std::uint32_t* room_left
) {
    difficulty::ClonePlan plan = difficulty::plan_clones(asks, budget);

    // Pvars larger than a slot's are asked for, and the pool's tail cannot give the room.
    if (plan.pvar_slots > 0
        && !difficulty::reserve_tail(mem(), at, plan.pvar_slots, room, room_left)) {
        // No room for large pvars: only the copies whose pvars fit their own slot.
        for (std::size_t i = 0; i < asks.size(); ++i) {
            asks[i].want = asks[i].pvslots > 0 ? 0 : asks[i].want;
        }

        plan = difficulty::plan_clones(asks, budget);
        *room = 0;
        *room_left = 0;
    }

    return plan;
}

// The enemy count slider, at a level's load: with k below 1 some enemies are deleted, with k above
// 1 copies are made beside the enemies that are free of the level's scripts, within the pool's
// budget. Leaves the level as it is when the pool or the file is not as expected.
void adjust_enemies(int level) {
    const double k = state().session.level[difficulty::kEnemyCount];

    // The slider is at retail.
    if (k == 1.0) {
        return;
    }

    const difficulty::PoolGlobals at =
        {level0(state().disc->pool),
         level0(state().disc->pool + kPoolSpare),
         level0(state().disc->pool + kPoolEnd),
         level0(state().disc->pool + kPoolPvars)};
    const difficulty::Pool pool = difficulty::Pool::read(mem(), at);
    const gaddr hdr = get<std::uint32_t>(level0(state().disc->load_state) + kLoadStateHeader);

    // The pool or the gameplay file is not as laid out: a wrong guess here would corrupt the level.
    if (!pool.sane() || hdr == 0) {
        state().mod->warn("enemy count: the level's moby pool is not as expected; left as it is");
        return;
    }

    // The gameplay file, still in memory: each placement's flags and pvar block, the pvar table
    // (+0x54: each block's address and size) and its two fix-up lists.
    const gaddr section = hdr + get<std::uint32_t>(hdr + kFilePlacements);
    const gaddr table = hdr + get<std::uint32_t>(hdr + kFilePvarTable);
    const std::unordered_map<std::uint32_t, Placement> by_pvars =
        read_placements(hdr, section, table);
    const Fixups fix = read_fixups(hdr, table, pool);

    Candidates found = pick_candidates(k, pool, by_pvars, fix, table);
    std::vector<difficulty::CloneAsk>& asks = found.asks;

    // Fewer enemies is done: nothing is copied.
    if (k < 1.0) {
        state().mod->info(
            "level " + std::to_string(level) + ": " + std::to_string(found.dropped) + " of "
            + std::to_string(found.eligible) + " enemies left out"
        );
        return;
    }

    const int free = difficulty::count_free(mem(), pool);
    const int budget = difficulty::clone_budget(free, level);
    gaddr room = 0;
    std::uint32_t room_left = 0;
    const difficulty::ClonePlan plan = plan_with_room(asks, budget, at, &room, &room_left);

    note_class_members(found.wanted, pool);
    const Placed placed = place_copies(found.wanted, plan, fix, &room, &room_left);
    const int made = placed.made;
    const int no_spot = placed.no_spot;

    state().mod->info(
        "level " + std::to_string(level) + ": " + std::to_string(made) + " copies of "
        + std::to_string(found.eligible) + " enemies (" + std::to_string(free)
        + " free slots, room for " + std::to_string(budget) + " beside the "
        + std::to_string(difficulty::kPoolReserve) + " kept for the game; "
        + std::to_string(plan.pvar_slots) + " reserved for pvars; " + std::to_string(no_spot)
        + " without a clear spot)"
    );
}

// Level_LoadGameplay: once the game has laid the level's mobys out, this level's values are
// rolled (with Chaos) or taken, the items' table rewritten and the enemy count applied. Returns
// the game's own cursor.
template <class Original>
std::uint32_t load_gameplay(Original original, std::int32_t full) {
    const std::uint32_t cursor = original(full);
    const int level = state().mod->loaded_level();

    // Not a level the mod knows (a menu, say): nothing to apply.
    if (level < 0 || level >= kLevels) {
        return cursor;
    }

    // This level's values (the host's in a party, rolled with Chaos), logged as Chaos or the trace
    // asks.
    const std::uint64_t seed = difficulty::level_values(state().session, level);
    state().anim.clear();
    state().watch.clear();
    std::memset(state().hit_owner, 0, sizeof state().hit_owner);
    state().rng =
        difficulty::Rng(difficulty::mix(seed, kLevelRngSalt + static_cast<std::uint64_t>(level)));

    rewrite_items(level);
    // Last level's copies are gone with it; only the host's game makes this level's.
    state().copy_class.clear();
    state().class_members.clear();
    state().member_set.clear();

    // The host's world brings a member its copies and takes out what it left out.
    if (!state().session.follow) {
        adjust_enemies(level);
    }

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
            "difficulty knows the PAL and NTSC-U discs, not " + std::string(mod.version())
        );
    }
}

// Hooks the enemies' updates (their speed) and the nanotech orb's (health pickups). One a disc
// lacks only leaves that class at its own speed: it is added to `missing`, not refused. Returns
// how many enemy updates were hooked.
int hook_updates(openrac::Mod& mod, std::string& missing) {
    const openrac_api& api = mod.api();
    int timed = 0;
    const int count = static_cast<int>(sizeof kUpdateHooks / sizeof kUpdateHooks[0]);

    // Each enemy class's update hook point.
    for (int i = 0; i < count; ++i) {
        void* original = nullptr;
        const int r = api.hook(
            mod.handle(),
            kUpdateHooks[i],
            "void(u32)",
            reinterpret_cast<void*>(kUpdateThunks[static_cast<std::size_t>(i)]),
            &original
        );

        // The disc has this hook point: keep its original; else say so.
        if (r == OPENRAC_HOOK_OK) {
            state().update_original[i] = reinterpret_cast<void (*)(std::uint32_t)>(original);
            ++timed;
        } else {
            missing += std::string(missing.empty() ? "" : ", ") + kUpdateHooks[i];
        }
    }

    void* ball = nullptr;

    // The nanotech orb's update, hooked the same way.
    if (api.hook(
            mod.handle(), kNanotechBall, "void(u32)", reinterpret_cast<void*>(&ball_update), &ball
        )
        == OPENRAC_HOOK_OK) {
        state().ball_original = reinterpret_cast<void (*)(std::uint32_t)>(ball);
    } else {
        missing += std::string(missing.empty() ? "" : ", ") + kNanotechBall;
    }

    return timed;
}

// The distance between two positions (or a moby's and a spot).
float distance(gaddr a, const float b[3]) {
    const float dx = get<float>(a + kMobyX) - b[0], dy = get<float>(a + kMobyY) - b[1],
                dz = get<float>(a + kMobyZ) - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// After a tick: a watched copy that its class's own start put on its original (a path follower
// begins at the path's first point, wherever it was placed) is taken out, before a frame draws the
// two as one that splits apart when hit. A copy that moved away on its own is no longer watched.
void check_copies() {
    int removed = 0;

    // Each watched copy, kept only while it has not moved on its own.
    for (std::size_t i = 0; i < state().watch.size();) {
        const Watched& w = state().watch[i];
        const bool copy_alive =
            static_cast<std::int8_t>(get<std::uint8_t>(w.copy + kMobyState)) >= 0
            && get<std::int16_t>(w.copy + kMobyClass) == w.cls;
        const bool orig_alive =
            static_cast<std::int8_t>(get<std::uint8_t>(w.orig + kMobyState)) >= 0
            && get<std::int16_t>(w.orig + kMobyClass) == w.cls;
        bool done = !copy_alive || !orig_alive;

        // Both are there: on the original's spot, or moved away from its own.
        if (!done) {
            const float at[3] =
                {get<float>(w.orig + kMobyX),
                 get<float>(w.orig + kMobyY),
                 get<float>(w.orig + kMobyZ)};

            // Its start put it on the original: take it out.
            if (distance(w.copy, at) < kStackedDistance) {
                state().delete_moby(w.copy);
                ++removed;
                done = true;
            } else {
                done = distance(w.copy, w.spot) > kAwakeDistance;
            }
        }

        // Watched no more: the last one takes its place.
        if (done) {
            state().watch[i] = state().watch.back();
            state().watch.pop_back();
        } else {
            ++i;
        }
    }

    // Some were taken out: say how many.
    if (removed > 0) {
        state().mod->info(
            std::to_string(removed)
            + " copies taken out: their class started them on their original (a path's start)"
        );
    }
}

// Each frame: the events' clock (in play, with the events on), then max health. A party's member
// has no clock of its own: the host decides the events (session.cpp: advance_events).
void on_frame() {
    // Copies made at this level's load and not yet moved on their own.
    if (!state().watch.empty()) {
        check_copies();
    }

    // Chaos events are on (or a party's host's event may be on) and the game is in play: the clock
    // runs on play time only.
    if ((state().session.events_on || state().session.follow) && in_play()) {
        const double dt = get<float>(exe(state().disc->tick) + kTickSeconds);
        difficulty::advance_events(
            state().session, dt > 0.0 && dt < kMaxFrameSeconds ? dt : kFallbackFrameSeconds
        );
    }

    keep_max_health();
}

// Takes the hook points every slider needs: one this OpenRAC lacks refuses the mod, naming it.
void take_hooks(openrac::Mod& mod) {
    take<TakeDamage>(mod, [](auto original, std::int32_t amount) {
        take_damage(original, amount);
    });
    take<GetHit>(mod, [](auto original, std::uint32_t m, std::int32_t mask, std::int32_t keep) {
        return get_hit(original, m, mask, keep);
    });
    take<TakeAmmo>(mod, [](auto original, std::int32_t item, std::int32_t amount) {
        return take_ammo(original, item, amount);
    });
    take<RecordDeath>(
        mod, [](auto original, std::uint32_t m, std::int32_t flags, std::int32_t index) {
            record_death(original, m, flags, index);
        }
    );
    take<LoadGameplay>(mod, [](auto original, std::int32_t full) {
        return load_gameplay(original, full);
    });
    take<ApproachSpeed>(mod, [](auto original, float up, float down) {
        approach_speed(original, up, down);
    });
}

}  // namespace

// The rac1 part's entry (game.h): checks the disc, reads the settings, takes the functions and the
// hook points it needs (one this OpenRAC lacks refuses the mod, naming it), and logs how it plays.
void load(openrac::Mod& mod) {
    // A load starts afresh: nothing carries over from an earlier one.
    g_state = std::make_unique<State>();
    state().mod = &mod;
    find_disc(mod);

    state().classes.clear();

    // Each class the mod knows, by its id.
    for (const auto& c : kClasses) {
        state().classes[c.id] = &c;
    }

    difficulty::start(state().session, mod, difficulty::kAllKnobs);

    state().create_moby =
        mod.required_function<std::uint32_t (*)(std::int32_t)>(state().disc->create_moby);
    state().delete_moby = mod.required_function<void (*)(std::uint32_t)>(state().disc->delete_moby);
    state().build_matrix =
        mod.required_function<void (*)(std::uint32_t)>(state().disc->build_matrix);
    state().ground =
        mod.required_function<float (*)(std::uint32_t, std::int32_t, float)>(state().disc->ground);
    state().line = mod.required_function<
        std::int32_t (*)(std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t, std::uint32_t)>(
        state().disc->line
    );

    take_hooks(mod);

    // The catalogue's updates: the enemies' (their speed) and the nanotech orb's (health pickups).
    // One a disc lacks only leaves that class at its own speed: logged, not refused.
    std::string missing;
    const int timed = hook_updates(mod, missing);

    // Some hook point is not on this disc.
    if (!missing.empty()) {
        mod.warn("no hook point on this disc for " + missing + ": those stay as the game has them");
    }

    mod.on_frame(on_frame);

    difficulty::log_start(state().session, std::to_string(timed) + " enemy updates timed");
}

}  // namespace difficulty::rac1
