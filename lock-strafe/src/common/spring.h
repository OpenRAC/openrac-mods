/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The angle spring the games ease a joint's turn with (DESIGN.md, "Hips and upper body"): Going
 * Commando's func_002AA898 with direction 2 and no limit, over its velocity step func_002AA198
 * (rac2-decomp src/game/002AA800.c and 002A7750.c). Each tick the velocity is held within the
 * distance left, takes accel times that distance less damping times itself, is held within the
 * distance again, and moves the angle on. Direction 2 never wraps through half a turn: the angle
 * goes to the target through 0, so the distance is the plain difference. No game memory, no game:
 * plain C that any game's part of the mod and the tests can use.
 */
#ifndef LOCK_STRAFE_COMMON_SPRING_H
#define LOCK_STRAFE_COMMON_SPRING_H

/* An angle easing toward a target, and its velocity (radians, radians a tick). */
typedef struct lock_strafe_spring {
    float angle;
    float velocity;
} lock_strafe_spring;

/* v held within -limit..limit. */
static inline float lock_strafe_clamp(float v, float limit) {
    return v > limit ? limit : (v < -limit ? -limit : v);
}

/* One tick of the spring toward `target` (func_002AA898(angle, target, velocity, accel, damping,
 * 0, 2)). */
static inline void lock_strafe_spring_step(
    lock_strafe_spring* s, float target, float accel, float damping
) {
    const float distance = target - s->angle;
    const float room = distance < 0.0f ? -distance : distance;

    s->velocity = lock_strafe_clamp(s->velocity, room);
    s->velocity += accel * distance - damping * s->velocity;
    s->velocity = lock_strafe_clamp(s->velocity, room);
    s->angle += s->velocity;
}

/* Whether the spring is at rest at 0: nothing left to show. */
static inline int lock_strafe_spring_still(const lock_strafe_spring* s) {
    const float a = s->angle < 0.0f ? -s->angle : s->angle;
    const float v = s->velocity < 0.0f ? -s->velocity : s->velocity;
    return a < 0.0005f && v < 0.0005f;
}

#endif /* LOCK_STRAFE_COMMON_SPRING_H */
