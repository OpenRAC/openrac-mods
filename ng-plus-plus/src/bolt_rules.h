// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++'s bolt rules, apart from the game (README.md, "Platinum and obsidian bolts and bolt
// scaling"): how much more a drop pays in each challenge mode run, how much more a tough enemy
// pays, how much of a drop comes as platinum and obsidian bolts, and the split of a drop into gold,
// platinum and obsidian coins.
//
// The game's coins are seven classes worth 1, 5, 20, 50, 100, 500 and 1,000 bolts (OpenRAC's
// openrac/games/rac2.h). A platinum coin is one of the same seven drawn as platinum and worth five
// times as much: 5, 25, 100, 250, 500, 2,500 and 5,000. An obsidian coin is one drawn as obsidian
// and worth 25 times: 25, 125, 500, 1,250, 2,500, 12,500 and 25,000.
//
// Nothing here reads or writes game memory, so tests/bolt_rules_test.cpp runs it alone.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace ng_plus_plus {

// The coin classes: the first (worth 1) and how many.
inline constexpr int kCoinClass = 0x0D;
inline constexpr int kCoinKinds = 7;

// What each coin class is worth in gold, from the first class on.
inline constexpr std::array<std::int32_t, kCoinKinds> kCoinValues = {1, 5, 20, 50, 100, 500, 1000};

// A platinum coin is worth this many times its gold class.
inline constexpr std::int32_t kPlatinumTimes = 5;

// An obsidian coin is worth this many times its gold class.
inline constexpr std::int32_t kObsidianTimes = 25;

// The three kinds of coin, the commonest first.
enum class CoinTier : std::uint8_t {
    Gold,
    Platinum,
    Obsidian,
};

// Returns how many times its gold class a coin of `tier` is worth.
constexpr std::int32_t tier_times(CoinTier tier) {
    return tier == CoinTier::Obsidian ? kObsidianTimes
                                      : (tier == CoinTier::Platinum ? kPlatinumTimes : 1);
}

// The kill streak's top, the game's x20 (rac2-decomp: g_bolt_multiplier, D_001A7A32).
inline constexpr int kTopStreak = 20;

// The largest drop the mod makes, far inside an s32.
inline constexpr std::int32_t kMostDrop = 1'000'000;

// Every bolt reward (drops, prizes, mission pay, trades) pays this many times the run scale that
// follows the tiers' prices (0.6.16); prices of sellers do not take it (rewards.cpp).
inline constexpr float kBoltBoost = 1.5f;

// Generous bolt scaling pays this many times the normal run scale.
inline constexpr float kGenerous = 1.5f;

// An enemy's own bolts up to this pay no more for its toughness; past it the square root of its
// bolts over this, at most kToughMost (twice: an enemy with 80 bolts or more). The game's drop
// already grows with an enemy's bolts, up to its coin limit; past that limit (three coins for a
// kill: at most 3,000) a bigger enemy pays the same, so toughness keeps a big enemy worth more.
inline constexpr std::int32_t kToughBase = 20;
inline constexpr float kToughMost = 2.0f;

// The most a drop is ever scaled, run and toughness together: normal's x9 times x2 (generous goes
// no further).
inline constexpr float kMostScale = 18.0f;

// A drop of the mod's may hold this many more coins per x1 of the enemy's toughness than the game
// allows it (three for a kill), rounded: 2 more for a crate or small enemy, up to 4 for the
// toughest, so a bigger enemy's drop is a bigger shower of coins.
inline constexpr float kExtraCoins = 2.0f;

// The most coins one burst makes, whatever the game's coin limit and the toughness allow (the game's
// own three plus up to four more is seven, and a test drop of 15,000 bolts at a x20 streak splits
// into 21; nothing past this is made).
inline constexpr int kBurstMost = 24;

// The most bolt pickups alive at once when the port cannot say how many moby slots are free: each
// is a moby of the game's table, and past the table's end the game cannot make one.
inline constexpr int kLivePickupsMost = 96;

// A burst with no cap of its own: the pool's free slots are its only limit.
inline constexpr int kNoBurstCap = 1 << 20;

