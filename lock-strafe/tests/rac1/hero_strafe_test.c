/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The strafe's decision (lock-strafe/src/rac1/strafe.h, and the joints' spring in src/common): when the hero strafes and how, which ticks
 * the hero's update does not see L2 and R2, where his facing turns and which way his velocity
 * goes; with the setting off, always the game's own.
 * Expected values are worked out by hand from the description in strafe.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/common/spring.h"
#include "../../src/rac1/strafe.h"
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #c);                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define BLASTER 0x0F
#define WRENCH 0x08
#define SWINGSHOT 0x0C
#define CIRCLE 0x20u
#define L1 0x4u
#define R1 0x8u

/* The hero running on level ground on foot, the blaster ready, nothing held. */
static rac1_strafe_input running(void) {
    rac1_strafe_input in =
        {1, RAC1_STRAFE_STATE_RUN, 0, 0, 0, RAC1_STRAFE_SLOT_READY, BLASTER, 0, 0.75f, 0.0f, 0.0f};
    return in;
}

/* One tick as the run does it, with the decision in the two places lock_strafe.c changes: the facing
 * eased toward its target by a fraction (the game springs it, the same direction), the velocity
 * along the planar angle (the facing for 99999, the run's request). */
static void tick(
    int kind, float camera, float stick, const rac1_strafe_hold* hold, float* facing, float v[2]
) {
    /* The stick handler adds the rotations as the game's FastAddRots does: within -pi..pi. */
    float move_yaw = camera + stick;

    if (move_yaw > 3.1415927f) {
        move_yaw -= 6.2831855f;
    } else if (move_yaw < -3.1415927f) {
        move_yaw += 6.2831855f;
    }

    const float target = rac1_strafe_turn_target(kind, 0, move_yaw, camera, hold);
    *facing += rac1_strafe_angle_diff(*facing, target) * 0.25f;
    float angle = rac1_strafe_planar_angle(kind, 99999.0f, move_yaw);

    if (!(angle <= 3.14159274f)) {
        angle = *facing;
    }

    v[0] = cosf(angle);
    v[1] = sinf(angle);
}

static void items(void) {
    const int aimable[] = {0x09, 0x0A, 0x0B, 0x0F, 0x10, 0x13, 0x15, 0x17};
    const int not_aimable[] =
        {WRENCH,
         SWINGSHOT,
         0x0D,
         0x0E,
         0x11,
         0x12,
         0x14,
         0x16,
         0x18,
         0x19,
         0x1A,
         0x1B,
         0x1F,
         0x20,
         0,
         0x26};

    for (unsigned i = 0; i < sizeof aimable / sizeof aimable[0]; ++i) {
        CHECK(rac1_strafe_aimable(aimable[i]));
    }

    for (unsigned i = 0; i < sizeof not_aimable / sizeof not_aimable[0]; ++i) {
        CHECK(!rac1_strafe_aimable(not_aimable[i]));
    }
}

/* The setting "button": both by default (and for anything unknown), or L2 or R2 alone. */
static void buttons(void) {
    CHECK(rac1_strafe_button_bits(RAC1_STRAFE_BUTTON_BOTH) == (RAC1_PAD_L2 | RAC1_PAD_R2));
    CHECK(rac1_strafe_button_bits(RAC1_STRAFE_BUTTON_L2) == RAC1_PAD_L2);
    CHECK(rac1_strafe_button_bits(RAC1_STRAFE_BUTTON_R2) == RAC1_PAD_R2);
    CHECK(rac1_strafe_button_bits(7) == RAC1_STRAFE_BUTTONS);
}

