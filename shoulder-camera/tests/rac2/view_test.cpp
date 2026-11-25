// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The shoulder camera's maths alone (src/rac2/view.hpp, src/rac2/discs.hpp): the aim held and
// toggled, and ended where it may not be; its look coming in and going; the eye slid in along its
// view line; the distance goal's share; where the shots go; the states it leaves alone; the discs.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "rac2/discs.hpp"
#include "rac2/view.hpp"

// Stops the test at the first failed condition and says where. It is a macro for the line number
// and the text of the condition, which a function cannot see.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

using namespace shoulder_camera::rac2;

namespace {

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

// Held: on while held, its look in after kBlendTicks and out as many after; not where it may not
// be.
void aim_held() {
    Aim a;
    a.step(true, true, false, true);
    CHECK(a.on && near(a.blend, 1.0f / kBlendTicks));

    for (int i = 0; i < 10; ++i) {
        a.step(true, false, false, true);
    }

    CHECK(a.on && a.blend == 1.0f);
    a.step(false, false, false, true);
    CHECK(!a.on && near(a.blend, 1.0f - 1.0f / kBlendTicks));

    for (int i = 0; i < 10; ++i) {
        a.step(false, false, false, true);
    }

    CHECK(a.blend == 0.0f);
    a.step(true, true, false, false);
    CHECK(!a.on && a.blend == 0.0f);
}

// Toggled: a press turns it on, it stays without the key, another press turns it off; leaving
// where it may be ends it, and coming back does not bring it back.
void aim_toggled() {
    Aim a;
    a.step(true, true, true, true);
    CHECK(a.on);
    a.step(false, false, true, true);
    CHECK(a.on);
    a.step(true, true, true, true);
    CHECK(!a.on);
    a.step(true, true, true, true);
    CHECK(a.on);
    a.step(false, false, true, false);
    CHECK(!a.on);
    a.step(false, false, true, true);
    CHECK(!a.on);
}

// The eye: 10 from Ratchet and 4 above, the view 0.07 below level; all in at 0.6, it is 6 away and
// lower by tan(-0.07) * 4; half way in, eased, half the way.
void shoulder() {
    const Eye from{10.0f, 4.0f};
    const Eye in = shoulder_eye(from, -0.07f, 0.6f, 1.0f);
    CHECK(near(in.distance, 6.0f) && near(in.height, 4.0f + std::tan(-0.07f) * 4.0f));
    const Eye out = shoulder_eye(from, -0.07f, 0.6f, 0.0f);
    CHECK(near(out.distance, 10.0f) && near(out.height, 4.0f));
    CHECK(near(eased(0.5f), 0.5f) && eased(0.25f) < 0.25f && eased(0.75f) > 0.75f);
    CHECK(near(shoulder_eye(from, 0.0f, 0.6f, 0.5f).distance, 8.0f));

    CHECK(near(pitch_of(std::sin(0.3f)), 0.3f) && pitch_of(2.0f) > 1.57f);
    CHECK(pitch_of(std::nanf("")) == 0.0f);
}

// The distance goal: 10 at 0.8 is 8; a goal that is no distance stays.
void distance() {
    CHECK(near(distance_goal(10.0f, 0.8f), 8.0f));
    CHECK(distance_goal(0.0f, 0.8f) == 0.0f && distance_goal(-1.0f, 1.15f) == -1.0f);
}

// The shots: 30 ahead along the yaw, level.
void shots() {
    const float gun[3] = {1.0f, 2.0f, 3.0f};
    float out[3] = {};
    aim_point(gun, 1.5707964f, 30.0f, out);
    CHECK(near(out[0], 1.0f) && near(out[1], 32.0f) && out[2] == 3.0f);
}

// Swimming and grinding are left alone; standing, running and jumping are not.
void states() {
    CHECK(aims_in_state(0) && aims_in_state(0x10));
    CHECK(!aims_in_state(0x33) && !aims_in_state(0x37) && !aims_in_state(0x28));
    CHECK(!aims_in_state(0x42) && aims_in_state(0x38));
}

// The three discs, and none else.
void discs() {
    CHECK(disc_of("ntsc") == &kDiscs[0] && disc_of("pal") == &kDiscs[1]);
    CHECK(disc_of("ntsc2") == &kDiscs[2] && disc_of("ntsc-pro") == nullptr);
    CHECK(kDiscs[1].hero == 0x00189EA0u && kDiscs[2].camera_matrix == 0x001B54F0u);
}

}  // namespace

int main() {
    aim_held();
    aim_toggled();
    shoulder();
    distance();
    shots();
    states();
    discs();
    std::puts("shoulder-camera view: all passed");
    return 0;
}
