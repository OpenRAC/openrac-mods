// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The space combat missions across new games (README.md, "The ship"): Feltzin's, Hrugis's and
// Gorn's missions (and Wupash's ship code) get more waves of enemy ships, tougher ships and more
// elites with each challenge mode, and the ship's tiers (ship_upgrades.h) land their damage here.
//
// How the game runs them (rac2-decomp, src/levels/05_feltzin_system and src/levels/shared): there
// is no wave spawner. A mission's ships are mobys the level places, in group lists; its start
// brings each to life with moby_init_spawned_sub_object (L05_0037AEC0: shown, its health from its
// record, its AI started, the mission's alive count up), and its death leaves the record for the
// game to bring back (SpawnFreeLevelObjectById does so for Feltzin's targets). So NG++'s waves are
// the game's own: when a fight's ships fall to a quarter of its peak, the dead ones are brought
// back with the same call, at most kReviveEachTick a tick. They are the level's own records, so
// they take no room in the moby pool. Never a boss's fight, never one of fewer than kWaveFloor
// ships: a single target's mission ends as the game ends it.
//
// The player's shots post their hits through QueueMobyImpact (rac2.combat.post_hit): Combat hands
// every hit in a space level here, which scales the player's by the ship's tier and divides every
// enemy's by its health (the curve's enemy_health; a boss's boss_health; an elite's twice).
//
// Only the game's thread uses it; nothing changes in a party guest's game but the hits.

#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bolt_api.h"
#include "game_view.h"
#include "moby_scan.h"
#include "openrac/mod.hpp"
#include "perk_rules.h"
#include "save_tiers.h"
#include "ship_upgrades.h"

namespace ng_plus_plus {

class Elites;

// ---- The rules ----

// The levels with space combat: Feltzin, Hrugis, Gorn and Wupash.
inline constexpr int kSpaceLevels[] = {5, 10, 15, 25};

// Returns whether level `level` is a space level.
bool is_space_level(int level);

// Returns the extra waves a fight gets in cycle `cycle`: none up to 1, one from 2, two from 4,
// three from 6; one more under the Swarm modifier (from cycle 2).
int extra_waves(int cycle, Modifier modifier);

// Returns the share of enemy ships that are elites in cycle `cycle`, in percent: none before 2,
// 20 at 2 and 10 more a run up to 60, doubled up to 80 under Elite guard; `wave` > 0 (a wave NG++
// brought back) adds kWaveElitePercent.
int space_elite_percent(int cycle, Modifier modifier, int wave);
inline constexpr int kWaveElitePercent = 15;

// An elite ship's health, times an ordinary one's.
inline constexpr float kShipEliteHealth = 2.0f;

// A fight's least peak for more waves, and the most ships brought back in one tick.
inline constexpr int kWaveFloor = 3;
inline constexpr int kReviveEachTick = 6;

// Returns whether a fight with `peak` ships at its height and `alive` alive now, `done` of its
// `extra` waves past, is due its next wave: at a quarter of its peak or under, before the last
// one dies.
bool wave_due(int peak, int alive, int done, int extra);

// Returns how much faster Feltzin's target spawner (class 4378) makes its targets in cycle `cycle`:
// 1.0 up to 1, x1.5 from 2, x2 from 4.
float target_spawn_rate(int cycle);

// A space level's enemy ship and what it is. `fighter` ones come back in waves; the others are a
// mission's boss or a capital ship.
struct ShipClass {
    int level;
    int moby_class;
    bool fighter;
    const char* what;
};

// Returns the class `moby_class` of level `level`, or null for none listed.
const ShipClass* ship_class_of(int level, int moby_class);

// The moby classes that are the player's side in a space level: his ship, its parts, Hrugis's
// freighters he defends. Their hits taken are never scaled as an enemy's.
bool is_player_side(int moby_class);

// The ship's shots (UpdateShipWeapons, moby_spawn_shot_effect).
inline constexpr int kShipShotClass = 4090;

// moby_init_spawned_sub_object's address in level `level` on the disc `version`, 0 for none.
std::uint32_t revive_address(std::string_view version, int level);

// ---- In the game ----

// What the curve's settings say (combat.h's EnemySettings).
struct SpaceSettings {
    bool on = true;         // The "space_missions" setting: waves, elites, the spawner.
    bool scaling = true;    // The "enemy_scaling" setting.
    float strength = 1.0f;  // The "enemy_strength" setting.
};

// The space missions.
class SpaceMissions {
public:
    using PostHitFn = void (*)(std::uint32_t, std::uint32_t);