/* Off: nothing strafes, L2 and R2 reach the game, nothing is latched. */
static void off(void) {
    rac1_strafe_hold hold = {0, 0, 0, 0.0f};
    rac1_strafe_input in = running();
    in.held = RAC1_PAD_R2 | CIRCLE;
    rac1_strafe_hold_update(RAC1_STRAFE_OFF, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(!hold.latched);
    CHECK(!rac1_strafe_takes_buttons(RAC1_STRAFE_OFF, &in));
    CHECK(rac1_strafe_kind(RAC1_STRAFE_OFF, &in, &hold) == RAC1_STRAFE_NONE);
    hold.latched = 1; /* even a latch left over gives nothing */
    CHECK(rac1_strafe_kind(RAC1_STRAFE_OFF, &in, &hold) == RAC1_STRAFE_NONE);
    CHECK(!rac1_strafe_turns_standing(RAC1_STRAFE_NONE, RAC1_STRAFE_STATE_STAND, &hold));
    CHECK(!rac1_strafe_swings_camera(RAC1_STRAFE_NONE, &hold));
    /* The game's own values pass through untouched. */
    CHECK(rac1_strafe_turn_target(RAC1_STRAFE_NONE, 0, 1.25f, -2.0f, &hold) == 1.25f);
    CHECK(rac1_strafe_planar_angle(RAC1_STRAFE_NONE, 99999.0f, 1.25f) == 99999.0f);
    CHECK(rac1_strafe_planar_angle(RAC1_STRAFE_NONE, 0.5f, 1.25f) == 0.5f);
}

/* Hold: L2 or R2 going down latches the facing; held, he strafes in the run, the jump and standing;
 * let go, the game's own. */
static void hold_mode(void) {
    const int mode = RAC1_STRAFE_HOLD;
    rac1_strafe_hold hold = {0, 0, 0, 0.0f};
    rac1_strafe_input in = running();
    in.slot_item = WRENCH;
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(!hold.latched && rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    CHECK(rac1_strafe_takes_buttons(mode, &in)); /* on foot: L2 and R2 are the strafe's */
    in.held = RAC1_PAD_R2;
    in.camera = -2.0f; /* the camera elsewhere: the swing starts */
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(hold.latched && hold.swinging && hold.yaw == 0.75f);
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_HELD);
    /* Held: the facing he turns to later is not latched again. */
    in.facing = -1.0f;
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(hold.yaw == 0.75f && hold.swinging);
    /* L2 does it too, and with both the latch stays. */
    in.held = RAC1_PAD_L2 | RAC1_PAD_R2;
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(hold.latched && hold.yaw == 0.75f);
    /* The jump, the alternate run and standing strafe too; other states do not. */
    const int strafing_states[] =
        {RAC1_STRAFE_STATE_RUN,
         RAC1_STRAFE_STATE_RUN_ALT,
         RAC1_STRAFE_STATE_JUMP,
         RAC1_STRAFE_STATE_STAND};

    for (unsigned i = 0; i < 4; ++i) {
        in.state = strafing_states[i];
        CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_HELD);
    }

    const int other_states[] = {0x01, 0x35, 0x0B, 0x81, 0x13, 0x77};

    for (unsigned i = 0; i < 6; ++i) {
        in.state = other_states[i];
        CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    }

    in.state = RAC1_STRAFE_STATE_RUN;
    /* A wall's own frame (the Magneboots) and a vehicle: not. */
    in.frame_mode = 1;
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    in.frame_mode = 0;
    in.vehicle_mode = 1;
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    CHECK(!rac1_strafe_takes_buttons(mode, &in));
    in.vehicle_mode = 0;
    /* The hoverboard: its tricks keep L2 and R2. */
    in.control_mode = RAC1_STRAFE_CONTROL_HOVERBOARD;
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    CHECK(!rac1_strafe_takes_buttons(mode, &in));
    in.control_mode = 0;
    /* A scene, a menu, a vendor (not game mode 0), or no level: the game keeps L2 and R2 and
     * nothing strafes. */
    in.playing = 0;
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    CHECK(!rac1_strafe_takes_buttons(mode, &in));
    in.playing = 1;
    /* L1, R1 and Circle never strafe and never latch. */
    hold.latched = 0;
    in.held = L1 | R1 | CIRCLE;
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(!hold.latched && rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    /* Let go: forgotten. */
    in.held = RAC1_PAD_L2;
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(hold.latched);
    in.held = 0;
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(!hold.latched && rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    /* Pressed in a vehicle (or in the game's own strafe state): latched once he is on foot, with the
     * facing then. */
    in.vehicle_mode = 2;
    in.held = RAC1_PAD_R2;
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(!hold.latched);
    in.vehicle_mode = 0;
    in.playing = 0; /* held through a scene: not latched either */
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(!hold.latched);
    in.playing = 1;
    in.state = RAC1_STRAFE_STATE_GAME;
    in.vehicle_mode = 0;
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(!hold.latched && !rac1_strafe_takes_buttons(mode, &in));
    in.state = RAC1_STRAFE_STATE_RUN;
    in.facing = 2.0f;
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(hold.latched && hold.yaw == 2.0f);
    /* The aimable weapon makes no difference to Hold. */
    in.slot_item = BLASTER;
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_HELD);
    CHECK(hold.swinging && rac1_strafe_swings_camera(RAC1_STRAFE_HELD, &hold));
    CHECK(!rac1_strafe_turns_standing(RAC1_STRAFE_HELD, RAC1_STRAFE_STATE_STAND, &hold));
    /* Targets: while the swing lasts the latched yaw for turns of mode 0; the move yaw for the
     * velocity. */
    hold.yaw = 0.5f;
    CHECK(rac1_strafe_turn_target(RAC1_STRAFE_HELD, 0, 1.25f, -2.0f, &hold) == 0.5f);
    CHECK(rac1_strafe_turn_target(RAC1_STRAFE_HELD, 1, 1.25f, -2.0f, &hold) == 1.25f);
    /* The swing done: the camera's, standing too, and no more swing. */
    hold.swinging = 0;
    CHECK(rac1_strafe_turn_target(RAC1_STRAFE_HELD, 0, 1.25f, -2.0f, &hold) == -2.0f);
    CHECK(rac1_strafe_turn_target(RAC1_STRAFE_HELD, 1, 1.25f, -2.0f, &hold) == 1.25f);
    CHECK(rac1_strafe_turns_standing(RAC1_STRAFE_HELD, RAC1_STRAFE_STATE_STAND, &hold));
    CHECK(!rac1_strafe_turns_standing(RAC1_STRAFE_HELD, RAC1_STRAFE_STATE_RUN, &hold));
    CHECK(!rac1_strafe_swings_camera(RAC1_STRAFE_HELD, &hold));
    CHECK(rac1_strafe_planar_angle(RAC1_STRAFE_HELD, 99999.0f, 1.25f) == 1.25f);
    CHECK(rac1_strafe_planar_angle(RAC1_STRAFE_HELD, 0.5f, 1.25f) == 0.5f);
}

/* Lock: a forward-firing weapon ready makes him face the camera without a button; anything else in
 * hand is Hold. */
static void lock_mode(void) {
    const int mode = RAC1_STRAFE_LOCK;
    rac1_strafe_hold hold = {0, 0, 0, 0.0f};
    rac1_strafe_input in = running();
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_LOCKED);
    CHECK(rac1_strafe_takes_buttons(mode, &in));
    in.state = RAC1_STRAFE_STATE_STAND;
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_LOCKED);
    CHECK(rac1_strafe_turns_standing(RAC1_STRAFE_LOCKED, in.state, &hold));
    CHECK(!rac1_strafe_turns_standing(RAC1_STRAFE_LOCKED, RAC1_STRAFE_STATE_RUN, &hold));
    CHECK(!rac1_strafe_swings_camera(RAC1_STRAFE_LOCKED, &hold));
    in.state = RAC1_STRAFE_STATE_RUN;
    /* L2 held with the weapon: still the camera's. */
    in.held = RAC1_PAD_L2;
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_LOCKED);
    /* The weapon not ready (being put away, coming out): Hold's, while L2 is held. */
    in.slot_state = 3;
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_HELD);
    in.held = 0;
    rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    in.slot_state = RAC1_STRAFE_SLOT_READY;
    /* The wrench and the gadgets: Hold. */
    const int others[] = {WRENCH, SWINGSHOT, 0x16, 0x12};

    for (unsigned i = 0; i < 4; ++i) {
        in.slot_item = others[i];
        in.held = 0;
        rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
        CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
        in.held = RAC1_PAD_R2;
        rac1_strafe_hold_update(mode, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
        CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_HELD);
    }
    /* Vehicles, walls, other states: never. */
    in.slot_item = BLASTER;
    in.vehicle_mode = 1;
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    in.vehicle_mode = 0;
    in.frame_mode = 2;
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    in.frame_mode = 0;
    in.state = 0x35; /* crouching */
    CHECK(rac1_strafe_kind(mode, &in, &hold) == RAC1_STRAFE_NONE);
    hold.swinging = 1; /* a swing never matters to Lock */
    hold.yaw = 0.5f;
    CHECK(rac1_strafe_turn_target(RAC1_STRAFE_LOCKED, 0, 1.25f, -2.0f, &hold) == -2.0f);
    CHECK(rac1_strafe_turn_target(RAC1_STRAFE_LOCKED, 1, 1.25f, -2.0f, &hold) == 1.25f);
    CHECK(!rac1_strafe_swings_camera(RAC1_STRAFE_LOCKED, &hold));
}

