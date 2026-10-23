// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The DualSense mod's logic alone: the vibration the mod plays and the strengths it gives the
// motors, and the PlayStation 2 buttons it writes into the pad report.
//
// The report the port's libpad2 replacement builds is active low: a button that is held reads
// 0. The mod's job is to clear the bit of the button an extra button stands for.
//
// The expected values are written out here by hand: the strengths from the table in rumble.cpp,
// the button bits from the console's own order in ps2_pad.hpp.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game_state.hpp"
#include "ps2_pad.hpp"
#include "rumble.hpp"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

using dualsense::Ps2Button;
using dualsense::Rumble;
using dualsense::RumbleStep;

// A report as the port builds it: every button released.
std::vector<std::uint8_t> released_report() {
    std::vector<std::uint8_t> report(dualsense::kPadReportSize, 0);
    report[0] = 0xFF;
    report[1] = 0xFF;

    for (int i = 2; i < 6; ++i) {
        report[static_cast<std::size_t>(i)] = 0x80;  // the sticks at their centre
    }

    return report;
}

bool held(const std::vector<std::uint8_t>& report, Ps2Button button) {
    const std::uint16_t buttons =
        static_cast<std::uint16_t>(report[0]) | static_cast<std::uint16_t>(report[1]) << 8;
    return (buttons & static_cast<std::uint16_t>(button)) == 0;
}

void test_the_addresses() {
    /* The addresses the mod reads, written out from the game's own structure
     * (games/rac1/include/hero.h) and the port's (port/game/rac1/host/front_end.c).
     * A typo in an offset would read the wrong field, and a wrong pointer would
     * fault the game, so they are spelled out here. */
    CHECK(dualsense::kHeroBasePal == 0x0013F450u);
    CHECK(dualsense::kHeroBaseNtsc == 0x0013F350u);

    // health.hp, the hero's hit points.
    CHECK(dualsense::kHeroHealthOffset == 0x22A8u);
    CHECK(dualsense::kHeroBasePal + dualsense::kHeroHealthOffset == 0x001416F8u);
    CHECK(dualsense::kHeroBaseNtsc + dualsense::kHeroHealthOffset == 0x001415F8u);
    // gMaxNanotech, the global.
    CHECK(dualsense::kMaxHealthPal == 0x0015EEA0u);
    CHECK(dualsense::kMaxHealthNtsc == 0x0015EDA0u);

    // items[0].item_id: the items are at 0x1090, the slot's own +0x28.
    CHECK(dualsense::kHeroItemIdOffset == 0x10B8u);
    CHECK(dualsense::kHeroBasePal + dualsense::kHeroItemIdOffset == 0x00140508u);
    CHECK(dualsense::kHeroBaseNtsc + dualsense::kHeroItemIdOffset == 0x00140408u);

    // items[0].moby: the slot's first field.
    CHECK(dualsense::kHeroWeaponMobyOffset == 0x1090u);
    CHECK(dualsense::kHeroBasePal + dualsense::kHeroWeaponMobyOffset == 0x001404E0u);
    CHECK(dualsense::kHeroBaseNtsc + dualsense::kHeroWeaponMobyOffset == 0x001403E0u);

    // unk20A8: the fire animation running. unk1BC: its age. state.current.
    CHECK(dualsense::kHeroFireFlagOffset == 0x20A8u);
    CHECK(dualsense::kHeroBasePal + dualsense::kHeroFireFlagOffset == 0x001414F8u);
    CHECK(dualsense::kHeroFireAgeOffset == 0x1BCu);
    CHECK(dualsense::kHeroBasePal + dualsense::kHeroFireAgeOffset == 0x0013F60Cu);
    CHECK(dualsense::kHeroStateOffset == 0x2084u);
    CHECK(dualsense::kHeroBasePal + dualsense::kHeroStateOffset == 0x001414D4u);

    // The moby's pvars, and the counters in them.
    CHECK(dualsense::kMobyPvarsOffset == 0x78u);
    CHECK(dualsense::kPvarCounter == 0x00u);
    CHECK(dualsense::kPvarShots == 0x04u);
}

