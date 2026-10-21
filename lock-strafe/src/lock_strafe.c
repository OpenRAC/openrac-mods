/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: the lock-strafe, a mod of OpenRAC's native port (README.md). Two modes, the
 * setting "mode": Lock with weapons (lock) and Hold L2 or R2 (hold). strafe.h says how the game
 * turns the stick into a heading, what each mode changes and why L2 and R2. The pad's report, the
 * hero's motion input, two of his routines and one of the follow camera's are hook points
 * (games/rac1.h): each replacement calls what it replaced, so whenever the hero is not strafing the
 * game's routines run as they are, but for L2 and R2, which the game does not see while he is on
 * foot. Without the mod the port runs the game's own.
 *
 * In OpenRAC 0.8 this was the port's own option (port/game/rac1/host/strafe.c in OpenRAC); the
 * code is that file's, on the mod interface.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openrac/games/rac1.h"
#include "openrac/openrac_mod.h"

#include "discs.h"
#include "strafe.h"

OPENRAC_MOD_DEFINE("lock-strafe", "1.0.0")

static const openrac_api* s_api;
static openrac_mod* s_mod;
static const lock_strafe_disc* s_disc;
static int s_mode;  /* RAC1_STRAFE_HOLD or RAC1_STRAFE_LOCK */
static int s_trace; /* a line each pad report */

/* What each hook point goes on to: the game's own, or a mod's loaded before this one. */
static openrac_rac1_Pad_ProcessReport s_pad_process_report;
static openrac_rac1_Hero_UpdateMotionInput s_update_motion_input;
static openrac_rac1_Hero_TurnToHeading s_turn_to_heading;
static openrac_rac1_Hero_SetPlanarVelocity s_set_planar_velocity;
static openrac_rac1_FollowCamera_ApplyOverrides s_camera_overrides;
static openrac_rac1_Hero_AdvanceAnimation s_advance_animation;

/* The game's functions it calls (discs.h). */
static float (*s_fast_cos)(float);
static float (*s_fast_sin)(float);
static void (*s_camera_slide)(int32_t);
static void (*s_camera_spring)(float, float);
static void (*s_camera_pan_off)(void);
static void (*s_camera_look_along)(openrac_gaddr, float, float);

/* The PadState words with button bits (pad.h: held, pressed, released, raw_held, raw_pressed,
 * raw_released and the unmasked three). */
static const uint32_t k_pad_words[] = {0x1A0u, 0x1A4u, 0x1A8u, 0x1B0u, 0x1B4u, 0x1B8u, 0x1C0u, 0x1C4u, 0x1C8u};
#define PAD_WORDS (sizeof(k_pad_words) / sizeof(k_pad_words[0]))

static uint32_t s_buttons; /* L2 and R2 as the player holds them, from the last report */
static rac1_strafe_hold s_hold;
static int s_backpedal; /* the walk or the run played backwards (strafe.h) */

#define GREF(T, a) OPENRAC_GREF(s_api, T, a)
#define HERO (s_disc->hero)
#define PAD (s_disc->pad)

static float get_float(openrac_gaddr at) {
    float v;
    memcpy(&v, OPENRAC_G(s_api, at), 4);
    return v;
}

static void put_float(openrac_gaddr at, float v) {
    memcpy(OPENRAC_G(s_api, at), &v, 4);
}

static openrac_gaddr level0(uint32_t address) {
    return s_api->relocate_level_data(0, address);
}

static float camera_yaw(void) {
    return get_float(level0(s_disc->camera_yaw));
}

static void read_input(rac1_strafe_input* in) {
    in->playing = s_api->loaded_level() >= 0 && GREF(int32_t, level0(s_disc->level_mode)) == 0;
    in->state = GREF(int32_t, HERO + 0x2084u);
    in->frame_mode = GREF(uint8_t, HERO + 0x20B3u);
    in->vehicle_mode = GREF(uint8_t, HERO + 0x20A4u);
    in->slot_state = GREF(int32_t, HERO + 0x1090u + 0x24u);
    in->slot_item = GREF(int32_t, HERO + 0x1090u + 0x28u);
    in->held = (GREF(uint32_t, PAD + 0x1A0u) & ~RAC1_STRAFE_BUTTONS) | s_buttons;
    in->facing = get_float(HERO + 0x98u);
    in->camera = camera_yaw();
    in->look_x = get_float(PAD + 0x100u);
}

