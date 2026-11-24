// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The homing rockets' rules (homing.h).

#include "homing.h"

#include <algorithm>
#include <cmath>

namespace ng_plus_plus {
namespace {

constexpr float kPi = 3.14159265f;

float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float length(const Vec3& v) {
    return std::sqrt(dot(v, v));
}

}  // namespace

bool targetable(const TargetFacts& facts) {
    return facts.state < 0x80u && facts.enemy && facts.has_health && facts.health > 0.0f
           && (facts.mode & 0x81u) == 0 && facts.alpha >= 8u;
}

float turn_limit(float speed, float distance, float base) {
    // Its turning circle's radius is speed / turn: a third of the way left at most.
    const float closing = distance > 1e-3f ? 3.0f * speed / distance : kPi;
    return std::min(kPi, std::max(base, closing));
}

Vec3 unit(const Vec3& v, const Vec3& fallback) {
    const float n = length(v);
    return n > 1e-6f ? Vec3{v[0] / n, v[1] / n, v[2] / n} : fallback;
}

Vec3 steer(const Vec3& forward, const Vec3& to, float most) {
    const Vec3 f = unit(forward, {1.0f, 0.0f, 0.0f});
    const Vec3 want = unit(to, f);
    const float c = std::max(-1.0f, std::min(1.0f, dot(f, want)));
    const float angle = std::acos(c);

    // Within this tick's turn: straight at it.
    if (angle <= most) {
        return want;
    }

    // The turn's plane; straight behind, any plane through the up axis (or another).
    Vec3 axis = cross(f, want);
    if (length(axis) < 1e-5f) {
        axis = cross(f, {0.0f, 0.0f, 1.0f});
        if (length(axis) < 1e-5f) {
            axis = cross(f, {1.0f, 0.0f, 0.0f});
        }
    }
    axis = unit(axis, {0.0f, 0.0f, 1.0f});

    // Rodrigues: f turned `most` about the axis (the axis is perpendicular to f).
    const Vec3 side = cross(axis, f);
    const float cs = std::cos(most);
    const float sn = std::sin(most);
    return unit({f[0] * cs + side[0] * sn, f[1] * cs + side[1] * sn, f[2] * cs + side[2] * sn}, f);
}

Verdict judge(Chase& chase, float distance, bool checked, bool blocked, const ChaseLimits& limits) {
    ++chase.ticks;

    if (checked) {
        chase.blocked = blocked ? chase.blocked + 1 : 0;
    }

    // Behind a wall for a while, or chased too long: let it go.
    if (chase.blocked >= limits.blocked || chase.ticks > limits.most_ticks) {
        return Verdict::Drop;
    }

    // At the target, and the game's hit has not come: it is there, it goes off.
    if (distance <= limits.reach) {
        if (++chase.near_ticks >= limits.near_ticks) {
            return Verdict::Strike;
        }
    } else {
        chase.near_ticks = 0;
    }

    return Verdict::Chase;
}

void drop(Chase& chase) {
    if (chase.target != 0 && !dropped(chase, chase.target)) {
        std::copy_backward(
            std::begin(chase.dropped), std::end(chase.dropped) - 1, std::end(chase.dropped)
        );
        chase.dropped[0] = chase.target;
    }
    retarget(chase, 0);
}

bool dropped(const Chase& chase, std::uint32_t address) {
    return address != 0
           && std::find(std::begin(chase.dropped), std::end(chase.dropped), address)
                  != std::end(chase.dropped);
}

void strike(Chase& chase) {
    const std::uint32_t target = chase.target;
    drop(chase);
    chase.struck = target;
}

std::uint32_t inherited(const Chase& parent) {
    return parent.target != 0 ? parent.target : parent.struck;
}

std::vector<std::uint32_t> split_targets(
    std::uint32_t parent, bool usable, const std::vector<std::uint32_t>& own, int count
) {
    std::vector<std::uint32_t> out;
    for (int n = 0; n < count; ++n) {
        const auto i = static_cast<std::size_t>(n);
        out.push_back(parent != 0 && usable ? parent : (i < own.size() ? own[i] : 0u));
    }
    return out;
}

void retarget(Chase& chase, std::uint32_t address) {
    chase.target = address;
    chase.ticks = 0;
    chase.near_ticks = 0;
    chase.blocked = 0;
}

}  // namespace ng_plus_plus
