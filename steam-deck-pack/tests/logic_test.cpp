// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>

#include "deck.hpp"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #c);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

int main() {
    CHECK(deck::axis(0, 100) == 128);
    CHECK(deck::axis(0.1f, 100) == 128);
    CHECK(deck::axis(std::numeric_limits<float>::quiet_NaN(), 100) == 128);
    CHECK(deck::axis(std::numeric_limits<float>::infinity(), 100) == 128);
    CHECK(deck::axis(1, 100) > 166);
    CHECK(deck::axis(-1, 100) < 90);
    CHECK(deck::axis(10000, 100) == 255);
    CHECK(deck::axis(-10000, 100) == 1);
    CHECK(deck::axis(2, 50) < deck::axis(2, 200));
    deck::Settings settings;
    deck::Motion motion;
    auto sample = motion.sample(1, 2, 3, true, settings);
    CHECK(sample.x > 128 && sample.y > 128);
    CHECK(motion.sample(1, 8, 8, true, settings).x == sample.x);  // Same tick, no acceleration.
    CHECK(motion.sample(2, 0, 0, true, settings).x == 128);       // No residual turn/drift.
    CHECK(motion.sample(3, 9, 9, false, settings).x == 128);
    CHECK(motion.sample(3, 9, 9, true, settings).x == 128);  // No stale motion after focus returns.
    settings.invert_y = true;
    CHECK(motion.sample(4, 2, 3, true, settings).y < 128);
    motion.sample(0xffffffffu, 0, 0, true, settings);
    CHECK(motion.sample(0, 2, 3, true, settings).x > 128);  // Tick wrap.
    std::array<std::uint8_t, 8> report{0x23, 0x45, 128, 128, 55, 200, 0xAA, 0xBB};
    deck::apply(report.data(), report.size(), {199, 90});
    CHECK(report[2] == 199 && report[3] == 90);
    CHECK(report[0] == 0x23 && report[1] == 0x45 && report[4] == 55 && report[5] == 200);
    CHECK(report[6] == 0xAA && report[7] == 0xBB);
    report[2] = 255;
    deck::apply(report.data(), report.size(), {30, 30});
    CHECK(report[2] == 255 && report[3] == 90);  // Physical stick priority, both axes.
    const auto before = report;
    deck::apply(report.data(), 4, {0, 0});
    deck::apply(nullptr, 8, {0, 0});
    CHECK(report == before);
    std::istringstream corrupted(
        "sensitivity=999999999999999999999999\nenabled=wrong\ninvert_y=2\n"
    );
    auto loaded = deck::read_settings(corrupted, {});
    CHECK(loaded.sensitivity == 100 && loaded.enabled && !loaded.invert_y);
    std::istringstream bounded(
        "sensitivity=-40\nsensitivity=201\nsensitivity=75garbage\nenabled=0\ninvert_y=1\n"
    );
    loaded = deck::read_settings(bounded, {});
    CHECK(loaded.sensitivity == 200 && !loaded.enabled && loaded.invert_y);
    std::ostringstream out;
    deck::write_settings(out, loaded);
    std::istringstream roundtrip(out.str());
    auto restored = deck::read_settings(roundtrip, {});
    CHECK(restored.sensitivity == 200 && !restored.enabled && restored.invert_y);
    std::puts("Steam Deck input logic: OK");
}
