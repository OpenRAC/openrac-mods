// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Ratchet & Clank: the strafe decision (the lock-strafe mod, lock_strafe.cpp), apart from game
// memory so that a test can run it (tests/rac1/hero_strafe_test.cpp). Moved from OpenRAC's
// port/game/rac1/host.
//
// How the game turns the stick into a heading. Each tick the hero's state code calls the stick
// handler (Hero_SetHeadingFromStick, func_L00_00211F80), which sets the hero's move yaw (+0x180) to
// the stick's angle plus the camera's yaw (D_L00_00166ED8): where the stick points on the screen.
// The state then eases the hero's facing (rot.z, +0x98) toward the move yaw (Hero_TurnToHeading,
// func_L00_002124E8), and the run builds his velocity along his facing (Hero_SetPlanarVelocity,
// func_L00_00212790, given an angle above pi: "the hero's own"), so he turns first and runs where
// he faces. Two places in the game do otherwise, and the strafe is built from them:
//
// - the run's start from a standstill with the stick pushed all the way (+0x3B8) asks for the
//   velocity along the move yaw instead of the facing: he moves where the stick points before he
//   has turned;
// - in state 0x81, while L2 or R2 is held, the state puts the camera's yaw in the move yaw around
//   its call of Hero_TurnToHeading and puts the stick's back afterwards: he faces the camera while
//   the stick moves him; the follow camera's per-tick requests (func_L00_002E74B0) then ask for a
//   strafe camera that slides along with him instead of swinging round to look where he goes.
//
// On foot the game gives the shoulder buttons in pairs: L1 or L2 looks in first person (state 1,
// from standing: held & 5), R1 or R2 crouches (held & 0xA: the crouch, the crouch-jumps, the
// slide from a run). With the setting on, in play on foot, the game sees L2 and R2 as not held
// (lock_strafe.cpp takes them out of the pad's state at each report, as the game's own input locks
// take buttons out), so L1 still looks and R1 still crouches, and L2 and R2 strafe. State 0x81
// keeps them: it has its strafe of its own on them. Scenes, menus, vendors, vehicles and the
// hoverboard keep them too. A camera of the level's own (any but the one behind him, id 0: Kalebo
// III's arena seen from a wall) does not strafe, its yaw not being where he aims, but L2 and R2
// stay taken: given back, L2 put him in first person there, where he could not move.
//
// Two ways to strafe, the later games':
//
// - Hold (Going Commando's): when L2 or R2 goes down the camera swings round, once, behind the
//   facing he has then, and he keeps that facing while it swings. From then on, as long as it is
//   held, he faces the camera's yaw as Lock does: the right stick (the game's own camera control)
//   turns the camera and him with it, and the stick moves him sideways, back and forward along the
//   move yaw. The swing ends when the camera is behind him, when the right stick is pushed
//   sideways, or after a while (a wall or a camera volume can keep it from getting there); nothing
//   pulls the camera back to the facing he had at the press after that.
// - Lock (Up Your Arsenal's lock-strafe): with a forward-firing weapon ready in hand
//   (is_aimable) he always faces the camera's yaw, which the right stick turns (the game's own
//   camera control), and the stick moves him; the camera does not swing to follow where he goes.
//   With anything else in hand, Hold.
//
// Either is done in the run (states 2 and 0x73) and the jumps (7, the running jump 9 rac1 takes
// for X while running ahead, and the double jump 0xE), on level ground; both also turn him
// standing (state 0) with the camera (Hold once its swing is done). Everything else is the game's
// own.
//
// Two feels (the setting "feel", DESIGN.md): Going Commando's (the default) has no swing in Hold
// (he turns to the camera, the camera stays), turns him at rac2's strafe rates, kills his momentum
// when he goes from one side to the other, and backpedals only within 45 degrees of straight back;
// the classic one is the mod's 1.0 behaviour. Apart from the feel: the hip twist (his pelvis turned
// toward where he goes and his spine back toward the camera, rac2's joints 9 and 10 on rac1's 1
// and 2) and the strafe flips (X with the stick pushed fully sideways or back: rac1's own side and
// back flip, state 0xB, entered from the strafe as rac2 does).
//
// This file has no game memory: each function takes what it reads as a struct and returns the
// answer, so tests/rac1/hero_strafe_test.cpp can run the decision alone. Its addresses and offsets
// are the decomp's, cited where they appear (seen in game code), and lock_strafe.cpp holds the
// rest of the mod (the settings, the hook points and the game memory).