/* What the strafe does now (strafe.h). */
static int strafe_kind(void) {
    rac1_strafe_input in;
    read_input(&in);
    return rac1_strafe_kind(s_mode, &in, &s_hold);
}

/* The hero moby's animation fields (the trace's). */
static int anim_byte(unsigned at) {
    const openrac_gaddr moby = GREF(openrac_gaddr, HERO + 0x2080u);
    return moby != 0 ? GREF(uint8_t, moby + at) : -1;
}

static float anim_float(unsigned at) {
    const openrac_gaddr moby = GREF(openrac_gaddr, HERO + 0x2080u);
    return moby != 0 ? get_float(moby + at) : 0.0f;
}

/* The setting "trace" (or OPENRAC_STRAFE_TRACE=1): a line each pad report in the log. */
static void trace(void) {
    if (!s_trace) {
        return;
    }
    rac1_strafe_input in;
    read_input(&in);
    char line[512];
    snprintf(line, sizeof line,
             "mode %d kind %d playing %d state %#x vehicle %d item %#x/%d held %#x seen %#x latched %d/%d/%.3f "
             "look %.2f facing %.3f move %.3f camera %.3f pos %.3f %.3f back %d anim %d/%d %d-%d %.3f speed %.3f",
             s_mode, rac1_strafe_kind(s_mode, &in, &s_hold), in.playing, (unsigned)in.state, in.vehicle_mode,
             (unsigned)in.slot_item, in.slot_state, (unsigned)in.held, GREF(uint32_t, PAD + 0x1A0u), s_hold.latched,
             s_hold.swinging, s_hold.yaw, in.look_x, in.facing, get_float(HERO + 0x180u), camera_yaw(),
             get_float(HERO + 0x80u), get_float(HERO + 0x84u), s_backpedal, anim_byte(0x52u), anim_byte(0x53u),
             anim_byte(0x50u), anim_byte(0x51u), anim_float(0x54u), get_float(HERO + 0xA90u));
    s_api->log(s_mod, OPENRAC_LOG_INFO, line);
}

/* Hero_TurnToHeading under the camera's yaw or Hold's latched one, as state 0x81 turns him while L2
 * or R2 is held: that yaw in the move yaw for the call, the stick's put back after it. */
static void turn_toward(float yaw, int32_t mode, float accel, float damping, float limit) {
    const openrac_gaddr move_yaw = HERO + 0x180u;
    const float stick = get_float(move_yaw);
    put_float(move_yaw, yaw);
    s_turn_to_heading(mode, accel, damping, limit);
    put_float(move_yaw, stick);
}

/* Pad_ProcessReport: one report into the pad's state, each poll, before the tick reads it. L2 and
 * R2 are kept here as the player holds them; in play, on foot, they are then taken out of the pad's
 * button words (and this poll's history entry), the way the game's own input locks (PadState.mode)
 * take buttons out, so that the game does not look or crouch on them (strafe.h). Hold's memory is
 * updated here, once a tick. */
static void pad_process_report(openrac_gaddr pad, openrac_gaddr report, int32_t size) {
    s_pad_process_report(pad, report, size);
    if (pad != PAD) {
        return;
    }
    s_buttons = GREF(uint32_t, PAD + 0x1A0u) & RAC1_STRAFE_BUTTONS;
    rac1_strafe_input in;
    read_input(&in);
    rac1_strafe_hold_update(s_mode, &in, &s_hold);
    if (rac1_strafe_takes_buttons(s_mode, &in)) {
        for (unsigned i = 0; i < PAD_WORDS; ++i) {
            GREF(uint32_t, PAD + k_pad_words[i]) &= ~RAC1_STRAFE_BUTTONS;
        }
        const int last = (GREF(int16_t, PAD + 0x18Eu) + 29) % 30; /* hist_btn[hist_idx - 1] */
        GREF(uint32_t, PAD + 0x1E0u + 4u * (unsigned)last) &= ~RAC1_STRAFE_BUTTONS;
    }
    trace();
}

