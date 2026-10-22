// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The names of the PlayStation 2's buttons, as the manifest's settings spell them, and the
// other way round.

#include "ps2_pad.hpp"

namespace dualsense {
namespace {

struct NamedButton {
    const char* name;
    Ps2Button button;
};

// The order the manifest lists them in.
constexpr NamedButton kNamedButtons[] = {
    {"cross", kCross},   {"square", kSquare},   {"circle", kCircle}, {"triangle", kTriangle},
    {"l1", kL1},         {"r1", kR1},           {"l2", kL2},        {"r2", kR2},
    {"select", kSelect}, {"start", kStart},     {"l3", kL3},        {"r3", kR3},
};

}  // namespace

const char* button_name(Ps2Button button) {
    for (const NamedButton& named : kNamedButtons) {
        if (named.button == button) {
            return named.name;
        }
    }
    return "none";
}

Ps2Button button_from_name(const char* name, bool* found) {
    if (name == nullptr) {
        if (found != nullptr) {
            *found = false;
        }
        return kSelect;
    }
    for (const NamedButton& named : kNamedButtons) {
        const char* a = named.name;
        const char* b = name;
        bool same = true;
        while (*a != '\0' && *b != '\0') {
            const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
            const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;
            if (ca != cb) {
                same = false;
                break;
            }
            ++a;
            ++b;
        }
        if (same && *a == '\0' && *b == '\0') {
            if (found != nullptr) {
                *found = true;
            }
            return named.button;
        }
    }
    if (found != nullptr) {
        *found = false;
    }
    return kSelect;
}

}  // namespace dualsense
