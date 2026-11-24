// SPDX-License-Identifier: GPL-3.0-or-later
// The shared part of the cheats mod (src/common/cheats.h), alone: the multiplier's steps and the
// launcher's choice, the bolts a pickup and the gift add (never past the count's most), and the
// HUD's line.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>

#include "cheats.h"
#include "pad_intent.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

int main() {
    constexpr int32_t kMost = std::numeric_limits<int32_t>::max();

    // The steps, their names and the launcher's values, both ways.
    CHECK(cheats::multiplier_names().size() == cheats::k_multipliers.size());
    CHECK(cheats::multiplier_names()[0] == "x1" && cheats::multiplier_names()[5] == "x100");
    CHECK(cheats::multiplier_index(10) == 4 && cheats::multiplier_index(7) == 0);
    CHECK(cheats::multiplier_at(4) == 10 && cheats::multiplier_at(-1) == 1);
    CHECK(cheats::multiplier_at(6) == 1);
    CHECK(cheats::parse_multiplier("100") == 100 && cheats::parse_multiplier("5") == 5);
    CHECK(cheats::parse_multiplier("") == 1 && cheats::parse_multiplier("7") == 1);

    // Each step stands for its own index.
    for (int i = 0; i < static_cast<int>(cheats::k_multipliers.size()); ++i) {
        CHECK(cheats::multiplier_index(cheats::multiplier_at(i)) == i);
    }

    // A pickup counts `multiplier` times: the rest is added, up to the count's most.
    CHECK(cheats::extra_bolts(5, 1, 100) == 0);
    CHECK(cheats::extra_bolts(5, 2, 100) == 5);
    CHECK(cheats::extra_bolts(50, 100, 1000) == 4950);
    CHECK(cheats::extra_bolts(0, 10, 100) == 0 && cheats::extra_bolts(-3, 10, 100) == 0);
    CHECK(cheats::extra_bolts(50, 100, kMost - 10) == 10);
    CHECK(cheats::extra_bolts(50, 100, kMost) == 0);

    // The gift, up to the count's most.
    CHECK(cheats::gift_bolts(10000, 0) == 10000);
    CHECK(cheats::gift_bolts(10000, kMost - 3) == 3 && cheats::gift_bolts(10000, kMost) == 0);
    CHECK(cheats::gift_bolts(0, 0) == 0);

    // The HUD's line: nothing when nothing is on, else each cheat that is.
    cheats::State state;
    CHECK(cheats::status_line(state).empty());
    state.invincible = true;
    CHECK(cheats::status_line(state) == "Cheats: invincible");
    state.infinite_ammo = true;
    state.multiplier = 100;
    CHECK(cheats::status_line(state) == "Cheats: invincible, infinite ammo, bolts x100");
    state = {};
    state.multiplier = 2;
    CHECK(cheats::status_line(state) == "Cheats: bolts x2");

    state = {};
    state.infinite_air = true;
    CHECK(cheats::status_line(state) == "Cheats: infinite oxygen");

    // The longest line fits the HUD's 63 characters.
    const cheats::State all{true, true, true, 100};
    CHECK(
        cheats::status_line(all) == "Cheats: invincible, infinite ammo, infinite oxygen, bolts x100"
    );
    CHECK(cheats::status_line(all).size() < 64);

    // The pad's report: the word (a bit clear for a button held), then the sticks, 0x7F at rest.
    auto report = [](unsigned held, int x, int y) {
        std::array<std::uint8_t, 18> r{};
        r.fill(0x7F);
        const unsigned word = ~held & 0xFFFFu;
        r[0] = static_cast<std::uint8_t>(word & 0xFF);
        r[1] = static_cast<std::uint8_t>(word >> 8);
        r[4] = static_cast<std::uint8_t>(x);
        r[5] = static_cast<std::uint8_t>(y);
        return r;
    };
    using cheats::Intent;
    const unsigned right = 1u << 5, left = 1u << 7, cross = 1u << 14;
    auto intent = [&](unsigned held, int x, int y) {
        const auto r = report(held, x, y);
        return cheats::read_intent(r.data(), static_cast<int>(r.size()));
    };

    // Cross before anything; the d-pad's Right and Left.
    CHECK(intent(cross, 0x7F, 0xFF) == Intent::Choose);
    CHECK(
        intent(right, 0x7F, 0x7F) == Intent::Next && intent(left, 0x7F, 0x7F) == Intent::Previous
    );
    // The stick: at rest within the game's lean, sideways a step, up or down the cursor moving.
    CHECK(intent(0, 0x90, 0x60) == Intent::Unknown);
    CHECK(intent(0, 0xFF, 0x90) == Intent::Next && intent(0, 0x00, 0x60) == Intent::Previous);
    CHECK(intent(0, 0xB0, 0xFF) == Intent::Moving && intent(0, 0x50, 0x00) == Intent::Moving);
    // A report too short for the stick, or for the buttons.
    {
        const auto r = report(0, 0xFF, 0x7F);
        CHECK(cheats::read_intent(r.data(), 4) == Intent::Unknown);
        CHECK(cheats::read_intent(r.data(), 1) == Intent::Unknown);
        CHECK(cheats::read_intent(nullptr, 18) == Intent::Unknown);
    }

    // A choice stepped the way the player means, the port's word when the pad does not say.
    CHECK(cheats::step_choice(Intent::Moving, 0, 2, 1) == std::nullopt);
    CHECK(cheats::step_choice(Intent::Choose, 1, 2, 0) == 0);
    CHECK(cheats::step_choice(Intent::Next, 5, 6, 0) == 0);
    CHECK(cheats::step_choice(Intent::Previous, 0, 6, 1) == 5);
    CHECK(cheats::step_choice(Intent::Unknown, 0, 6, 3) == 3);
    CHECK(cheats::step_choice(Intent::Next, 0, 0, 0) == std::nullopt);
    // Only Cross, or a press the pad does not show, chooses a button.
    CHECK(cheats::chooses(Intent::Choose) && cheats::chooses(Intent::Unknown));
    CHECK(!cheats::chooses(Intent::Next) && !cheats::chooses(Intent::Previous));
    CHECK(!cheats::chooses(Intent::Moving));

    std::puts("cheats: ok");
    return 0;
}