/* Hero_UpdateMotionInput: the stick read into the hero, early in his update each tick. Standing,
 * Lock and Hold after its swing turn him toward the camera first, where the game's standing state
 * does not turn him: state 0x81's turn toward the camera with L2 or R2 held. */
static void update_motion_input(void) {
    rac1_strafe_input in;
    read_input(&in);
    if (rac1_strafe_turns_standing(rac1_strafe_kind(s_mode, &in, &s_hold), in.state, &s_hold)) {
        const float frame = get_float(s_api->relocate_data(s_disc->frame_scale));
        const float step = get_float(s_api->relocate_data(s_disc->turn_step));
        turn_toward(camera_yaw(), 0, frame * 0.017f, frame * 0.3f, step * 4.712389f);
    }
    s_update_motion_input();
}

/* Hero_TurnToHeading: eases the facing toward the move yaw (+0x180). While strafing, toward the
 * camera's yaw, or while Hold's swing lasts the facing it latched. */
static void turn_to_heading(int32_t mode, float accel, float damping, float limit) {
    const int kind = mode == 0 ? strafe_kind() : RAC1_STRAFE_NONE;
    if (kind == RAC1_STRAFE_NONE) {
        s_turn_to_heading(mode, accel, damping, limit);
        return;
    }
    turn_toward(rac1_strafe_turn_target(kind, mode, get_float(HERO + 0x180u), camera_yaw(), &s_hold), mode, accel,
                damping, limit);
}

/* The follow camera's per-tick requests from the hero's state (the camera moby's update calls it
 * before it places the eye). In state 0x81 with L2 or R2 held, the game's strafe, it asks for its
 * strafe camera and returns before its own panning and shake: the camera slides along with the
 * hero (+0x116), held behind him by its lateral spring at 0.04 / 0.2, and the pan timers are
 * cleared. While strafing the same is asked for: the game's routine runs, then what its panning
 * part changed is put as that branch leaves it (the pan weight as it was, the timers 0, the spring
 * and the slide asked for). Hold's swing turns the camera round behind the latched facing, as the
 * game's own requests to look along a heading do (a turn standing in for the right stick's, which
 * the stick overrides when moved and the camera's reset puts back to 0 at the end of each camera
 * update). It is asked for only while the swing lasts (strafe.h): after it nothing turns the camera
 * back toward that facing, and the right stick's turn stays where the player leaves it. */
static void camera_overrides(openrac_gaddr camera) {
    const int kind = strafe_kind();
    const openrac_gaddr data = GREF(openrac_gaddr, camera + 0x70u);
    if (kind == RAC1_STRAFE_NONE) {
        s_camera_overrides(camera);
        return;
    }
    const openrac_gaddr pan = level0(s_disc->camera_pan);
    const float pan_before = get_float(pan);
    s_camera_overrides(camera);
    put_float(pan, pan_before);
    s_camera_slide(0);
    s_camera_pan_off();
    s_camera_spring(0.04f, 0.2f);
    GREF(int16_t, data + 0x12u) = 0;
    GREF(int16_t, data + 0x14u) = 0;
    GREF(int16_t, data + 0x16u) = 0;
    if (rac1_strafe_swings_camera(kind, &s_hold)) {
        /* The heading as the game's camera volumes give theirs: (FastCos, FastSin, 0), on the
         * game's stack, where the game reads it. */
        openrac_gaddr saved;
        const openrac_gaddr dir = s_api->stack_push(16, &saved);
        put_float(dir, s_fast_cos(s_hold.yaw));
        put_float(dir + 4u, s_fast_sin(s_hold.yaw));
        put_float(dir + 8u, 0.0f);
        put_float(dir + 12u, 0.0f);
        s_camera_look_along(dir, 0.20943952f, 0.0f);
        s_api->stack_pop(saved);
    }
}

