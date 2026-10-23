// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// A probe of the PlayStation 5 controller the mod uses: what SDL sees of it, whether the extra
// buttons the mod binds can be read, and whether the light bar and the motors answer.
//
// Not a test of the mod: a check of the hardware under it, so that a machine with a controller
// attached can say what the controller reports and what it does.
//
//   probe [milliseconds]
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

namespace {

// The buttons the mod binds, and the names the mod's settings use.
struct Extra {
    const char* name;
    SDL_GamepadButton button;
};

const Extra kExtras[] = {
    {"touchpad", SDL_GAMEPAD_BUTTON_TOUCHPAD},
    {"left_paddle", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1},
    {"right_paddle", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1},
    {"microphone", SDL_GAMEPAD_BUTTON_MISC1},
};

const char* type_name(SDL_GamepadType type) {
    switch (type) {
        case SDL_GAMEPAD_TYPE_PS5:
            return "PS5 (DualSense)";
        case SDL_GAMEPAD_TYPE_PS4:
            return "PS4 (DualShock 4)";
        case SDL_GAMEPAD_TYPE_PS3:
            return "PS3";
        case SDL_GAMEPAD_TYPE_XBOX360:
            return "Xbox 360";
        case SDL_GAMEPAD_TYPE_XBOXONE:
            return "Xbox One";
        case SDL_GAMEPAD_TYPE_STANDARD:
            return "standard";
        default:
            return "unknown";
    }
}

}  // namespace

int main(int argc, char** argv) {
    const unsigned long duration = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 4000;

    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL would not start: %s\n", SDL_GetError());
        return 1;
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    std::printf("gamepads: %d\n", count);

    if (ids == nullptr || count == 0) {
        std::fprintf(stderr, "no gamepad: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    SDL_Gamepad* pad = nullptr;

    for (int i = 0; i < count; ++i) {
        SDL_Gamepad* candidate = SDL_OpenGamepad(ids[i]);

        if (candidate == nullptr) {
            continue;
        }

        const SDL_GamepadType type = SDL_GetGamepadType(candidate);
        std::printf("  %s\n", SDL_GetGamepadName(candidate));
        std::printf("    type      : %s\n", type_name(type));
        std::printf("    vendor    : %04x\n", SDL_GetGamepadVendor(candidate));
        std::printf("    product   : %04x\n", SDL_GetGamepadProduct(candidate));
        std::printf("    firmware  : %d\n", SDL_GetGamepadFirmwareVersion(candidate));
        std::printf("    touchpads : %d\n", SDL_GetNumGamepadTouchpads(candidate));

        for (const Extra& extra : kExtras) {
            std::printf(
                "    %-9s : %s\n",
                extra.name,
                SDL_GamepadHasButton(candidate, extra.button) ? "there" : "not on this controller"
            );
        }

        if (type == SDL_GAMEPAD_TYPE_PS5) {
            pad = candidate;
            break;
        }

        SDL_CloseGamepad(candidate);
    }

    if (pad == nullptr) {
        std::fprintf(stderr, "no DualSense among them\n");
        SDL_Quit();
        return 3;
    }

    // The light bar and the motors: the mod drives them the same way.
    std::printf("\nthe light bar goes green, then the motors buzz for a moment\n");
    SDL_SetGamepadLED(pad, 0, 255, 0);
    const unsigned long until = SDL_GetTicks() + static_cast<unsigned long>(duration);
    bool buzzed = false;

    while (SDL_GetTicks() < until) {
        const unsigned long now = SDL_GetTicks();

        if (!buzzed && now > until - static_cast<unsigned long>(duration) / 2) {
            SDL_RumbleGamepad(pad, 0xFFFF, 0x4000, 400);
            buzzed = true;
        }

        SDL_SetGamepadLED(pad, 0, 255, 0);
        SDL_Delay(16);
    }

    SDL_RumbleGamepad(pad, 0, 0, 0);
    SDL_SetGamepadLED(pad, 0, 0, 0);
    SDL_CloseGamepad(pad);
    SDL_Quit();
    std::printf("done\n");
    return 0;
}
