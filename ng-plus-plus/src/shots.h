// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The perks that need OpenRAC 1.29's weapon calls (weapon_api.h; README.md, "What each tier
// does"): more shots of each weapon whose shot is a moby, fired again by the game's own spawn
// (weapon_refire); what a shot does as it ends (on_shot_end): the RYNO II's split homing rockets,
// the Gravity Bomb's and the Bomb Glove's mini bombs, the Seeker Gun's seekers seeking again, the
// Minirocket Tube's cluster blasts; the Minirocket Tube's rockets splitting in flight; the tiers'
// colour, glow and trails on every shot and what it makes (weapon_look); the Pulse Rifle's fire
// rate (weapon_record); and for the perks, the statuses (moby_status: the Ice Gun's freeze, the
// slows and stuns) and the game's own blast at a point (a detonating rocket in a weapon's look).
//
// Every shot made counts toward kShotsPerTick (the port's OPENRAC_SHOTS_PER_TICK_MAX is for all
// mods) and the port refuses them while the moby pool runs low; NG++ asks for the most room
// (moby_pool.h). A port older than 1.29 has none of it: perks.h goes on with what 1.28 gives.
//
// Only the game's thread uses it; nothing in a party guest's game.

#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "game_view.h"
#include "openrac/mod.hpp"
#include "perk_rules.h"
#include "weapon_api.h"
#include "weapons.h"

namespace ng_plus_plus {

class Perks;

// The most shots NG++ makes in one tick (refires and spawns): the rest wait for the next ticks.
inline constexpr int kShotsPerTick = 64;

// The 1.29 perks.
class Shots {
public:
    Shots(openrac::Mod& mod, const GameView& view, const Weapons& weapons, Perks& perks)
        : m_mod(mod),
          m_view(view),
          m_weapons(weapons),
          m_perks(perks) {}

    // Takes the weapon calls (only during load) and says what this port lacks.
    void start();

    // Returns whether the port has the weapon calls (1.29).
    bool active() const { return m_calls != nullptr; }

    // Puts the moby in `slot` under `status` for `ticks`. False without the call or refused.
    bool status(int slot, Status status, int ticks);

    // The game's rocket blast at `at` in item `item`'s look (a detonating rocket). False without
    // the call, past this tick's shots, or refused.
    bool blast(const std::array<float, 3>& at, int item);

    // Rockets (class `oclass`) from `at` at the `count` nearest enemies within `radius`, in item
    // `item`'s look. Returns how many were made.
    int rockets_at(const std::array<float, 3>& at, int oclass, int count, float radius, int item);

    // Once a game tick: the looks, the rockets splitting in flight, the shots waiting, the fire
    // rate.
    void tick(std::uint32_t tick);

    // What was made since the last summary, for the perks' log line.
    struct Counts {
        int refires = 0;
        int spawns = 0;
        int statuses = 0;
        int blasts = 0;
    };

    Counts take_counts();

private:
    // A shot the mod knows: told, refired or made, until it ends.
    struct Tracked {
        int oclass = 0;
        int item = 0;
        std::uint32_t born = 0;
        int generation = 0;  // 0 a shot the hero fired, 1 one made from it, ...
        std::uint32_t root =
            0;  // The first shot's serial (or slot): what all of them count toward.
        std::array<float, 3> direction{};
        float speed = 0.0f;
        bool split = false;  // Split in flight already.
    };

    // A shot to make at a later tick.
    struct Pending {
        std::uint32_t when = 0;
        int oclass = 0;
        int item = 0;
        std::array<float, 3> at{};
        std::array<float, 3> velocity{};
        int target = -1;
        std::uint32_t flags = 0;
        int generation = 0;
        std::uint32_t root = 0;
    };

    static void fired(void* user, const WeaponShot* shot);
    static void ended(void* user, const ShotEnd* end);
    void on_fire(const WeaponShot& shot);
    void on_end(const ShotEnd& end);

    // Makes a shot now (or later when this tick's are spent). Returns its slot, -1 for none (or
    // not yet).
    int spawn(const Pending& shot);

    // The stage `item` acts at now (Perks's, overdrive included), and the RYNO II's tier.
    Stage stage(int item) const;
    int tier(int item) const;

    // The weapons' looks, sent where they changed.
    void looks(std::uint32_t tick);

    // The Pulse Rifle's fire rate in the level's record.
    void fire_rates();

    openrac::Mod& m_mod;
    const GameView& m_view;
    const Weapons& m_weapons;
    Perks& m_perks;
    const WeaponCallsTable* m_calls = nullptr;
    std::uint32_t m_tick = 0;
    int m_level = -1;
    int m_budget = kShotsPerTick;
    std::unordered_map<int, Tracked> m_tracked;          // By the shot's slot.
    std::unordered_map<std::uint32_t, int> m_root_made;  // Shots made from each first shot.
    std::vector<Pending> m_pending;
    std::array<std::uint64_t, kItems> m_looks{};  // Each item's look sent (material, rgb).
    std::array<float, kItems> m_speed{};          // Each weapon's last told shot's speed.

    struct Rate {
        int level = -1;
        openrac_gaddr record = 0;
        float base = 0.0f;
        float applied = 1.0f;
    };

    Rate m_rate;
    Counts m_counts;
};

}  // namespace ng_plus_plus
