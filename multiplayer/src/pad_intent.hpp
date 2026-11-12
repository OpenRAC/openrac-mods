// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The pages' events as the player meant them, from the first pad's report (README.md, "The pages
// and the stick").
//
// On the pause menu's pages the game turns the left stick into Left or Right as soon as it leans a
// third of the way, sideways or not (its pad reading, func_002181F0 in games/rac1
// src/game/system/pad.c), and OpenRAC's port (up to OpenRAC/OpenRAC#459) steps a row's choice, and
// chooses a row's button or a question's Yes, on any of Cross, Left and Right. So the stick pushed
// down onto a row a little off straight could leave the party, save it, go to the host, give up a
// rescue or remove a member. Each event is checked against the pad's report of that tick: the stick
// pushed more up or down than sideways changes nothing, and only Cross (or a press the pad does not
// show: a mouse click, a touch) chooses or answers Yes.
#pragma once

#include <cstdint>
#include <cstdlib>

#include "openrac/openrac_mod.h"

namespace mp {

// What the pad says the player does.
enum class Intent {
    // Nothing on the pad says (a mouse, a touch, a key another mod presses): the port's word holds.
    Unknown,
    // Cross.
    Choose,
    // Right or Left on the d-pad, or the left stick pushed more sideways than up or down.
    Next,
    Previous,
    // The left stick pushed more up or down than sideways: the cursor moving.
    Moving,
};

// Returns what the pad's report says: the buttons' 16-bit word, little-endian, a bit clear for a
// button held (Right bit 5, Left bit 7, Cross bit 14); then the right stick's x and y, then the
// left's, 0x80 at rest. The game takes a stick as a direction 0x30 from its centre.
inline Intent read_intent(const std::uint8_t* report, int size) {
    if (report == nullptr || size < 2) {
        return Intent::Unknown;
    }

    const unsigned held =
        ~(static_cast<unsigned>(report[0]) | static_cast<unsigned>(report[1]) << 8);

    // Cross, whatever else is held; then the d-pad's Right and Left.
    if ((held & (1u << 14)) != 0) {
        return Intent::Choose;
    }

    if ((held & (1u << 5)) != 0) {
        return Intent::Next;
    }

    if ((held & (1u << 7)) != 0) {
        return Intent::Previous;
    }

    // No sticks in the report.
    if (size < 6) {
        return Intent::Unknown;
    }

    const int dx = static_cast<int>(report[4]) - 0x7F;
    const int dy = static_cast<int>(report[5]) - 0x7F;

    // At rest, as far as the game is concerned.
    if (std::abs(dx) < 0x30 && std::abs(dy) < 0x30) {
        return Intent::Unknown;
    }

    // More sideways than up or down: a step; else the cursor moving.
    if (std::abs(dx) > std::abs(dy)) {
        return dx > 0 ? Intent::Next : Intent::Previous;
    }

    return Intent::Moving;
}

// Returns the event a page is given for `e`, the player doing `intent`: a choice stepped by the
// cursor moving, or a button chosen by anything but Cross, becomes a tick's event (OPENRAC_UI_FRAME,
// which every page takes each tick and only shows itself again on); a question's Yes by anything
// but Cross becomes No. Everything else is `e` as it is.
inline openrac_ui_event as_meant(Intent intent, const openrac_ui_event& e) {
    openrac_ui_event out = e;
    const bool chosen = intent == Intent::Choose || intent == Intent::Unknown;

    // Not what the player did: a tick of the page instead.
    if ((e.kind == OPENRAC_UI_CHANGE && intent == Intent::Moving)
        || (e.kind == OPENRAC_UI_CHOOSE && !chosen)) {
        out.kind = OPENRAC_UI_FRAME;
        out.item = -1;
        out.value = 0;
    } else if (e.kind == OPENRAC_UI_ANSWER && e.value != 0 && !chosen) {
        out.value = 0;
    }

    return out;
}

}  // namespace mp
