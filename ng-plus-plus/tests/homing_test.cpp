// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.5.4's homing rockets (src/homing.cpp): a rocket flown tick by tick at its full speed with
// `steer` and `turn_limit` reaches a target beside it, behind it or circling, where a fixed turn
// circles it; the targets a shot may fly at; and when a chase strikes or lets go.

#include <cmath>
#include <cstdint>

#include "check.h"
#include "homing.h"

namespace {

using namespace ng_plus_plus;

// The RYNO II rocket's speed on NTSC-U (units a tick) and the least turn shots.cpp gives.
constexpr float kSpeed = 0.6f;
constexpr float kBase = 0.09f;

// What a target the game would hit is within (its collision, about).
constexpr float kHitRadius = 0.8f;

float distance(const Vec3& a, const Vec3& b) {
    const float x = a[0] - b[0];
    const float y = a[1] - b[1];
    const float z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}

float length(const Vec3& v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// A target's place at tick `t`.
using Path = Vec3 (*)(int t);

Vec3 still(int /*t*/) {
    return {10.0f, 0.0f, 1.0f};
}

// An enemy running round a circle of 3 units at a third of the rocket's speed.
Vec3 circling(int t) {
    const float a = static_cast<float>(t) * (kSpeed / 3.0f) / 3.0f;
    return {10.0f + 3.0f * std::cos(a), 3.0f * std::sin(a), 1.0f};
}

// Flies a rocket from `at` heading `forward` at `speed` for at most `ticks`; returns the tick it
// came within kHitRadius of the target (-1 never). `fixed` turns by `base` alone (no closing turn),
// as a plain turn-rate limit would. Checks the speed never changes.
int fly(Vec3 at, Vec3 forward, Path target, float speed, float base, int ticks, bool fixed) {
    forward = unit(forward, {1.0f, 0.0f, 0.0f});

    for (int t = 0; t < ticks; ++t) {
        const Vec3 aim = target(t);

        if (distance(at, aim) <= kHitRadius) {
            return t;
        }

        const Vec3 to{aim[0] - at[0], aim[1] - at[1], aim[2] - at[2]};
        const float d = length(to);
        forward = steer(forward, to, fixed ? base : turn_limit(speed, d, base));
        CHECK(std::fabs(length(forward) - 1.0f) < 1e-3f);

        // Its whole speed, every tick: the step's length is the speed.
        for (int c = 0; c < 3; ++c) {
            at[static_cast<std::size_t>(c)] += forward[static_cast<std::size_t>(c)] * speed;
        }
    }

    return -1;
}

void test_orbit() {
    // Beside the target, flying past it: a fixed turn of 0.09 a tick (a circle of 6.7 units)
    // circles a target 2.5 units off its line like a firefly; the closing turn hits it at once.
    const Vec3 from{10.0f, -2.5f, 1.0f};
    const Vec3 across{1.0f, 0.0f, 0.0f};
    CHECK(fly(from, across, still, kSpeed, kBase, 600, true) < 0);
    const int hit = fly(from, across, still, kSpeed, kBase, 600, false);
    CHECK(hit >= 0 && hit < 30);

    // A target circling round: reached all the same, in well under a second.
    const int moving = fly({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, circling, kSpeed, kBase, 600, false);
    CHECK(moving >= 0 && moving < 60);

    // PAL's ticks: a fifth longer steps and turns.
    const int pal = fly(from, across, still, kSpeed * 1.2f, kBase * 1.2f, 600, false);
    CHECK(pal >= 0 && pal < 30);
}

void test_behind() {
    // Straight behind the rocket, 10 units: it turns round and hits, within a second.
    const int hit = fly({20.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, still, kSpeed, kBase, 600, false);
    CHECK(hit >= 0 && hit < 60);

    // Straight behind, exactly opposed: steer still picks a plane to turn in.
    const Vec3 turned = steer({1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, 0.5f);
    CHECK(std::fabs(length(turned) - 1.0f) < 1e-3f);
    CHECK(turned[0] < 0.9f);
}

void test_steer() {
    // Within the turn: straight at it; past it, turned by exactly the turn.
    const Vec3 at = steer({1.0f, 0.0f, 0.0f}, {1.0f, 0.1f, 0.0f}, 0.5f);
    CHECK(std::fabs(at[1] / at[0] - 0.1f) < 1e-3f);
    const Vec3 part = steer({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.25f);
    CHECK(std::fabs(std::atan2(part[1], part[0]) - 0.25f) < 1e-3f);

    // No target direction: the heading kept.
    const Vec3 kept = steer({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.25f);
    CHECK(std::fabs(kept[1] - 1.0f) < 1e-4f);

    // The turn grows as it closes: never under the base, half a turn at most.
    CHECK(turn_limit(kSpeed, 100.0f, kBase) == kBase);
    CHECK(turn_limit(kSpeed, 2.0f, kBase) > 0.8f);
    CHECK(turn_limit(kSpeed, 0.0f, kBase) <= 3.1416f);
}

void test_targets() {
    TargetFacts live;
    live.state = 1;
    live.enemy = true;
    live.has_health = true;
    live.health = 40.0f;
    live.mode = 0x20;
    live.alpha = 0x80;
    CHECK(targetable(live));

    // Dead or going (0xFD, 0xFE), out of health, no health record, not an enemy.
    TargetFacts dead = live;
    dead.state = 0xFE;
    CHECK(!targetable(dead));
    dead.state = 0xFD;
    CHECK(!targetable(dead));
    TargetFacts spent = live;
    spent.health = 0.0f;
    CHECK(!targetable(spent));
    TargetFacts none = live;
    none.has_health = false;
    CHECK(!targetable(none));
    TargetFacts prop = live;
    prop.enemy = false;
    CHECK(!targetable(prop));

    // Held hidden (a Yeti still in the snow at its spawn point) or faded out.
    TargetFacts hidden = live;
    hidden.mode = 0x21;
    CHECK(!targetable(hidden));
    hidden.mode = 0xA0;
    CHECK(!targetable(hidden));
    TargetFacts faded = live;
    faded.alpha = 4;
    CHECK(!targetable(faded));
}

void test_chase() {
    ChaseLimits limits;
    limits.reach = 2.0f;

    // Within reach without the game's hit for its ticks: it strikes; out of reach starts over.
    Chase chase;
    retarget(chase, 0x100);
    CHECK(judge(chase, 1.0f, false, false, limits) == Verdict::Chase);
    CHECK(judge(chase, 5.0f, false, false, limits) == Verdict::Chase);
    CHECK(judge(chase, 1.0f, false, false, limits) == Verdict::Chase);
    CHECK(judge(chase, 1.0f, false, false, limits) == Verdict::Chase);
    CHECK(judge(chase, 1.0f, false, false, limits) == Verdict::Strike);

    // A wall in between at two sight checks in a row (one clear between starts over): let go.
    Chase walled;
    retarget(walled, 0x200);
    CHECK(judge(walled, 20.0f, true, true, limits) == Verdict::Chase);
    CHECK(judge(walled, 20.0f, true, false, limits) == Verdict::Chase);
    CHECK(judge(walled, 20.0f, false, false, limits) == Verdict::Chase);
    CHECK(judge(walled, 20.0f, true, true, limits) == Verdict::Chase);
    CHECK(judge(walled, 20.0f, true, true, limits) == Verdict::Drop);

    // Chased too long: let go.
    Chase longest;
    retarget(longest, 0x300);
    Verdict last = Verdict::Chase;
    for (int t = 0; t <= limits.most_ticks; ++t) {
        last = judge(longest, 20.0f, false, false, limits);
    }
    CHECK(last == Verdict::Drop);

    // A target let go is not taken again; the last four are kept.
    drop(longest);
    CHECK(longest.target == 0);
    CHECK(dropped(longest, 0x300));
    CHECK(!dropped(longest, 0x400));
    CHECK(!dropped(longest, 0));
    for (std::uint32_t a = 1; a <= 4; ++a) {
        retarget(longest, a);
        drop(longest);
    }
    CHECK(!dropped(longest, 0x300));
    CHECK(dropped(longest, 1) && dropped(longest, 4));
}

}  // namespace

int main() {
    test_orbit();
    test_behind();
    test_steer();
    test_targets();
    test_chase();
    return ng_plus_plus::test::result("ng_plus_plus_homing");
}
