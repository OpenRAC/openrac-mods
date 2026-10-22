// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The PlayStation 2's pad, as the port's libpad2 replacement reports it to the game
// (port/game/common/lib/pad.c): two bytes of buttons, the four stick axes, then twelve
// pressures. The buttons are active low: a button that is held reads 0.
//
// The bit order is the console's own (the port's platform/pad.h): the low byte is the first
// byte of the report and the high byte the second. The game byte-swaps the pair when it
// reads them, so this is the order the report carries, not the order the game thinks in.

#pragma once

#include <cstdint>

namespace dualsense {

// The size of the report the port's scePad2Read builds.
inline constexpr int kPadReportSize = 18;

// The buttons, in the report's order.
enum Ps2Button : std::uint16_t {
    kSelect = 1u << 0,
    kL3 = 1u << 1,
    kR3 = 1u << 2,
    kStart = 1u << 3,
    kUp = 1u << 4,
    kRight = 1u << 5,
    kDown = 1u << 6,
    kLeft = 1u << 7,
    kL2 = 1u << 8,
    kR2 = 1u << 9,
    kL1 = 1u << 10,
    kR1 = 1u << 11,
    kTriangle = 1u << 12,
    kCircle = 1u << 13,
    kCross = 1u << 14,
    kSquare = 1u << 15,
};

// The two bytes of buttons in the report: the low byte first.
inline constexpr int kButtonBytes = 2;

// A button of the report, read out of its two bytes.
inline std::uint16_t read_buttons(const std::uint8_t* report) {
    return static_cast<std::uint16_t>(report[0]) | static_cast<std::uint16_t>(report[1]) << 8;
}

// Presses a button in the report: the bit is cleared, the button is active low.
// Returns false when the report is too short to carry the buttons.
inline bool press_button(std::uint8_t* report, int size, Ps2Button button) {
    if (size < kButtonBytes) {
        return false;
    }
    const auto bit = static_cast<std::uint16_t>(button);
    std::uint16_t buttons = read_buttons(report);
    buttons = static_cast<std::uint16_t>(buttons & ~bit);
    report[0] = static_cast<std::uint8_t>(buttons & 0xFF);
    report[1] = static_cast<std::uint8_t>(buttons >> 8);
    return true;
}

// The name of a button, as the manifest's settings spell it.
const char* button_name(Ps2Button button);

// The button a setting's value names, or kSelect with `found` false when the name is not one
// of them ("none" is no button at all, which is found and is kSelect's bit cleared by the
// caller, so the caller asks for it separately).
Ps2Button button_from_name(const char* name, bool* found);

}  // namespace dualsense
