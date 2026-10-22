// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The vibration, and the vibrations the game's events make (rumble.hpp).

#include "rumble.hpp"

#include <cstring>

namespace dualsense {
namespace {

// A strength as a percentage of the full one.
std::uint16_t scaled(int percent) {
    if (percent <= 0) {
        return 0;
    }
    if (percent >= 100) {
        return kFull;
    }
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(kFull) *
                                       static_cast<std::uint32_t>(percent) / 100u);
}

bool is(const char* kind, const char* name) {
    return kind != nullptr && std::strcmp(kind, name) == 0;
}

}  // namespace

void Rumble::play(std::vector<RumbleStep> steps) {
    steps_ = std::move(steps);
    index_ = 0;
    heavy_ = 0;
    light_ = 0;
    if (steps_.empty()) {
        until_ = 0;
        return;
    }
    started_ = 0;
    until_ = 0;  // the first step begins at the first update
    heavy_ = steps_[0].heavy;
    light_ = steps_[0].light;
}

void Rumble::stop() {
    steps_.clear();
    index_ = 0;
    heavy_ = 0;
    light_ = 0;
    until_ = 0;
}

bool Rumble::update(std::uint32_t now) {
    if (!playing()) {
        heavy_ = 0;
        light_ = 0;
        return false;
    }
    if (until_ == 0) {
        // The first update after play() begins the first step.
        started_ = now;
        until_ = now + steps_[index_].ms;
        heavy_ = steps_[index_].heavy;
        light_ = steps_[index_].light;
        return true;
    }
    if (now < until_) {
        return true;
    }
    // The step is over: the next one begins, or the vibration ends.
    ++index_;
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

std::vector<RumbleStep> rumble_for(const char* kind, int strength, std::uint32_t fall_ms) {
    std::vector<RumbleStep> steps;
    if (is(kind, "damage")) {
        // A crack from the light motor, then the heavy motor takes over.
        steps.push_back({0, scaled(strength), 60});
        steps.push_back({scaled(strength), 0, 140});
        return steps;
    }
    if (is(kind, "hit")) {
        steps.push_back({scaled(strength / 2), 0, 90});
        return steps;
    }
    if (is(kind, "break")) {
        steps.push_back({scaled(strength / 3), scaled(strength / 6), 70});
        return steps;
    }
    if (is(kind, "pickup")) {
        steps.push_back({0, scaled(strength / 2), 50});
        return steps;
    }
    if (is(kind, "gold")) {
        steps.push_back({0, scaled(strength / 2), 70});
        steps.push_back({0, scaled(strength), 180});
        return steps;
    }
    if (is(kind, "land")) {
        // The longer the fall, the harder the landing.
        int percent = strength / 2;
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
