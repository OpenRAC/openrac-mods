// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

namespace deck {

struct Settings {
    bool enabled = true;
    int sensitivity = 100;
    bool invert_y = false;
};

inline int sensitivity(int value) {
    return std::clamp(value, 25, 200) / 25 * 25;
}

// Store only the pack's preferences, never the port's or the launcher's files.
inline Settings read_settings(std::istream& in, Settings defaults) {
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "enabled" && (value == "0" || value == "1")) {
            defaults.enabled = value == "1";
        } else if (key == "invert_y" && (value == "0" || value == "1")) {
            defaults.invert_y = value == "1";
        } else if (key == "sensitivity") {
            try {
                std::size_t used = 0;
                const int number = std::stoi(value, &used);
                if (used == value.size()) {
                    defaults.sensitivity = sensitivity(number);
                }
            } catch (...) {
                // A malformed preference leaves the manifest's default in place.
            }
        }
    }
    return defaults;
}

inline void write_settings(std::ostream& out, const Settings& settings) {
    out << "enabled=" << settings.enabled << '\n'
        << "sensitivity=" << settings.sensitivity << '\n'
        << "invert_y=" << settings.invert_y << '\n';
}

struct Stick {
    std::uint8_t x = 128;
    std::uint8_t y = 128;
};

// Steam Input produces mouse counts for both Gyro -> Mouse and the right trackpad. The SDK
// does not expose their identities or raw sensors, so their relative gain is set in Steam.
// Use the game's right stick: its camera modes and invert options keep their normal behaviour.
inline std::uint8_t axis(float delta, int gain) {
    if (!std::isfinite(delta) || std::fabs(delta) < 0.15f) {
        return 128;
    }
    // Cross the game's approximately 0.3 stick dead zone without a nonzero idle output.
    const float magnitude = std::min(127.0f, 39.0f + std::fabs(delta) * gain * 0.04f);
    const int signed_value = static_cast<int>(std::lround(std::copysign(magnitude, delta)));
    return static_cast<std::uint8_t>(std::clamp(128 + signed_value, 0, 255));
}

class Motion {
public:
    Stick sample(std::uint32_t tick, float dx, float dy, bool active, const Settings& settings) {
        if (!active) {
            tick_ = tick;
            seen_ = true;
            stick_ = {};
        } else if (!seen_ || tick != tick_) {
            tick_ = tick;
            seen_ = true;
            stick_ =
                {axis(dx, settings.sensitivity),
                 axis(settings.invert_y ? -dy : dy, settings.sensitivity)};
        }
        return stick_;
    }

private:
    bool seen_ = false;
    std::uint32_t tick_ = 0;
    Stick stick_;
};

inline void apply(std::uint8_t* report, int size, Stick mouse) {
    if (report == nullptr || size < 6) {
        return;
    }
    // A physical stick outside the game's dead zone takes priority; buttons/left stick untouched.
    if (std::abs(static_cast<int>(report[2]) - 128) > 38
        || std::abs(static_cast<int>(report[3]) - 128) > 38) {
        return;
    }
    if (mouse.x != 128 || mouse.y != 128) {
        report[2] = mouse.x;
        report[3] = mouse.y;
    }
}

}  // namespace deck
