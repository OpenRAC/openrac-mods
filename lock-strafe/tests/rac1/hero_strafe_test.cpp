// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The strafe's decision (src/rac1/strafe.hpp, and the joints' spring in src/common/spring.hpp):
// when the hero strafes and how, which ticks the hero's update does not see L2 and R2, where his
// facing turns and which way his velocity goes; with the setting off, always the game's own.
//
// Expected values are worked out by hand from the description in strafe.hpp.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../../src/common/spring.hpp"
#include "../../src/rac1/strafe.hpp"

// Stops the test at the first failed condition and says where. It is a macro for the line number
// and the text of the condition, which a function cannot see.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #condition);                                \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

using namespace lock_strafe;
using namespace lock_strafe::rac1;

namespace {

// The item ids slot 0 holds in the tests: the Blaster (Lock's), the wrench and the Swingshot
// (neither).
constexpr int kItemBlaster = 0x0F;
constexpr int kItemWrench = 0x08;
constexpr int kItemSwingshot = 0x0C;

// The request for the facing Hero_SetPlanarVelocity gets from the run, an angle above pi.
constexpr float kFacingRequest = 99999.0f;

// The glide's inputs: the hero running or not, the stick pushed (the target speed) or let go.
constexpr bool kRunning = true;
constexpr bool kNotRunning = false;
constexpr float kPushed = 0.095f;
constexpr float kLetGo = 0.0f;

// The pad bits of Circle, kPadL1 and kPadR1, which the strafe leaves to the game.
constexpr std::uint32_t kPadCircle = 0x20u;
constexpr std::uint32_t kPadL1 = 0x4u;
constexpr std::uint32_t kPadR1 = 0x8u;

// Returns the hero running on level ground on foot, the Blaster ready, nothing held.
HeroInput running() {
    return {true, kStateRun, 0, 0, 0, kSlotReady, kItemBlaster, 0, 0.75f, 0.0f, 0.0f, false};
}

// Runs one tick as the run does it, with the decision in the two places lock_strafe.cpp changes:
// the facing eased toward its target by a fraction (the game springs it, the same direction), the
// velocity along the planar angle (the facing for 99999, the run's request).
void tick(
    Kind kind, float camera, float stick, const HoldState& hold, float& facing, float (&v)[2]
) {
    // The stick handler adds the rotations as the game's FastAddRots does: within -pi..pi.
    float move_yaw = camera + stick;

    // The stick handler wraps the move yaw into -pi..pi by a turn.
    if (move_yaw > 3.1415927f) {
        move_yaw -= 6.2831855f;
    } else if (move_yaw < -3.1415927f) {
        move_yaw += 6.2831855f;
    }

    const float target = turn_target(kind, 0, move_yaw, camera, hold);
    facing += angle_diff(facing, target) * 0.25f;
    float angle = planar_angle(kind, 99999.0f, move_yaw);

    // The run's request for the facing, an angle above pi, is the hero's own facing.
    if (!(angle <= 3.14159274f)) {
        angle = facing;
    }

    v[0] = std::cos(angle);
    v[1] = std::sin(angle);
}

void test_lock_aims_only_forward_firing_items() {
    const int aimable[] = {0x09, 0x0A, 0x0B, 0x0F, 0x10, 0x13, 0x15, 0x17};
    const int not_aimable[] =
        {kItemWrench,
         kItemSwingshot,
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

    // Each of Lock's weapons is aimable.
    for (unsigned i = 0; i < sizeof aimable / sizeof aimable[0]; ++i) {
        CHECK(is_aimable(aimable[i]));
    }

    // Everything else, the wrench and the gadgets among them, is not.
    for (unsigned i = 0; i < sizeof not_aimable / sizeof not_aimable[0]; ++i) {
        CHECK(!is_aimable(not_aimable[i]));
    }
}

// The setting "button": both by default (and for anything unknown), or L2 or R2 alone.
void test_the_button_setting_picks_the_strafe_bits() {
    CHECK(button_bits(Button::Both) == (kPadL2 | kPadR2));
    CHECK(button_bits(Button::L2) == kPadL2);
    CHECK(button_bits(Button::R2) == kPadR2);
    CHECK(button_bits(static_cast<Button>(7)) == kStrafeButtons);
}

// Off: nothing strafes, L2 and R2 reach the game, nothing is latched.
void test_off_leaves_everything_to_the_game() {
    HoldState hold = {0, 0, 0, 0.0f};
    HeroInput in = running();
    in.held = kPadR2 | kPadCircle;
    update_hold(Mode::Off, Feel::Classic, in, hold);
    CHECK(!hold.latched);
    CHECK(!takes_buttons(Mode::Off, in));
    CHECK(strafe_kind(Mode::Off, in, hold) == Kind::None);
    hold.latched = true;  // even a latch left over gives nothing
    CHECK(strafe_kind(Mode::Off, in, hold) == Kind::None);
    CHECK(!turns_standing(Kind::None, kStateStand, hold));
    CHECK(!swings_camera(Kind::None, hold));
    // The game's own values pass through untouched.
    CHECK(turn_target(Kind::None, 0, 1.25f, -2.0f, hold) == 1.25f);
    CHECK(planar_angle(Kind::None, 99999.0f, 1.25f) == 99999.0f);
    CHECK(planar_angle(Kind::None, 0.5f, 1.25f) == 0.5f);
}

// Hold: L2 or R2 going down latches the facing; held, he strafes in the run, the jump and standing;
// let go, the game's own.
void test_hold_latches_the_facing_while_a_button_is_held() {
    const Mode mode = Mode::Hold;
    HoldState hold = {0, 0, 0, 0.0f};
    HeroInput in = running();
    in.slot_item = kItemWrench;
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(!hold.latched && strafe_kind(mode, in, hold) == Kind::None);
    CHECK(takes_buttons(mode, in));  // on foot: L2 and R2 are the strafe's
    in.held = kPadR2;
    in.camera = -2.0f;  // the camera elsewhere: the swing starts
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(hold.latched && hold.swinging && hold.yaw == 0.75f);
    CHECK(strafe_kind(mode, in, hold) == Kind::Held);
    // Held: the facing he turns to later is not latched again.
    in.facing = -1.0f;
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(hold.yaw == 0.75f && hold.swinging);
    // L2 does it too, and with both the latch stays.
    in.held = kPadL2 | kPadR2;
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(hold.latched && hold.yaw == 0.75f);
    // The jumps (the plain one, the running jump X gives strafing ahead, the double jump), the
    // alternate run and standing strafe too; other states do not (the flip 0xB is the mod's own,
    // 0x0A and 0x10 the pack's jumps, 0x06 the fall).
    const int strafing_states[] =
        {kStateRun, kStateRunAlt, kStateJump, kStateRunJump, kStateDoubleJump, kStateStand};

    // Each state the strafe works in latches and holds.
    for (unsigned i = 0; i < 6; ++i) {
        in.state = strafing_states[i];
        CHECK(strafe_kind(mode, in, hold) == Kind::Held);
    }

    const int other_states[] = {0x01, 0x35, 0x0B, 0x81, 0x13, 0x77, 0x06, 0x0A, 0x10};

    // Every other state is the game's own.
    for (unsigned i = 0; i < 9; ++i) {
        in.state = other_states[i];
        CHECK(strafe_kind(mode, in, hold) == Kind::None);
    }

    in.state = kStateRun;
    // A wall's own frame (the Magneboots) and a vehicle: not.
    in.frame_mode = 1;
    CHECK(strafe_kind(mode, in, hold) == Kind::None);
    in.frame_mode = 0;
    in.vehicle_mode = 1;
    CHECK(strafe_kind(mode, in, hold) == Kind::None);
    CHECK(!takes_buttons(mode, in));
    in.vehicle_mode = 0;
    // The hoverboard: its tricks keep L2 and R2.
    in.control_mode = kControlHoverboard;
    CHECK(strafe_kind(mode, in, hold) == Kind::None);
    CHECK(!takes_buttons(mode, in));
    in.control_mode = 0;
    // A scene, a menu, a vendor (not game mode 0), or no level: the game keeps L2 and R2 and
    // nothing strafes.
    in.playing = false;
    CHECK(strafe_kind(mode, in, hold) == Kind::None);
    CHECK(!takes_buttons(mode, in));
    in.playing = true;
    // kPadL1, kPadR1 and Circle never strafe and never latch.
    hold.latched = false;
    in.held = kPadL1 | kPadR1 | kPadCircle;
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(!hold.latched && strafe_kind(mode, in, hold) == Kind::None);
    // Let go: forgotten.
    in.held = kPadL2;
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(hold.latched);
    in.held = 0;
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(!hold.latched && strafe_kind(mode, in, hold) == Kind::None);
    // Pressed in a vehicle (or in the game's own strafe state): latched once he is on foot, with
    // the facing then.
    in.vehicle_mode = 2;
    in.held = kPadR2;
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(!hold.latched);
    in.vehicle_mode = 0;
    in.playing = false;  // held through a scene: not latched either
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(!hold.latched);
    in.playing = true;
    in.state = kStateGame;
    in.vehicle_mode = 0;
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(!hold.latched && !takes_buttons(mode, in));
    in.state = kStateRun;
    in.facing = 2.0f;
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(hold.latched && hold.yaw == 2.0f);
    // The aimable weapon makes no difference to Hold.
    in.slot_item = kItemBlaster;
    CHECK(strafe_kind(mode, in, hold) == Kind::Held);
    CHECK(hold.swinging && swings_camera(Kind::Held, hold));
    CHECK(!turns_standing(Kind::Held, kStateStand, hold));
    // Targets: while the swing lasts the latched yaw for turns of mode 0; the move yaw for the
    // velocity.
    hold.yaw = 0.5f;
    CHECK(turn_target(Kind::Held, 0, 1.25f, -2.0f, hold) == 0.5f);
    CHECK(turn_target(Kind::Held, 1, 1.25f, -2.0f, hold) == 1.25f);
    // The swing done: the camera's, standing too, and no more swing.
    hold.swinging = false;
    CHECK(turn_target(Kind::Held, 0, 1.25f, -2.0f, hold) == -2.0f);
    CHECK(turn_target(Kind::Held, 1, 1.25f, -2.0f, hold) == 1.25f);
    CHECK(turns_standing(Kind::Held, kStateStand, hold));
    CHECK(!turns_standing(Kind::Held, kStateRun, hold));
    CHECK(!swings_camera(Kind::Held, hold));
    CHECK(planar_angle(Kind::Held, 99999.0f, 1.25f) == 1.25f);
    CHECK(planar_angle(Kind::Held, 0.5f, 1.25f) == 0.5f);
}

// Lock: a forward-firing weapon ready makes him face the camera without a button; anything else in
// hand is Hold.
void test_lock_faces_the_camera_with_a_ready_weapon() {
    const Mode mode = Mode::Lock;
    HoldState hold = {0, 0, 0, 0.0f};
    HeroInput in = running();
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(strafe_kind(mode, in, hold) == Kind::Locked);
    CHECK(takes_buttons(mode, in));
    in.state = kStateStand;
    CHECK(strafe_kind(mode, in, hold) == Kind::Locked);
    CHECK(turns_standing(Kind::Locked, in.state, hold));
    CHECK(!turns_standing(Kind::Locked, kStateRun, hold));
    CHECK(!swings_camera(Kind::Locked, hold));
    in.state = kStateRun;
    // L2 held with the weapon: still the camera's.
    in.held = kPadL2;
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(strafe_kind(mode, in, hold) == Kind::Locked);
    // The weapon not ready (being put away, coming out): Hold's, while L2 is held.
    in.slot_state = 3;
    CHECK(strafe_kind(mode, in, hold) == Kind::Held);
    in.held = 0;
    update_hold(mode, Feel::Classic, in, hold);
    CHECK(strafe_kind(mode, in, hold) == Kind::None);
    in.slot_state = kSlotReady;
    // The wrench and the gadgets: Hold.
    const int others[] = {kItemWrench, kItemSwingshot, 0x16, 0x12};

    // Items that do not fire ahead leave Lock to Hold.
    for (unsigned i = 0; i < 4; ++i) {
        in.slot_item = others[i];
        in.held = 0;
        update_hold(mode, Feel::Classic, in, hold);
        CHECK(strafe_kind(mode, in, hold) == Kind::None);
        in.held = kPadR2;
        update_hold(mode, Feel::Classic, in, hold);
        CHECK(strafe_kind(mode, in, hold) == Kind::Held);
    }

    // Vehicles, walls, other states: never.
    in.slot_item = kItemBlaster;
    in.vehicle_mode = 1;
    CHECK(strafe_kind(mode, in, hold) == Kind::None);
    in.vehicle_mode = 0;
    in.frame_mode = 2;
    CHECK(strafe_kind(mode, in, hold) == Kind::None);
    in.frame_mode = 0;
    in.state = 0x35;  // crouching
    CHECK(strafe_kind(mode, in, hold) == Kind::None);
    hold.swinging = true;  // a swing never matters to Lock
    hold.yaw = 0.5f;
    CHECK(turn_target(Kind::Locked, 0, 1.25f, -2.0f, hold) == -2.0f);
    CHECK(turn_target(Kind::Locked, 1, 1.25f, -2.0f, hold) == 1.25f);
    CHECK(!swings_camera(Kind::Locked, hold));
}

// Moving: stick right (a quarter turn clockwise of the camera's forward) for a while.
void test_facing_and_velocity_while_the_stick_is_right() {
    const float camera = 0.5f;
    const float right = -1.5707964f;
    const HoldState none = {0, 0, 0, 0.0f};
    const HoldState latched = {1, 1, 0, 1.2f};  // swinging behind 1.2
    float facing;
    float v[2];
    // Lock: he ends facing the camera's way and moving at right angles to it.
    facing = camera;

    // Sixty ticks let the facing ease toward the camera's yaw.
    for (int i = 0; i < 60; ++i) {
        tick(Kind::Locked, camera, right, none, facing, v);
    }

    CHECK(std::fabs(facing - camera) < 1e-4f);
    CHECK(std::fabs(v[0] * std::cos(camera) + v[1] * std::sin(camera)) < 1e-4f);  // no forward part
    CHECK(
        std::fabs(v[0] - std::cos(camera + right)) < 1e-4f
        && std::fabs(v[1] - std::sin(camera + right)) < 1e-4f
    );
    // Hold, swinging behind 1.2 with the camera still elsewhere: he keeps 1.2 and moves where the
    // stick says.
    facing = 1.2f;

    // Sixty ticks of Hold's swing: the facing eases toward the latched yaw.
    for (int i = 0; i < 60; ++i) {
        tick(Kind::Held, camera, right, latched, facing, v);
    }

    CHECK(std::fabs(facing - 1.2f) < 1e-4f);
    CHECK(
        std::fabs(v[0] - std::cos(camera + right)) < 1e-4f
        && std::fabs(v[1] - std::sin(camera + right)) < 1e-4f
    );
    // The game's way: he turns round to face where he goes.
    facing = camera;

    // No strafe: sixty ticks of the game's own turn toward the move yaw.
    for (int i = 0; i < 60; ++i) {
        tick(Kind::None, camera, right, none, facing, v);
    }

    CHECK(std::fabs(facing - (camera + right)) < 1e-4f);
    // Back: still facing the camera's way, moving toward it.
    facing = camera;

    // Sixty ticks with the stick pushed straight back.
    for (int i = 0; i < 60; ++i) {
        tick(Kind::Locked, camera, 3.1415927f, none, facing, v);
    }

    CHECK(
        std::fabs(facing - camera) < 1e-4f
        && v[0] * std::cos(camera) + v[1] * std::sin(camera) < -0.999f
    );
}

// The camera's yaw over a tick as the follow camera turns it: the right stick's turn when it is
// pushed (the game's), else Hold's swing toward the latched facing while lock_strafe.c asks for it,
// else left where it is (the strafe camera slides along with him, it does not turn).
float camera_tick(float camera, float look_x, Kind kind, const HoldState& hold) {
    // The right stick's turn wins; otherwise the swing, while it lasts, turns the camera.
    if (look_x != 0.0f) {
        camera += look_x * 0.05f;
    } else if (swings_camera(kind, hold)) {
        camera += angle_diff(camera, hold.yaw) * 0.15f;
    }

    return angle_diff(0.0f, camera);
}

// The whole of Hold, a tick at a time: the pad's report (Hold's memory), the camera, the run.
struct HoldPlay {
    HoldState hold;
    HeroInput in;
    float v[2];
    Kind kind;
};

void hold_tick(HoldPlay& p, std::uint32_t held, float look_x, float stick) {
    p.in.held = held;
    p.in.look_x = look_x;
    update_hold(Mode::Hold, Feel::Classic, p.in, p.hold);
    p.kind = strafe_kind(Mode::Hold, p.in, p.hold);
    p.in.camera = camera_tick(p.in.camera, look_x, p.kind, p.hold);
    tick(p.kind, p.in.camera, stick, p.hold, p.in.facing, p.v);
}

// Hold as it plays: the swing once, then the right stick turns the camera and him with it, nothing
// pulls the camera back, and letting go leaves the camera where it is.
void test_hold_plays_through_the_swing_and_the_right_stick() {
    const float right = -1.5707964f;
    HoldPlay p = {{0, 0, 0, 0.0f}, running(), {0.0f, 0.0f}, Kind::None};
    p.in.slot_item = kItemWrench;
    p.in.facing = 2.0f;
    p.in.camera = -0.5f;  // the camera well off to his side
    // Pressed: the camera swings round behind his facing (2.0), which he keeps meanwhile.
    int ticks = 0;
    hold_tick(p, kPadR2, 0.0f, right);
    CHECK(p.kind == Kind::Held && p.hold.swinging && p.hold.yaw == 2.0f);

    // The swing lasts until the camera is behind the latched facing, which he keeps meanwhile.
    while (p.hold.swinging && ticks < 200) {
        hold_tick(p, kPadR2, 0.0f, right);
        CHECK(!p.hold.swinging || std::fabs(p.in.facing - 2.0f) < 1e-4f);
        ++ticks;
    }

    CHECK(!p.hold.swinging && ticks < kSwingTicks);  // ended by getting there
    CHECK(std::fabs(angle_diff(p.in.camera, 2.0f)) < kSwingDone);

    // Then he faces the camera's yaw, tick by tick.
    for (int i = 0; i < 30; ++i) {
        hold_tick(p, kPadR2, 0.0f, right);
    }

    CHECK(std::fabs(angle_diff(p.in.facing, p.in.camera)) < 1e-4f);
    // The right stick turns the camera, and his facing follows it.
    const float before = p.in.camera;

    // Pushing the right stick sideways turns the camera with it.
    for (int i = 0; i < 20; ++i) {
        hold_tick(p, kPadR2, 1.0f, right);
    }

    const float turned = p.in.camera;
    CHECK(std::fabs(angle_diff(before, turned) - 1.0f) < 1e-4f);

    // The stick let go leaves the camera where it was turned.
    for (int i = 0; i < 30; ++i) {
        hold_tick(p, kPadR2, 0.0f, right);
        // No snap back: the stick let go, the camera stays where it was turned, no swing again.
        CHECK(p.in.camera == turned && !p.hold.swinging);
    }

    CHECK(std::fabs(angle_diff(p.in.facing, turned)) < 1e-4f);
    CHECK(std::fabs(angle_diff(p.in.facing, 2.0f)) > 0.9f);  // not the press-time facing
    // The left stick moves him relative to it: right is at right angles, no forward part.
    CHECK(std::fabs(p.v[0] * std::cos(turned) + p.v[1] * std::sin(turned)) < 1e-4f);
    CHECK(
        std::fabs(p.v[0] - std::cos(turned + right)) < 1e-4f
        && std::fabs(p.v[1] - std::sin(turned + right)) < 1e-4f
    );

    // Back: still facing it, moving toward the camera.
    for (int i = 0; i < 5; ++i) {
        hold_tick(p, kPadR2, 0.0f, 3.1415927f);
    }

    CHECK(std::fabs(angle_diff(p.in.facing, turned)) < 1e-4f);
    CHECK(p.v[0] * std::cos(turned) + p.v[1] * std::sin(turned) < -0.999f);
    // Let go: the game's own, and the camera does not move on its account.
    const float facing = p.in.facing;
    hold_tick(p, 0, 0.0f, 0.0f);
    CHECK(p.kind == Kind::None && !p.hold.latched && !p.hold.swinging);
    CHECK(p.in.camera == turned);
    CHECK(std::fabs(angle_diff(p.in.facing, facing)) < 1e-4f);  // stick ahead: he stays

    // Pressed again, the right stick pushed during the swing: the swing ends there and then, and
    // he turns with the camera from where the player takes it.
    p.in.facing = 0.0f;
    p.in.camera = 3.0f;
    hold_tick(p, kPadL2, 0.0f, 0.0f);
    CHECK(p.hold.swinging);
    hold_tick(p, kPadL2, 0.5f, 0.0f);
    CHECK(!p.hold.swinging);
    const float taken = p.in.camera;

    // With the stick centred the camera stays where it is, tick after tick.
    for (int i = 0; i < 60; ++i) {
        hold_tick(p, kPadL2, 0.0f, 0.0f);
        CHECK(p.in.camera == taken);
    }

    CHECK(std::fabs(angle_diff(p.in.facing, taken)) < 1e-4f);

    // A camera that cannot get there (a wall): the swing gives up after its ticks.
    p.in.held = 0;
    update_hold(Mode::Hold, Feel::Classic, p.in, p.hold);
    p.in.facing = 1.0f;
    p.in.camera = -1.0f;
    p.in.held = kPadR2;
    update_hold(Mode::Hold, Feel::Classic, p.in, p.hold);
    CHECK(p.hold.swinging);

    // Held for kSwingTicks ticks, the swing ends with the camera still elsewhere.
    for (int i = 0; i < kSwingTicks; ++i) {
        update_hold(Mode::Hold, Feel::Classic, p.in, p.hold);
    }

    CHECK(!p.hold.swinging && p.hold.latched);
}

// Lock as it plays: the right stick's turn stays, nothing swings the camera back.
void test_lock_plays_through_the_right_stick() {
    HoldState hold = {0, 0, 0, 0.0f};
    HeroInput in = running();
    in.camera = 0.3f;
    float facing = 0.3f;
    float v[2];

    // Twenty ticks with the right stick pushed: the camera turns and he follows.
    for (int i = 0; i < 20; ++i) {
        update_hold(Mode::Lock, Feel::Classic, in, hold);
        const Kind kind = strafe_kind(Mode::Lock, in, hold);
        CHECK(kind == Kind::Locked);
        in.camera = camera_tick(in.camera, -1.0f, kind, hold);
        tick(kind, in.camera, 0.0f, hold, facing, v);
    }

    const float turned = in.camera;

    // Forty ticks with the stick let go: the camera stays where it was turned.
    for (int i = 0; i < 40; ++i) {
        update_hold(Mode::Lock, Feel::Classic, in, hold);
        const Kind kind = strafe_kind(Mode::Lock, in, hold);
        in.camera = camera_tick(in.camera, 0.0f, kind, hold);
        tick(kind, in.camera, 0.0f, hold, facing, v);
        CHECK(in.camera == turned);
    }

    CHECK(std::fabs(angle_diff(facing, turned)) < 1e-4f);
}

// The backpedal: when it plays the walk or the run backwards, and the step back itself.
void test_the_backpedal_starts_and_stops_at_its_edges() {
    const float deg = 0.017453292f;
    const int run = kStateRun;
    // Strafing in the run: straight back and up to 110 degrees off, the edges with their
    // hysteresis.
    CHECK(backpedals(Kind::Held, Feel::Classic, run, 0.5f, 0.5f + 3.1415927f, 0));
    CHECK(backpedals(Kind::Locked, Feel::Classic, kStateRunAlt, 0.5f, 0.5f - 120.0f * deg, 0));
    CHECK(!backpedals(Kind::Held, Feel::Classic, run, 0.5f, 0.5f + 105.0f * deg, 0));  // not yet
    CHECK(backpedals(Kind::Held, Feel::Classic, run, 0.5f, 0.5f + 105.0f * deg, 1));   // kept
    CHECK(!backpedals(Kind::Held, Feel::Classic, run, 0.5f, 0.5f + 95.0f * deg, 1));   // left
    CHECK(!backpedals(Kind::Held, Feel::Classic, run, 0.5f, 0.5f - 90.0f * deg, 0));   // sideways
    CHECK(!backpedals(Kind::Held, Feel::Classic, run, 0.5f, 0.5f, 1));                 // forward
    // Across the wrap of the angles: facing 170, moving at -30 (160 off).
    CHECK(backpedals(Kind::Held, Feel::Classic, run, 170.0f * deg, -30.0f * deg, 0));
    // Not strafing, or not in the run (standing, the jump): the game's.
    CHECK(!backpedals(Kind::None, Feel::Classic, run, 0.5f, 0.5f + 3.1415927f, 1));
    CHECK(!backpedals(Kind::Held, Feel::Classic, kStateJump, 0.5f, 0.5f + 3.1415927f, 0));
    CHECK(!backpedals(Kind::Held, Feel::Classic, kStateStand, 0.5f, 0.5f + 3.1415927f, 0));

    // The step back, a sequence of 4 keys at rates 0.5, 0.25, 0.5, 1.
    const float rates[4] = {0.5f, 0.25f, 0.5f, 1.0f};
    AnimationKeys a = {2, 3, 0.6f, 0.5f};
    step_animation_back(a, 0.25f, 4, rates);  // within the interval
    CHECK(a.key == 2 && a.next_key == 3 && std::fabs(a.fraction - 0.35f) < 1e-6f && a.rate == 0.5f);
    step_animation_back(a, 0.45f, 4, rates);  // 0.1 below: 0.2 ticks into key 1's interval
    CHECK(
        a.key == 1 && a.next_key == 2 && a.rate == 0.25f
        && std::fabs(a.fraction - (1.0f - 0.2f * 0.25f)) < 1e-6f
    );
    // Back past key 0: the last key and key 0, its rate.
    a = AnimationKeys{0, 1, 0.1f, 0.5f};
    step_animation_back(a, 0.2f, 4, rates);
    CHECK(
        a.key == 3 && a.next_key == 0 && a.rate == 1.0f
        && std::fabs(a.fraction - (1.0f - 0.2f)) < 1e-6f
    );
    // Several keys in one tick.
    a = AnimationKeys{3, 0, 0.5f, 1.0f};
    step_animation_back(a, 1.0f, 4, rates);  // 0.5 ticks over: key 2 (rate 0.5) takes 0.25 of it
    CHECK(a.key == 2 && a.next_key == 3 && std::fabs(a.fraction - 0.75f) < 1e-6f);
    // Never left where the advance would round it up to the next key.
    a = AnimationKeys{2, 3, 0.001f, 0.5f};
    step_animation_back(a, 0.002f, 4, rates);
    CHECK(a.key == 1 && a.fraction <= kBackTop);
    // A whole cycle backwards visits every key in reverse and comes back, and a key of no rate or a
    // huge step stops the loop.
    a = AnimationKeys{0, 1, 0.5f, 0.5f};
    int seen = 0;

    // Step back until five keys have been left or the loop gives up.
    for (int i = 0; i < 200 && seen < 5; ++i) {
        const int before = a.key;
        step_animation_back(a, 0.1f, 4, rates);

        // A key change steps one key back and the old key becomes key B.
        if (a.key != before) {
            CHECK(a.key == (before + 3) % 4 && a.next_key == before);
            ++seen;
        }
    }

    CHECK(seen == 5);
    const float stuck[4] = {0.5f, 0.0f, 0.5f, 0.5f};
    a = AnimationKeys{2, 3, 0.1f, 0.5f};
    step_animation_back(a, 0.5f, 4, stuck);
    CHECK(a.key == 1 && a.fraction == kBackTop);  // a key of no rate holds, as forward
    a = AnimationKeys{2, 3, 0.1f, 0.5f};
    step_animation_back(a, 1e30f, 4, rates);
    CHECK(a.fraction >= 0.0f && a.fraction <= kBackTop);
}

// Going Commando's feel: Hold with no swing, its turn rates, its backpedal edges.
void test_going_commandos_feel_turns_at_its_own_rates() {
    const float deg = 0.017453292f;
    const int run = kStateRun;
    HeroInput in;
    HoldState hold;
    std::memset(&in, 0, sizeof in);
    std::memset(&hold, 0, sizeof hold);
    in.playing = true;
    in.state = run;
    in.facing = 0.3f;
    in.camera = 1.2f;
    in.held = kPadL2;
    // L2 goes down: latched, no swing, and the turn goes for the camera at once.
    update_hold(Mode::Hold, Feel::Commando, in, hold);
    CHECK(hold.latched && !hold.swinging);
    const Kind kind = strafe_kind(Mode::Hold, in, hold);
    CHECK(kind == Kind::Held && !swings_camera(kind, hold));
    CHECK(turn_target(kind, 0, -1.0f, in.camera, hold) == in.camera);
    // Standing, he turns to the camera from the first tick.
    CHECK(turns_standing(kind, kStateStand, hold));

    // The rates: on the ground 0.037 / 0.25 / 12.2 rad/s, in the air 0.015 / 0.2 / 15, scaled.
    float accel, damping, limit;
    commando_rates(run, 1.44f, 0.02f, accel, damping, limit);
    CHECK(std::fabs(accel - 1.44f * 0.037f) < 1e-7f && std::fabs(damping - 1.44f * 0.25f) < 1e-7f);
    CHECK(std::fabs(limit - 0.02f * 12.2173061f) < 1e-7f);
    commando_rates(kStateJump, 1.0f, 1.0f / 60.0f, accel, damping, limit);
    CHECK(accel == 0.015f && damping == 0.2f && std::fabs(limit - 0.25f) < 1e-6f);
    // The running and double jumps: the jump group's rates too.
    commando_rates(kStateRunJump, 1.0f, 1.0f / 60.0f, accel, damping, limit);
    CHECK(accel == 0.015f && damping == 0.2f);
    commando_rates(kStateDoubleJump, 1.0f, 1.0f / 60.0f, accel, damping, limit);
    CHECK(accel == 0.015f && damping == 0.2f);

    // The backpedal within 45 degrees of straight back: 140 starts it, 132 keeps it, 128 ends it,
    // 120 never starts it.
    CHECK(backpedals(Kind::Held, Feel::Commando, run, 0.5f, 0.5f + 140.0f * deg, 0));
    CHECK(!backpedals(Kind::Held, Feel::Commando, run, 0.5f, 0.5f + 120.0f * deg, 0));
    CHECK(backpedals(Kind::Held, Feel::Commando, run, 0.5f, 0.5f - 132.0f * deg, 1));
    CHECK(!backpedals(Kind::Held, Feel::Commando, run, 0.5f, 0.5f - 128.0f * deg, 1));
}

// The sides and the side reversal: going left after right stops him, going right after left eases
// back in over four ticks.
void test_the_side_reversal_stops_and_eases_back() {
    const float deg = 0.017453292f;
    CHECK(side_of(1.0f, 1.0f + 90.0f * deg) == Side::Left);
    CHECK(side_of(1.0f, 1.0f + 50.0f * deg) == Side::Left);
    CHECK(side_of(1.0f, 1.0f + 40.0f * deg) == Side::None);
    CHECK(side_of(1.0f, 1.0f - 90.0f * deg) == Side::Right);
    CHECK(side_of(1.0f, 1.0f + 180.0f * deg) == Side::None);
    // Across the wrap: facing 170, moving at -100 (90 to his left).
    CHECK(side_of(170.0f * deg, -100.0f * deg) == Side::Left);

    ReverseTimers r = {0, 0};
    CHECK(reverse_step(r, Side::Right) == 1.0f);
    CHECK(reverse_step(r, Side::Right) == 1.0f);
    // Left now: the right is recent, his speed goes (rac2's -0.25, never below 0 here).
    CHECK(reverse_step(r, Side::Left) == 0.0f);
    CHECK(reverse_step(r, Side::Left) == 0.0f);

    // The right fades after five ticks; then left goes on at full speed.
    for (int i = 0; i < 3; ++i) {
        reverse_step(r, Side::Left);
    }

    CHECK(reverse_step(r, Side::Left) == 1.0f);
    // Right after left: 0, 0.25, 0.5, 0.75, then 1 (the left counting down 4, 3, 2, 1, 0).
    CHECK(reverse_step(r, Side::Right) == 0.0f);
    CHECK(reverse_step(r, Side::Right) == 0.25f);
    CHECK(reverse_step(r, Side::Right) == 0.5f);
    CHECK(reverse_step(r, Side::Right) == 0.75f);
    CHECK(reverse_step(r, Side::Right) == 1.0f);
    // Forward or back: no side, nothing changed.
    CHECK(reverse_step(r, Side::None) == 1.0f);
}

// The hip twist: its target, and the springs that ease the pelvis and the spine to it and back.
void test_the_hip_twist_eases_to_its_target_and_back() {
    const float deg = 0.017453292f;
    // Strafing right with the stick pushed: the pelvis toward the move, -90 degrees.
    CHECK(
        std::fabs(twist_target(Kind::Held, 1.0f, false, 0.5f, 0.5f - 90.0f * deg) + 90.0f * deg)
        < 1e-5f
    );
    // Across the wrap, the short way round.
    CHECK(
        std::fabs(twist_target(Kind::Locked, 1.0f, 0, 170.0f * deg, -100.0f * deg) - 90.0f * deg)
        < 1e-4f
    );
    // Not strafing, the stick near its centre, or backpedalling: none.
    CHECK(twist_target(Kind::None, 1.0f, 0, 0.5f, 2.0f) == 0.0f);
    CHECK(twist_target(Kind::Held, 0.15f, 0, 0.5f, 2.0f) == 0.0f);
    CHECK(twist_target(Kind::Held, 1.0f, 1, 0.5f, 2.0f) == 0.0f);

    // The spring's first tick from rest: velocity accel x distance, the angle moved by it.
    Spring s = {0.0f, 0.0f};
    spring_step(s, 1.0f, 0.037f, 0.27f);
    CHECK(std::fabs(s.velocity - 0.037f) < 1e-7f && std::fabs(s.angle - 0.037f) < 1e-7f);

    // It gets there without passing it (the velocity held within the distance left), and holds.
    for (int i = 0; i < 600; ++i) {
        spring_step(s, 1.0f, 0.037f, 0.27f);
        CHECK(s.angle <= 1.0f + 1e-6f);
    }

    CHECK(std::fabs(s.angle - 1.0f) < 1e-3f);

    // Back to 0 through 0's side, never past it, and then at rest.
    for (int i = 0; i < 600; ++i) {
        spring_step(s, 0.0f, 0.025f, 0.27f);
        CHECK(s.angle >= -1e-6f);
    }

    CHECK(spring_is_still(s));
    // From one side to the other it goes straight through 0, not round the back.
    s = Spring{2.5f, 0.0f};
    spring_step(s, -2.5f, 0.037f, 0.27f);
    CHECK(s.velocity < 0.0f && s.angle < 2.5f);
}

// Going Commando's quaternion for a turn about one axis (func_00283898, axis 2): a = -FastSin,
// b = FastCos, root = sqrt(|2b| + 2), (a / root, root / 2) in the x and w slots (swapped when 2b is
// below 0), the x slot's value then moved to z.
void rac2_axis_quaternion(float angle, float q[4]) {
    const float a = -std::sin(angle);
    const float b = std::cos(angle);
    const float twob = b + b;
    const float root = std::sqrt(std::fabs(twob) + 2.0f);
    const float half = 0.5f * root;
    const float ratio = a / root;
    q[0] = 0.0f;
    q[1] = 0.0f;
    q[2] = twob < 0.0f ? half : ratio;
    q[3] = twob < 0.0f ? ratio : half;
}

// The joint's x axis (where a forward-walking leg points) in its parent's frame, x and y: the first
// row of the quaternion's rotation, which the game's joint matrices take as the joint's first axis
// (OpenRAC's moby evaluator, quat_rows: 1 - 2(y^2 + z^2), 2(xy - zw)).
void joint_forward(const float q[4], float out[2]) {
    out[0] = 1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2]);
    out[1] = 2.0f * (q[0] * q[1] - q[2] * q[3]);
}

