// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The angle spring the games ease a joint's turn with (DESIGN.md, "Hips and upper body"): Going
// Commando's func_002AA898 with direction 2 and no limit, over its velocity step func_002AA198
// (rac2-decomp src/game/002AA800.c and 002A7750.c). Each tick the velocity is held within the
// distance left, takes accel times that distance less damping times itself, is held within the
// distance again, and moves the angle on. Direction 2 never wraps through half a turn: the angle
// goes to the target through 0, so the distance is the plain difference. No game memory, no game:
// any game's part of the mod and the tests can use it.

#pragma once

namespace lock_strafe {

// The rest angle and velocity below which a spring shows nothing, in radians and radians a tick.
inline constexpr float kSpringRest = 0.0005f;

// An angle easing toward a target, and its velocity.
struct Spring {
    float angle;     // Radians.
    float velocity;  // Radians a tick.
};

// Returns `v` held within -limit..limit.
inline float clamp_to_limit(float v, float limit) {
    return v > limit ? limit : (v < -limit ? -limit : v);
}

// Steps the spring one tick toward `target` (func_002AA898(angle, target, velocity, accel,
// damping, 0, 2)).
inline void spring_step(Spring& s, float target, float accel, float damping) {
    const float distance = target - s.angle;
    const float room = distance < 0.0f ? -distance : distance;

    s.velocity = clamp_to_limit(s.velocity, room);
    s.velocity += accel * distance - damping * s.velocity;
    s.velocity = clamp_to_limit(s.velocity, room);
    s.angle += s.velocity;
}

// Returns whether the spring is at rest at 0: nothing left to show.
inline bool spring_is_still(const Spring& s) {
    const float a = s.angle < 0.0f ? -s.angle : s.angle;
    const float v = s.velocity < 0.0f ? -s.velocity : s.velocity;

    return a < kSpringRest && v < kSpringRest;
}

}  // namespace lock_strafe