// The "bolt_scaling" setting.
enum class BoltScaling : std::uint8_t {
    Off,       // the game's drops as they are
    Normal,    // the run scale and platinum bolts
    Generous,  // the run scale times kGenerous
};

// Returns the setting of the manifest's value `value` ("off", "normal", "generous"); Normal for
// one it does not know.
BoltScaling bolt_scaling_of(std::string_view value);

// Returns how much more every drop pays in challenge mode cycle `cycle` (the save's counter: 0 the
// first game, 1 the first challenge mode): 1.0 up to cycle 1, then in step with the tiers' prices
// (x2.25, x3.75, x6, x9). Generous is half as much again; Off is 1.0.
float run_scale(int cycle, BoltScaling scaling);

// Returns how much more an enemy whose own bolts (its base drop, before the kill streak) are
// `base` pays: 1.0 up to kToughBase, then the square root of base over it, at most kToughMost.
// The game already pays a big enemy more in proportion, up to its coin limit; this is on top of it.
float toughness(std::int32_t base);

// Returns how much more a drop pays in cycle `cycle` from an enemy whose own bolts are `base`: the
// run scale times its toughness, at most kMostScale; 1.0 before cycle 2 and with scaling off. It
// scales what the game's own burst paid in coins (its streak, doubling, pacing and coin limit
// done), never the amount the game is asked for, so the streak never multiplies the scale.
float drop_scale(int cycle, BoltScaling scaling, std::int32_t base);

// Returns how many more coins than the game's limit a drop of an enemy of `toughness` may hold:
// kExtraCoins per x1 of it, rounded.
int extra_coins(float toughness);

// Returns how many coins a burst may make: the game's own limit `limit` (negative for none), at most
// `burst_most`, and at most the room left for pickups (`live_most` less the `live` ones alive, 0
// when none). Never negative. What the budget leaves out of a drop is paid at once, so a smaller
// budget folds the same bolts into fewer, bigger coins (split takes the most valuable first).
int coin_budget(int limit, int live, int burst_most, int live_most);

// Returns the fraction of a drop paid in platinum in cycle `cycle` at a kill streak of `streak`
// (1 to 20; 0 counts as 1): none up to cycle 1, then from the run's least at x1 to its most at x20.
float platinum_share(int cycle, int streak);

// Returns the fraction of a drop paid in obsidian in cycle `cycle` at a kill streak of `streak`
// (1 to 20; 0 counts as 1): none before cycle 2, then hints that grow with each run and, faster,
// with the streak (by the square of how far along the streak is), so most come at high streaks.
// Obsidian takes its part of the drop before platinum's.
float obsidian_share(int cycle, int streak);

// Returns `amount` times `scale`, rounded, at most kMostDrop (0 stays 0, a negative 0).
std::int32_t scale_amount(std::int32_t amount, float scale);

// A drop as coins: how many of each class are gold, platinum and obsidian, and what did not fit.
struct CoinPlan {
    std::array<int, kCoinKinds> gold{};      // Gold coins by class, from the 1-bolt one.
    std::array<int, kCoinKinds> platinum{};  // Platinum coins by class, from the 5-bolt one.
    std::array<int, kCoinKinds> obsidian{};  // Obsidian coins by class, from the 25-bolt one.
    std::int32_t leftover = 0;               // Bolts no coin holds (only under a coin limit).

    // Returns how many coins the plan makes.
    int coins() const;

    // Returns what the plan's coins are worth together.
    std::int32_t worth() const;
};

// Splits `total` bolts into coins: `obsidian_share` of it (rounded, then down to a multiple of 25)
// as obsidian, then `share` of it (rounded, down to a multiple of 5, at most what obsidian left) as
// platinum, the rest gold, the most valuable coin first of any kind, as the game's own greedy
// split. With `limit` at 0 or more, at most that many coins; what does not fit is leftover. Under
// a limit, each rarer kind with a coin's worth keeps a place until it has a coin, so the big
// common coins can't push it out of a small burst.
CoinPlan split(std::int32_t total, float share, float obsidian_share, int limit);

// The split with no obsidian.
inline CoinPlan split(std::int32_t total, float share, int limit) {
    return split(total, share, 0.0f, limit);
}

}  // namespace ng_plus_plus