// The modifier composed with what is already there, as the evaluator's mode 0: q times r.
void quaternion_multiply(const float q[4], const float r[4], float out[4]) {
    out[0] = q[3] * r[0] + r[3] * q[0] + (q[1] * r[2] - q[2] * r[1]);
    out[1] = q[3] * r[1] + r[3] * q[1] + (q[2] * r[0] - q[0] * r[2]);
    out[2] = q[3] * r[2] + r[3] * q[2] + (q[0] * r[1] - q[1] * r[0]);
    out[3] = q[3] * r[3] - (q[0] * r[0] + q[1] * r[1] + q[2] * r[2]);
}

// The twist's quaternion turns the joint the game's way: the same turn as Going Commando builds for
// the same angle (up to the sign of the whole quaternion), and a positive angle points the joint's
// front toward his left. 1.1.0 sent the sine the other way round: his hips turned away from where
// he went, the moonwalk.
void test_the_joint_turns_the_way_the_games_do() {
    const float deg = 0.017453292f;

    // Every joint angle from -135 to 135 degrees, in steps of 5.
    for (int d = -135; d <= 135; d += 5) {
        const float a = static_cast<float>(d) * deg;
        float q[4];
        float g[4];
        float f[2];
        joint_yaw(a, q);
        rac2_axis_quaternion(a, g);
        const float dot = q[0] * g[0] + q[1] * g[1] + q[2] * g[2] + q[3] * g[3];
        CHECK(std::fabs(std::fabs(dot) - 1.0f) < 1e-5f);
        joint_forward(q, f);
        CHECK(std::fabs(f[0] - std::cos(a)) < 1e-5f && std::fabs(f[1] - std::sin(a)) < 1e-5f);
    }

    // A quarter turn left: z = -sin(45 degrees), his front to his left (0, 1).
    float q[4];
    float f[2];
    joint_yaw(90.0f * deg, q);
    CHECK(q[0] == 0.0f && q[1] == 0.0f && q[2] < -0.7f && q[3] > 0.7f);
    joint_forward(q, f);
    CHECK(std::fabs(f[0]) < 1e-5f && std::fabs(f[1] - 1.0f) < 1e-5f);
}

