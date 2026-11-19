// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// What the player means on a row of the pause page, from the pad (README.md, "The page and the
// stick"). The game turns the left stick into the pad's directions as soon as it leans past a
// third of the way, sideways or not (its pad reading, func_002181F0 in games/rac1
// src/game/system/pad.c); the pause page's rows step their value on Left and Right, and the port
// steps a row's value on (and chooses a button for) any of Cross, Left and Right, always forward.
// So the stick pushed down a little off straight steps the row the cursor arrives at. Each event of
// the page is checked against the pad's report of the tick: a stick pushed more up or down than
// sideways is the cursor moving, not a change.
#pragma once

#include <cstdint>
#include <optional>

namespace cheats {

// What the pad says the player does.
enum class Intent {
    // Nothing on the pad says (a mouse, a touch, a key another mod presses): the port's word holds.
    Unknown,
    // Cross: a button chosen, a choice stepped on.
    Choose,
    // Right on the d-pad, or the left stick pushed more right than up or down.
    Next,
    // Left on the d-pad, or the left stick pushed more left than up or down.
    Previous,
    // The left stick pushed more up or down than sideways: the cursor moving.
    Moving,
};

// Returns what the pad's report says (the report as the console's pad library gives it: the
// buttons' 16-bit word, little-endian, a bit clear for a button held; then the right stick's x and
// y, then the left's, 0x80 at rest).
Intent read_intent(const std::uint8_t* report, int size);

// Returns a choice's new index for a change the port reports (`said`), the choice at `current` of
// `count`: stepped the way the player means, the port's when the pad does not say, nothing when it
// is the cursor moving.
std::optional<int> step_choice(Intent intent, int current, int count, int said);

// Returns whether a button's choice the port reports is the player's: not Left, Right or the stick.
bool chooses(Intent intent);

}  // namespace cheats
