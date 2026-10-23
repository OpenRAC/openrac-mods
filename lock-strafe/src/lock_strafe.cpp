// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Ratchet & Clank: the lock-strafe, a mod of OpenRAC's native port (README.md). Two modes, the
// setting "mode": Lock with weapons (lock) and Hold L2 or R2 (hold). strafe.h says how the game
// turns the stick into a heading, what each mode changes and why L2 and R2. The pad's report, the
// hero's motion input, two of his routines and one of the follow camera's are hook points
// (openrac/games/rac1.hpp): each replacement calls what it replaced, so whenever the hero is not
// strafing the game's routines run as they are, but for L2 and R2, which the game does not see
// while he is on foot. Without the mod the port runs the game's own.
//
// Written with the SDK's C++ layer (openrac/mod.hpp): the reference C++ mod. In OpenRAC 0.8 this
// was the port's own option (port/game/rac1/host/strafe.c in OpenRAC), and the first version of
// this mod was that file in C on the interface's table; the behaviour is the same.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "discs.h"
#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"
#include "strafe.h"

namespace {

using openrac::gaddr;
namespace rac1 = openrac::rac1;

openrac::Mod* s_mod;
const lock_strafe_disc* s_disc;
int s_mode;    // RAC1_STRAFE_HOLD or RAC1_STRAFE_LOCK
bool s_trace;  // a line each pad report

// The game's functions it calls (discs.h).
float (*s_fast_cos)(float);
float (*s_fast_sin)(float);
void (*s_camera_slide)(int32_t);
void (*s_camera_spring)(float, float);
void (*s_camera_pan_off)();
void (*s_camera_look_along)(gaddr, float, float);

// The PadState words with button bits (pad.h: held, pressed, released, raw_held, raw_pressed,
// raw_released and the unmasked three).
constexpr uint32_t k_pad_words[] =
    {0x1A0u, 0x1A4u, 0x1A8u, 0x1B0u, 0x1B4u, 0x1B8u, 0x1C0u, 0x1C4u, 0x1C8u};

uint32_t s_buttons;  // L2 and R2 as the player holds them, from the last report
rac1_strafe_hold s_hold;
int s_backpedal;  // the walk or the run played backwards (strafe.h)

template <class T>
T& ref(gaddr address) {
    return s_mod->memory().ref<T>(address);
}

float get_float(gaddr at) {
    return s_mod->memory().read<float>(at);
}

void put_float(gaddr at, float v) {
    s_mod->memory().write<float>(at, v);
}

gaddr hero() {
    return s_disc->hero;
}

gaddr pad() {
    return s_disc->pad;
}

gaddr level0(uint32_t address) {
    return s_mod->relocate_level(0, address);
}

float camera_yaw() {
    return get_float(level0(s_disc->camera_yaw));
}

void read_input(rac1_strafe_input* in) {
    in->playing = s_mod->loaded_level() >= 0 && ref<int32_t>(level0(s_disc->level_mode)) == 0;
    in->state = ref<int32_t>(hero() + 0x2084u);
    in->frame_mode = ref<uint8_t>(hero() + 0x20B3u);
    in->vehicle_mode = ref<uint8_t>(hero() + 0x20A4u);
    in->slot_state = ref<int32_t>(hero() + 0x1090u + 0x24u);
    in->slot_item = ref<int32_t>(hero() + 0x1090u + 0x28u);
    in->held = (ref<uint32_t>(pad() + 0x1A0u) & ~RAC1_STRAFE_BUTTONS) | s_buttons;
    in->facing = get_float(hero() + 0x98u);
    in->camera = camera_yaw();
    in->look_x = get_float(pad() + 0x100u);
}

// What the strafe does now (strafe.h).
int strafe_kind() {
    rac1_strafe_input in;
    read_input(&in);
    return rac1_strafe_kind(s_mode, &in, &s_hold);
}

// The hero moby's animation fields (the trace's).
int anim_byte(unsigned at) {
    const gaddr moby = ref<gaddr>(hero() + 0x2080u);
    return moby != 0 ? ref<uint8_t>(moby + at) : -1;
}

float anim_float(unsigned at) {
    const gaddr moby = ref<gaddr>(hero() + 0x2080u);
    return moby != 0 ? get_float(moby + at) : 0.0f;
}

double d(float v) {
    return static_cast<double>(v);
}

// The setting "trace" (or OPENRAC_STRAFE_TRACE=1): a line each pad report in the log.
void trace() {
    if (!s_trace) {
        return;
    }

    rac1_strafe_input in;
    read_input(&in);
    char line[512];
    std::snprintf(
        line,
        sizeof line,
        "mode %d kind %d playing %d state %#x vehicle %d item %#x/%d held %#x seen %#x latched "
        "%d/%d/%.3f "
        "look %.2f facing %.3f move %.3f camera %.3f pos %.3f %.3f back %d anim %d/%d %d-%d %.3f "
        "speed %.3f",
        s_mode,
        rac1_strafe_kind(s_mode, &in, &s_hold),
        in.playing,
        static_cast<unsigned>(in.state),
        in.vehicle_mode,
        static_cast<unsigned>(in.slot_item),
        in.slot_state,
        static_cast<unsigned>(in.held),
        ref<uint32_t>(pad() + 0x1A0u),
        s_hold.latched,
        s_hold.swinging,
        d(s_hold.yaw),
        d(in.look_x),
        d(in.facing),
        d(get_float(hero() + 0x180u)),
        d(camera_yaw()),
        d(get_float(hero() + 0x80u)),
        d(get_float(hero() + 0x84u)),
        s_backpedal,
        anim_byte(0x52u),
        anim_byte(0x53u),
        anim_byte(0x50u),
        anim_byte(0x51u),
        d(anim_float(0x54u)),
        d(get_float(hero() + 0xA90u))
    );
    s_mod->info(line);
}

// Hero_TurnToHeading under the camera's yaw or Hold's latched one, as state 0x81 turns him while L2
// or R2 is held: that yaw in the move yaw for the call, the stick's put back after it.
void turn_toward(float yaw, int32_t mode, float accel, float damping, float limit) {
    const gaddr move_yaw = hero() + 0x180u;
    const float stick = get_float(move_yaw);
    put_float(move_yaw, yaw);
    s_mod->original<rac1::Hero_TurnToHeading>()(mode, accel, damping, limit);
    put_float(move_yaw, stick);
}

// Pad_ProcessReport: one report into the pad's state, each poll, before the tick reads it. L2 and
// R2 are kept here as the player holds them; in play, on foot, they are then taken out of the pad's
// button words (and this poll's history entry), the way the game's own input locks (PadState.mode)
// take buttons out, so that the game does not look or crouch on them (strafe.h). Hold's memory is
// updated here, once a tick.
template <class Original>
void pad_process_report(Original original, gaddr pad_state, gaddr report, int32_t size) {
    original(pad_state, report, size);

    if (pad_state != pad()) {
        return;
    }

    s_buttons = ref<uint32_t>(pad() + 0x1A0u) & RAC1_STRAFE_BUTTONS;
    rac1_strafe_input in;
    read_input(&in);
    rac1_strafe_hold_update(s_mode, &in, &s_hold);

    if (rac1_strafe_takes_buttons(s_mode, &in)) {
        for (const uint32_t word : k_pad_words) {
            ref<uint32_t>(pad() + word) &= ~RAC1_STRAFE_BUTTONS;
        }

        const int last = (ref<int16_t>(pad() + 0x18Eu) + 29) % 30;  // hist_btn[hist_idx - 1]
        ref<uint32_t>(pad() + 0x1E0u + 4u * static_cast<unsigned>(last)) &= ~RAC1_STRAFE_BUTTONS;
    }

    trace();
}

// Hero_UpdateMotionInput: the stick read into the hero, early in his update each tick. Standing,
// Lock and Hold after its swing turn him toward the camera first, where the game's standing state
// does not turn him: state 0x81's turn toward the camera with L2 or R2 held.
template <class Original>
void update_motion_input(Original original) {
    rac1_strafe_input in;
    read_input(&in);

    if (rac1_strafe_turns_standing(rac1_strafe_kind(s_mode, &in, &s_hold), in.state, &s_hold)) {
        const float frame = get_float(s_mod->relocate(s_disc->frame_scale));
        const float step = get_float(s_mod->relocate(s_disc->turn_step));
        turn_toward(camera_yaw(), 0, frame * 0.017f, frame * 0.3f, step * 4.712389f);
    }

    original();
}

// Hero_TurnToHeading: eases the facing toward the move yaw (+0x180). While strafing, toward the
// camera's yaw, or while Hold's swing lasts the facing it latched.
template <class Original>
void turn_to_heading(Original original, int32_t mode, float accel, float damping, float limit) {
    const int kind = mode == 0 ? strafe_kind() : RAC1_STRAFE_NONE;

    if (kind == RAC1_STRAFE_NONE) {
        original(mode, accel, damping, limit);
        return;
    }

    turn_toward(
        rac1_strafe_turn_target(kind, mode, get_float(hero() + 0x180u), camera_yaw(), &s_hold),
        mode,
        accel,
        damping,
        limit
    );
}

// The follow camera's per-tick requests from the hero's state (the camera moby's update calls it
// before it places the eye). In state 0x81 with L2 or R2 held, the game's strafe, it asks for its
// strafe camera and returns before its own panning and shake: the camera slides along with the
// hero (+0x116), held behind him by its lateral spring at 0.04 / 0.2, and the pan timers are
// cleared. While strafing the same is asked for: the game's routine runs, then what its panning
// part changed is put as that branch leaves it (the pan weight as it was, the timers 0, the spring
// and the slide asked for). Hold's swing turns the camera round behind the latched facing, as the
// game's own requests to look along a heading do (a turn standing in for the right stick's, which
// the stick overrides when moved and the camera's reset puts back to 0 at the end of each camera
// update). It is asked for only while the swing lasts (strafe.h): after it nothing turns the camera
// back toward that facing, and the right stick's turn stays where the player leaves it.
template <class Original>
void camera_overrides(Original original, gaddr camera) {
    const int kind = strafe_kind();
    const gaddr data = ref<gaddr>(camera + 0x70u);

    if (kind == RAC1_STRAFE_NONE) {
        original(camera);
        return;
    }

    const gaddr pan = level0(s_disc->camera_pan);
    const float pan_before = get_float(pan);
    original(camera);
    put_float(pan, pan_before);
    s_camera_slide(0);
    s_camera_pan_off();
    s_camera_spring(0.04f, 0.2f);
    ref<int16_t>(data + 0x12u) = 0;
    ref<int16_t>(data + 0x14u) = 0;
    ref<int16_t>(data + 0x16u) = 0;

    if (rac1_strafe_swings_camera(kind, &s_hold)) {
        // The heading as the game's camera volumes give theirs: (FastCos, FastSin, 0), on the
        // game's stack, where the game reads it.
        openrac::StackFrame frame(*s_mod, 16);
        const gaddr dir = frame.address();
        put_float(dir, s_fast_cos(s_hold.yaw));
        put_float(dir + 4u, s_fast_sin(s_hold.yaw));
        put_float(dir + 8u, 0.0f);
        put_float(dir + 12u, 0.0f);
        s_camera_look_along(dir, 0.20943952f, 0.0f);
    }
}

// Hero_SetPlanarVelocity: the velocity at the ground speed along an angle, the facing for one
// above pi. While strafing, along the move yaw instead: where the stick points, as the game's own
// run start from a standstill asks for.
template <class Original>
void set_planar_velocity(Original original, float angle) {
    if (!(angle <= 3.14159274f)) {
        angle = rac1_strafe_planar_angle(strafe_kind(), angle, get_float(hero() + 0x180u));
    }

    original(angle);
}

// Ratchet's animation advance (once a tick after his state's update): the key and blend of his
// moby moved on by his animation speed (+0xA90) times the key's rate (+0xA94). While he strafes away
// from his facing in the walk or the run (strafe.h, the backpedal), the sequence is stepped back
// here by what the advance would add, and the blend left that much lower so that the advance,
// adding it, ends where the step back did: the rest of what it does (the sounds, the items that
// follow his speed, the distance moved) is the game's. Only a sequence looping on itself (keys A
// and B in it, no blend from another, no sub-range or restart pending).
template <class Original>
void advance_animation(Original original) {
    rac1_strafe_input in;
    read_input(&in);
    const int kind = rac1_strafe_kind(s_mode, &in, &s_hold);
    s_backpedal =
        rac1_strafe_backpedal(kind, in.state, in.facing, get_float(hero() + 0x180u), s_backpedal);
    const gaddr moby = ref<gaddr>(hero() + 0x2080u);

    if (!s_backpedal || moby == 0) {
        original();
        return;
    }

    const int seq = ref<uint8_t>(moby + 0x53u);

    if (ref<uint8_t>(moby + 0x52u) != seq
        || (seq != RAC1_STRAFE_SEQ_WALK && seq != RAC1_STRAFE_SEQ_RUN)
        || ref<int32_t>(hero() + 0xAB0u) != -1 || ref<int32_t>(hero() + 0xAB8u) != 0) {
        original();
        return;
    }

    const gaddr sequence =
        ref<gaddr>(ref<gaddr>(moby + 0x24u) + 0x48u + 4u * static_cast<unsigned>(seq));
    const int count = ref<uint8_t>(sequence + 0x10u);

    if (ref<uint8_t>(moby + 0x50u) >= count || ref<uint8_t>(moby + 0x51u) >= count) {
        original();
        return;
    }

    float rates[256];

    for (int i = 0; i < count; ++i) {
        rates[i] = get_float(ref<gaddr>(sequence + 0x1Cu + 4u * static_cast<unsigned>(i)));
    }

    const float step = get_float(hero() + 0xA90u) * get_float(hero() + 0xA94u);
    rac1_strafe_anim a =
        {ref<uint8_t>(moby + 0x50u),
         ref<uint8_t>(moby + 0x51u),
         get_float(moby + 0x54u),
         get_float(hero() + 0xA94u)};
    rac1_strafe_anim_back(&a, step, count, rates);
    ref<uint8_t>(moby + 0x50u) = static_cast<uint8_t>(a.key);
    ref<uint8_t>(moby + 0x51u) = static_cast<uint8_t>(a.next_key);
    ref<gaddr>(moby + 0x68u) = ref<gaddr>(sequence + 0x1Cu + 4u * static_cast<unsigned>(a.key));
    ref<gaddr>(moby + 0x6Cu) =
        ref<gaddr>(sequence + 0x1Cu + 4u * static_cast<unsigned>(a.next_key));
    put_float(hero() + 0xA94u, a.rate);
    // The advance adds speed times the rate it now reads: the step of the interval stepped into.
    put_float(moby + 0x54u, a.fraction - get_float(hero() + 0xA90u) * a.rate);
    original();
}

// The game's function `name` on this disc, or a refusal naming it.
template <class Fn>
Fn game_function(const char* name) {
    Fn fn = s_mod->function<Fn>(name);

    if (fn == nullptr) {
        throw openrac::Error(
            std::string("this OpenRAC has no game function ") + name + " on the "
            + std::string(s_mod->version()) + " disc"
        );
    }

    return fn;
}

void load(openrac::Mod& mod) {
    s_mod = &mod;
    s_disc = nullptr;

    if (mod.game() != "rac1") {
        throw openrac::Error("lock-strafe is for Ratchet & Clank (rac1)");
    }

    for (const auto& disc : k_lock_strafe_discs) {
        if (mod.version() == disc.version) {
            s_disc = &disc;
        }
    }

    if (s_disc == nullptr) {
        throw openrac::Error(
            "lock-strafe knows the PAL and NTSC-U discs, not " + std::string(mod.version())
        );
    }

    s_mode = mod.setting<std::string>("mode") == "hold" ? RAC1_STRAFE_HOLD : RAC1_STRAFE_LOCK;
    s_trace = mod.setting<bool>("trace") || std::getenv("OPENRAC_STRAFE_TRACE") != nullptr;
    s_buttons = 0;
    s_hold = rac1_strafe_hold{};
    s_backpedal = 0;

    s_fast_cos = game_function<float (*)(float)>(s_disc->fast_cos);
    s_fast_sin = game_function<float (*)(float)>(s_disc->fast_sin);
    s_camera_slide = game_function<void (*)(int32_t)>(s_disc->camera_slide);
    s_camera_spring = game_function<void (*)(float, float)>(s_disc->camera_spring);
    s_camera_pan_off = game_function<void (*)()>(s_disc->camera_pan_off);
    s_camera_look_along = game_function<void (*)(gaddr, float, float)>(s_disc->camera_look_along);

    // A hook point this OpenRAC lacks throws: the load is refused with its name, and the port lets
    // go of the hooks already taken.
    mod.hook<rac1::Pad_ProcessReport>(
        [](auto original, uint32_t pad_state, uint32_t report, int32_t size) {
            pad_process_report(original, pad_state, report, size);
        }
    );
    mod.hook<rac1::Hero_UpdateMotionInput>([](auto original) { update_motion_input(original); });
    mod.hook<rac1::Hero_TurnToHeading>(
        [](auto original, int32_t m, float accel, float damping, float limit) {
            turn_to_heading(original, m, accel, damping, limit);
        }
    );
    mod.hook<rac1::Hero_SetPlanarVelocity>([](auto original, float angle) {
        set_planar_velocity(original, angle);
    });
    mod.hook<rac1::FollowCamera_ApplyOverrides>([](auto original, uint32_t camera) {
        camera_overrides(original, camera);
    });
    mod.hook<rac1::Hero_AdvanceAnimation>([](auto original) { advance_animation(original); });

    mod.info(
        std::string(s_mode == RAC1_STRAFE_HOLD ? "Hold L2 or R2" : "Lock with weapons") + " on the "
        + std::string(mod.version()) + " disc" + (s_trace ? ", tracing each pad report" : "")
    );
}

}  // namespace

OPENRAC_MOD_CPP("lock-strafe", "1.0.0", load)
