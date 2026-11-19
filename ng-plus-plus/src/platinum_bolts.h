// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Platinum bolts and bolt scaling (README.md): from challenge mode's second run, every drop pays
// more (bolt_rules.h's run scale and toughness), and part of it comes as platinum coins, the game's
// own coins drawn as platinum and worth five times as much.
//
// A drop passes through three hook points of OpenRAC 1.23 (hooks.h). The burst (Bolt_SpawnPickups)
// goes on to the game's with the scaled amount, so the game's roll, challenge mode doubling, pacing
// and coin limit still decide the total; the coins it makes (Bolt_SpawnPickup) are only written
// down meanwhile: their worth, where and how fast each flies. Then the mod makes that total again
// as gold and platinum coins (bolt_rules.h's split), each at a place and speed the game chose, and
// draws the platinum ones with moby_material. Collecting a platinum coin (Bolt_Collect) adds the
// four times its class the game does not.

#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "bolt_api.h"
#include "bolt_rules.h"
#include "game_view.h"
#include "openrac/mod.hpp"

namespace ng_plus_plus {

// The platinum coins and the drops' scaling.
//
// Holds the mod and the game view by reference; the mod's state owns them. Only the game's thread
// uses it.
class PlatinumBolts {
public:
    // The burst's signature: (moby, low, high, flags, surface, maximum, height).
    using BurstFn = void(
        std::uint32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t, float
    );

    // A coin's: (source, position, velocity, flags, value, counted), its moby or 0.
    using CoinFn = std::uint32_t(
        std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::int32_t
    );

    // Scales the drops of `view`'s game as `scaling` says, through `mod`.
    PlatinumBolts(openrac::Mod& mod, const GameView& view, BoltScaling scaling)
        : m_mod(mod),
          m_view(view),
          m_scaling(scaling) {}

    // Takes the three bolt hook points when the port has them and moby_material; an OpenRAC
    // without them (older than 1.23) keeps the game's drops, which is logged once. Nothing is
    // taken with the setting off. Only during load.
    void start();

    // Returns whether the drops are the mod's (the hook points taken, the setting on).
    bool enabled() const { return m_enabled; }

    // A burst (Bolt_SpawnPickups): from cycle 2 on, the game's with the amount scaled, its coins
    // made again as gold and platinum; else the game's as it is.
    void spawn_pickups(
        openrac::Original<BurstFn> original,
        std::uint32_t moby,
        std::int32_t low,
        std::int32_t high,
        std::int32_t flags,
        std::int32_t surface,
        std::int32_t maximum,
        float height
    );

    // A coin (Bolt_SpawnPickup): inside one of the mod's bursts written down and not made (0);
    // else the game's, which forgets a platinum coin whose moby it reuses.
    std::uint32_t spawn_pickup(
        openrac::Original<CoinFn> original,
        std::uint32_t source,
        std::uint32_t position,
        std::uint32_t velocity,
        std::uint32_t flags,
        std::int32_t value,
        std::int32_t counted
    );

    // A coin collected (Bolt_Collect): the game's, then for a platinum coin the four times its
    // class more, its material taken off.
    std::uint32_t collect(
        openrac::Original<std::uint32_t(std::uint32_t)> original, std::uint32_t moby
    );

    // Once a game tick: forgets the platinum coins of a level left (the port drops their materials
    // too).
    void tick();

private:
    // One coin the game's burst made: where it was to start and fly, and how.
    struct Recorded {
        std::uint32_t source;           // The moby it came from.
        std::array<float, 4> position;  // Its start.
        std::array<float, 4> velocity;  // Its speed a tick.
        std::uint32_t flags;            // The burst's flags (2: it flies to the hero).
        std::int32_t counted;           // Whether the level's bolt score counts it.
        std::int32_t value;             // Its worth.
    };

    // Makes the coins of `plan` at the places the game chose for its own, then credits what the
    // plan could not fit (`plan.leftover`) where the game does.
    void pay(const CoinPlan& plan);

    // Makes one coin of class `kind` (from kCoinClass), platinum or gold, as the `index`th of the
    // burst. Returns its moby, or 0.
    std::uint32_t make_coin(int index, int kind, bool platinum);

    // Marks `moby` a platinum coin of class `kind`: drawn as platinum, worth five times. A coin
    // that cannot be drawn so gets its extra worth at once.
    void mark(std::uint32_t moby, int kind);

    openrac::Mod& m_mod;                  // The mod; the SDK keeps it for the program.
    const GameView& m_view;               // The game; outlives this.
    BoltScaling m_scaling;                // The "bolt_scaling" setting.
    MobyMaterialFn m_material = nullptr;  // The port's moby_material (null before 1.23).
    bool m_enabled = false;               // The hook points are taken.
    bool m_recording = false;             // Inside a burst of the mod's.
    std::vector<Recorded> m_burst;        // The coins the game's burst made.
    int m_level = -1;                     // The level the platinum coins are in.
    bool m_mark_logged = false;           // A coin that could not be drawn was logged.

    // The platinum coins out: each one's moby to its class (from kCoinClass).
    std::unordered_map<std::uint32_t, int> m_platinum;
};

}  // namespace ng_plus_plus