/* Hero_SetPlanarVelocity: the velocity at the ground speed along an angle, the facing for one
 * above pi. While strafing, along the move yaw instead: where the stick points, as the game's own
 * run start from a standstill asks for. */
static void set_planar_velocity(float angle) {
    if (!(angle <= 3.14159274f)) {
        angle = rac1_strafe_planar_angle(strafe_kind(), angle, get_float(HERO + 0x180u));
    }
    s_set_planar_velocity(angle);
}

/* Ratchet's animation advance (once a tick after his state's update): the key and blend of his
 * moby moved on by his animation speed (+0xA90) times the key's rate (+0xA94). While he strafes away
 * from his facing in the walk or the run (strafe.h, the backpedal), the sequence is stepped back
 * here by what the advance would add, and the blend left that much lower so that the advance,
 * adding it, ends where the step back did: the rest of what it does (the sounds, the items that
 * follow his speed, the distance moved) is the game's. Only a sequence looping on itself (keys A
 * and B in it, no blend from another, no sub-range or restart pending). */
static void advance_animation(void) {
    rac1_strafe_input in;
    read_input(&in);
    const int kind = rac1_strafe_kind(s_mode, &in, &s_hold);
    s_backpedal = rac1_strafe_backpedal(kind, in.state, in.facing, get_float(HERO + 0x180u), s_backpedal);
    const openrac_gaddr moby = GREF(openrac_gaddr, HERO + 0x2080u);
    if (!s_backpedal || moby == 0) {
        s_advance_animation();
        return;
    }
    const int seq = GREF(uint8_t, moby + 0x53u);
    if (GREF(uint8_t, moby + 0x52u) != seq || (seq != RAC1_STRAFE_SEQ_WALK && seq != RAC1_STRAFE_SEQ_RUN)
        || GREF(int32_t, HERO + 0xAB0u) != -1 || GREF(int32_t, HERO + 0xAB8u) != 0) {
        s_advance_animation();
        return;
    }
    const openrac_gaddr sequence = GREF(openrac_gaddr, GREF(openrac_gaddr, moby + 0x24u) + 0x48u + 4u * (unsigned)seq);
    const int count = GREF(uint8_t, sequence + 0x10u);
    if (GREF(uint8_t, moby + 0x50u) >= count || GREF(uint8_t, moby + 0x51u) >= count) {
        s_advance_animation();
        return;
    }
    float rates[256];
    for (int i = 0; i < count; ++i) {
        rates[i] = get_float(GREF(openrac_gaddr, sequence + 0x1Cu + 4u * (unsigned)i));
    }
    const float step = get_float(HERO + 0xA90u) * get_float(HERO + 0xA94u);
    rac1_strafe_anim a = {GREF(uint8_t, moby + 0x50u), GREF(uint8_t, moby + 0x51u), get_float(moby + 0x54u),
                          get_float(HERO + 0xA94u)};
    rac1_strafe_anim_back(&a, step, count, rates);
    GREF(uint8_t, moby + 0x50u) = (uint8_t)a.key;
    GREF(uint8_t, moby + 0x51u) = (uint8_t)a.next_key;
    GREF(openrac_gaddr, moby + 0x68u) = GREF(openrac_gaddr, sequence + 0x1Cu + 4u * (unsigned)a.key);
    GREF(openrac_gaddr, moby + 0x6Cu) = GREF(openrac_gaddr, sequence + 0x1Cu + 4u * (unsigned)a.next_key);
    put_float(HERO + 0xA94u, a.rate);
    /* The advance adds speed times the rate it now reads: the step of the interval stepped into. */
    put_float(moby + 0x54u, a.fraction - get_float(HERO + 0xA90u) * a.rate);
    s_advance_animation();
}