// Where his legs walk for each way the stick points, facing ahead (the camera's yaw): the pelvis
// toward where he goes and the spine back, so his legs walk forward along his velocity and his
// body faces ahead; within 45 degrees of straight back, no twist and the walk or run played
// backwards (rac1 has no backpedal animation of its own). Never forward-walking legs pointing
// away from where he moves: the moonwalk.
void test_the_legs_follow_the_velocity_in_each_direction() {
    const float deg = 0.017453292f;
    const float facing = 0.4f;
    bool was = false;

    // Every stick direction round the circle, in steps of 5 degrees.
    for (int d = -180; d < 180; d += 5) {
        // The edge itself (135 degrees) is either, by a float's last bit.
        if (std::abs(d) == 135) {
            continue;
        }

        const float stick = static_cast<float>(d) * deg;
        const float move_yaw = angle_diff(0.0f, facing + stick);
        const float v[2] = {std::cos(move_yaw), std::sin(move_yaw)};
        const bool back = backpedals(Kind::Locked, Feel::Commando, kStateRun, facing, move_yaw, 0);
        const float twist = twist_target(Kind::Locked, 1.0f, back, facing, move_yaw);
        float pelvis[4];
        float spine[4];
        float upper[4];
        float legs[2];
        float body[2];
        joint_yaw(twist, pelvis);
        joint_yaw(-twist, spine);
        quaternion_multiply(pelvis, spine, upper);
        joint_forward(pelvis, legs);
        joint_forward(upper, body);
        // Into the world, by his facing.
        const float lw[2] =
            {std::cos(facing) * legs[0] - std::sin(facing) * legs[1],
             std::sin(facing) * legs[0] + std::cos(facing) * legs[1]};
        const float bw[2] =
            {std::cos(facing) * body[0] - std::sin(facing) * body[1],
             std::sin(facing) * body[0] + std::cos(facing) * body[1]};
        // The body above the spine always faces ahead.
        CHECK(
            std::fabs(bw[0] - std::cos(facing)) < 1e-4f
            && std::fabs(bw[1] - std::sin(facing)) < 1e-4f
        );

        // Within 45 degrees of straight back he backpedals; anywhere else he walks forward.
        if (std::abs(d) > 135) {
            // Back: the walk played backwards, legs ahead, so they step back along his velocity.
            CHECK(back && twist == 0.0f);
            CHECK(lw[0] * v[0] + lw[1] * v[1] < -0.7f);
        } else {
            // Else forward, the legs along his velocity.
            CHECK(!back);
            CHECK(lw[0] * v[0] + lw[1] * v[1] > 0.9999f);
        }
    }

    // The four ways by name. Left (+90 degrees from his facing): the left side, legs to his left.
    float q[4];
    float f[2];
    const float left = twist_target(Kind::Held, 1.0f, false, 0.0f, 90.0f * deg);
    CHECK(side_of(0.0f, 90.0f * deg) == Side::Left);
    joint_yaw(left, q);
    joint_forward(q, f);
    CHECK(f[1] > 0.9999f);
    // Right: the right side, legs to his right.
    const float right = twist_target(Kind::Held, 1.0f, false, 0.0f, -90.0f * deg);
    CHECK(side_of(0.0f, -90.0f * deg) == Side::Right);
    joint_yaw(right, q);
    joint_forward(q, f);
    CHECK(f[1] < -0.9999f);
    // Forward: no side, no twist, no backpedal.
    CHECK(side_of(0.0f, 0.0f) == Side::None);
    CHECK(twist_target(Kind::Held, 1.0f, false, 0.0f, 0.0f) == 0.0f);
    was = backpedals(Kind::Held, Feel::Commando, kStateRun, 0.0f, 0.0f, 0);
    CHECK(!was);
    // Back: the backpedal (the walk or run played backwards), never forward legs.
    was = backpedals(Kind::Held, Feel::Commando, kStateRun, 0.0f, kPi, 0);
    CHECK(was);
    CHECK(twist_target(Kind::Held, 1.0f, was, 0.0f, kPi) == 0.0f);
    CHECK(side_of(0.0f, kPi) == Side::None);
}