/* Moving: stick right (a quarter turn clockwise of the camera's forward) for a while. */
static void movement(void) {
    const float camera = 0.5f;
    const float right = -1.5707964f;
    const rac1_strafe_hold none = {0, 0, 0, 0.0f};
    const rac1_strafe_hold latched = {1, 1, 0, 1.2f}; /* swinging behind 1.2 */
    float facing, v[2];
    /* Lock: he ends facing the camera's way and moving at right angles to it. */
    facing = camera;

    for (int i = 0; i < 60; ++i) {
        tick(RAC1_STRAFE_LOCKED, camera, right, &none, &facing, v);
    }

    CHECK(fabsf(facing - camera) < 1e-4f);
    CHECK(fabsf(v[0] * cosf(camera) + v[1] * sinf(camera)) < 1e-4f); /* no forward part */
    CHECK(fabsf(v[0] - cosf(camera + right)) < 1e-4f && fabsf(v[1] - sinf(camera + right)) < 1e-4f);
    /* Hold, swinging behind 1.2 with the camera still elsewhere: he keeps 1.2 and moves where the
     * stick says. */
    facing = 1.2f;

    for (int i = 0; i < 60; ++i) {
        tick(RAC1_STRAFE_HELD, camera, right, &latched, &facing, v);
    }

    CHECK(fabsf(facing - 1.2f) < 1e-4f);
    CHECK(fabsf(v[0] - cosf(camera + right)) < 1e-4f && fabsf(v[1] - sinf(camera + right)) < 1e-4f);
    /* The game's way: he turns round to face where he goes. */
    facing = camera;

    for (int i = 0; i < 60; ++i) {
        tick(RAC1_STRAFE_NONE, camera, right, &none, &facing, v);
    }

    CHECK(fabsf(facing - (camera + right)) < 1e-4f);
    /* Back: still facing the camera's way, moving toward it. */
    facing = camera;

    for (int i = 0; i < 60; ++i) {
        tick(RAC1_STRAFE_LOCKED, camera, 3.1415927f, &none, &facing, v);
    }

    CHECK(fabsf(facing - camera) < 1e-4f && v[0] * cosf(camera) + v[1] * sinf(camera) < -0.999f);
}

/* The camera's yaw over a tick as the follow camera turns it: the right stick's turn when it is
 * pushed (the game's), else Hold's swing toward the latched facing while lock_strafe.c asks for it,
 * else left where it is (the strafe camera slides along with him, it does not turn). */
static float camera_tick(float camera, float look_x, int kind, const rac1_strafe_hold* hold) {
    if (look_x != 0.0f) {
        camera += look_x * 0.05f;
    } else if (rac1_strafe_swings_camera(kind, hold)) {
        camera += rac1_strafe_angle_diff(camera, hold->yaw) * 0.15f;
    }

    return rac1_strafe_angle_diff(0.0f, camera);
}

/* The whole of Hold, a tick at a time: the pad's report (Hold's memory), the camera, the run. */
typedef struct hold_play {
    rac1_strafe_hold hold;
    rac1_strafe_input in;
    float v[2];
    int kind;
} hold_play;

static void hold_tick(hold_play* p, uint32_t held, float look_x, float stick) {
    p->in.held = held;
    p->in.look_x = look_x;
    rac1_strafe_hold_update(RAC1_STRAFE_HOLD, RAC1_STRAFE_FEEL_CLASSIC, &p->in, &p->hold);
    p->kind = rac1_strafe_kind(RAC1_STRAFE_HOLD, &p->in, &p->hold);
    p->in.camera = camera_tick(p->in.camera, look_x, p->kind, &p->hold);
    tick(p->kind, p->in.camera, stick, &p->hold, &p->in.facing, p->v);
}

/* Hold as it plays: the swing once, then the right stick turns the camera and him with it, nothing
 * pulls the camera back, and letting go leaves the camera where it is. */
