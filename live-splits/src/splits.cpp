/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The live-splits mod's state machine (splits.hpp): the game's levels in, LiveSplit's commands
 * out.
 *
 * How it works: the mod tells it when a planet is reached and when one is left. It remembers the
 * planets the run has been to (one bit each), so a planet seen again means either the level was
 * loaded again (Ratchet died: nothing happens) or a new run began (the player went back to the
 * title). It never touches the game or the network, so splits_test.cpp runs it alone.
 */
#include "splits.hpp"

#include <cstdio>

namespace livesplit {

namespace {

/* The size of the buffer a formatted time is written to: room for "hhhh:mm.ss" and more (assumed). */
constexpr std::size_t kTimeTextSize = 32;

/* The hundredths of a second in a minute, and in a second. */
constexpr long long kHundredthsPerMinute = 6000;
constexpr long long kHundredthsPerSecond = 100;

/* The seconds in a minute. */
constexpr long long kSecondsPerMinute = 60;

}  // namespace

std::string format_time(double seconds) {
    long long hundredths = static_cast<long long>(seconds * 100.0 + 0.5);

    // A negative time cannot be shown; LiveSplit's parser reads none.
    if (hundredths < 0) {
        hundredths = 0;
    }

    char text[kTimeTextSize];
    std::snprintf(
        text,
        sizeof text,
        "%lld:%02lld.%02lld",
        hundredths / kHundredthsPerMinute,
        (hundredths / kHundredthsPerSecond) % kSecondsPerMinute,
        hundredths % kHundredthsPerSecond
    );
    return text;
}

std::vector<Action> Splits::reached(int planet) {
    std::vector<Action> actions;
    began_ = false;

    // Not a planet (none, or a level the mod has no name for): nothing to do.
    if (planet < 0 || planet >= kPlanetCount) {
        return actions;
    }

    // The same planet again: the level's gameplay was loaded again, which the game does when
    // Ratchet dies (the level is loaded anew, the planet does not change).
    if (planet == reached_) {
        return actions;
    }

    const bool again = (visited_ & (1u << planet)) != 0;
    reached_ = planet;

    // A planet the run has already been to: the player went back to the title and began a new
    // game, or loaded a save. The run begins again.
    if (again) {
        visited_ = 1u << planet;
        split_at_ = -1;
        finished_ = false;
        started_ = false;
    } else {
        visited_ |= 1u << planet;
    }

    // The first planet of the run: the timer begins here, and this is not a split.
    if (!started_) {
        started_ = true;
        began_ = true;

        // Only when the mod owns the timer does it reset and start it.
        if (who_ == StartWho::kMod) {
            actions.push_back(Action::kReset);
            actions.push_back(Action::kStart);
        }

        return actions;
    }

    // Splitting when a planet is reached: the planet just reached ends the segment before it.
    if (when_ == SplitWhen::kReached) {
        actions.push_back(Action::kSplit);
    }

    return actions;
}

std::vector<Action> Splits::left(int planet) {
    std::vector<Action> actions;
    began_ = false;

    // Not a planet: nothing to do.
    if (planet < 0 || planet >= kPlanetCount) {
        return actions;
    }

    // No run has begun, so there is nothing to split.
    if (!started_) {
        return actions;
    }

    // Splitting when a planet is left: the planet just left ends its segment.
    if (when_ == SplitWhen::kLeft) {
        // The same planet again: the player went back to it and left it once more.
        if (planet == split_at_) {
            return actions;
        }

        split_at_ = planet;
        actions.push_back(Action::kSplit);
        return actions;
    }

    // The planets are split when they are reached: the run ends when the game leaves the last one,
    // Veldin, where the last boss is.
    if (planet == kLastPlanet && !finished_) {
        finished_ = true;
        actions.push_back(Action::kSplit);
    }

    return actions;
}

}  // namespace livesplit
