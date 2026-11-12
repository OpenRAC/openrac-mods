// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// What every game's part of the cheats mod shares (README.md): which cheats are on, the bolt
// multiplier's steps, the bolts a pickup adds and the HUD's status line. No game memory and no
// host, so the tests run it alone (tests/cheats_test.cpp).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cheats {

// The bolt multiplier's steps: the manifest's choices and the pause page's list, in that order.
inline constexpr std::array<int, 6> k_multipliers = {1, 2, 3, 5, 10, 100};

// The bolts the pause page's gift button adds.
inline constexpr int32_t k_bolt_gift = 10000;

// The cheats that are on now.
struct State {
    bool invincible = false;
    bool infinite_ammo = false;
    bool infinite_air = false;
    // One of k_multipliers; 1 is the game's own count.
    int multiplier = 1;
};

// Returns the index in k_multipliers of `value`, 0 (x1) for a value that is not a step.
int multiplier_index(int value);

// Returns the step at `index` of k_multipliers, 1 for an index out of range.
int multiplier_at(int index);

// Returns the multiplier the launcher's choice names ("1" ... "100"), 1 for any other text.
int parse_multiplier(const std::string& text);

// Returns the steps as the pause page shows them: "x1", "x2", ...
std::vector<std::string> multiplier_names();

// Returns the bolts to add after a pickup that gave `gained`, so that it counts `multiplier`
// times, without taking the count past INT32_MAX from `have` (the count after the pickup); 0 when
// there is nothing to add.
int32_t extra_bolts(int32_t gained, int multiplier, int32_t have);

// Returns the bolts to add for a gift of `gift` to `have`, without passing INT32_MAX.
int32_t gift_bolts(int32_t gift, int32_t have);

// Returns the HUD's line naming the cheats that are on, "" when none is. Only characters the
// game's HUD font draws.
std::string status_line(const State& state);

}  // namespace cheats