static void hold_play_through(void) {
    const float right = -1.5707964f;
    hold_play p = {{0, 0, 0, 0.0f}, running(), {0.0f, 0.0f}, 0};
    p.in.slot_item = WRENCH;
    p.in.facing = 2.0f;
    p.in.camera = -0.5f; /* the camera well off to his side */
    /* Pressed: the camera swings round behind his facing (2.0), which he keeps meanwhile. */
    int ticks = 0;
    hold_tick(&p, RAC1_PAD_R2, 0.0f, right);
    CHECK(p.kind == RAC1_STRAFE_HELD && p.hold.swinging && p.hold.yaw == 2.0f);

    while (p.hold.swinging && ticks < 200) {
        hold_tick(&p, RAC1_PAD_R2, 0.0f, right);
        CHECK(!p.hold.swinging || fabsf(p.in.facing - 2.0f) < 1e-4f);
        ++ticks;
    }

    CHECK(!p.hold.swinging && ticks < RAC1_STRAFE_SWING_TICKS); /* ended by getting there */
    CHECK(fabsf(rac1_strafe_angle_diff(p.in.camera, 2.0f)) < RAC1_STRAFE_SWING_DONE);
    /* Then he faces the camera's yaw, tick by tick. */

    for (int i = 0; i < 30; ++i) {
        hold_tick(&p, RAC1_PAD_R2, 0.0f, right);
    }

    CHECK(fabsf(rac1_strafe_angle_diff(p.in.facing, p.in.camera)) < 1e-4f);
    /* The right stick turns the camera, and his facing follows it. */
    const float before = p.in.camera;

    for (int i = 0; i < 20; ++i) {
        hold_tick(&p, RAC1_PAD_R2, 1.0f, right);
    }

    const float turned = p.in.camera;
    CHECK(fabsf(rac1_strafe_angle_diff(before, turned) - 1.0f) < 1e-4f);

    for (int i = 0; i < 30; ++i) {
        hold_tick(&p, RAC1_PAD_R2, 0.0f, right);
        /* No snap back: the stick let go, the camera stays where it was turned, no swing again. */
        CHECK(p.in.camera == turned && !p.hold.swinging);
    }

    CHECK(fabsf(rac1_strafe_angle_diff(p.in.facing, turned)) < 1e-4f);
    CHECK(fabsf(rac1_strafe_angle_diff(p.in.facing, 2.0f)) > 0.9f); /* not the press-time facing */
    /* The left stick moves him relative to it: right is at right angles, no forward part. */
    CHECK(fabsf(p.v[0] * cosf(turned) + p.v[1] * sinf(turned)) < 1e-4f);
    CHECK(
        fabsf(p.v[0] - cosf(turned + right)) < 1e-4f && fabsf(p.v[1] - sinf(turned + right)) < 1e-4f
    );
    /* Back: still facing it, moving toward the camera. */

    for (int i = 0; i < 5; ++i) {
        hold_tick(&p, RAC1_PAD_R2, 0.0f, 3.1415927f);
    }

    CHECK(fabsf(rac1_strafe_angle_diff(p.in.facing, turned)) < 1e-4f);
    CHECK(p.v[0] * cosf(turned) + p.v[1] * sinf(turned) < -0.999f);
    /* Let go: the game's own, and the camera does not move on its account. */
    const float facing = p.in.facing;
    hold_tick(&p, 0, 0.0f, 0.0f);
    CHECK(p.kind == RAC1_STRAFE_NONE && !p.hold.latched && !p.hold.swinging);
    CHECK(p.in.camera == turned);
    CHECK(fabsf(rac1_strafe_angle_diff(p.in.facing, facing)) < 1e-4f); /* stick ahead: he stays */

    /* Pressed again, the right stick pushed during the swing: the swing ends there and then, and
     * he turns with the camera from where the player takes it. */
    p.in.facing = 0.0f;
    p.in.camera = 3.0f;
    hold_tick(&p, RAC1_PAD_L2, 0.0f, 0.0f);
    CHECK(p.hold.swinging);
    hold_tick(&p, RAC1_PAD_L2, 0.5f, 0.0f);
    CHECK(!p.hold.swinging);
    const float taken = p.in.camera;

    for (int i = 0; i < 60; ++i) {
        hold_tick(&p, RAC1_PAD_L2, 0.0f, 0.0f);
        CHECK(p.in.camera == taken);
    }

    CHECK(fabsf(rac1_strafe_angle_diff(p.in.facing, taken)) < 1e-4f);

    /* A camera that cannot get there (a wall): the swing gives up after its ticks. */
    p.in.held = 0;
    rac1_strafe_hold_update(RAC1_STRAFE_HOLD, RAC1_STRAFE_FEEL_CLASSIC, &p.in, &p.hold);
    p.in.facing = 1.0f;
    p.in.camera = -1.0f;
    p.in.held = RAC1_PAD_R2;
    rac1_strafe_hold_update(RAC1_STRAFE_HOLD, RAC1_STRAFE_FEEL_CLASSIC, &p.in, &p.hold);
    CHECK(p.hold.swinging);

    for (int i = 0; i < RAC1_STRAFE_SWING_TICKS; ++i) {
        rac1_strafe_hold_update(RAC1_STRAFE_HOLD, RAC1_STRAFE_FEEL_CLASSIC, &p.in, &p.hold);
    }

    CHECK(!p.hold.swinging && p.hold.latched);
}

