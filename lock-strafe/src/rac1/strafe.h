/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: the strafe decision (the lock-strafe mod, lock_strafe.cpp), apart from game memory
 * so that a test can run it (tests/hero_strafe_test.c). Moved from OpenRAC's port/game/rac1/host.
 *
 * How the game turns the stick into a heading. Each tick the hero's state code calls the stick
 * handler (Hero_SetHeadingFromStick, func_L00_00211F80), which sets the hero's move yaw (+0x180) to
 * the stick's angle plus the camera's yaw (D_L00_00166ED8): where the stick points on the screen.
 * The state then eases the hero's facing (rot.z, +0x98) toward the move yaw (Hero_TurnToHeading,
 * func_L00_002124E8), and the run builds his velocity along his facing (Hero_SetPlanarVelocity,
 * func_L00_00212790, given an angle above pi: "the hero's own"), so he turns first and runs where
 * he faces. Two places in the game do otherwise, and the strafe is built from them:
 *
 * - the run's start from a standstill with the stick pushed all the way (+0x3B8) asks for the
 *   velocity along the move yaw instead of the facing: he moves where the stick points before he
 *   has turned;
 * - in state 0x81, while L2 or R2 is held, the state puts the camera's yaw in the move yaw around
 *   its call of Hero_TurnToHeading and puts the stick's back afterwards: he faces the camera while
 *   the stick moves him; the follow camera's per-tick requests (func_L00_002E74B0) then ask for a
 *   strafe camera that slides along with him instead of swinging round to look where he goes.
 *
 * On foot the game gives the shoulder buttons in pairs: L1 or L2 looks in first person (state 1,
 * from standing: held & 5), R1 or R2 crouches (held & 0xA: the crouch, the crouch-jumps, the
 * slide from a run). With the setting on, in play on foot, the game sees L2 and R2 as not held
 * (lock_strafe.cpp takes them out of the pad's state at each report, as the game's own input locks take
 * buttons out), so L1 still looks and R1 still crouches, and L2 and R2 strafe. State 0x81 keeps
 * them: it has its strafe of its own on them. Scenes, menus, vendors, vehicles and the hoverboard keep them too.
 *
 * Two ways to strafe, the later games':
 *
 * - Hold (Going Commando's): when L2 or R2 goes down the camera swings round, once, behind the
 *   facing he has then, and he keeps that facing while it swings. From then on, as long as it is
 *   held, he faces the camera's yaw as Lock does: the right stick (the game's own camera control)
 *   turns the camera and him with it, and the stick moves him sideways, back and forward along the
 *   move yaw. The swing ends when the camera is behind him, when the right stick is pushed
 *   sideways, or after a while (a wall or a camera volume can keep it from getting there); nothing
 *   pulls the camera back to the facing he had at the press after that.
 * - Lock (Up Your Arsenal's lock-strafe): with a forward-firing weapon ready in hand
 *   (rac1_strafe_aimable) he always faces the camera's yaw, which the right stick turns (the game's
 *   own camera control), and the stick moves him; the camera does not swing to follow where he
 *   goes. With anything else in hand, Hold.
 *
 * Either is done in the run (states 2 and 0x73) and the jump (7), on level ground; both also turn
 * him standing (state 0) with the camera (Hold once its swing is done). Everything else is the game's own.
 *
 * Two feels (the setting "feel", DESIGN.md): Going Commando's (the default) has no swing in Hold (he
 * turns to the camera, the camera stays), turns him at rac2's strafe rates, kills his momentum when
 * he goes from one side to the other, and backpedals only within 45 degrees of straight back; the
 * classic one is the mod's 1.0 behaviour. Apart from the feel: the hip twist (his pelvis turned
 * toward where he goes and his spine back toward the camera, rac2's joints 9 and 10 on rac1's 1 and
 * 2) and the strafe flips (X with the stick pushed fully sideways or back: rac1's own side and back
 * flip, state 0xB, entered from the strafe as rac2 does).
 *
 * This file is plain C with no game memory: each function takes what it reads as a struct and
 * returns the answer, so tests/hero_strafe_test.c can run the decision alone. Its addresses and
 * offsets are the decomp's, cited where they appear (seen in game code), and lock_strafe.cpp holds
 * the rest of the mod (the settings, the hook points and the game memory).
 */