void test_the_pointer_guard() {
    /* A pointer the game did not mean must not be followed. The slot holds
     * whatever was in it when no weapon is out, and the crash reports show it
     * can be a game's address well past its RAM. */
    CHECK(!dualsense::in_ram(0x80000000u, 4));  // what the slot held
    CHECK(dualsense::in_ram(0x001404E0u, 4));   // a moby in the data
    CHECK(dualsense::in_ram(0x001404E0u, 0x7Cu));
    CHECK(dualsense::in_ram(0x00100000u, 0x100u));
    CHECK(!dualsense::in_ram(0x01FFFFFFu, 0x100u));  // the end of the RAM
    CHECK(dualsense::in_ram(0x00000000u, 4));
    // And the port region is not the game's RAM either.
    CHECK(!dualsense::in_ram(0xE0000000u, 4));
}

void test_press_button() {
    std::vector<std::uint8_t> report = released_report();
    // Nothing is held.
    CHECK(!held(report, dualsense::kCross));
    CHECK(!held(report, dualsense::kSquare));
    CHECK(!held(report, dualsense::kStart));
    // The touchpad becomes Circle: the bit is cleared.
    CHECK(
        dualsense::press_button(report.data(), static_cast<int>(report.size()), dualsense::kCircle)
    );
    CHECK(held(report, dualsense::kCircle));
    // The other buttons are untouched.
    CHECK(!held(report, dualsense::kCross));
    CHECK(!held(report, dualsense::kSquare));
    CHECK(!held(report, dualsense::kStart));
    // The bits are in the console's order: the low byte is the report's first byte.
    // Circle is bit 13, so it is in the high byte, bit 5.
    CHECK(report[1] == 0xDF);
    CHECK(report[0] == 0xFF);
}

void test_press_several() {
    std::vector<std::uint8_t> report = released_report();
    dualsense::press_button(report.data(), static_cast<int>(report.size()), dualsense::kTriangle);
    dualsense::press_button(report.data(), static_cast<int>(report.size()), dualsense::kSquare);
    CHECK(held(report, dualsense::kTriangle));
    CHECK(held(report, dualsense::kSquare));
    CHECK(!held(report, dualsense::kCross));
    // Triangle is bit 12 and Square is bit 15: both in the high byte, bits 4 and 7.
    CHECK(report[1] == (0xFF & ~0x10 & ~0x80));
}

void test_a_short_report() {
    // A report too short for the buttons: nothing is written, and it says so.
    std::uint8_t report[1] = {0xFF};
    CHECK(!dualsense::press_button(report, 1, dualsense::kCross));
    CHECK(report[0] == 0xFF);
}

void test_button_names() {
    bool found = false;
    CHECK(dualsense::button_from_name("circle", &found) == dualsense::kCircle);
    CHECK(found);
    CHECK(dualsense::button_from_name("CIRCLE", &found) == dualsense::kCircle);
    CHECK(found);
    CHECK(dualsense::button_from_name("cross", &found) == dualsense::kCross);
    CHECK(found);
    CHECK(dualsense::button_from_name("start", &found) == dualsense::kStart);
    CHECK(found);
    // A name that is not one of them.
    dualsense::button_from_name("nonsense", &found);
    CHECK(!found);
    dualsense::button_from_name("", &found);
    CHECK(!found);
    dualsense::button_from_name(nullptr, &found);
    CHECK(!found);
    // The names the other way round.
    CHECK(std::strcmp(dualsense::button_name(dualsense::kCircle), "circle") == 0);
    CHECK(std::strcmp(dualsense::button_name(dualsense::kSquare), "square") == 0);
    CHECK(std::strcmp(dualsense::button_name(dualsense::kL1), "l1") == 0);
}