#pragma once

#include <cmath>
#include <cstdint>

namespace lock_strafe::rac1 {

// ---- Constants ----

// The mod's setting "mode". Off is the mod switched off, the game's own.
enum class Mode : int {
    Off,
    Hold,
    Lock,
};

// The pad's button bits as the game keeps them (PadState.held): L2 and R2, the strafe's.
inline constexpr std::uint32_t kPadL2 = 0x1u;
inline constexpr std::uint32_t kPadR2 = 0x2u;
inline constexpr std::uint32_t kStrafeButtons = kPadL2 | kPadR2;

// Cross, the jump (try_start_hero_jump's pad_pressed_within(0x40, ...)).
inline constexpr std::uint32_t kPadX = 0x40u;

// The mod's setting "button": which of L2 and R2 strafe. Both is the default; one alone leaves the
// other the game's (R1 or R2 crouches, L1 or L2 looks) and another mod's, such as one that raises
// the wrench on R2.
enum class Button : int {
    Both,
    L2,
    R2,
};

// The mod's setting "feel".
enum class Feel : int {
    Classic,
    Commando,
};

// The hero's states the strafe works in: standing, running, running with +0x20A9 set (the same
// movement code), jumping: the plain jump, the running jump (X running within 60 degrees of his
// facing, so strafing ahead) and the double jump, all of the jump group's movement code.
inline constexpr int kStateStand = 0x00;
inline constexpr int kStateRun = 0x02;
inline constexpr int kStateRunAlt = 0x73;
inline constexpr int kStateJump = 0x07;
inline constexpr int kStateRunJump = 0x09;
inline constexpr int kStateDoubleJump = 0x0E;

// The side and back flip (HERO_STATE_HIGH_JUMP; rac2's state 11, the same state).
inline constexpr int kStateFlip = 0x0B;

// The state with a strafe of its own on L2 and R2: left as the game has it.
inline constexpr int kStateGame = 0x81;

// The hero's control mode on the hoverboard (hero +0x208C; set by state 0x6B in Rilgar and Kalebo
// III, seen in game code): its tricks are on L2 and R2, so the game keeps them.
inline constexpr int kControlHoverboard = 0x16;

// Slot 0 ready (HeroItemSlot.state).
inline constexpr int kSlotReady = 2;

// What a strafe does this tick.
enum class Kind : int {
    None,    // The game's own turning and velocity.
    Held,    // L2 or R2 held: the swing behind his facing, then the camera's yaw.
    Locked,  // Facing the camera's yaw.
};

// Hold's swing ends with the camera's yaw this near the latched facing (radians), the right stick
// pushed this far sideways (the game's own test for the stick moved, CameraType0_HeroTweaks), or
// after this many ticks.
inline constexpr float kSwingDone = 0.05f;
inline constexpr float kSwingStick = 0.3f;
inline constexpr int kSwingTicks = 120;

// Going Commando's strafe turn, a tick at 60 Hz: the accel and damping (scaled by the tick's
// g_frame_scale_sq, as rac1 scales its own) and the limit in radians a second (times
// g_frame_time): on the ground func_L00_002B35A0's (0.037, 0.25, 0.2036 a frame), in the air the
// jump group's (0.015, 0.2, 0.25 a frame). Seen in rac2's code.
inline constexpr float kTurnAccel = 0.037f;
inline constexpr float kTurnDamping = 0.25f;
inline constexpr float kTurnLimit = 12.2173061f;
inline constexpr float kAirAccel = 0.015f;
inline constexpr float kAirDamping = 0.2f;
inline constexpr float kAirLimit = 15.0f;

// The side reversal (func_L00_002B35A0's timers +0x146C / +0x146E): ticks a side stays recent.
inline constexpr int kSideTicks = 5;

// Which side the move yaw is on from his facing.
enum class Side : int {
    None,
    Left,
    Right,
};

// The hip twist (func_L00_002B7B80): the stick past this, the joints (rac1's pelvis and first spine
// joint, standing for rac2's 9 and 10), and the springs' accel (pelvis, spine) and damping, a tick
// at 60 Hz, scaled as the turn's. Seen in rac2's code; the joints from rac1's skeleton.
inline constexpr float kTwistStick = 0.15f;
inline constexpr std::uint32_t kJointPelvis = 1;
inline constexpr std::uint32_t kJointSpine = 2;
inline constexpr float kTwistPelvisAccel = 0.037f;
inline constexpr float kTwistSpineAccel = 0.025f;
inline constexpr float kTwistDamping = 0.27f;

// The strafe flip (func_L00_002C9FC8): the stick past this, the direction it must not be
// (stick_direction_vs_facing: 0 left, 1 right, 2 forward, 3 back), and the drift it starts with
// (state 0xB's +0x4DC, 0.0833 a frame: 5 units a second). Seen in rac2's code.
inline constexpr float kFlipStick = 0.9f;
inline constexpr int kDirectionForward = 2;
inline constexpr float kFlipDrift = 5.0f;

// Pi and two pi as 32-bit floats, as the game's own angle arithmetic has them (seen in game code).
inline constexpr float kPi = 3.14159274f;
inline constexpr float kTwoPi = 6.28318548f;

// The walk and the run sequences, by index, whose cycle is played backwards by the backpedal (seen
// in game code).
inline constexpr int kSeqWalk = 3;
inline constexpr int kSeqRun = 4;

// The move yaw's angle from the facing at which the backpedal starts and at which it stops, in
// radians (assumed: chosen to avoid flicker at the edge).
inline constexpr float kBackEnter = 1.91986218f;  // 110 degrees
inline constexpr float kBackLeave = 1.74532926f;  // 100 degrees

// Going Commando's edges: within 45 degrees of straight back (func_L00_002B39F0), leaving 5
// degrees on.
inline constexpr float kBackEnterCommando = 2.35619449f;  // 135 degrees
inline constexpr float kBackLeaveCommando = 2.26892803f;  // 130 degrees

// The highest blend lock_strafe.cpp leaves: the advance takes one from 0.99 up as 1 and steps
// forward (assumed).
inline constexpr float kBackTop = 0.985f;

// The angle between the facing and the move yaw under which a strafe's glide ends (3 degrees, the
// game's own rule for its run start from a standstill: +0x3B8 builds along the move yaw until the
// yaw error is under 0.0524).
inline constexpr float kTurned = 0.0523598790f;

// ---- Types ----

// What the decision reads of the hero and the pad.
struct HeroInput {
    // A level is loaded and its game mode is 0: not a scene, a menu, a vendor (D_L00_0015F6A8).
    bool playing;