/* Lock as it plays: the right stick's turn stays, nothing swings the camera back. */
static void lock_play_through(void) {
    rac1_strafe_hold hold = {0, 0, 0, 0.0f};
    rac1_strafe_input in = running();
    in.camera = 0.3f;
    float facing = 0.3f, v[2];

    for (int i = 0; i < 20; ++i) {
        rac1_strafe_hold_update(RAC1_STRAFE_LOCK, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
        const int kind = rac1_strafe_kind(RAC1_STRAFE_LOCK, &in, &hold);
        CHECK(kind == RAC1_STRAFE_LOCKED);
        in.camera = camera_tick(in.camera, -1.0f, kind, &hold);
        tick(kind, in.camera, 0.0f, &hold, &facing, v);
    }

    const float turned = in.camera;

    for (int i = 0; i < 40; ++i) {
        rac1_strafe_hold_update(RAC1_STRAFE_LOCK, RAC1_STRAFE_FEEL_CLASSIC, &in, &hold);
        const int kind = rac1_strafe_kind(RAC1_STRAFE_LOCK, &in, &hold);
        in.camera = camera_tick(in.camera, 0.0f, kind, &hold);
        tick(kind, in.camera, 0.0f, &hold, &facing, v);
        CHECK(in.camera == turned);
    }

    CHECK(fabsf(rac1_strafe_angle_diff(facing, turned)) < 1e-4f);
}

/* The backpedal: when it plays the walk or the run backwards, and the step back itself. */
static void backpedal(void) {
    const float deg = 0.017453292f;
    const int run = RAC1_STRAFE_STATE_RUN;
    /* Strafing in the run: straight back and up to 110 degrees off, the edges with their hysteresis. */
    CHECK(rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_CLASSIC, run, 0.5f, 0.5f + 3.1415927f, 0
    ));
    CHECK(rac1_strafe_backpedal(
        RAC1_STRAFE_LOCKED,
        RAC1_STRAFE_FEEL_CLASSIC,
        RAC1_STRAFE_STATE_RUN_ALT,
        0.5f,
        0.5f - 120.0f * deg,
        0
    ));
    CHECK(!rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_CLASSIC, run, 0.5f, 0.5f + 105.0f * deg, 0
    )); /* not yet */
    CHECK(rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_CLASSIC, run, 0.5f, 0.5f + 105.0f * deg, 1
    )); /* kept */
    CHECK(!rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_CLASSIC, run, 0.5f, 0.5f + 95.0f * deg, 1
    )); /* left */
    CHECK(!rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_CLASSIC, run, 0.5f, 0.5f - 90.0f * deg, 0
    )); /* sideways */
    CHECK(
        !rac1_strafe_backpedal(RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_CLASSIC, run, 0.5f, 0.5f, 1)
    ); /* forward */
    /* Across the wrap of the angles: facing 170, moving at -30 (160 off). */
    CHECK(rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_CLASSIC, run, 170.0f * deg, -30.0f * deg, 0
    ));
    /* Not strafing, or not in the run (standing, the jump): the game's. */
    CHECK(!rac1_strafe_backpedal(
        RAC1_STRAFE_NONE, RAC1_STRAFE_FEEL_CLASSIC, run, 0.5f, 0.5f + 3.1415927f, 1
    ));
    CHECK(!rac1_strafe_backpedal(
        RAC1_STRAFE_HELD,
        RAC1_STRAFE_FEEL_CLASSIC,
        RAC1_STRAFE_STATE_JUMP,
        0.5f,
        0.5f + 3.1415927f,
        0
    ));
    CHECK(!rac1_strafe_backpedal(
        RAC1_STRAFE_HELD,
        RAC1_STRAFE_FEEL_CLASSIC,
        RAC1_STRAFE_STATE_STAND,
        0.5f,
        0.5f + 3.1415927f,
        0
    ));

    /* The step back, a sequence of 4 keys at rates 0.5, 0.25, 0.5, 1. */
    const float rates[4] = {0.5f, 0.25f, 0.5f, 1.0f};
    rac1_strafe_anim a = {2, 3, 0.6f, 0.5f};
    rac1_strafe_anim_back(&a, 0.25f, 4, rates); /* within the interval */
    CHECK(a.key == 2 && a.next_key == 3 && fabsf(a.fraction - 0.35f) < 1e-6f && a.rate == 0.5f);
    rac1_strafe_anim_back(&a, 0.45f, 4, rates); /* 0.1 below: 0.2 ticks into key 1's interval */
    CHECK(
        a.key == 1 && a.next_key == 2 && a.rate == 0.25f
        && fabsf(a.fraction - (1.0f - 0.2f * 0.25f)) < 1e-6f
    );
    /* Back past key 0: the last key and key 0, its rate. */
    a = (rac1_strafe_anim){0, 1, 0.1f, 0.5f};
    rac1_strafe_anim_back(&a, 0.2f, 4, rates);
    CHECK(
        a.key == 3 && a.next_key == 0 && a.rate == 1.0f && fabsf(a.fraction - (1.0f - 0.2f)) < 1e-6f
    );
    /* Several keys in one tick. */
    a = (rac1_strafe_anim){3, 0, 0.5f, 1.0f};
    rac1_strafe_anim_back(
        &a, 1.0f, 4, rates
    ); /* 0.5 ticks over: key 2 (rate 0.5) takes 0.25 of it */
    CHECK(a.key == 2 && a.next_key == 3 && fabsf(a.fraction - 0.75f) < 1e-6f);
    /* Never left where the advance would round it up to the next key. */
    a = (rac1_strafe_anim){2, 3, 0.001f, 0.5f};
    rac1_strafe_anim_back(&a, 0.002f, 4, rates);
    CHECK(a.key == 1 && a.fraction <= RAC1_STRAFE_BACK_TOP);
    /* A whole cycle backwards visits every key in reverse and comes back, and a key of no rate or a
     * huge step stops the loop. */
    a = (rac1_strafe_anim){0, 1, 0.5f, 0.5f};
    int seen = 0;

    for (int i = 0; i < 200 && seen < 5; ++i) {
        const int before = a.key;
        rac1_strafe_anim_back(&a, 0.1f, 4, rates);

        if (a.key != before) {
            CHECK(a.key == (before + 3) % 4 && a.next_key == before);
            ++seen;
        }
    }

    CHECK(seen == 5);
    const float stuck[4] = {0.5f, 0.0f, 0.5f, 0.5f};
    a = (rac1_strafe_anim){2, 3, 0.1f, 0.5f};
    rac1_strafe_anim_back(&a, 0.5f, 4, stuck);
    CHECK(
        a.key == 1 && a.fraction == RAC1_STRAFE_BACK_TOP
    ); /* a key of no rate holds, as forward */
    a = (rac1_strafe_anim){2, 3, 0.1f, 0.5f};
    rac1_strafe_anim_back(&a, 1e30f, 4, rates);
    CHECK(a.fraction >= 0.0f && a.fraction <= RAC1_STRAFE_BACK_TOP);
}

