// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// What the player means on a row of the pause page (pad_intent.h).
#include "pad_intent.h"

#include <cstdlib>

namespace cheats {

namespace {

// The buttons' bits in the report's word (a bit clear: held), the console pad's order.
constexpr unsigned kRight = 1u << 5;
constexpr unsigned kLeft = 1u << 7;
constexpr unsigned kCross = 1u << 14;

// The left stick's bytes, its centre, and how far it leans before the game takes it as a
// direction (the game's own pad reading: 0x30 from the centre).
constexpr int kLeftX = 4;
constexpr int kLeftY = 5;
constexpr int kCentre = 0x7F;
constexpr int kLean = 0x30;

}  // namespace

Intent read_intent(const std::uint8_t* report, int size) {
    // Too short to hold the buttons.
    if (report == nullptr || size < 2) {
        return Intent::Unknown;
    }

    const unsigned held =
        ~(static_cast<unsigned>(report[0]) | static_cast<unsigned>(report[1]) << 8);

    // Cross, whatever else is held.
    if ((held & kCross) != 0) {
        return Intent::Choose;
    }

    // The d-pad's Right.
    if ((held & kRight) != 0) {
        return Intent::Next;
    }

    // The d-pad's Left.
    if ((held & kLeft) != 0) {
        return Intent::Previous;
    }

    // No sticks in the report.
    if (size <= kLeftY) {
        return Intent::Unknown;
    }

    const int dx = static_cast<int>(report[kLeftX]) - kCentre;
    const int dy = static_cast<int>(report[kLeftY]) - kCentre;

    // The stick at rest, as far as the game is concerned.
    if (std::abs(dx) < kLean && std::abs(dy) < kLean) {
        return Intent::Unknown;
    }

    // More sideways than up or down: a step; else the cursor moving.
    if (std::abs(dx) > std::abs(dy)) {
        return dx > 0 ? Intent::Next : Intent::Previous;
    }

    return Intent::Moving;
}

std::optional<int> step_choice(Intent intent, int current, int count, int said) {
    // No choices to step through.
    if (count <= 0) {
        return std::nullopt;
    }

    // What the player means.
    switch (intent) {
        case Intent::Moving:
            return std::nullopt;
        case Intent::Choose:
        case Intent::Next:
            return (current + 1) % count;
        case Intent::Previous:
            return (current - 1 + count) % count;
        case Intent::Unknown:
        default:
            return said;
    }
}

bool chooses(Intent intent) {
    return intent == Intent::Choose || intent == Intent::Unknown;
}

}  // namespace cheats