void test_the_vibration_plays_in_order() {
    Rumble rumble;
    CHECK(!rumble.playing());
    rumble.play({{100, 0, 50}, {0, 200, 50}});
    CHECK(rumble.playing());
    // The first update begins the first step.
    CHECK(rumble.update(1000));
    CHECK(rumble.heavy() == 100);
    CHECK(rumble.light() == 0);
    // Half way through it: the same.
    CHECK(rumble.update(1020));
    CHECK(rumble.heavy() == 100);
    // Its time is up: the second step.
    CHECK(rumble.update(1050));
    CHECK(rumble.heavy() == 0);
    CHECK(rumble.light() == 200);
    // And then it is over.
    CHECK(!rumble.update(1100));
    CHECK(rumble.heavy() == 0);
    CHECK(rumble.light() == 0);
    CHECK(!rumble.playing());
}

void test_a_vibration_is_replaced() {
    Rumble rumble;
    rumble.play({{100, 0, 500}});
    CHECK(rumble.update(0));
    CHECK(rumble.heavy() == 100);
    // A new one takes its place.
    rumble.play({{0, 50, 10}});
    CHECK(rumble.update(10));
    CHECK(rumble.heavy() == 0);
    CHECK(rumble.light() == 50);
}

void test_stopping() {
    Rumble rumble;
    rumble.play({{100, 0, 500}});
    CHECK(rumble.update(0));
    rumble.stop();
    CHECK(!rumble.playing());
    CHECK(rumble.heavy() == 0);
    CHECK(rumble.light() == 0);
    CHECK(!rumble.update(10));
}

void test_the_vibrations_of_the_events() {
    // Every event makes something, and nothing makes more than it should.
    const char* kinds[] = {"damage", "hit", "break", "pickup", "gold", "land"};

    for (const char* kind : kinds) {
        const std::vector<RumbleStep> steps = dualsense::rumble_for(kind, 100);
        CHECK(!steps.empty());

        for (const RumbleStep& step : steps) {
            CHECK(step.heavy <= dualsense::kFull);
            CHECK(step.light <= dualsense::kFull);
            CHECK(step.ms > 0);
        }
    }

    // An event the mod does not know: nothing.
    CHECK(dualsense::rumble_for("nonsense", 100).empty());
    CHECK(dualsense::rumble_for(nullptr, 100).empty());

    // The damage is a crack and then a thump: two steps.
    CHECK(dualsense::rumble_for("damage", 100).size() == 2);
    // A gold bolt is two blips.
    CHECK(dualsense::rumble_for("gold", 100).size() == 2);
    // The rest is one step.
    CHECK(dualsense::rumble_for("hit", 100).size() == 1);
    CHECK(dualsense::rumble_for("break", 100).size() == 1);
    CHECK(dualsense::rumble_for("pickup", 100).size() == 1);
    CHECK(dualsense::rumble_for("land", 100).size() == 1);

    // The strength is a percentage of the full one.
    const std::vector<RumbleStep> half = dualsense::rumble_for("damage", 50);
    CHECK(half[0].light == static_cast<std::uint16_t>(dualsense::kFull / 2));
    // At zero there is no vibration's strength, but the step is still there.
    const std::vector<RumbleStep> none = dualsense::rumble_for("damage", 0);
    CHECK(none[0].light == 0);
    CHECK(none[1].heavy == 0);
}

void test_the_landing_grows_with_the_fall() {
    const std::vector<RumbleStep> short_fall = dualsense::rumble_for("land", 100, 100);
    const std::vector<RumbleStep> long_fall = dualsense::rumble_for("land", 100, 900);
    CHECK(!short_fall.empty());
    CHECK(!long_fall.empty());
    // A longer fall hits harder.
    CHECK(long_fall[0].heavy > short_fall[0].heavy);
    // A fall of no time at all is the gentlest.
    const std::vector<RumbleStep> no_fall = dualsense::rumble_for("land", 100, 0);
    CHECK(no_fall[0].heavy <= short_fall[0].heavy);
}

}  // namespace

int main() {
    test_the_addresses();
    test_the_pointer_guard();
    test_press_button();
    test_press_several();
    test_a_short_report();
    test_button_names();
    test_the_vibration_plays_in_order();
    test_a_vibration_is_replaced();
    test_stopping();
    test_the_vibrations_of_the_events();
    test_the_landing_grows_with_the_fall();
    std::puts("dualsense: all passed");
    return 0;
}