    int state;         // Hero +0x2084.
    int frame_mode;    // Hero +0x20B3: 0 on level ground; others turn the hero's own frame.
    int vehicle_mode;  // Hero +0x20A4: 0 on foot.
    int control_mode;  // Hero +0x208C: kControlHoverboard on the board.
    int slot_state;    // Hero +0x1090 + 0x24: slot 0's state.
    int slot_item;     // Hero +0x1090 + 0x28: slot 0's item.

    // The pad's buttons as the player holds them (+0x1A0, L2 and R2 from before lock_strafe.cpp
    // takes them).
    std::uint32_t held;

    float facing;  // Hero +0x98.
    float camera;  // The camera's yaw (D_L00_00166ED8).
    float look_x;  // The right stick sideways (PadState.analog[0], +0x100).

    // The camera in use is not the one behind him (Camera +0x86, not 0).
    bool fixed_camera;
};

// What Hold remembers from tick to tick.
struct HoldState {
    bool latched;   // L2 or R2 held since it went down on foot.
    bool swinging;  // The camera still swinging round behind yaw.
    int ticks;      // Ticks of the swing so far.
    float yaw;      // The facing when it went down.
};

// Where his speed goes as a strafe ends (the glide, explained at glide_angle).
struct GlideState {
    bool valid;    // The yaw holds the stick's last move yaw, from this strafe.
    bool leaving;  // A strafe in the run has ended: the velocity follows the stick.
    float yaw;     // The last move yaw with the stick pushed.
};

// The ticks since he last went each way (func_L00_002B35A0's +0x146E and +0x146C).
struct ReverseTimers {
    int left;
    int right;
};

// Where a looping sequence stands (the hero moby's +0x50, +0x51, +0x54, and hero +0xA94).
struct AnimationKeys {
    int key;         // Key A.
    int next_key;    // Key B, A + 1 going forward.
    float fraction;  // From A to B.
    float rate;      // This interval's: key A's rate (the first word of its record).
};

// ---- Decision ----

// Returns b - a, within -pi..pi.
inline float angle_diff(float a, float b) {
    float d = b - a;

    // Above pi: wrap down by a turn, so the shortest way round is found.
    while (d > kPi) {
        d -= kTwoPi;
    }

    // Below minus pi: wrap up by a turn.
    while (d < -kPi) {
        d += kTwoPi;
    }

    return d;
}

// Returns whether slot 0's item fires forward or at what the hero faces: Lock's weapons (slot 0's
// item ids): the Suck Cannon, the Bomb Glove, the Devastator, the Blaster, the Pyrocitor, the Tesla
// Claw, the Morph-o-Ray and the R.Y.N.O. Not the wrench (8), nor what does not fire ahead: the
// Swingshot, the Visibomb (a guided rocket seen from it), the Taunter, the Mine Glove, the
// Walloper (a punch), the Glove of Doom (its agents find their own way), the Hydrodisplacer, the
// Drone Device, the Decoy Glove and the gadgets.
inline bool is_aimable(int item) {
    // Each of Lock's weapons answers yes; every other item answers no.
    switch (item) {
        case 0x09:  // Suck Cannon
        case 0x0A:  // Bomb Glove
        case 0x0B:  // Devastator
        case 0x0F:  // Blaster
        case 0x10:  // Pyrocitor
        case 0x13:  // Tesla Claw
        case 0x15:  // Morph-o-Ray
        case 0x17:  // R.Y.N.O.
            return true;
        default:
            // Any other item does not fire ahead.
            return false;
    }
}

// Returns the pad bits the setting "button" strafes on: L2, R2 or both.
inline std::uint32_t button_bits(Button button) {
    // One button alone strafes on its own bit; anything else strafes on both.
    switch (button) {
        case Button::L2:
            return kPadL2;
        case Button::R2:
            return kPadR2;
        case Button::Both:
            break;
    }

    return kStrafeButtons;
}

// Returns whether a state is one of the jumps the strafe works in.
inline bool is_jumping(int state) {
    return state == kStateJump || state == kStateRunJump || state == kStateDoubleJump;
}

// Returns whether the hero is in play on foot, in a state the setting changes L2 and R2 in.
inline bool is_on_foot(const HeroInput& in) {
    return in.playing && in.vehicle_mode == 0 && in.control_mode != kControlHoverboard
           && in.state != kStateGame;
}

// Returns whether the game sees L2 and R2 as not held this tick: the setting on, in play on foot.
inline bool takes_buttons(Mode mode, const HeroInput& in) {
    return mode != Mode::Off && is_on_foot(in);
}

// Updates Hold's memory, once a tick at the pad's report: L2 or R2 going down on foot latches the
// facing and starts the swing; the swing ends with the camera behind that facing, the right stick
// pushed sideways or the ticks run out; letting both go, or the setting off, forgets it all.
inline void update_hold(Mode mode, Feel feel, const HeroInput& in, HoldState& hold) {
    // With the setting off or both buttons up there is nothing to remember.
    if (mode == Mode::Off || (in.held & kStrafeButtons) == 0) {
        hold.latched = false;
        hold.swinging = false;

        return;
    }

    // The button has gone down only now: latch the facing, if the hero is on foot.
    if (!hold.latched) {
        // Off foot (a vehicle, a scene, the game's own strafe state) or under a camera of the
        // level's own the press is not Hold's (yet: back behind him, still held, it latches).
        if (is_on_foot(in) && !in.fixed_camera) {
            hold.latched = true;

            // Going Commando's has no swing: he turns to the camera at once.
            hold.swinging = feel == Feel::Classic;
            hold.ticks = 0;
            hold.yaw = in.facing;
        }

        return;
    }

    // While the swing lasts, check whether any of its three ends has come.
    if (hold.swinging) {
        const float d = angle_diff(in.camera, hold.yaw);
        const float x = in.look_x < 0.0f ? -in.look_x : in.look_x;

        // The camera is behind the latched facing, the right stick was pushed, or the ticks ran out
        // (a wall or a camera volume can keep the camera from getting there).
        if ((d < kSwingDone && d > -kSwingDone) || x >= kSwingStick
            || ++hold.ticks >= kSwingTicks) {
            hold.swinging = false;
        }
    }
}

// Returns what the strafe does this tick.
inline Kind strafe_kind(Mode mode, const HeroInput& in, const HoldState& hold) {
    // The setting off, no play, a vehicle, the hoverboard, the hero on a frame of his own, or a
    // camera of the level's own (Kalebo III's arena seen from a wall: its yaw is not where he
    // aims, so facing it turned him toward the wall): the game's.
    if (mode == Mode::Off || !in.playing || in.vehicle_mode != 0
        || in.control_mode == kControlHoverboard || in.frame_mode != 0 || in.fixed_camera) {
        return Kind::None;
    }

    // A state the strafe does not work in (the game's own strafe state among them).
    if (in.state != kStateStand && in.state != kStateRun && in.state != kStateRunAlt
        && !is_jumping(in.state)) {
        return Kind::None;
    }

    // Lock: a forward-firing weapon ready in hand faces the camera.
    if (mode == Mode::Lock && in.slot_state == kSlotReady && is_aimable(in.slot_item)) {
        return Kind::Locked;
    }

    // Otherwise Hold's latch decides: held means a strafe, let go means the game's own.
    return hold.latched ? Kind::Held : Kind::None;
}

// Returns whether the camera is asked this tick to swing round behind Hold's latched facing.
inline bool swings_camera(Kind kind, const HoldState& hold) {
    return kind == Kind::Held && hold.swinging;
}

// Returns the yaw Hero_TurnToHeading eases the facing toward. While strafing (its first argument
// 0, the turns that follow the move yaw) that is the latched yaw while Hold's swing lasts, and the
// camera's after it and in Lock; else the move yaw, as the game has it.
inline float turn_target(
    Kind kind, int mode, float move_yaw, float camera_yaw, const HoldState& hold
) {
    // Not a turn that follows the move yaw, or no strafe: the game's target.
    if (mode != 0 || kind == Kind::None) {
        return move_yaw;
    }

    return swings_camera(kind, hold) ? hold.yaw : camera_yaw;
}

// Returns the angle Hero_SetPlanarVelocity builds the velocity along. While strafing, a request
// for the facing (an angle above pi) is the move yaw instead; an angle of the caller's own is
// kept.
inline float planar_angle(Kind kind, float requested, float move_yaw) {
    return kind != Kind::None && !(requested <= kPi) ? move_yaw : requested;
}

// Returns the angle Hero_SetPlanarVelocity builds along, and keeps the glide's memory, once a
// call: where his speed goes as a strafe ends. The run builds his velocity along his facing (a
// request above pi), and a strafe's speed lies along the stick instead, so two moments would push
// him ahead:
//
// - the stick let go: the game's stick handler (func_L00_00211F80) sets the move yaw to his facing
//   and his target speed (+0x190) to 0, and the run eases his speed down along that yaw, a shuffle
//   forward as he stops. The last move yaw the stick gave is kept while he slows;
// - the strafe let go (L2 or R2 up, or the weapon put away in Lock) while he runs on: the game
//   turns him toward the stick over several ticks and runs along his facing meanwhile, so running
//   back he went on forward first. The velocity follows the stick until his facing is within 3
//   degrees of it (kTurned), or until he stops or leaves the run.
//
// Both only redirect his speed: how fast he goes and turns stays the game's.
//
// `kind` is the strafe now, `running` whether he is in the run's states, `target_speed` the
// stick's (0 let go). An angle of the caller's own (pi or below) is always kept.
inline float glide_angle(
    GlideState& glide,
    Kind kind,
    bool running,
    float target_speed,
    float facing,
    float move_yaw,
    float requested
) {
    // An angle of the caller's own: the game's.
    if (requested <= kPi) {
        return requested;
    }

    const bool pushed = target_speed > 0.0f;

    // Strafing: along the stick, or its last yaw while he slows with it let go.
    if (kind != Kind::None) {
        // The stick pushed: its yaw, remembered.
        if (pushed) {
            glide.valid = true;
            glide.yaw = move_yaw;
        }

        glide.leaving = running;

        return planar_angle(kind, requested, pushed || !glide.valid ? move_yaw : glide.yaw);
    }

    // The strafe ended in the run and he has not turned to the stick yet: along the stick.
    if (glide.leaving && running && pushed && std::fabs(angle_diff(facing, move_yaw)) > kTurned) {
        glide.valid = true;
        glide.yaw = move_yaw;

        return move_yaw;
    }

    // It ended with the stick let go: he slows along the stick's last yaw.
    if (glide.leaving && running && !pushed && glide.valid) {
        return glide.yaw;
    }

    // Turned, stopped or out of the run: the game's facing from now on.
    glide.valid = false;
    glide.leaving = false;

    return requested;
}

// Returns whether the glide is forgotten at a pad report: out of play or off foot (a scene, a
// menu, a vehicle; `playing_on_foot` false), or no strafe and not in the run (standing, jumping,
// flipping), so the next run starts as the game's.
inline bool glide_ends(Kind kind, int state, bool playing_on_foot) {
    return !playing_on_foot || (kind == Kind::None && state != kStateRun && state != kStateRunAlt);
}

// Returns whether lock_strafe.cpp turns him toward the camera itself early in the hero's update:
// standing, where the game's standing state does not turn him, in Lock and in Hold once its swing
// is done.
inline bool turns_standing(Kind kind, int state, const HoldState& hold) {
    return state == kStateStand && (kind == Kind::Locked || (kind == Kind::Held && !hold.swinging));
}

// The backpedal. Ratchet & Clank has no animation for walking sideways or back: in the run
// (states 2 and 0x73) the walk (sequence 3) or the run (4) loops, the walk/run routine
// (func_L00_002293E8) setting its speed (hero +0xA90) from his ground speed, and his own animation
// advance (func_L00_00232EF0) moving it on by that speed times the key's rate (+0xA94). That
// advance only goes forward (a negative speed would leave the blend below 0, never stepping a key
// back, unlike the advance other mobys get from their +0x58), so while he strafes away from where
// he faces, lock_strafe.cpp steps the walk or the run back itself and the advance adds nothing
// more: the cycle plays backwards, at the speed the game chose. It starts with the move yaw more
// than 110 degrees from his facing and stops under 100 (no flicker at the edge): the constants for
// it are above.
//
// Returns whether he backpedals this tick; `was` is last tick's answer.
inline bool backpedals(Kind kind, Feel feel, int state, float facing, float move_yaw, bool was) {
    // Only a strafe in the run's states can backpedal.
    if (kind == Kind::None || (state != kStateRun && state != kStateRunAlt)) {
        return false;
    }

    float d = angle_diff(facing, move_yaw);

    // The angle's size is what matters, not its side.
    if (d < 0.0f) {
        d = -d;
    }

    // Going Commando's edges: within 45 degrees of straight back.
    if (feel == Feel::Commando) {
        return d > (was ? kBackLeaveCommando : kBackEnterCommando);
    }

    return d > (was ? kBackLeave : kBackEnter);
}

// ---- Going Commando's feel ----

// Sets the turn's rates while strafing, Going Commando's (DESIGN.md): on the ground, or in the
// jump. `frame_scale_sq` and `frame_time` are the game's (g_frame_scale_sq, g_frame_time); the
// rates come out in `accel`, `damping` and `limit`.
inline void commando_rates(
    int state, float frame_scale_sq, float frame_time, float& accel, float& damping, float& limit
) {
    // In a jump, the jump group's rates.
    if (is_jumping(state)) {
        accel = frame_scale_sq * kAirAccel;
        damping = frame_scale_sq * kAirDamping;
        limit = frame_time * kAirLimit;

        return;
    }

    accel = frame_scale_sq * kTurnAccel;
    damping = frame_scale_sq * kTurnDamping;
    limit = frame_time * kTurnLimit;
}

// Returns which side the move yaw is on from his facing: left (90 degrees anticlockwise, the
// game's stick_direction_vs_facing 0) or right, each within 45 degrees, else neither.
inline Side side_of(float facing, float move_yaw) {
    const float d = angle_diff(facing, move_yaw);
    const float quarter = kPi * 0.5f;
    const float eighth = kPi * 0.25f;
    float left = d - quarter;
    float right = d + quarter;

    left = left < 0.0f ? -left : left;
    right = right < 0.0f ? -right : right;

    // Within 45 degrees of his left.
    if (left <= eighth) {
        return Side::Left;
    }

    return right <= eighth ? Side::Right : Side::None;
}

// Steps the side reversal's timers one tick and returns what his speed is multiplied by. Going
// left soon after going right: rac2 multiplies by -0.25, which on rac1's speed would be below 0,
// so 0; going right soon after left: 0, then 0.25, 0.5 and 0.75 as the left fades; else 1.
inline float reverse_step(ReverseTimers& timers, Side side) {
    float f = 1.0f;

    // Both timers count down first, as the game's func_00282978 does.
    if (timers.left > 0) {
        --timers.left;
    }

    // The right's timer too.
    if (timers.right > 0) {
        --timers.right;
    }

    // Going left: the right fades away at once.
    if (side == Side::Left) {
        timers.left = kSideTicks;

        // The right still recent: his speed goes.
        if (timers.right != 0) {
            f = 0.0f;
        }
    } else if (side == Side::Right) {
        timers.right = kSideTicks;

        // The left still recent: his speed comes back as it fades.
        if (timers.left != 0) {
            f = (4.0f - static_cast<float>(timers.left)) * 0.25f;
        }
    }

    return f < 0.0f ? 0.0f : f;
}

// Returns the hip twist's target for the pelvis (the spine's is minus it): the move yaw from his
// facing while strafing with the stick pushed, not backpedalling; else 0, where the twist springs
// back.
inline float twist_target(Kind kind, float stick, bool backpedal, float facing, float move_yaw) {
    // Not strafing, the stick near its centre, or going backwards: no twist.
    if (kind == Kind::None || !(stick > kTwistStick) || backpedal) {
        return 0.0f;
    }

    return angle_diff(facing, move_yaw);
}

// Writes into `q` a joint's turn by `angle` about z as the games' joint modifiers take it, the
// unit quaternion (x, y, z, w): a positive angle turns the joint the way his facing turns as it
// grows (from his front toward his left). The games build it with the sine negated, (0, 0,
// -sin(a/2), cos(a/2)): rac1's build_axis_rotation_quaternion (PAL func_L00_001FFED8: FastSin, then
// neg.s) and Going Commando's func_00283898, which its twist's records go through (func_002A91F0).
// Their joint matrices take the rows of the quaternion's rotation as the joint's axes, the
// transpose, so the negated sine is what turns it by +a. 1.1.0 sent +sin and so turned his hips
// away from where he went: his legs walked one way while he slid the other (DESIGN.md). Seen in
// game code.
inline void joint_yaw(float angle, float (&q)[4]) {
    q[0] = 0.0f;
    q[1] = 0.0f;
    q[2] = -std::sin(angle * 0.5f);
    q[3] = std::cos(angle * 0.5f);
}

// Returns whether X this tick starts a strafe flip, the game's direction check apart: strafing,
// standing or running, X pressed, the stick pushed fully.
inline bool starts_flip(Kind kind, int state, std::uint32_t pressed, float stick) {
    return kind != Kind::None
           && (state == kStateStand || state == kStateRun || state == kStateRunAlt)
           && (pressed & kPadX) != 0 && stick > kFlipStick;
}

// Steps a looping sequence back one tick by `step` (speed times rate, as the advance takes it
// forward): below 0 it moves to the interval before, key B taking key A and key A the one before
// it (the last after key 0), with that key's rate; the mirror of the advance's step forward.
// `count` is the sequence's keys and `rates` each key's rate.
inline void step_animation_back(AnimationKeys& a, float step, int count, const float* rates) {
    // A sequence with no keys, or a key outside it, cannot be stepped.
    if (count <= 0 || a.key < 0 || a.key >= count) {
        return;
    }

    float f = a.fraction - step;

    // While the step takes the fraction below 0, move to the interval before and carry the rest.
    for (int n = 0; f < 0.0f; ++n) {
        // No rate to divide by, or a whole loop in one tick: stop at a key instead.
        if (!(a.rate > 0.0f) || n > count) {
            f = 0.0f;

            break;
        }

        // What is left of the step, in ticks of the interval it left.
        const float ticks = -f / a.rate;

        a.next_key = a.key;
        a.key = a.key == 0 ? count - 1 : a.key - 1;
        a.rate = rates[a.key];
        f = 1.0f - ticks * a.rate;
    }

    a.fraction = f > kBackTop ? kBackTop : f;
}

}  // namespace lock_strafe::rac1
