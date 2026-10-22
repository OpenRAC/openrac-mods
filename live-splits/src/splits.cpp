/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The live-splits mod's state machine (splits.hpp): the game's levels in, LiveSplit's commands
 * out.
 */
#include "splits.hpp"

#include <cstdio>

namespace livesplit {

std::string format_time(double seconds) {
    long long hundredths = static_cast<long long>(seconds * 100.0 + 0.5);
    if (hundredths < 0) {
        hundredths = 0;
    }
    char text[32];
    std::snprintf(
        text,
        sizeof text,
        "%lld:%02lld.%02lld",
        hundredths / 6000,
        (hundredths / 100) % 60,
        hundredths % 100
    );
    return text;
}

std::vector<Action> Splits::reached(int planet) {
    std::vector<Action> actions;
    began_ = false;
    if (planet < 0 || planet >= kPlanetCount) {
        return actions;
    }
    if (planet == reached_) {
        /* The same planet again: the level's gameplay was loaded again, which the game does when
         * Ratchet dies (the level is loaded anew, the planet does not change). */
        return actions;
    }
    const bool again = (visited_ & (1u << planet)) != 0;
    reached_ = planet;
    if (again) {
        /* A planet the run has already been to: the player went back to the title and began a new
         * game, or loaded a save. The run begins again. */
        visited_ = 1u << planet;
        split_at_ = -1;
        finished_ = false;
        started_ = false;
    } else {
        visited_ |= 1u << planet;
    }
    if (!started_) {
        /* The first planet of the run: the timer begins here. */
        started_ = true;
        began_ = true;
        if (who_ == StartWho::kMod) {
            actions.push_back(Action::kReset);
            actions.push_back(Action::kStart);
        }
        return actions;
    }
    if (when_ == SplitWhen::kReached) {
        actions.push_back(Action::kSplit);
    }
    return actions;
}

std::vector<Action> Splits::left(int planet) {
    std::vector<Action> actions;
    began_ = false;
    if (planet < 0 || planet >= kPlanetCount) {
        return actions;
    }
    if (!started_) {
        return actions;
    }
    if (when_ == SplitWhen::kLeft) {
        if (planet == split_at_) {
            /* The same planet again: the player went back to it and left it once more. */
            return actions;
        }
        split_at_ = planet;
        actions.push_back(Action::kSplit);
        return actions;
    }
    /* The planets are split when they are reached: the run ends when the game leaves the last one,
     * Veldin, where the last boss is. */
    if (planet == kLastPlanet && !finished_) {
        finished_ = true;
        actions.push_back(Action::kSplit);
    }
    return actions;
}

}  // namespace livesplit