// The glide (strafe.hpp): where his speed goes as a strafe ends. kFacingRequest is a request for
// the facing.

// Strafing: along the stick; the stick let go, along its last yaw; an angle of the caller's own
// kept; a strafe begun with the stick centred has the game's move yaw.
void test_the_glide_goes_along_the_stick_while_strafing() {
    GlideState g = {0, 0, 0.0f};
    CHECK(glide_angle(g, Kind::Held, kRunning, kLetGo, 0.0f, 0.2f, kFacingRequest) == 0.2f);
    CHECK(!g.valid);
    CHECK(glide_angle(g, Kind::Held, kRunning, kPushed, 0.0f, -1.5f, kFacingRequest) == -1.5f);
    CHECK(g.valid && g.leaving);
    // Let go: the game's move yaw is his facing (0), the slide stays on the stick's.
    CHECK(glide_angle(g, Kind::Held, kRunning, kLetGo, 0.0f, 0.0f, kFacingRequest) == -1.5f);
    CHECK(glide_angle(g, Kind::Locked, kNotRunning, kLetGo, 0.0f, 0.0f, kFacingRequest) == -1.5f);
    // The caller's own angle, strafing or not.
    CHECK(glide_angle(g, Kind::Held, kRunning, kPushed, 0.0f, -1.5f, 0.25f) == 0.25f);
    CHECK(glide_angle(g, Kind::None, kRunning, kPushed, 0.0f, -1.5f, -3.0f) == -3.0f);
    CHECK(glide_angle(g, Kind::None, kRunning, kPushed, 0.0f, -1.5f, kPi) == kPi);
}

