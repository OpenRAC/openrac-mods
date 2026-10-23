// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The vibration, and the vibrations the game's events make (rumble.hpp). A Rumble holds the
// steps of one vibration and moves from one to the next as the time the caller passes in grows;
// rumble_for() builds the steps for each kind of event, scaled by the strength setting.

#include "rumble.hpp"

#include <cstring>
#include <utility>

namespace dualsense {
namespace {

// A strength as a percentage of the full one.
std::uint16_t scaled(int percent) {
    // No strength at all is no vibration.
    if (percent <= 0) {
        return 0;
    }

    // A full percentage (or more) is the full strength.
    if (percent >= 100) {
        return kFull;
    }

    return static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(kFull) * static_cast<std::uint32_t>(percent) / 100u
    );
}

// Whether the event kind is the named one; a null kind is none of them.
bool is(const char* kind, const char* name) {
    return kind != nullptr && std::strcmp(kind, name) == 0;
}

}  // namespace

// Replaces the vibration with `steps`; the first step begins at the next update().
void Rumble::play(std::vector<RumbleStep> steps) {
    steps_ = std::move(steps);
    index_ = 0;
    heavy_ = 0;
    light_ = 0;

    // There is nothing to play, so the motors stay still.
    if (steps_.empty()) {
        until_ = 0;
        return;
    }

    started_ = 0;
    until_ = 0;  // the first step begins at the first update
    heavy_ = steps_[0].heavy;
    light_ = steps_[0].light;
}

// Drops the steps left and lets the motors go.
void Rumble::stop() {
    steps_.clear();
    index_ = 0;
    heavy_ = 0;
    light_ = 0;
    until_ = 0;
}

// Moves the vibration on to the time `now`; returns true while it is still playing.
bool Rumble::update(std::uint32_t now) {
    // Nothing is playing, so the motors are at rest.
    if (!playing()) {
        heavy_ = 0;
        light_ = 0;
        return false;
    }

    // play() was called and no update has seen it yet.
    if (until_ == 0) {
        // The first update after play() begins the first step.
        started_ = now;
        until_ = now + steps_[index_].ms;
        heavy_ = steps_[index_].heavy;
        light_ = steps_[index_].light;
        return true;
    }

    // The step has time left, so it goes on.
    if (now < until_) {
        return true;
    }

    // The step is over: the next one begins, or the vibration ends.
    ++index_;

    // That was the last step: the vibration ends and the motors are let go.
    if (!playing()) {
        heavy_ = 0;
        light_ = 0;
        until_ = 0;
        return false;
    }

    started_ = now;
    until_ = now + steps_[index_].ms;
    heavy_ = steps_[index_].heavy;
    light_ = steps_[index_].light;
    return true;
}

// The steps for an event, one block per kind; an unknown kind makes no steps.
std::vector<RumbleStep> rumble_for(const char* kind, int strength, std::uint32_t fall_ms) {
    std::vector<RumbleStep> steps;

    // The player was hit.
    if (is(kind, "damage")) {
        // A crack from the light motor, then the heavy motor takes over.
        steps.push_back({0, scaled(strength), 60});
        steps.push_back({scaled(strength), 0, 140});
        return steps;
    }

    // The player hit something.
    if (is(kind, "hit")) {
        steps.push_back({scaled(strength / 2), 0, 90});
        return steps;
    }

    // Something broke.
    if (is(kind, "break")) {
        steps.push_back({scaled(strength / 3), scaled(strength / 6), 70});
        return steps;
    }

    // Something was picked up.
    if (is(kind, "pickup")) {
        steps.push_back({0, scaled(strength / 2), 50});
        return steps;
    }

    // A gold bolt was found.
    if (is(kind, "gold")) {
        steps.push_back({0, scaled(strength / 2), 70});
        steps.push_back({0, scaled(strength), 180});
        return steps;
    }

    // The player landed.
    if (is(kind, "land")) {
        // The longer the fall, the harder the landing.
        int percent = strength / 2;

        // A fall of over 600 ms lands at full strength, one of over 300 ms at three quarters
        // (assumed: tuned by feel).
        if (fall_ms > 600) {
            percent = strength;
        } else if (fall_ms > 300) {
            percent = (strength * 3) / 4;
        }

        steps.push_back({scaled(percent), 0, 120});
        return steps;
    }

    return steps;
}

}  // namespace dualsense
