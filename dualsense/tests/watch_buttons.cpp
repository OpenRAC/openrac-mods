// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// A live check of the extra buttons: it watches the touchpad and the microphone button and says
// what it sees, and what the mod would write into the pad report for the game.
//
// Press the touchpad and the microphone button while it runs.
//
//   watch [milliseconds]
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

// The output is read while it runs, so it is not buffered.
static void set_unbuffered() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
}

#include "ps2_pad.hpp"

namespace {

// The report as the port's libpad2 replacement builds it: every button released.
void make_report(std::uint8_t* report) {
    std::memset(report, 0, 18);
    report[0] = 0xFF;
    report[1] = 0xFF;

    for (int i = 2; i < 6; ++i) {
        report[static_cast<std::size_t>(i)] = 0x80;
    }
}

bool holds(const std::uint8_t* report, dualsense::Ps2Button button) {
    const std::uint16_t buttons =
        static_cast<std::uint16_t>(report[0]) | static_cast<std::uint16_t>(report[1]) << 8;
    return (buttons & static_cast<std::uint16_t>(button)) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    set_unbuffered();
    const unsigned long duration = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 15000;

    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL would not start: %s\n", SDL_GetError());
        return 1;
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);

    if (ids == nullptr || count == 0) {
        std::fprintf(stderr, "no gamepad\n");
        return 2;
    }

    SDL_Gamepad* pad = nullptr;

    for (int i = 0; i < count; ++i) {
        SDL_Gamepad* candidate = SDL_OpenGamepad(ids[i]);

        if (candidate != nullptr && SDL_GetGamepadType(candidate) == SDL_GAMEPAD_TYPE_PS5) {
            pad = candidate;
            break;
        }
    }

    if (pad == nullptr) {
        std::fprintf(stderr, "no DualSense\n");
        return 3;
    }

    std::printf("%s\n", SDL_GetGamepadName(pad));
    std::printf("watching for %lu ms: press the touchpad and the microphone button\n\n", duration);

    bool last_touchpad = false;
    bool last_microphone = false;
    bool last_paddle = false;
    const unsigned long until = SDL_GetTicks() + static_cast<unsigned long>(duration);

    while (SDL_GetTicks() < until) {
        const bool touchpad = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_TOUCHPAD);
        const bool microphone = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_MISC1);
        const bool paddle = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1)
                            || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1);

        if (touchpad != last_touchpad || microphone != last_microphone || paddle != last_paddle) {
            // What the mod would write into the report the game reads.
            std::uint8_t report[18];
            make_report(report);

            if (touchpad) {
                dualsense::press_button(report, 18, dualsense::kCircle);
            }

            if (microphone) {
                dualsense::press_button(report, 18, dualsense::kTriangle);
            }

            if (paddle) {
                dualsense::press_button(report, 18, dualsense::kSquare);
            }

            std::printf(
                "touchpad %-3s  microphone %-3s  paddle %-3s  ->  the game sees: circle %s, "
                "triangle %s, square %s\n",
                touchpad ? "on" : "off",
                microphone ? "on" : "off",
                paddle ? "on" : "off",
                holds(report, dualsense::kCircle) ? "pressed" : "-",
                holds(report, dualsense::kTriangle) ? "pressed" : "-",
                holds(report, dualsense::kSquare) ? "pressed" : "-"
            );
            last_touchpad = touchpad;
            last_microphone = microphone;
            last_paddle = paddle;
        }

        SDL_Delay(10);
    }

    std::printf("\ndone\n");
    SDL_CloseGamepad(pad);
    SDL_Quit();
    return 0;
}