// The strafe let go while he runs on: along the stick until he has turned to it, then the game's;
// with the stick let go at the same time, along its last yaw while he slows.
void test_the_glide_follows_the_stick_as_a_strafe_ends() {
    GlideState g = {0, 0, 0.0f};
    // Running straight back, facing 0.
    glide_angle(g, Kind::Held, kRunning, kPushed, 0.0f, kPi, kFacingRequest);
    CHECK(g.leaving);
    // L2 up: still along the stick while his facing is off it.
    CHECK(glide_angle(g, Kind::None, kRunning, kPushed, 0.0f, kPi, kFacingRequest) == kPi);
    CHECK(glide_angle(g, Kind::None, kRunning, kPushed, 2.0f, 3.0f, kFacingRequest) == 3.0f);
    // Within 3 degrees: the game's facing, and the glide over.
    CHECK(
        glide_angle(g, Kind::None, kRunning, kPushed, 2.99f, 3.0f, kFacingRequest) == kFacingRequest
    );
    CHECK(!g.leaving && !g.valid);
    // Over: a later turn of the game's own is the game's.
    CHECK(
        glide_angle(g, Kind::None, kRunning, kPushed, 0.0f, 3.0f, kFacingRequest) == kFacingRequest
    );

    // Across the wrap: facing a hair under pi, the stick a hair over minus pi, 2 degrees apart.
    g = GlideState{0, 0, 0.0f};
    glide_angle(g, Kind::Held, kRunning, kPushed, 0.0f, -3.12f, kFacingRequest);
    CHECK(
        glide_angle(g, Kind::None, kRunning, kPushed, 3.14f, -3.12f, kFacingRequest)
        == kFacingRequest
    );

    // L2 and the stick let go together: he slows along the stick's last yaw, not forward.
    g = GlideState{0, 0, 0.0f};
    glide_angle(g, Kind::Held, kRunning, kPushed, 0.0f, -1.5f, kFacingRequest);
    CHECK(glide_angle(g, Kind::None, kRunning, kLetGo, 0.0f, 0.0f, kFacingRequest) == -1.5f);
    // Pushed again before he turned: along the new stick.
    CHECK(glide_angle(g, Kind::None, kRunning, kPushed, 0.0f, 1.0f, kFacingRequest) == 1.0f);
    CHECK(g.yaw == 1.0f);

    // Out of the run (a jump, a stop) ends it: the game's, and not again on landing.
    g = GlideState{0, 0, 0.0f};
    glide_angle(g, Kind::Held, kRunning, kPushed, 0.0f, kPi, kFacingRequest);
    CHECK(
        glide_angle(g, Kind::None, kNotRunning, kPushed, 0.0f, 3.0f, kFacingRequest)
        == kFacingRequest
    );
    CHECK(
        glide_angle(g, Kind::None, kRunning, kPushed, 0.0f, 3.0f, kFacingRequest) == kFacingRequest
    );

    // A strafe standing (not in the run) leaves nothing behind when it ends.
    g = GlideState{0, 0, 0.0f};
    glide_angle(g, Kind::Locked, kNotRunning, kPushed, 0.0f, 2.0f, kFacingRequest);
    CHECK(!g.leaving);
    CHECK(
        glide_angle(g, Kind::None, kRunning, kPushed, 0.0f, 2.0f, kFacingRequest) == kFacingRequest
    );

    // Never strafed: the game's, always.
    g = GlideState{0, 0, 0.0f};
    CHECK(
        glide_angle(g, Kind::None, kRunning, kPushed, 0.0f, 2.0f, kFacingRequest) == kFacingRequest
    );
    CHECK(
        glide_angle(g, Kind::None, kRunning, kLetGo, 0.0f, 0.0f, kFacingRequest) == kFacingRequest
    );
}

