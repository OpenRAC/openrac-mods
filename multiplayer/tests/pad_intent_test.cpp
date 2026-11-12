/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The pages' events as the player meant them (pad_intent.hpp): what the pad's report says, and the
 * event a page is given for each event the port sends with it.
 */
#include <array>
#include <cstdint>
#include <cstdio>

#include "pad_intent.hpp"

namespace {

int failures = 0;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

constexpr unsigned kRight = 1u << 5;
constexpr unsigned kLeft = 1u << 7;
constexpr unsigned kCross = 1u << 14;

// A report: `held` the buttons held (the word has them clear), the left stick at (x, y).
mp::Intent intent(unsigned held, int x, int y, int size = 18) {
    std::array<std::uint8_t, 18> r{};
    r.fill(0x7F);
    const unsigned word = ~held & 0xFFFFu;
    r[0] = static_cast<std::uint8_t>(word & 0xFF);
    r[1] = static_cast<std::uint8_t>(word >> 8);
    r[4] = static_cast<std::uint8_t>(x);
    r[5] = static_cast<std::uint8_t>(y);
    return mp::read_intent(r.data(), size);
}

openrac_ui_event event(std::uint32_t kind, int item, int value) {
    openrac_ui_event e{};
    e.size = sizeof e;
    e.kind = kind;
    e.item = item;
    e.value = value;
    return e;
}

void the_report() {
    using mp::Intent;
    // Cross first, whatever the stick; the d-pad.
    CHECK(intent(kCross, 0x7F, 0xFF) == Intent::Choose);
    CHECK(intent(kRight, 0x7F, 0x7F) == Intent::Next);
    CHECK(intent(kLeft, 0x7F, 0x7F) == Intent::Previous);
    // The stick: at rest within the game's lean; down a little to the side, the cursor moving;
    // sideways, a step.
    CHECK(intent(0, 0x90, 0x60) == Intent::Unknown);
    CHECK(intent(0, 0xB0, 0xFF) == Intent::Moving);
    CHECK(intent(0, 0x50, 0x00) == Intent::Moving);
    CHECK(intent(0, 0xFF, 0x90) == Intent::Next);
    CHECK(intent(0, 0x00, 0x60) == Intent::Previous);
    // Too short for the sticks, or the buttons.
    CHECK(intent(0, 0xFF, 0x7F, 4) == Intent::Unknown);
    CHECK(intent(kCross, 0x7F, 0x7F, 1) == Intent::Unknown);
    CHECK(mp::read_intent(nullptr, 18) == Intent::Unknown);
}

void the_events() {
    using mp::Intent;
    // A choice stepped by the cursor moving: a tick instead. By Left, Right or Cross: as it is.
    const openrac_ui_event change = event(OPENRAC_UI_CHANGE, 2, 1);
    openrac_ui_event e = mp::as_meant(Intent::Moving, change);
    CHECK(e.kind == OPENRAC_UI_FRAME && e.item == -1 && e.value == 0 && e.size == sizeof e);
    CHECK(mp::as_meant(Intent::Previous, change).kind == OPENRAC_UI_CHANGE);
    CHECK(mp::as_meant(Intent::Choose, change).value == 1);

    // A button: chosen by Cross, or a press the pad does not show; not by the stick or the d-pad.
    const openrac_ui_event choose = event(OPENRAC_UI_CHOOSE, 5, 0);
    CHECK(mp::as_meant(Intent::Choose, choose).kind == OPENRAC_UI_CHOOSE);
    CHECK(mp::as_meant(Intent::Unknown, choose).kind == OPENRAC_UI_CHOOSE);
    CHECK(mp::as_meant(Intent::Moving, choose).kind == OPENRAC_UI_FRAME);
    CHECK(mp::as_meant(Intent::Next, choose).kind == OPENRAC_UI_FRAME);
    CHECK(mp::as_meant(Intent::Previous, choose).kind == OPENRAC_UI_FRAME);

    // A question's Yes: Cross's only; No as it is.
    const openrac_ui_event yes = event(OPENRAC_UI_ANSWER, -1, 1);
    CHECK(mp::as_meant(Intent::Choose, yes).value == 1);
    CHECK(
        mp::as_meant(Intent::Moving, yes).value == 0
        && mp::as_meant(Intent::Moving, yes).kind == OPENRAC_UI_ANSWER
    );
    CHECK(mp::as_meant(Intent::Next, yes).value == 0);
    CHECK(mp::as_meant(Intent::Moving, event(OPENRAC_UI_ANSWER, -1, 0)).value == 0);

    // Everything else as it is.
    for (const std::uint32_t kind :
         {OPENRAC_UI_SHOW, OPENRAC_UI_FRAME, OPENRAC_UI_BACK, OPENRAC_UI_HIDE}) {
        CHECK(mp::as_meant(Intent::Moving, event(kind, 3, 0)).kind == kind);
    }
}

}  // namespace

int main() {
    the_report();
    the_events();

    if (failures == 0) {
        std::puts("pad intent: ok");
    }

    return failures == 0 ? 0 : 1;
}
