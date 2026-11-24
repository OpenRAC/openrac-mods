// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The shared part of the cheats mod (cheats.h).
#include "cheats.h"

#include <cstdint>
#include <limits>

namespace cheats {

int multiplier_index(int value) {
    // Each step, until the one that is `value`.
    for (std::size_t i = 0; i < k_multipliers.size(); ++i) {
        // This step is the value.
        if (k_multipliers[i] == value) {
            return static_cast<int>(i);
        }
    }

    return 0;
}

int multiplier_at(int index) {
    // An index out of the steps: the game's own count.
    if (index < 0 || index >= static_cast<int>(k_multipliers.size())) {
        return 1;
    }

    return k_multipliers[static_cast<std::size_t>(index)];
}

int parse_multiplier(const std::string& text) {
    // Each step, until the one whose number is the text.
    for (int m : k_multipliers) {
        // The text is this step's number.
        if (text == std::to_string(m)) {
            return m;
        }
    }

    return 1;
}

std::vector<std::string> multiplier_names() {
    std::vector<std::string> names;

    // Each step as "x" and its number.
    for (int m : k_multipliers) {
        names.push_back("x" + std::to_string(m));
    }

    return names;
}

int32_t extra_bolts(int32_t gained, int multiplier, int32_t have) {
    // Nothing picked up, or no multiplier: nothing to add.
    if (gained <= 0 || multiplier <= 1) {
        return 0;
    }

    const int64_t extra = static_cast<int64_t>(gained) * (multiplier - 1);
    const int64_t room = static_cast<int64_t>(std::numeric_limits<int32_t>::max()) - have;

    // The count is at its most already.
    if (room <= 0) {
        return 0;
    }

    return static_cast<int32_t>(extra < room ? extra : room);
}

int32_t gift_bolts(int32_t gift, int32_t have) {
    const int64_t room = static_cast<int64_t>(std::numeric_limits<int32_t>::max()) - have;

    // No gift, or the count is at its most already.
    if (gift <= 0 || room <= 0) {
        return 0;
    }

    return static_cast<int32_t>(gift < room ? gift : room);
}

std::string status_line(const State& state) {
    std::string line;

    // Each cheat that is on, after a comma when another came before it.
    auto add = [&line](const std::string& part) {
        line += line.empty() ? "Cheats: " + part : ", " + part;
    };

    // Ratchet takes no hit.
    if (state.invincible) {
        add("invincible");
    }

    // Shots take no ammo.
    if (state.infinite_ammo) {
        add("infinite ammo");
    }

    // Swimming takes no air.
    if (state.infinite_air) {
        add("infinite oxygen");
    }

    // Bolts count more than once.
    if (state.multiplier > 1) {
        add("bolts x" + std::to_string(state.multiplier));
    }

    return line;
}

}  // namespace cheats