/* Going Commando's feel: Hold with no swing, its turn rates, its backpedal edges. */
static void commando(void) {
    const float deg = 0.017453292f;
    const int run = RAC1_STRAFE_STATE_RUN;
    rac1_strafe_input in;
    rac1_strafe_hold hold;
    memset(&in, 0, sizeof in);
    memset(&hold, 0, sizeof hold);
    in.playing = 1;
    in.state = run;
    in.facing = 0.3f;
    in.camera = 1.2f;
    in.held = RAC1_PAD_L2;
    /* L2 goes down: latched, no swing, and the turn goes for the camera at once. */
    rac1_strafe_hold_update(RAC1_STRAFE_HOLD, RAC1_STRAFE_FEEL_COMMANDO, &in, &hold);
    CHECK(hold.latched && !hold.swinging);
    const int kind = rac1_strafe_kind(RAC1_STRAFE_HOLD, &in, &hold);
    CHECK(kind == RAC1_STRAFE_HELD && !rac1_strafe_swings_camera(kind, &hold));
    CHECK(rac1_strafe_turn_target(kind, 0, -1.0f, in.camera, &hold) == in.camera);
    /* Standing, he turns to the camera from the first tick. */
    CHECK(rac1_strafe_turns_standing(kind, RAC1_STRAFE_STATE_STAND, &hold));

    /* The rates: on the ground 0.037 / 0.25 / 12.2 rad/s, in the air 0.015 / 0.2 / 15, scaled. */
    float accel, damping, limit;
    rac1_strafe_commando_rates(run, 1.44f, 0.02f, &accel, &damping, &limit);
    CHECK(fabsf(accel - 1.44f * 0.037f) < 1e-7f && fabsf(damping - 1.44f * 0.25f) < 1e-7f);
    CHECK(fabsf(limit - 0.02f * 12.2173061f) < 1e-7f);
    rac1_strafe_commando_rates(
        RAC1_STRAFE_STATE_JUMP, 1.0f, 1.0f / 60.0f, &accel, &damping, &limit
    );
    CHECK(accel == 0.015f && damping == 0.2f && fabsf(limit - 0.25f) < 1e-6f);

    /* The backpedal within 45 degrees of straight back: 140 starts it, 132 keeps it, 128 ends it,
     * 120 never starts it. */
    CHECK(rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_COMMANDO, run, 0.5f, 0.5f + 140.0f * deg, 0
    ));
    CHECK(!rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_COMMANDO, run, 0.5f, 0.5f + 120.0f * deg, 0
    ));
    CHECK(rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_COMMANDO, run, 0.5f, 0.5f - 132.0f * deg, 1
    ));
    CHECK(!rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_COMMANDO, run, 0.5f, 0.5f - 128.0f * deg, 1
    ));
}

/* The sides and the side reversal: going left after right stops him, going right after left eases
 * back in over four ticks. */
static void reversal(void) {
    const float deg = 0.017453292f;
    CHECK(rac1_strafe_side(1.0f, 1.0f + 90.0f * deg) == RAC1_STRAFE_SIDE_LEFT);
    CHECK(rac1_strafe_side(1.0f, 1.0f + 50.0f * deg) == RAC1_STRAFE_SIDE_LEFT);
    CHECK(rac1_strafe_side(1.0f, 1.0f + 40.0f * deg) == RAC1_STRAFE_SIDE_NONE);
    CHECK(rac1_strafe_side(1.0f, 1.0f - 90.0f * deg) == RAC1_STRAFE_SIDE_RIGHT);
    CHECK(rac1_strafe_side(1.0f, 1.0f + 180.0f * deg) == RAC1_STRAFE_SIDE_NONE);
    /* Across the wrap: facing 170, moving at -100 (90 to his left). */
    CHECK(rac1_strafe_side(170.0f * deg, -100.0f * deg) == RAC1_STRAFE_SIDE_LEFT);

    rac1_strafe_reverse r = {0, 0};
    CHECK(rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_RIGHT) == 1.0f);
    CHECK(rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_RIGHT) == 1.0f);
    /* Left now: the right is recent, his speed goes (rac2's -0.25, never below 0 here). */
    CHECK(rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_LEFT) == 0.0f);
    CHECK(rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_LEFT) == 0.0f);
    /* The right fades after five ticks; then left goes on at full speed. */

    for (int i = 0; i < 3; ++i) {
        rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_LEFT);
    }

    CHECK(rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_LEFT) == 1.0f);
    /* Right after left: 0, 0.25, 0.5, 0.75, then 1 (the left counting down 4, 3, 2, 1, 0). */
    CHECK(rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_RIGHT) == 0.0f);
    CHECK(rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_RIGHT) == 0.25f);
    CHECK(rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_RIGHT) == 0.5f);
    CHECK(rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_RIGHT) == 0.75f);
    CHECK(rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_RIGHT) == 1.0f);
    /* Forward or back: no side, nothing changed. */
    CHECK(rac1_strafe_reverse_step(&r, RAC1_STRAFE_SIDE_NONE) == 1.0f);
}