#ifndef OPENRAC_RAC1_STRAFE_H
#define OPENRAC_RAC1_STRAFE_H

#include <stdint.h>

/* ---- Constants ---- */

/* The mode (the mod's setting "mode"; off is the mod switched off, the game's own). */
#define RAC1_STRAFE_OFF 0
#define RAC1_STRAFE_HOLD 1
#define RAC1_STRAFE_LOCK 2

/* The pad's button bits as the game keeps them (PadState.held): L2 and R2, the strafe's. */
#define RAC1_PAD_L2 0x1u
#define RAC1_PAD_R2 0x2u
#define RAC1_STRAFE_BUTTONS (RAC1_PAD_L2 | RAC1_PAD_R2)
/* Cross, the jump (try_start_hero_jump's pad_pressed_within(0x40, ...)). */
#define RAC1_PAD_X 0x40u

/* The feel (the mod's setting "feel"). */
#define RAC1_STRAFE_FEEL_CLASSIC 0
#define RAC1_STRAFE_FEEL_COMMANDO 1

/* The hero's states the strafe works in: standing, running, running with +0x20A9 set (the same
 * movement code), jumping. */
#define RAC1_STRAFE_STATE_STAND 0x00
#define RAC1_STRAFE_STATE_RUN 0x02
#define RAC1_STRAFE_STATE_RUN_ALT 0x73
#define RAC1_STRAFE_STATE_JUMP 0x07
/* The side and back flip (HERO_STATE_HIGH_JUMP; rac2's state 11, the same state). */
#define RAC1_STRAFE_STATE_FLIP 0x0B
/* The state with a strafe of its own on L2 and R2: left as the game has it. */
#define RAC1_STRAFE_STATE_GAME 0x81
/* The hero's control mode on the hoverboard (hero +0x208C; set by state 0x6B in Rilgar and Kalebo
 * III, seen in game code): its tricks are on L2 and R2, so the game keeps them. */
#define RAC1_STRAFE_CONTROL_HOVERBOARD 0x16
/* Slot 0 ready (HeroItemSlot.state). */
#define RAC1_STRAFE_SLOT_READY 2

/* What a strafe does this tick. */
#define RAC1_STRAFE_NONE 0
#define RAC1_STRAFE_HELD 1   /* L2 or R2 held: the swing behind his facing, then the camera's yaw */
#define RAC1_STRAFE_LOCKED 2 /* facing the camera's yaw */

/* Hold's swing ends with the camera's yaw this near the latched facing (radians), the right stick
 * pushed this far sideways (the game's own test for the stick moved, CameraType0_HeroTweaks), or
 * after this many ticks. */
#define RAC1_STRAFE_SWING_DONE 0.05f
#define RAC1_STRAFE_SWING_STICK 0.3f
#define RAC1_STRAFE_SWING_TICKS 120

/* Going Commando's strafe turn, a tick at 60 Hz: the accel and damping (scaled by the tick's
 * g_frame_scale_sq, as rac1 scales its own) and the limit in radians a second (times g_frame_time):
 * on the ground func_L00_002B35A0's (0.037, 0.25, 0.2036 a frame), in the air the jump group's
 * (0.015, 0.2, 0.25 a frame). Seen in rac2's code. */
#define RAC1_STRAFE_TURN_ACCEL 0.037f
#define RAC1_STRAFE_TURN_DAMPING 0.25f
#define RAC1_STRAFE_TURN_LIMIT 12.2173061f
#define RAC1_STRAFE_AIR_ACCEL 0.015f
#define RAC1_STRAFE_AIR_DAMPING 0.2f
#define RAC1_STRAFE_AIR_LIMIT 15.0f

/* The side reversal (func_L00_002B35A0's timers +0x146C / +0x146E): ticks a side stays recent. */
#define RAC1_STRAFE_SIDE_TICKS 5
#define RAC1_STRAFE_SIDE_NONE 0
#define RAC1_STRAFE_SIDE_LEFT 1
#define RAC1_STRAFE_SIDE_RIGHT 2

/* The hip twist (func_L00_002B7B80): the stick past this, the joints (rac1's pelvis and first spine
 * joint, standing for rac2's 9 and 10), and the springs' accel (pelvis, spine) and damping, a tick
 * at 60 Hz, scaled as the turn's. Seen in rac2's code; the joints from rac1's skeleton. */
