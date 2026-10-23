// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The names of the PlayStation 2's buttons, as the manifest's settings spell them, and the
// other way round. The table is searched in order, and a name matches without regard to the case
// of its letters, so "Circle" in a settings file is the same as "circle".

#include "ps2_pad.hpp"

namespace dualsense {
namespace {

struct NamedButton {
    const char* name;
    Ps2Button button;
};

// The order the manifest lists them in.
constexpr NamedButton kNamedButtons[] = {
    {"cross", kCross},
    {"square", kSquare},
    {"circle", kCircle},
    {"triangle", kTriangle},
    {"l1", kL1},
    {"r1", kR1},
    {"l2", kL2},
    {"r2", kR2},
    {"select", kSelect},
    {"start", kStart},
    {"l3", kL3},
    {"r3", kR3},
};

}  // namespace

// The name of a button, or "none" for one the table does not list.
const char* button_name(Ps2Button button) {
    // Each listed button is compared with the one asked for.
    for (const NamedButton& named : kNamedButtons) {
        // This is the button asked for.
        if (named.button == button) {
            return named.name;
        }
    }

    return "none";
}

// The button a name spells, ignoring the case of its letters. Sets `*found` (when given) to
// whether the name is in the table; returns kSelect when it is not.
Ps2Button button_from_name(const char* name, bool* found) {
    // A missing name names nothing.
    if (name == nullptr) {
        // The caller may not care whether the name was found.
        if (found != nullptr) {
            *found = false;
        }

        return kSelect;
    }

    // Each listed name is compared with the one given.
    for (const NamedButton& named : kNamedButtons) {
        const char* a = named.name;
        const char* b = name;
        bool same = true;

        // The two names are compared letter by letter, in lower case, until one of them ends.
        while (*a != '\0' && *b != '\0') {
            const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
            const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;

            // A letter differs, so this is not the name.
            if (ca != cb) {
                same = false;
                break;
            }

            ++a;
            ++b;
        }

        // Every letter matched and both names ended together, so this is the button.
        if (same && *a == '\0' && *b == '\0') {
            // The caller may not care whether the name was found.
            if (found != nullptr) {
                *found = true;
            }

            return named.button;
        }
    }

    // No listed name matched.
    if (found != nullptr) {
        *found = false;
    }

    return kSelect;
}

}  // namespace dualsense
