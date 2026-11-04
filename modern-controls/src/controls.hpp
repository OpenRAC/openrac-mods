// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

namespace modern {
// Circular deadzone preserves direction, with continuous travel outside it.
struct Axis {
    float x, y;
};

inline Axis stick(int x, int y, float deadzone) {
    float a = std::clamp((x - 128) / 127.f, -1.f, 1.f);
    float b = std::clamp((y - 128) / 127.f, -1.f, 1.f);
    float length = std::hypot(a, b);
    if (length <= deadzone) {
        return {};
    }
    float magnitude = (std::min(length, 1.f) - deadzone) / (1.f - deadzone);
    return {a / length * magnitude, b / length * magnitude};
}

inline float mouse(float counts, int percent) {
    return std::isfinite(counts) ? counts * .0015f * percent / 100.f : 0.f;
}

inline float rate(float axis, int degrees) {
    // OpenRAC runs both RAC1 builds at 60 ticks/s (docs/port/speedrun/RULES.md).
    return axis * degrees * .01745329252f / 60.f;
}
}  // namespace modern