#define RAC1_STRAFE_TWIST_STICK 0.15f
#define RAC1_STRAFE_JOINT_PELVIS 1
#define RAC1_STRAFE_JOINT_SPINE 2
#define RAC1_STRAFE_TWIST_PELVIS_ACCEL 0.037f
#define RAC1_STRAFE_TWIST_SPINE_ACCEL 0.025f
#define RAC1_STRAFE_TWIST_DAMPING 0.27f
/* The port turns a joint about its z the other way from the hero's yaw (hero_set_joints), so the
 * twist's angle is sent negated. Sent as it was, his hips turned away from where he went and he
 * moonwalked (reported on 1.1.0). */
#define RAC1_STRAFE_TWIST_SENSE (-1.0f)

/* The strafe flip (func_L00_002C9FC8): the stick past this, the direction it must not be
 * (stick_direction_vs_facing: 0 left, 1 right, 2 forward, 3 back), and the drift it starts with
 * (state 0xB's +0x4DC, 0.0833 a frame: 5 units a second). Seen in rac2's code. */
#define RAC1_STRAFE_FLIP_STICK 0.9f
#define RAC1_STRAFE_DIRECTION_FORWARD 2
#define RAC1_STRAFE_FLIP_DRIFT 5.0f

/* Pi and two pi as 32-bit floats, as the game's own angle arithmetic has them (seen in game code). */
#define RAC1_STRAFE_PI 3.14159274f
#define RAC1_STRAFE_TWO_PI 6.28318548f

/* The backpedal (explained below): the walk and the run sequences, by index, whose cycle is played
 * backwards (seen in game code); the move yaw's angle from the facing at which the backpedal starts
 * and at which it stops (assumed: chosen to avoid flicker at the edge); and the highest blend the
 * mod leaves, since the advance takes one from 0.99 up as 1 and steps forward (assumed). */
#define RAC1_STRAFE_SEQ_WALK 3
#define RAC1_STRAFE_SEQ_RUN 4
#define RAC1_STRAFE_BACK_ENTER 1.91986218f /* 110 degrees */
#define RAC1_STRAFE_BACK_LEAVE 1.74532926f /* 100 degrees */
/* Going Commando's: within 45 degrees of straight back (func_L00_002B39F0), leaving 5 degrees on. */
#define RAC1_STRAFE_BACK_ENTER_COMMANDO 2.35619449f /* 135 degrees */
#define RAC1_STRAFE_BACK_LEAVE_COMMANDO 2.26892803f /* 130 degrees */
/* The highest blend lock_strafe.cpp leaves: the advance takes one from 0.99 up as 1 and steps forward. */
#define RAC1_STRAFE_BACK_TOP 0.985f

/* ---- Types ---- */

/* What the decision reads of the hero and the pad. */
typedef struct rac1_strafe_input {
    int playing; /* a level loaded and its game mode 0 (D_L00_0015F6A8: not a scene, a menu, a vendor) */
    int state;        /* hero +0x2084 */
    int frame_mode;   /* hero +0x20B3: 0 on level ground; others turn the hero's own frame */
    int vehicle_mode; /* hero +0x20A4: 0 on foot */
    int control_mode; /* hero +0x208C: RAC1_STRAFE_CONTROL_HOVERBOARD on the board */
    int slot_state;   /* hero +0x1090 + 0x24: slot 0's state */
    int slot_item;    /* hero +0x1090 + 0x28: slot 0's item */
    uint32_t
        held; /* the pad's buttons as the player holds them (+0x1A0, L2 and R2 from before lock_strafe.cpp takes them) */
    float facing; /* hero +0x98 */
    float camera; /* the camera's yaw (D_L00_00166ED8) */
    float look_x; /* the right stick sideways (PadState.analog[0], +0x100) */
} rac1_strafe_input;

/* What Hold remembers from tick to tick. */
typedef struct rac1_strafe_hold {
    int latched;  /* L2 or R2 held since it went down on foot */
    int swinging; /* the camera still swinging round behind yaw */
    int ticks;    /* ticks of the swing so far */
    float yaw;    /* the facing when it went down */
} rac1_strafe_hold;