// When the pad report forgets the glide: no strafe and out of the run, or out of play on foot
// (a scene starting mid-run keeps his state but must not slide him along an old stick).
void test_the_pad_report_forgets_the_glide() {
    CHECK(glide_ends(Kind::None, kStateStand, 1));
    CHECK(glide_ends(Kind::None, kStateJump, 1));
    CHECK(glide_ends(Kind::None, kStateFlip, 1));
    CHECK(!glide_ends(Kind::None, kStateRun, 1));
    CHECK(!glide_ends(Kind::None, kStateRunAlt, 1));
    CHECK(!glide_ends(Kind::Held, kStateStand, 1));
    CHECK(!glide_ends(Kind::Locked, kStateJump, 1));
    CHECK(glide_ends(Kind::None, kStateRun, 0));
    CHECK(glide_ends(Kind::None, kStateRunAlt, 0));
}

// The reported case, tick by tick: running straight back with L2 held, then L2 let go with the
// stick still back. The game turns his facing toward the stick a step a tick (here 0.2 rad, its
// run turn's order) and builds the velocity along the angle it is given. Before the fix the first
// ticks after L2 went forward, along his facing; now no tick moves him forward of where he faced,
// and once turned the game's facing takes over. Also with L2 let go and the stick let go together:
// he only ever slows backwards.
void test_the_glide_plays_through_a_strafe_ending() {
    const float back = kPi;
    GlideState g = {0, 0, 0.0f};
    float facing = 0.0f;
    bool handed_back = false;

    // Sixty ticks: Hold for ten, then the strafe is let go while he runs on.
    for (int tick = 0; tick < 60; ++tick) {
        const Kind kind = tick < 10 ? Kind::Held : Kind::None;

        // Strafing he faces the camera (0); after L2 the game turns him toward the stick.
        if (kind == Kind::None) {
            const float d = angle_diff(facing, back);
            facing += d > 0.2f ? 0.2f : (d < -0.2f ? -0.2f : d);
        }

        float angle = glide_angle(g, kind, kRunning, kPushed, facing, back, kFacingRequest);

        // The game's request for the facing comes back once the glide is over.
        if (!(angle <= kPi)) {
            angle = facing;
            handed_back = true;
        }

        // Never forward along where he faced while strafing.
        CHECK(std::cos(angle) < -0.99f);
    }

    CHECK(handed_back);

    // L2 and the stick let go together after a backward strafe: every tick slows him backwards.
    g = GlideState{0, 0, 0.0f};
    facing = 0.0f;

    // Thirty ticks: Hold for ten, then let go with the stick let go too.
    for (int tick = 0; tick < 30; ++tick) {
        const Kind kind = tick < 10 ? Kind::Held : Kind::None;
        const float target = tick < 10 ? kPushed : kLetGo;
        // The stick handler puts his facing in the move yaw when the stick is centred.
        const float move_yaw = tick < 10 ? back : facing;
        float angle = glide_angle(g, kind, kRunning, target, facing, move_yaw, kFacingRequest);

        // A request handed back is the game's facing.
        if (!(angle <= kPi)) {
            angle = facing;
        }

        CHECK(std::cos(angle) < -0.99f);
    }
}

