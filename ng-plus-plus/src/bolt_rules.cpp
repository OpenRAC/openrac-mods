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

// The obsidian share from kFirstCycle on at a streak of x1, and at x20; the last holds past them.
// Hints in the second challenge mode (none at x1, 8% at x20), growing with each run.
constexpr float kObsidianLeast[] = {0.00f, 0.02f, 0.05f, 0.08f};
constexpr float kObsidianMost[] = {0.08f, 0.15f, 0.25f, 0.35f};

// The first challenge mode's (cycle 1) taste of platinum, at x1 and at x20, with no bigger drops,
// so the platinum bolts show up before the run scale starts. Platinum comes in steps of 5 bolts:
// at 20% a drop of 25 bolts already shows one.
constexpr int kTasteCycle = 1;
constexpr float kTasteLeast = 0.20f;
constexpr float kTasteMost = 0.40f;

// How many cycles the tables have.
constexpr int kRunCycles = static_cast<int>(sizeof kRunScale / sizeof kRunScale[0]);

// One kind of coin in the split's order: what it is worth, its tier, its class.
struct CoinKind {
    std::int32_t worth;  // Bolts.
    CoinTier tier;       // Gold, platinum or obsidian (paid from that part).
    int kind;            // Its class, from kCoinClass.
};

// Every coin, the most valuable first: the game's greedy split over the three kinds together. At
// the same worth the rarer first, so the shinier coin takes a place under a coin limit.
constexpr CoinKind kSplitOrder[] = {
    {25000, CoinTier::Obsidian, 6},
    {12500, CoinTier::Obsidian, 5},
    {5000, CoinTier::Platinum, 6},
    {2500, CoinTier::Obsidian, 4},
    {2500, CoinTier::Platinum, 5},
    {1250, CoinTier::Obsidian, 3},
    {1000, CoinTier::Gold, 6},
    {500, CoinTier::Obsidian, 2},
    {500, CoinTier::Platinum, 4},
    {500, CoinTier::Gold, 5},
    {250, CoinTier::Platinum, 3},
    {125, CoinTier::Obsidian, 1},
    {100, CoinTier::Platinum, 2},
    {100, CoinTier::Gold, 4},
    {50, CoinTier::Gold, 3},
    {25, CoinTier::Obsidian, 0},
    {25, CoinTier::Platinum, 1},
    {20, CoinTier::Gold, 2},
    {5, CoinTier::Platinum, 0},
    {5, CoinTier::Gold, 1},
    {1, CoinTier::Gold, 0},
};

// How many tiers there are.
constexpr std::size_t kTierCount = 3;

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

float obsidian_share(int cycle, int streak) {
    // No obsidian before the second challenge mode.
    if (cycle < kFirstCycle) {
        return 0.0f;
    }

    // No streak yet counts as x1, past the top as the top.
    const int at = streak < 1 ? 1 : (streak > kTopStreak ? kTopStreak : streak);
    const float least = by_run(kObsidianLeast, cycle);
    const float most = by_run(kObsidianMost, cycle);
    const float t = static_cast<float>(at - 1) / static_cast<float>(kTopStreak - 1);

    // By the square: little until the streak is long.
    return least + (most - least) * t * t;
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
        const auto at = static_cast<std::size_t>(n);
        count += gold[at] + platinum[at] + obsidian[at];
    }

    return count;
}

std::int32_t CoinPlan::worth() const {
    std::int32_t total = 0;

    // A platinum coin is worth kPlatinumTimes its class, an obsidian one kObsidianTimes.
    for (int n = 0; n < kCoinKinds; ++n) {
        const auto at = static_cast<std::size_t>(n);
        total += kCoinValues[at]
                 * (gold[at] + kPlatinumTimes * platinum[at] + kObsidianTimes * obsidian[at]);
    }

    return total;
}

CoinPlan split(std::int32_t total, float share, float obsidian_share, int limit) {
    CoinPlan plan;

    // No drop: no coins.
    if (total <= 0) {
        return plan;
    }

    // `fraction` of the drop, clamped to 0..1 and rounded (0.65 is no exact float), down to a
    // multiple of `step` (the kind's least coin).
    const auto part_of = [total](float fraction, std::int32_t step) {
        const float clamped = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
        const auto wanted = std::llround(static_cast<double>(total) * clamped);
        return static_cast<std::int32_t>(wanted / step) * step;
    };

    // Obsidian's part first, then platinum's from what is left, the rest gold.
    std::array<std::int32_t, kTierCount> parts{};
    parts[2] = part_of(obsidian_share, kObsidianTimes);
    const std::int32_t platinum = part_of(share, kPlatinumTimes);
    const std::int32_t left = total - parts[2];
    parts[1] = platinum < left ? platinum : left / kPlatinumTimes * kPlatinumTimes;
    parts[0] = total - parts[2] - parts[1];
    std::array<bool, kTierCount> taken{};
    int room = limit;

    // The most valuable coin first, each from its own part, while there is room. Under a limit, a
    // coin leaves a place free for each rarer kind that has a coin's worth and no coin yet.
    for (const CoinKind& coin : kSplitOrder) {
        const auto tier = static_cast<std::size_t>(coin.tier);
        std::int32_t& part = parts[tier];
        int count = part / coin.worth;
        int fits = room;

        // The places the rarer kinds keep.
        for (std::size_t rarer = tier + 1; rarer < kTierCount; ++rarer) {
            const std::int32_t least = tier_times(static_cast<CoinTier>(rarer));

            // A rarer kind with a coin's worth and no coin yet.
            if (limit >= 0 && !taken[rarer] && parts[rarer] >= least) {
                --fits;
            }
        }

        // Under a limit, only what still fits.
        if (limit >= 0 && count > fits) {
            count = fits > 0 ? fits : 0;
        }

        part -= count * coin.worth;
        room -= count;

        // The kind has a coin now.
        if (count > 0) {
            taken[tier] = true;
        }

        auto& counts = coin.tier == CoinTier::Obsidian
                           ? plan.obsidian
                           : (coin.tier == CoinTier::Platinum ? plan.platinum : plan.gold);
        counts[static_cast<std::size_t>(coin.kind)] += count;
    }

    plan.leftover = parts[0] + parts[1] + parts[2];
    return plan;
}

}  // namespace ng_plus_plus