/* ---- Decision ---- */

/* b - a, within -pi..pi. */
static inline float rac1_strafe_angle_diff(float a, float b) {
    float d = b - a;

    // Above pi: wrap down by a turn, so the shortest way round is found.
    while (d > RAC1_STRAFE_PI) {
        d -= RAC1_STRAFE_TWO_PI;
    }

    // Below minus pi: wrap up by a turn.
    while (d < -RAC1_STRAFE_PI) {
        d += RAC1_STRAFE_TWO_PI;
    }

    return d;
}

/* The items that fire forward or at what the hero faces, Lock's weapons (slot 0's item ids):
 * the Suck Cannon, the Bomb Glove, the Devastator, the Blaster, the Pyrocitor, the Tesla Claw, the
 * Morph-o-Ray and the R.Y.N.O. Not the wrench (8), nor what does not fire ahead: the Swingshot,
 * the Visibomb (a guided rocket seen from it), the Taunter, the Mine Glove, the Walloper (a
 * punch), the Glove of Doom (its agents find their own way), the Hydrodisplacer, the Drone Device,
 * the Decoy Glove and the gadgets. */
static inline int rac1_strafe_aimable(int item) {
    // Each of Lock's weapons answers yes; every other item answers no.
    switch (item) {
        case 0x09: /* Suck Cannon */
        case 0x0A: /* Bomb Glove */
        case 0x0B: /* Devastator */
        case 0x0F: /* Blaster */
        case 0x10: /* Pyrocitor */
        case 0x13: /* Tesla Claw */
        case 0x15: /* Morph-o-Ray */
        case 0x17: /* R.Y.N.O. */
            return 1;
        default:
            return 0;
    }
}

/* In play on foot, in a state the setting changes L2 and R2 in. */
static inline int rac1_strafe_on_foot(const rac1_strafe_input* in) {
    return in->playing && in->vehicle_mode == 0
           && in->control_mode != RAC1_STRAFE_CONTROL_HOVERBOARD
           && in->state != RAC1_STRAFE_STATE_GAME;
}

/* Whether the game sees L2 and R2 as not held this tick: the setting on, in play on foot. */
static inline int rac1_strafe_takes_buttons(int mode, const rac1_strafe_input* in) {
    return mode != RAC1_STRAFE_OFF && rac1_strafe_on_foot(in);
}

/* Hold's memory, once a tick at the pad's report: L2 or R2 going down on foot latches the
 * facing and starts the swing; the swing ends with the camera behind that facing, the right stick
 * pushed sideways or the ticks run out; letting both go, or the setting off, forgets it all. */
static inline void rac1_strafe_hold_update(
    int mode, int feel, const rac1_strafe_input* in, rac1_strafe_hold* hold
) {
    // With the setting off or both buttons up there is nothing to remember.
    if (mode == RAC1_STRAFE_OFF || (in->held & RAC1_STRAFE_BUTTONS) == 0) {
        hold->latched = 0;
        hold->swinging = 0;
        return;
    }

    // The button has only just gone down: latch the facing, if the hero is on foot.
    if (!hold->latched) {
        // Off foot (a vehicle, a scene, the game's own strafe state) the press is not Hold's.
        if (rac1_strafe_on_foot(in)) {
            hold->latched = 1;
            /* Going Commando's has no swing: he turns to the camera at once. */
            hold->swinging = feel == RAC1_STRAFE_FEEL_CLASSIC;
            hold->ticks = 0;
            hold->yaw = in->facing;
        }

        return;
    }

    // While the swing lasts, check whether any of its three ends has come.
    if (hold->swinging) {
        const float d = rac1_strafe_angle_diff(in->camera, hold->yaw);
        const float x = in->look_x < 0.0f ? -in->look_x : in->look_x;

        // The camera is behind the latched facing, the right stick was pushed, or the ticks ran out
        // (a wall or a camera volume can keep the camera from getting there).
        if ((d < RAC1_STRAFE_SWING_DONE && d > -RAC1_STRAFE_SWING_DONE)
            || x >= RAC1_STRAFE_SWING_STICK || ++hold->ticks >= RAC1_STRAFE_SWING_TICKS) {
            hold->swinging = 0;
        }
    }
}