#undef kRunning
#undef kNotRunning
#undef kPushed
#undef kLetGo

// The strafe flip: X pressed while strafing on the ground with the stick pushed fully.
void test_a_flip_needs_x_and_the_stick_pushed_fully() {
    const std::uint32_t x = kPadX;
    CHECK(starts_flip(Kind::Held, kStateStand, x, 1.0f));
    CHECK(starts_flip(Kind::Locked, kStateRun, x | kPadL2, 0.95f));
    CHECK(starts_flip(Kind::Held, kStateRunAlt, x, 0.91f));
    // The stick not past 0.9, X not pressed, in the air, or not strafing: the game's jump.
    CHECK(!starts_flip(Kind::Held, kStateRun, x, 0.9f));
    CHECK(!starts_flip(Kind::Held, kStateRun, 0x80u, 1.0f));
    CHECK(!starts_flip(Kind::Held, kStateJump, x, 1.0f));
    CHECK(!starts_flip(Kind::None, kStateRun, x, 1.0f));
}

// A camera of the level's own (Kalebo III's arena seen from a wall): nothing strafes in either
// mode, standing, running or in the air, but L2 and R2 stay taken (given back, L2 put him in first
// person, stuck); a press is not latched. Edge cases: the camera changing while L2 is held, while
// latched and mid-swing, while running with the glide on, and scenes or vehicles under it.
void test_a_levels_own_camera_is_not_strafed() {
    static const int kStates[] = {kStateStand, kStateRun, kStateRunAlt, kStateJump};
    HoldState hold = {0, 0, 0, 0.0f};
    HeroInput in = running();
    in.fixed_camera = true;

    // Each mode and state on foot: buttons taken, no strafe, no turn, no swing, no camera.
    for (unsigned i = 0; i < sizeof kStates / sizeof kStates[0]; ++i) {
        in.state = kStates[i];
        CHECK(takes_buttons(Mode::Lock, in));
        CHECK(takes_buttons(Mode::Hold, in));
        CHECK(strafe_kind(Mode::Lock, in, hold) == Kind::None);
        CHECK(strafe_kind(Mode::Hold, in, hold) == Kind::None);
        CHECK(turn_target(Kind::None, 0, 1.25f, -2.0f, hold) == 1.25f);
        CHECK(planar_angle(Kind::None, 99999.0f, 1.25f) == 99999.0f);
        CHECK(!turns_standing(Kind::None, in.state, hold));
        CHECK(!swings_camera(Kind::None, hold));
    }

    // Off foot under it: the game's, as anywhere (a scene, a vehicle, the game's own strafe).
    in.state = kStateGame;
    CHECK(!takes_buttons(Mode::Lock, in));
    in.state = kStateRun;
    in.vehicle_mode = 1;
    CHECK(!takes_buttons(Mode::Hold, in));
    in.vehicle_mode = 0;
    in.playing = false;
    CHECK(!takes_buttons(Mode::Hold, in));
    in.playing = true;
    // The setting off: the game's buttons.
    CHECK(!takes_buttons(Mode::Off, in));

    // L2 pressed under it: not latched, held on, still not; the camera back behind him, still
    // held: latched then, at his facing, and Hold strafes.
    in.held = kPadL2;
    in.facing = 0.3f;
    update_hold(Mode::Hold, Feel::Classic, in, hold);
    CHECK(!hold.latched);
    update_hold(Mode::Hold, Feel::Classic, in, hold);
    CHECK(!hold.latched);
    in.fixed_camera = false;
    update_hold(Mode::Hold, Feel::Classic, in, hold);
    CHECK(hold.latched && hold.yaw == 0.3f && hold.swinging);
    CHECK(strafe_kind(Mode::Hold, in, hold) == Kind::Held);

    // Latched and mid-swing, the camera turns into the level's: no strafe and no swing asked at
    // once; the latch stays while held, so back behind him he strafes on at the same yaw.
    in.fixed_camera = true;
    update_hold(Mode::Hold, Feel::Classic, in, hold);
    CHECK(strafe_kind(Mode::Hold, in, hold) == Kind::None);
    CHECK(!swings_camera(Kind::None, hold));
    in.fixed_camera = false;
    CHECK(strafe_kind(Mode::Hold, in, hold) == Kind::Held);
    CHECK(hold.yaw == 0.3f);

    // Let go under it: forgotten.
    in.fixed_camera = true;
    in.held = 0;
    update_hold(Mode::Hold, Feel::Classic, in, hold);
    CHECK(!hold.latched && !hold.swinging);

    // Lock with the blaster, the camera back behind him: locked again.
    in.fixed_camera = false;
    CHECK(strafe_kind(Mode::Lock, in, hold) == Kind::Locked);

    // Running in a strafe when the camera turns into the level's: the strafe ends as letting go
    // of L2 does (the glide follows the stick until he faces it), not a jump to the camera.
    {
        GlideState g = {0, 0, 0.0f};
        const float facing = 0.0f, stick = 1.5707964f;
        CHECK(glide_angle(g, Kind::Locked, 1, 0.1f, facing, stick, 99999.0f) == stick);
        CHECK(g.leaving);
        CHECK(glide_angle(g, Kind::None, 1, 0.1f, facing, stick, 99999.0f) == stick);
        CHECK(!glide_ends(Kind::None, kStateRun, 1));
        CHECK(glide_ends(Kind::None, kStateStand, 1));
    }
}

