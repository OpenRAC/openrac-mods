// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.6.22's line at the top of the picture (src/notices.h): the port keeps a line until it is
// replaced or cleared, so each line counts its own time and clears itself.

#include <string>
#include <vector>

#include "check.h"
#include "notices.h"

namespace {

using namespace ng_plus_plus;

}  // namespace

int main() {
    std::string shown;
    int calls = 0;
    Notices notices([&](const std::string& text) {
        shown = text;
        ++calls;
    });

    // Nothing up: ticks do nothing.
    notices.tick();
    CHECK(calls == 0 && !notices.up());

    // A line stays up for its time, then clears itself once.
    notices.show("Challenge: Elite Guard", 3);
    CHECK(shown == "Challenge: Elite Guard" && notices.up());
    notices.tick();
    notices.tick();
    CHECK(shown == "Challenge: Elite Guard" && notices.up());
    notices.tick();
    CHECK(shown.empty() && !notices.up() && calls == 2);
    notices.tick();
    CHECK(calls == 2);

    // A new line takes the old one's place and its own full time.
    notices.show("Overdrive: RYNO II", 2);
    notices.tick();
    notices.show("Last stand!", 2);
    notices.tick();
    CHECK(shown == "Last stand!" && notices.up());
    notices.tick();
    CHECK(shown.empty() && !notices.up());

    // A time of zero still clears, after one tick.
    notices.show("x", 0);
    notices.tick();
    CHECK(shown.empty());

    return test::result("notices_test");
}