/* What the strafe does this tick (RAC1_STRAFE_NONE, _HELD, _LOCKED). */
static inline int rac1_strafe_kind(
    int mode, const rac1_strafe_input* in, const rac1_strafe_hold* hold
) {
    // The setting off, no play, a vehicle, the hoverboard, or the hero on a frame of his own: the game's.
    if (mode == RAC1_STRAFE_OFF || !in->playing || in->vehicle_mode != 0
        || in->control_mode == RAC1_STRAFE_CONTROL_HOVERBOARD || in->frame_mode != 0) {
        return RAC1_STRAFE_NONE;
    }

    // A state the strafe does not work in (the game's own strafe state among them).
    if (in->state != RAC1_STRAFE_STATE_STAND && in->state != RAC1_STRAFE_STATE_RUN
        && in->state != RAC1_STRAFE_STATE_RUN_ALT && in->state != RAC1_STRAFE_STATE_JUMP) {
        return RAC1_STRAFE_NONE;
    }

    // Lock: a forward-firing weapon ready in hand faces the camera.
    if (mode == RAC1_STRAFE_LOCK && in->slot_state == RAC1_STRAFE_SLOT_READY
        && rac1_strafe_aimable(in->slot_item)) {
        return RAC1_STRAFE_LOCKED;
    }

    return hold->latched ? RAC1_STRAFE_HELD : RAC1_STRAFE_NONE;
}

/* Whether the camera is asked this tick to swing round behind Hold's latched facing. */
static inline int rac1_strafe_swings_camera(int kind, const rac1_strafe_hold* hold) {
    return kind == RAC1_STRAFE_HELD && hold->swinging;
}

/* The yaw Hero_TurnToHeading eases the facing toward: while strafing (its first argument 0, the
 * turns that follow the move yaw) the latched yaw while Hold's swing lasts, the camera's after it
 * and in Lock; else the move yaw, as the game has it. */
static inline float rac1_strafe_turn_target(
    int kind, int mode, float move_yaw, float camera_yaw, const rac1_strafe_hold* hold
) {
    // Not a turn that follows the move yaw, or no strafe: the game's target.
    if (mode != 0 || kind == RAC1_STRAFE_NONE) {
        return move_yaw;
    }

    return rac1_strafe_swings_camera(kind, hold) ? hold->yaw : camera_yaw;
}

/* The angle Hero_SetPlanarVelocity builds the velocity along: while strafing, a request for the
 * facing (an angle above pi) is the move yaw instead; an angle of the caller's own is kept. */
static inline float rac1_strafe_planar_angle(int kind, float requested, float move_yaw) {
    return kind != RAC1_STRAFE_NONE && !(requested <= RAC1_STRAFE_PI) ? move_yaw : requested;
}

/* The way he slides once the stick is let go. With the stick centred the game's stick handler
 * (func_L00_00211F80) sets the move yaw to his facing and his target speed (+0x190) to 0, and the
 * run eases his speed down over a few ticks along that yaw: after a strafe that is forward, a
 * shuffle ahead as he stops. So the last move yaw the stick gave is kept and used while he slows. */
typedef struct rac1_strafe_glide {
    int valid; /* yaw is the stick's, from this strafe */
    float yaw; /* the last move yaw with the stick pushed */
} rac1_strafe_glide;

/* The move yaw the velocity is built along while strafing (kind not none): the stick's while it is
 * pushed (target_speed above 0), the last one it gave while he slows to a stop. Not strafing, it
 * forgets the yaw and gives the move yaw back. */
static inline float rac1_strafe_glide_yaw(
    rac1_strafe_glide* g, int kind, float target_speed, float move_yaw
) {
    // Not strafing: nothing to keep.
    if (kind == RAC1_STRAFE_NONE) {
        g->valid = 0;
        return move_yaw;
    }

    // The stick pushed: its yaw, remembered.
    if (target_speed > 0.0f) {
        g->valid = 1;
        g->yaw = move_yaw;
        return move_yaw;
    }

    return g->valid ? g->yaw : move_yaw;
}

/* Whether lock_strafe.cpp turns him toward the camera itself early in the hero's update: standing, where
 * the game's standing state does not turn him, in Lock and in Hold once its swing is done. */