// The report: L2 held, X while running ahead (rac1's running jump, state 9), then the stick
// pushed left in the air. He keeps facing ahead (the latched yaw), and the double jump after it
// too; he goes left along the stick. 1.2.1 left state 9 to the game, which turned him left.
void test_the_running_jump_keeps_facing_ahead() {
    const int jumps[] = {kStateRunJump, kStateDoubleJump};

    // The running jump and the double jump behave alike.
    for (unsigned j = 0; j < 2; ++j) {
        HoldState hold = {0, 0, 0, 0.0f};
        HeroInput in = running();
        float facing = 0.0f, v[2];
        in.slot_item = kItemWrench;  // Hold, not Lock
        in.facing = facing;
        in.camera = facing;
        in.held = kPadL2;
        update_hold(Mode::Hold, Feel::Commando, in, hold);
        CHECK(hold.latched);
        in.state = jumps[j];

        // A second in the air with the stick left.
        for (int t = 0; t < 60; ++t) {
            const Kind kind = strafe_kind(Mode::Hold, in, hold);
            CHECK(kind == Kind::Held);
            tick(kind, in.camera, 1.5707964f, hold, facing, v);
        }

        CHECK(std::fabs(facing) < 1e-4f);
        CHECK(v[1] > 0.99f);  // along the stick: left
    }
}

}  // namespace

int main() {
    test_lock_aims_only_forward_firing_items();
    test_the_button_setting_picks_the_strafe_bits();
    test_off_leaves_everything_to_the_game();
    test_hold_latches_the_facing_while_a_button_is_held();
    test_lock_faces_the_camera_with_a_ready_weapon();
    test_facing_and_velocity_while_the_stick_is_right();
    test_hold_plays_through_the_swing_and_the_right_stick();
    test_lock_plays_through_the_right_stick();
    test_the_backpedal_starts_and_stops_at_its_edges();
    test_going_commandos_feel_turns_at_its_own_rates();
    test_the_side_reversal_stops_and_eases_back();
    test_the_hip_twist_eases_to_its_target_and_back();
    test_the_joint_turns_the_way_the_games_do();
    test_the_legs_follow_the_velocity_in_each_direction();
    test_a_flip_needs_x_and_the_stick_pushed_fully();
    test_the_glide_goes_along_the_stick_while_strafing();
    test_the_glide_follows_the_stick_as_a_strafe_ends();
    test_the_pad_report_forgets_the_glide();
    test_the_glide_plays_through_a_strafe_ending();
    test_a_levels_own_camera_is_not_strafed();
    test_the_running_jump_keeps_facing_ahead();
    std::puts(
        "hero strafe: weapons, buttons, off, hold, lock, the buttons taken, facing, velocity, "
        "swing, right stick, backpedal, Going Commando's feel, side reversal, hip twist, its "
        "joints' turn, the legs for each direction, flips, the glide as a strafe ends and a "
        "level's own camera and the running jump passed"
    );
    return 0;
}