    SpaceMissions(
        openrac::Mod& mod,
        const GameView& view,
        const SaveTiers& tiers,
        const ShipUpgrades& ship,
        SpaceSettings settings
    )
        : m_mod(mod),
          m_view(view),
          m_tiers(tiers),
          m_ship(ship),
          m_scan(view),
          m_settings(settings) {}

    // The run's modifier, through `elites`.
    void set_elites(const Elites* elites) { m_elites = elites; }

    // Finds moby_material.
    void start();

    // Returns whether the hits of the level now are this part's (a space level).
    bool owns_hits() const { return is_space_level(m_view.loaded_level()); }

    // A hit posted to `victim` from the request at `request` in a space level (Moby_PostHit,
    // through Combat): the player's times the ship's tier and its perks, an enemy's divided by its
    // health. The request is put back after.
    void post_hit(PostHitFn original, std::uint32_t victim, std::uint32_t request);

    // Once a game tick: the waves, the elites' look and the target spawner.
    void tick(std::uint32_t tick);

private:
    // One ship of the fight now.
    struct Ship {
        int moby_class = 0;
        int slot = 0;
        int wave = 0;  // 0 the game's, n the nth wave NG++ brought back.
        bool elite = false;
        bool alive = false;
    };

    // Returns the motion record of the moby at `moby` (moby_get_motion_data), 0 for none.
    std::uint32_t motion_of(std::uint32_t moby) const;

    // Returns whether the ship at `moby` is alive (its motion record's +0x110).
    bool ship_alive(std::uint32_t moby) const;

    // Returns the health the hits on the moby at `moby` of class `moby_class` are divided by.
    float health_of(std::uint32_t moby, int moby_class) const;

    // Returns whether the hit request at `request` is the player's (his ship or its shots).
    bool players_hit(std::uint32_t request) const;

    // Rolls whether ship `ship` is an elite, and draws it so.
    void roll_elite(std::uint32_t moby, Ship& ship);

    // The fight: new ships seen, the alive count, the next wave and the ships brought back.
    void run_fight(int level);

    // Brings back up to kReviveEachTick of the pending ships.
    void revive_pending(int level);

    // Runs Feltzin's target spawner faster.
    void speed_spawner();

#ifdef NGPP_TEST_SETUP
    // Test runs only: brings every placed fighter to life (NGPP_SPACE_REVIVE=1) and logs them.
    void test_revive(int level);
    int m_test_ticks = 0;
#endif

    // A new level: the fight forgotten.
    void enter(int level);

    openrac::Mod& m_mod;
    const GameView& m_view;
    const SaveTiers& m_tiers;
    const ShipUpgrades& m_ship;
    MobyScan m_scan;
    SpaceSettings m_settings;
    const Elites* m_elites = nullptr;
    MobyMaterialFn m_material = nullptr;
    bool m_obsidian = false;

    int m_level = -1;
    std::unordered_map<std::uint32_t, Ship> m_fight;  // Address to ship.
    std::vector<std::uint32_t> m_pending;             // Ships to bring back.
    int m_peak = 0;
    int m_waves = 0;
    int m_quiet = 0;
    bool m_logged_revive = false;
    int m_hits = 0;  // The player's hits, for the overcharge.
    float m_spawner_carry = 0.0f;
    std::uint32_t m_tick = 0;  // The tick now, for the scan the hits read.
};

}  // namespace ng_plus_plus