static inline int rac1_strafe_turns_standing(int kind, int state, const rac1_strafe_hold* hold) {
    return state == RAC1_STRAFE_STATE_STAND
           && (kind == RAC1_STRAFE_LOCKED || (kind == RAC1_STRAFE_HELD && !hold->swinging));
}

/* The backpedal. RAC1 has no animation for walking sideways or back: in the run (states 2 and
 * 0x73) the walk (sequence 3) or the run (4) loops, the walk/run routine (func_L00_002293E8)
 * setting its speed (hero +0xA90) from his ground speed, and his own animation advance
 * (func_L00_00232EF0) moving it on by that speed times the key's rate (+0xA94). That advance only
 * goes forward (a negative speed would leave the blend below 0, never stepping a key back, unlike
 * the advance other mobys get from their +0x58), so while he strafes away from where he faces,
 * lock_strafe.cpp steps the walk or the run back itself and the advance adds nothing more: the cycle
 * plays backwards, at the speed the game chose. It starts with the move yaw more than 110 degrees
 * from his facing and stops under 100 (no flicker at the edge): the constants for it are above. */

/* Whether he backpedals this tick (was: last tick's answer). */
static inline int rac1_strafe_backpedal(
    int kind, int feel, int state, float facing, float move_yaw, int was
) {
    // Only a strafe in the run's states can backpedal.
    if (kind == RAC1_STRAFE_NONE
        || (state != RAC1_STRAFE_STATE_RUN && state != RAC1_STRAFE_STATE_RUN_ALT)) {
        return 0;
    }

    float d = rac1_strafe_angle_diff(facing, move_yaw);

    // The angle's size is what matters, not its side.
    if (d < 0.0f) {
        d = -d;
    }

    // Going Commando's edges: within 45 degrees of straight back.
    if (feel == RAC1_STRAFE_FEEL_COMMANDO) {
        return d > (was ? RAC1_STRAFE_BACK_LEAVE_COMMANDO : RAC1_STRAFE_BACK_ENTER_COMMANDO);
    }

    return d > (was ? RAC1_STRAFE_BACK_LEAVE : RAC1_STRAFE_BACK_ENTER);
}

/* ---- Going Commando's feel ---- */

/* The turn's rates while strafing, Going Commando's (DESIGN.md): on the ground, or in the jump.
 * frame_scale_sq and frame_time are the game's (g_frame_scale_sq, g_frame_time). */
static inline void rac1_strafe_commando_rates(
    int state, float frame_scale_sq, float frame_time, float* accel, float* damping, float* limit
) {
    // In the jump, the jump group's rates.
    if (state == RAC1_STRAFE_STATE_JUMP) {
        *accel = frame_scale_sq * RAC1_STRAFE_AIR_ACCEL;
        *damping = frame_scale_sq * RAC1_STRAFE_AIR_DAMPING;
        *limit = frame_time * RAC1_STRAFE_AIR_LIMIT;
        return;
    }

    *accel = frame_scale_sq * RAC1_STRAFE_TURN_ACCEL;
    *damping = frame_scale_sq * RAC1_STRAFE_TURN_DAMPING;
    *limit = frame_time * RAC1_STRAFE_TURN_LIMIT;
}

/* Which side the move yaw is on from his facing: left (90 degrees anticlockwise, the game's
 * stick_direction_vs_facing 0) or right, each within 45 degrees, else neither. */
static inline int rac1_strafe_side(float facing, float move_yaw) {
    const float d = rac1_strafe_angle_diff(facing, move_yaw);
    const float quarter = RAC1_STRAFE_PI * 0.5f;
    const float eighth = RAC1_STRAFE_PI * 0.25f;
    float left = d - quarter;
    float right = d + quarter;

    left = left < 0.0f ? -left : left;
    right = right < 0.0f ? -right : right;

    // Within 45 degrees of his left.
    if (left <= eighth) {
        return RAC1_STRAFE_SIDE_LEFT;
    }

    return right <= eighth ? RAC1_STRAFE_SIDE_RIGHT : RAC1_STRAFE_SIDE_NONE;
}

/* The ticks since he last went each way (func_L00_002B35A0's +0x146E and +0x146C). */
typedef struct rac1_strafe_reverse {
    int left;
    int right;
} rac1_strafe_reverse;