/* The hip twist: its target, and the springs that ease the pelvis and the spine to it and back. */
static void twist(void) {
    const float deg = 0.017453292f;
    /* Strafing right with the stick pushed: the pelvis toward the move, -90 degrees. */
    CHECK(
        fabsf(
            rac1_strafe_twist_target(RAC1_STRAFE_HELD, 1.0f, 0, 0.5f, 0.5f - 90.0f * deg)
            + 90.0f * deg
        )
        < 1e-5f
    );
    /* Across the wrap, the short way round. */
    CHECK(
        fabsf(
            rac1_strafe_twist_target(RAC1_STRAFE_LOCKED, 1.0f, 0, 170.0f * deg, -100.0f * deg)
            - 90.0f * deg
        )
        < 1e-4f
    );
    /* Not strafing, the stick near its centre, or backpedalling: none. */
    CHECK(rac1_strafe_twist_target(RAC1_STRAFE_NONE, 1.0f, 0, 0.5f, 2.0f) == 0.0f);
    CHECK(rac1_strafe_twist_target(RAC1_STRAFE_HELD, 0.15f, 0, 0.5f, 2.0f) == 0.0f);
    CHECK(rac1_strafe_twist_target(RAC1_STRAFE_HELD, 1.0f, 1, 0.5f, 2.0f) == 0.0f);

    /* The spring's first tick from rest: velocity accel x distance, the angle moved by it. */
    lock_strafe_spring s = {0.0f, 0.0f};
    lock_strafe_spring_step(&s, 1.0f, 0.037f, 0.27f);
    CHECK(fabsf(s.velocity - 0.037f) < 1e-7f && fabsf(s.angle - 0.037f) < 1e-7f);
    /* It gets there without passing it (the velocity held within the distance left), and holds. */

    for (int i = 0; i < 600; ++i) {
        lock_strafe_spring_step(&s, 1.0f, 0.037f, 0.27f);
        CHECK(s.angle <= 1.0f + 1e-6f);
    }

    CHECK(fabsf(s.angle - 1.0f) < 1e-3f);
    /* Back to 0 through 0's side, never past it, and then at rest. */

    for (int i = 0; i < 600; ++i) {
        lock_strafe_spring_step(&s, 0.0f, 0.025f, 0.27f);
        CHECK(s.angle >= -1e-6f);
    }

    CHECK(lock_strafe_spring_still(&s));
    /* From one side to the other it goes straight through 0, not round the back. */
    s = (lock_strafe_spring){2.5f, 0.0f};
    lock_strafe_spring_step(&s, -2.5f, 0.037f, 0.27f);
    CHECK(s.velocity < 0.0f && s.angle < 2.5f);
}

/* Going Commando's quaternion for a turn about one axis (func_00283898, axis 2): a = -FastSin,
 * b = FastCos, root = sqrt(|2b| + 2), (a / root, root / 2) in the x and w slots (swapped when 2b is
 * below 0), the x slot's value then moved to z. */
static void rac2_axis_quaternion(float angle, float q[4]) {
    const float a = -sinf(angle);
    const float b = cosf(angle);
    const float twob = b + b;
    const float root = sqrtf(fabsf(twob) + 2.0f);
    const float half = 0.5f * root;
    const float ratio = a / root;
    q[0] = 0.0f;
    q[1] = 0.0f;
    q[2] = twob < 0.0f ? half : ratio;
    q[3] = twob < 0.0f ? ratio : half;
}

/* The joint's x axis (where a forward-walking leg points) in its parent's frame, x and y: the first
 * row of the quaternion's rotation, which the game's joint matrices take as the joint's first axis
 * (OpenRAC's moby evaluator, quat_rows: 1 - 2(y^2 + z^2), 2(xy - zw)). */
static void joint_forward(const float q[4], float out[2]) {
    out[0] = 1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2]);
    out[1] = 2.0f * (q[0] * q[1] - q[2] * q[3]);
}

/* The modifier composed with what is already there, as the evaluator's mode 0: q times r. */
static void quaternion_multiply(const float q[4], const float r[4], float out[4]) {
    out[0] = q[3] * r[0] + r[3] * q[0] + (q[1] * r[2] - q[2] * r[1]);
    out[1] = q[3] * r[1] + r[3] * q[1] + (q[2] * r[0] - q[0] * r[2]);
    out[2] = q[3] * r[2] + r[3] * q[2] + (q[0] * r[1] - q[1] * r[0]);
    out[3] = q[3] * r[3] - (q[0] * r[0] + q[1] * r[1] + q[2] * r[2]);
}

/* The twist's quaternion turns the joint the game's way: the same turn as Going Commando builds for
 * the same angle (up to the sign of the whole quaternion), and a positive angle points the joint's
 * front toward his left. 1.1.0 sent the sine the other way round: his hips turned away from where
 * he went, the moonwalk. */
static void joint_yaw(void) {
    const float deg = 0.017453292f;

    for (int d = -135; d <= 135; d += 5) {
        const float a = (float)d * deg;
        float q[4];
        float g[4];
        float f[2];
        rac1_strafe_joint_yaw(a, q);
        rac2_axis_quaternion(a, g);
        const float dot = q[0] * g[0] + q[1] * g[1] + q[2] * g[2] + q[3] * g[3];
        CHECK(fabsf(fabsf(dot) - 1.0f) < 1e-5f);
        joint_forward(q, f);
        CHECK(fabsf(f[0] - cosf(a)) < 1e-5f && fabsf(f[1] - sinf(a)) < 1e-5f);
    }

    /* A quarter turn left: z = -sin(45 degrees), his front to his left (0, 1). */
    float q[4];
    float f[2];
    rac1_strafe_joint_yaw(90.0f * deg, q);
    CHECK(q[0] == 0.0f && q[1] == 0.0f && q[2] < -0.7f && q[3] > 0.7f);
    joint_forward(q, f);
    CHECK(fabsf(f[0]) < 1e-5f && fabsf(f[1] - 1.0f) < 1e-5f);
}

/* Where his legs walk for each way the stick points, facing ahead (the camera's yaw): the pelvis
 * toward where he goes and the spine back, so his legs walk forward along his velocity and his
 * body faces ahead; within 45 degrees of straight back, no twist and the walk or run played
 * backwards (rac1 has no backpedal animation of its own). Never forward-walking legs pointing
 * away from where he moves: the moonwalk. */
