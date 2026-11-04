// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <cstdlib>

#include "controls.hpp"
#include "kbm.hpp"
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

bool near(float a, float b) {
    return std::abs(a - b) < .0001f;
}

int main() {
    CHECK(modern::stick(128, 128, .1f).x == 0);
    CHECK(modern::stick(139, 128, .1f).x == 0);
    CHECK(modern::stick(142, 128, .1f).x > 0);
    auto d = modern::stick(255, 255, .1f);
    CHECK(near(std::hypot(d.x, d.y), 1));
    CHECK(near(d.x, d.y));
    CHECK(near(modern::stick(255, 128, .1f).x, 1));
    CHECK(near(modern::stick(0, 128, .1f).x, -1));
    CHECK(near(modern::mouse(100, 200), modern::mouse(100, 100) * 2));
    CHECK(modern::mouse(NAN, 100) == 0);
    CHECK(near(modern::rate(1, 240) * 60, 240 * .01745329252f));
    CHECK(near(modern::controller_rate(1, 240, 200), modern::rate(1, 240) * 2));
    CHECK(near(modern::controller_rate(1, 240, 200, 75), modern::rate(1, 240) * 1.5f));
    CHECK(near(modern::controller_rate(0, 240, 300, 200), 0));
    auto v = kbm::rotate({1, 0, 0}, {0, 0, 1}, modern::rate(1, 360) * 60);
    CHECK(near(v.x, 1) && near(v.y, 0));
    auto w = kbm::with_elevation({2, 0, 0}, {0, 0, 1}, .5f);
    CHECK(near(kbm::length(w), 2));
    CHECK(near(kbm::elevation(w, {0, 0, 1}), .5f));
}