/* One tick of the side reversal: what his speed is multiplied by. Going left soon after going right:
 * rac2 multiplies by -0.25, which on rac1's speed would be below 0, so 0; going right soon after
 * left: 0, then 0.25, 0.5 and 0.75 as the left fades; else 1. */
static inline float rac1_strafe_reverse_step(rac1_strafe_reverse* r, int side) {
    float f = 1.0f;

    // Both timers count down first, as the game's func_00282978 does.
    if (r->left > 0) {
        --r->left;
    }

    // The right's timer too.
    if (r->right > 0) {
        --r->right;
    }

    // Going left: the right fades away at once.
    if (side == RAC1_STRAFE_SIDE_LEFT) {
        r->left = RAC1_STRAFE_SIDE_TICKS;

        // The right still recent: his speed goes.
        if (r->right != 0) {
            f = 0.0f;
        }
    } else if (side == RAC1_STRAFE_SIDE_RIGHT) {
        r->right = RAC1_STRAFE_SIDE_TICKS;

        // The left still recent: his speed comes back as it fades.
        if (r->left != 0) {
            f = (4.0f - (float)r->left) * 0.25f;
        }
    }

    return f < 0.0f ? 0.0f : f;
}

/* The hip twist's target for the pelvis (the spine's is minus it): the move yaw from his facing
 * while strafing with the stick pushed, not backpedalling; else 0, where the twist springs back. */
static inline float rac1_strafe_twist_target(
    int kind, float stick, int backpedal, float facing, float move_yaw
) {
    // Not strafing, the stick near its centre, or going backwards: no twist.
    if (kind == RAC1_STRAFE_NONE || !(stick > RAC1_STRAFE_TWIST_STICK) || backpedal) {
        return 0.0f;
    }

    return rac1_strafe_angle_diff(facing, move_yaw);
}

/* Whether X this tick starts a strafe flip, the game's direction check apart: strafing, standing
 * or running, X pressed, the stick pushed fully. */
static inline int rac1_strafe_flips(int kind, int state, uint32_t pressed, float stick) {
    return kind != RAC1_STRAFE_NONE
           && (state == RAC1_STRAFE_STATE_STAND || state == RAC1_STRAFE_STATE_RUN
               || state == RAC1_STRAFE_STATE_RUN_ALT)
           && (pressed & RAC1_PAD_X) != 0 && stick > RAC1_STRAFE_FLIP_STICK;
}

/* Where a looping sequence stands (the hero moby's +0x50, +0x51, +0x54, and hero +0xA94). */
typedef struct rac1_strafe_anim {
    int key;        /* key A */
    int next_key;   /* key B, A + 1 going forward */
    float fraction; /* from A to B */
    float rate;     /* this interval's: key A's rate (the first word of its record) */
} rac1_strafe_anim;

/* One tick of the sequence backwards by step (speed times rate, as the advance takes it forward):
 * below 0 it moves to the interval before, key B taking key A and key A the one before it (the
 * last after key 0), with that key's rate; the mirror of the advance's step forward. count: the
 * sequence's keys, rates: each key's rate. */
static inline void rac1_strafe_anim_back(
    rac1_strafe_anim* a, float step, int count, const float* rates
) {
    // A sequence with no keys, or a key outside it, cannot be stepped.
    if (count <= 0 || a->key < 0 || a->key >= count) {
        return;
    }

    float f = a->fraction - step;

    // While the step takes the fraction below 0, move to the interval before and carry the rest.
    for (int n = 0; f < 0.0f; ++n) {
        // No rate to divide by, or a whole loop in one tick: stop at a key instead.
        if (!(a->rate > 0.0f)
            || n > count) { /* no rate, or a whole loop in a tick: stop at a key */
            f = 0.0f;
            break;
        }

        const float ticks = -f / a->rate; /* what is left of the step, in ticks */
        a->next_key = a->key;
        a->key = a->key == 0 ? count - 1 : a->key - 1;
        a->rate = rates[a->key];
        f = 1.0f - ticks * a->rate;
    }

    a->fraction = f > RAC1_STRAFE_BACK_TOP ? RAC1_STRAFE_BACK_TOP : f;
}

#endif /* OPENRAC_RAC1_STRAFE_H */