static void directions(void) {
    const float deg = 0.017453292f;
    const float facing = 0.4f;
    int was = 0;

    for (int d = -180; d < 180; d += 5) {
        /* The edge itself (135 degrees) is either, by a float's last bit. */
        if (abs(d) == 135) {
            continue;
        }

        const float stick = (float)d * deg;
        const float move_yaw = rac1_strafe_angle_diff(0.0f, facing + stick);
        const float v[2] = {cosf(move_yaw), sinf(move_yaw)};
        const int back = rac1_strafe_backpedal(
            RAC1_STRAFE_LOCKED, RAC1_STRAFE_FEEL_COMMANDO, RAC1_STRAFE_STATE_RUN, facing, move_yaw, 0
        );
        const float twist =
            rac1_strafe_twist_target(RAC1_STRAFE_LOCKED, 1.0f, back, facing, move_yaw);
        float pelvis[4];
        float spine[4];
        float upper[4];
        float legs[2];
        float body[2];
        rac1_strafe_joint_yaw(twist, pelvis);
        rac1_strafe_joint_yaw(-twist, spine);
        quaternion_multiply(pelvis, spine, upper);
        joint_forward(pelvis, legs);
        joint_forward(upper, body);
        /* Into the world, by his facing. */
        const float lw[2] = {
            cosf(facing) * legs[0] - sinf(facing) * legs[1],
            sinf(facing) * legs[0] + cosf(facing) * legs[1]
        };
        const float bw[2] = {
            cosf(facing) * body[0] - sinf(facing) * body[1],
            sinf(facing) * body[0] + cosf(facing) * body[1]
        };
        /* The body above the spine always faces ahead. */
        CHECK(fabsf(bw[0] - cosf(facing)) < 1e-4f && fabsf(bw[1] - sinf(facing)) < 1e-4f);

        if (abs(d) > 135) {
            /* Back: the walk played backwards, legs ahead, so they step back along his velocity. */
            CHECK(back && twist == 0.0f);
            CHECK(lw[0] * v[0] + lw[1] * v[1] < -0.7f);
        } else {
            /* Else forward, the legs along his velocity. */
            CHECK(!back);
            CHECK(lw[0] * v[0] + lw[1] * v[1] > 0.9999f);
        }
    }

    /* The four ways by name. Left (+90 degrees from his facing): the left side, legs to his left. */
    float q[4];
    float f[2];
    const float left = rac1_strafe_twist_target(RAC1_STRAFE_HELD, 1.0f, 0, 0.0f, 90.0f * deg);
    CHECK(rac1_strafe_side(0.0f, 90.0f * deg) == RAC1_STRAFE_SIDE_LEFT);
    rac1_strafe_joint_yaw(left, q);
    joint_forward(q, f);
    CHECK(f[1] > 0.9999f);
    /* Right: the right side, legs to his right. */
    const float right = rac1_strafe_twist_target(RAC1_STRAFE_HELD, 1.0f, 0, 0.0f, -90.0f * deg);
    CHECK(rac1_strafe_side(0.0f, -90.0f * deg) == RAC1_STRAFE_SIDE_RIGHT);
    rac1_strafe_joint_yaw(right, q);
    joint_forward(q, f);
    CHECK(f[1] < -0.9999f);
    /* Forward: no side, no twist, no backpedal. */
    CHECK(rac1_strafe_side(0.0f, 0.0f) == RAC1_STRAFE_SIDE_NONE);
    CHECK(rac1_strafe_twist_target(RAC1_STRAFE_HELD, 1.0f, 0, 0.0f, 0.0f) == 0.0f);
    was = rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_COMMANDO, RAC1_STRAFE_STATE_RUN, 0.0f, 0.0f, 0
    );
    CHECK(!was);
    /* Back: the backpedal (the walk or run played backwards), never forward legs. */
    was = rac1_strafe_backpedal(
        RAC1_STRAFE_HELD, RAC1_STRAFE_FEEL_COMMANDO, RAC1_STRAFE_STATE_RUN, 0.0f, RAC1_STRAFE_PI, 0
    );
    CHECK(was);
    CHECK(rac1_strafe_twist_target(RAC1_STRAFE_HELD, 1.0f, was, 0.0f, RAC1_STRAFE_PI) == 0.0f);
    CHECK(rac1_strafe_side(0.0f, RAC1_STRAFE_PI) == RAC1_STRAFE_SIDE_NONE);
}

/* The strafe flip: X pressed while strafing on the ground with the stick pushed fully. */
static void flips(void) {
    const uint32_t x = RAC1_PAD_X;
    CHECK(rac1_strafe_flips(RAC1_STRAFE_HELD, RAC1_STRAFE_STATE_STAND, x, 1.0f));
    CHECK(rac1_strafe_flips(RAC1_STRAFE_LOCKED, RAC1_STRAFE_STATE_RUN, x | RAC1_PAD_L2, 0.95f));
    CHECK(rac1_strafe_flips(RAC1_STRAFE_HELD, RAC1_STRAFE_STATE_RUN_ALT, x, 0.91f));
    /* The stick not past 0.9, X not pressed, in the air, or not strafing: the game's jump. */
    CHECK(!rac1_strafe_flips(RAC1_STRAFE_HELD, RAC1_STRAFE_STATE_RUN, x, 0.9f));
    CHECK(!rac1_strafe_flips(RAC1_STRAFE_HELD, RAC1_STRAFE_STATE_RUN, 0x80u, 1.0f));
    CHECK(!rac1_strafe_flips(RAC1_STRAFE_HELD, RAC1_STRAFE_STATE_JUMP, x, 1.0f));
    CHECK(!rac1_strafe_flips(RAC1_STRAFE_NONE, RAC1_STRAFE_STATE_RUN, x, 1.0f));
}

int main(void) {
    items();
    buttons();
    off();
    hold_mode();
    lock_mode();
    movement();
    hold_play_through();
    lock_play_through();
    backpedal();
    commando();
    reversal();
    twist();
    joint_yaw();
    directions();
    flips();
    puts(
        "hero strafe: weapons, buttons, off, hold, lock, the buttons taken, facing, velocity, "
        "swing, right stick, backpedal, Going Commando's feel, side reversal, hip twist, its "
        "joints' turn, the legs for each direction and flips passed"
    );
    return 0;
}
