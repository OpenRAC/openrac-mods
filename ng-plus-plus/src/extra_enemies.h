// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// More enemies each new game (README.md, "More enemies"): from the second challenge mode, each
// enemy a level places gets a copy beside it as the level loads, two from the fourth. OpenRAC
// makes the copies (interface 1.25: moby_copy_check and moby_copy, port/game/rac2/host/
// enemy_copies.c): it says which enemies may be copied (never a boss, a turret, a mine, a spawner,
// the crystal creatures of Tabora and Grelbin, nor one the level's scripts need alone: a wave's, an
// arena's, one of the enemies a door waits for) and puts each copy at a clear spot beside its
// original, on its floor. A copy is the game's own enemy: it drops its original's bolts through the
// game's own drop, so the run's scale, platinum and obsidian bolts apply, and it counts for the kill
// streak.
//
// At the level's load (rac2.level.load, as it returns), in the party's host only (a guest's game
// shows the host's). Only the classes of the per-planet table (copy_entry) are copied. An OpenRAC
// before 1.26 makes no copies (1.25's never woke): the mod says so once and goes without.
//
// Only the game's thread uses it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "bolt_api.h"
#include "openrac/mod.hpp"
#include "save_tiers.h"
#include "game_view.h"

namespace ng_plus_plus {
class Elites;
}

namespace ng_plus_plus {

// The copies each enemy gets in challenge mode cycle `cycle` (the save's counter: 0 the first game,
// 1 the first challenge mode): none up to cycle 1, one from cycle 2, two from cycle 3, three from cycle 4.
int extra_copies(int cycle);

// One enemy of the per-planet table: a class a level's copies may be of, at most how many copies of
// each, and what it is. Only the classes a hidden run of that level saw copied, woken, fighting,
// killed, dropping its bolts once and gone like its original are listed (README.md, "More
// enemies").
struct CopyEntry {
    int level;         // The game's level number (rac2-decomp: src/levels/NN_name).
    int oclass;        // The moby class.
    int most;          // Copies of each at most (1 for the big ones).
    const char* what;  // The enemy, and the planet.
};

// Returns the table's entry for class `oclass` in level `level`, or null: no copies of it.
const CopyEntry* copy_entry(int level, int oclass);

// Returns how many copies each enemy of class `oclass` gets in level `level` in cycle `cycle`: the
// cycle's (extra_copies) up to the entry's most, none for a class the table has not.
int copies_of(int level, int oclass, int cycle);

// The interface minor that makes copies, the one whose copies wake with their squad (a 1.25 port's
// copies of a squad's enemies never woke, were never drawn and could not be hit: NG++ 0.4.1's stuck
// enemies), and moby_copy_check's answers (OPENRAC_COPY_* of 1.25): one it may copy, then why not.
inline constexpr std::uint16_t kCopyMinor = 25;
inline constexpr std::uint16_t kSquadCopyMinor = 26;
inline constexpr int kCopyOk = 0;
inline constexpr int kCopyNotEnemy = 1;
inline constexpr int kCopyNotPlaced = 2;
inline constexpr int kCopyScripted = 3;
inline constexpr int kCopyListed = 4;

// moby_copy_check's and moby_copy's signatures.
using MobyCopyCheckFn = int (*)(openrac_mod*, int);
using MobyCopyFn = int (*)(openrac_mod*, int, int);

#if OPENRAC_MOD_API_MINOR < 25
// The port's table up to 1.25's copies, as released: the 1.23 table (bolt_api.h's) and the two
// entries after it.
struct CopyApi {
    BoltApi upto_material;  // Up to 1.23's moby_material (1.24 added no entry).
    MobyCopyCheckFn moby_copy_check;
    MobyCopyFn moby_copy;
};
#else
// An SDK of 1.25 or later: its own table has them.
using CopyApi = openrac_api;
#endif

// The port's copy calls, or nulls on a port older than 1.26 (or whose table ends before them).
struct CopyCalls {
    MobyCopyCheckFn check = nullptr;
    MobyCopyFn copy = nullptr;
};

// Returns `api`'s copy calls.
inline CopyCalls copy_calls_of(const openrac_api& api) {
    const std::size_t end = offsetof(CopyApi, moby_copy) + sizeof(MobyCopyFn);

    // An older port has no such entries (what follows its table is not its), or 1.25's copies,
    // which never woke.
    if (api.api_minor < kSquadCopyMinor || api.size < end) {
        return {};
    }

    const auto* table = reinterpret_cast<const CopyApi*>(&api);
    return {table->moby_copy_check, table->moby_copy};
}

// The ticks between the tries at a load's missed copies, and how many tries: two seconds apart for
// twenty seconds, while the level wakes and its packs move apart.
inline constexpr int kRetryTicks = 120;
inline constexpr int kRetries = 10;

// More enemies.
//
// Holds the mod, the view and the tiers by reference; the mod's state owns them.
class ExtraEnemies {
public:
    // Copies enemies in `mod`'s game when `on` (the "extra_enemies" setting), not in a party
    // guest's (`tiers`' party).
    ExtraEnemies(openrac::Mod& mod, const GameView& view, const SaveTiers& tiers, bool on)
        : m_mod(mod),
          m_view(view),
          m_tiers(tiers),
          m_on(on) {}

    // Makes elites of some copies, and the Swarm modifier's extra copy, through `elites`.
    void set_elites(Elites* elites) { m_elites = elites; }

    // Takes the level's load. An OpenRAC without copies or without the hook point: logged once.
    void start();

    // At the level's load, once the game has placed its mobys: the copies of this cycle. Returns
    // how many were made.
    int level_loaded();

    // Once a game tick: copies a crowded load left out are tried again (a few times, a second
    // apart) once the level has spawned, its packs have moved and the pool has room.
    void tick();

private:
    openrac::Mod& m_mod;      // The mod; the SDK keeps it for the program.
    const GameView& m_view;   // The game on this disc.
    const SaveTiers& m_tiers; // The tiers, for the party.
    bool m_on;                // The setting.
    CopyCalls m_calls;        // The port's copy calls, or none.
    Elites* m_elites = nullptr;  // The elites and the run's modifier, or none.

    // The copies a load could not make (no room or no spot yet): the placed enemy's slot and the
    // copy's index, tried again by tick() while the level stays.
    std::vector<std::pair<int, int>> m_pending;
    int m_pending_level = -1;  // The level they are for.
    int m_retries = 0;         // Tries made again for this load.
    int m_wait = 0;            // Ticks to the next one.
    int m_load_level = -1;     // The level of the last load, and the copies each enemy got then.
    int m_load_each = -1;

    // The copies each enemy gets now: the cycle's, and the Swarm modifier's one more.
    int copies_each() const;

    // Makes copy `index` of the enemy in `slot` unless the pool is near its end. Returns 1 made, 0
    // not made (the port had no room or spot), -1 left alone to keep the pool's last slots for the
    // bolts and the level.
    int make_copy(int slot, int index);
};

}  // namespace ng_plus_plus
