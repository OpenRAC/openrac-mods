// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++'s bolt rules (bolt_rules.h).

#include "bolt_rules.h"

#include <cmath>

namespace ng_plus_plus {
namespace {

// The first cycle the bolt rules change anything: challenge mode's second run, where the tiers'
// prices and the enemies' curve start (tiers.h).
constexpr int kFirstCycle = 2;

// The run scale from kFirstCycle on, one a cycle; the last holds past the table. In step with the
// tiers' prices: the four bought tiers' factors over the Mega price (tiers.cpp, kPriceTimes).
constexpr float kRunScale[] = {1.5f, 2.5f, 4.0f, 6.0f};

// The platinum share from kFirstCycle on at a streak of x1, and at x20; the last holds past them.
constexpr float kShareLeast[] = {0.10f, 0.15f, 0.20f, 0.25f};
constexpr float kShareMost[] = {0.50f, 0.65f, 0.80f, 0.90f};

// The first challenge mode's (cycle 1) taste of platinum, at x1 and at x20: a little, with no
// bigger drops, so the platinum bolts show up before the run scale starts.
constexpr int kTasteCycle = 1;
constexpr float kTasteLeast = 0.05f;
constexpr float kTasteMost = 0.25f;

// How many cycles the tables have.
constexpr int kRunCycles = static_cast<int>(sizeof kRunScale / sizeof kRunScale[0]);

// One kind of coin in the split's order: what it is worth, whether it is platinum, its class.
struct CoinKind {
    std::int32_t worth;  // Bolts.
    bool platinum;       // A platinum coin (paid from the platinum part).
    int kind;            // Its class, from kCoinClass.
};

// Every coin, the most valuable first: the game's greedy split over gold and platinum together.
// At the same worth platinum first, so the shinier coin takes a place under a coin limit.
constexpr CoinKind kSplitOrder[] = {
    {5000, true, 6},
    {2500, true, 5},
    {1000, false, 6},
    {500, true, 4},
    {500, false, 5},
    {250, true, 3},
    {100, true, 2},
    {100, false, 4},
    {50, false, 3},
    {25, true, 1},
    {20, false, 2},
    {5, true, 0},
    {5, false, 1},
    {1, false, 0},
};

// Returns the table's entry for `cycle` (kFirstCycle the first), the last past it.
float by_run(const float* table, int cycle) {
    const int at = cycle - kFirstCycle;
    return table[at < kRunCycles ? at : kRunCycles - 1];
}

}  // namespace

BoltScaling bolt_scaling_of(std::string_view value) {
    // The manifest's choices; anything else is the default.
    if (value == "off") {
        return BoltScaling::Off;
    }

    return value == "generous" ? BoltScaling::Generous : BoltScaling::Normal;
}

float run_scale(int cycle, BoltScaling scaling) {
    // Off, and the runs the game balances itself, pay as the game does.
    if (scaling == BoltScaling::Off || cycle < kFirstCycle) {
        return 1.0f;
    }

    const float scale = by_run(kRunScale, cycle);

    // Generous: half as much again.
    return scaling == BoltScaling::Generous ? scale * kGenerous : scale;
}

float toughness(std::int32_t base) {
    // A small enemy, a crate: as the game pays.
    if (base <= kToughBase) {
        return 1.0f;
    }

    const float factor = std::sqrt(static_cast<float>(base) / static_cast<float>(kToughBase));
    return factor < kToughMost ? factor : kToughMost;
}

float platinum_share(int cycle, int streak) {
    // No platinum in the first game.
    if (cycle < kTasteCycle) {
        return 0.0f;
    }

    // No streak yet counts as x1, past the top as the top.
    const int at = streak < 1 ? 1 : (streak > kTopStreak ? kTopStreak : streak);
    const bool taste = cycle < kFirstCycle;
    const float least = taste ? kTasteLeast : by_run(kShareLeast, cycle);
    const float most = taste ? kTasteMost : by_run(kShareMost, cycle);
    const float t = static_cast<float>(at - 1) / static_cast<float>(kTopStreak - 1);

    return least + (most - least) * t;
}

std::int32_t scale_amount(std::int32_t amount, float scale) {
    // Nothing to scale.
    if (amount <= 0) {
        return 0;
    }

    const double scaled = std::floor(static_cast<double>(amount) * scale + 0.5);

    // At most kMostDrop: the game doubles and paces it after.
    return scaled >= kMostDrop ? kMostDrop : static_cast<std::int32_t>(scaled);
}

int CoinPlan::coins() const {
    int count = 0;

    // Both kinds, every class.
    for (int n = 0; n < kCoinKinds; ++n) {
        count += gold[static_cast<std::size_t>(n)] + platinum[static_cast<std::size_t>(n)];
    }

    return count;
}

std::int32_t CoinPlan::worth() const {
    std::int32_t total = 0;

    // A platinum coin is worth kPlatinumTimes its class.
    for (int n = 0; n < kCoinKinds; ++n) {
        const auto at = static_cast<std::size_t>(n);
        total += kCoinValues[at] * (gold[at] + kPlatinumTimes * platinum[at]);
    }

    return total;
}

CoinPlan split(std::int32_t total, float share, int limit) {
    CoinPlan plan;

    // No drop: no coins.
    if (total <= 0) {
        return plan;
    }

    // The platinum part: rounded (0.65 is no exact float), then down to whole platinum bolts (5
    // the least coin), never more than the drop.
    const float clamped = share < 0.0f ? 0.0f : (share > 1.0f ? 1.0f : share);
    const auto wanted = std::llround(static_cast<double>(total) * clamped);
    std::int32_t platinum = static_cast<std::int32_t>(wanted / kPlatinumTimes) * kPlatinumTimes;
    std::int32_t gold = total - platinum;
    int room = limit;

    // The most valuable coin first, each from its own part, while there is room.
    for (const CoinKind& coin : kSplitOrder) {
        std::int32_t& part = coin.platinum ? platinum : gold;
        int count = part / coin.worth;

        // Under a limit, only what still fits.
        if (limit >= 0 && count > room) {
            count = room;
        }

        part -= count * coin.worth;
        room -= count;
        auto& counts = coin.platinum ? plan.platinum : plan.gold;
        counts[static_cast<std::size_t>(coin.kind)] += count;
    }

    plan.leftover = platinum + gold;
    return plan;
}

}  // namespace ng_plus_plus
