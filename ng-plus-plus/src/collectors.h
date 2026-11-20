// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The collectors pay more each new game (README.md): Tabora's mystic, who buys crystals, and
// Grelbin's trader, who buys moonstones, pay the run scale (bolt_rules.h: x1.5, x2.5, x4, x6 from
// the second challenge mode) times the game's price, so they keep up with the tiers' prices.
//
// The game pays them in each one's update (func_L08_00435A78 on Tabora, func_L19_0042B058's
// trader on Grelbin, NTSC-U v1.01): its dialog prints the count and the total with a text of two
// numbers (crystals 10135, moonstones 10209), and on the player's yes it adds count times the price
// (a crystal 1,000 bolts, a moonstone 3,000; ten times as much in challenge mode) to the bolts and
// sets the count to 0. Neither has a hook point, so the mod
//   - gives the dialog's text with the scaled total written in place of its second number, and
//   - watches each tick for a trade (the count down to 0 and the bolts up by at least the game's
//     pay, in the same level) and adds the rest of the scaled pay.
//
// Only the game's thread uses it.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "bolt_rules.h"
#include "game_view.h"
#include "openrac/mod.hpp"

namespace ng_plus_plus {

// The game's text ids of the two dialogs (the same on every disc).
inline constexpr std::int32_t kCrystalTradeText = 10135;
inline constexpr std::int32_t kMoonstoneTradeText = 10209;

// What one crystal and one moonstone pay in the game, and how many times that in challenge mode.
inline constexpr std::int32_t kCrystalPay = 1000;
inline constexpr std::int32_t kMoonstonePay = 3000;
inline constexpr std::int32_t kChallengePayTimes = 10;

// Returns `format` (a text of the game's with printf numbers) with its second "%d" replaced by
// `total`, or none when it has fewer than two. Pure: the dialog's one substitution.
std::optional<std::string> with_total(std::string_view format, std::int32_t total);

// Returns what `count` items at the game's `pay` each come to at `scale`, rounded, within an s32.
std::int32_t collector_total(std::int32_t count, std::int32_t pay, float scale);

// The two collectors' pay.
//
// Holds the mod and the view by reference; the mod's state owns them.
class Collectors {
public:
    // Scales the collectors of `view`'s game as `scaling` says, through `mod`.
    Collectors(openrac::Mod& mod, const GameView& view, BoltScaling scaling)
        : m_mod(mod),
          m_view(view),
          m_scaling(scaling) {}

    // Returns the dialog's text of `id` with the scaled total, or none (another text, nothing to
    // trade, or no scale in this run).
    std::optional<std::string> dialog(std::int32_t id) const;

    // Once a game tick: a trade made since the last tick gets the rest of its scaled pay.
    void tick();

private:
    // Returns the scale of this run (1.0 when the collectors pay as the game does).
    float scale() const;

    // Returns what one item of the game's `pay` is worth in this game (ten times in challenge
    // mode).
    std::int32_t pay_now(std::int32_t pay) const;

    openrac::Mod& m_mod;      // The mod; the SDK keeps it for the program.
    const GameView& m_view;   // The game; outlives this.
    BoltScaling m_scaling;    // The "bolt_scaling" setting.
    bool m_seen = false;      // The counts below were read at a tick.
    int m_level = -1;         // The level at the last tick.
    std::int32_t m_bolts = 0; // The bolts at the last tick.
    std::int32_t m_crystals = 0;    // Tabora's crystals at the last tick.
    std::int32_t m_moonstones = 0;  // Grelbin's moonstones at the last tick.
};

}  // namespace ng_plus_plus