/* The game's function `name` on this disc, or a refusal naming it. */
static void* game_function(const char* name, int* missing) {
    void* fn = s_api->function(name);
    if (fn == NULL && !*missing) {
        char reason[128];
        snprintf(reason, sizeof reason, "this OpenRAC has no game function %s on the %s disc", name, s_api->version);
        s_api->fail(s_mod, reason);
        *missing = 1;
    }
    return fn;
}

static int take(int result, const char* point) {
    if (result != OPENRAC_HOOK_OK) {
        char reason[160];
        snprintf(reason, sizeof reason, "this OpenRAC cannot hook %s (%s)", point,
                 result == OPENRAC_HOOK_SIGNATURE ? "its signature is another" : "no such hook point");
        s_api->fail(s_mod, reason);
        return 0;
    }
    return 1;
}

OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    s_disc = NULL;
    if (strcmp(api->game, "rac1") != 0) {
        api->fail(mod, "lock-strafe is for Ratchet & Clank (rac1)");
        return 1;
    }
    for (size_t i = 0; i < sizeof k_lock_strafe_discs / sizeof k_lock_strafe_discs[0]; ++i) {
        if (strcmp(api->version, k_lock_strafe_discs[i].version) == 0) {
            s_disc = &k_lock_strafe_discs[i];
        }
    }
    if (s_disc == NULL) {
        char reason[96];
        snprintf(reason, sizeof reason, "lock-strafe knows the PAL and NTSC-U discs, not %s", api->version);
        api->fail(mod, reason);
        return 1;
    }

    const char* mode = api->setting_string(mod, "mode");
    s_mode = mode != NULL && strcmp(mode, "hold") == 0 ? RAC1_STRAFE_HOLD : RAC1_STRAFE_LOCK;
    s_trace = api->setting_bool(mod, "trace") || getenv("OPENRAC_STRAFE_TRACE") != NULL;

    int missing = 0;
    s_fast_cos = (float (*)(float))game_function(s_disc->fast_cos, &missing);
    s_fast_sin = (float (*)(float))game_function(s_disc->fast_sin, &missing);
    s_camera_slide = (void (*)(int32_t))game_function(s_disc->camera_slide, &missing);
    s_camera_spring = (void (*)(float, float))game_function(s_disc->camera_spring, &missing);
    s_camera_pan_off = (void (*)(void))game_function(s_disc->camera_pan_off, &missing);
    s_camera_look_along = (void (*)(openrac_gaddr, float, float))game_function(s_disc->camera_look_along, &missing);
    if (missing) {
        return 1;
    }

    if (!take(OPENRAC_RAC1_HOOK(api, mod, Pad_ProcessReport, pad_process_report, &s_pad_process_report),
              "Pad_ProcessReport")
        || !take(OPENRAC_RAC1_HOOK(api, mod, Hero_UpdateMotionInput, update_motion_input, &s_update_motion_input),
                 "Hero_UpdateMotionInput")
        || !take(OPENRAC_RAC1_HOOK(api, mod, Hero_TurnToHeading, turn_to_heading, &s_turn_to_heading),
                 "Hero_TurnToHeading")
        || !take(OPENRAC_RAC1_HOOK(api, mod, Hero_SetPlanarVelocity, set_planar_velocity, &s_set_planar_velocity),
                 "Hero_SetPlanarVelocity")
        || !take(OPENRAC_RAC1_HOOK(api, mod, FollowCamera_ApplyOverrides, camera_overrides, &s_camera_overrides),
                 "FollowCamera_ApplyOverrides")
        || !take(OPENRAC_RAC1_HOOK(api, mod, Hero_AdvanceAnimation, advance_animation, &s_advance_animation),
                 "Hero_AdvanceAnimation")) {
        return 1;
    }

    char line[96];
    snprintf(line, sizeof line, "%s on the %s disc%s", s_mode == RAC1_STRAFE_HOLD ? "Hold L2 or R2" : "Lock with weapons",
             api->version, s_trace ? ", tracing each pad report" : "");
    api->log(mod, OPENRAC_LOG_INFO, line);
    return 0;
}
