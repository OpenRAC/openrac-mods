// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Ratchet & Clank: the lock-strafe, a mod of OpenRAC's native port (README.md). Two modes, the
// setting "mode": Lock with weapons (lock) and Hold the strafe button (hold). strafe.h says how the
// game turns the stick into a heading, what each mode changes and why L2 and R2. The pad's report,
// the hero's motion input, two of his routines and one of the follow camera's are hook points
// (openrac/games/rac1.hpp): each replacement calls what it replaced, so whenever the hero is not
// strafing the game's routines run as they are, but for L2 and R2, which the game does not see
// while he is on foot. Without the mod the port runs the game's own.
//
// Written with the SDK's C++ layer (openrac/mod.hpp): the reference C++ mod. In OpenRAC 0.8 this
// was the port's own option (port/game/rac1/host/strafe.c in OpenRAC), and the first version of
// this mod was that file in C on the interface's table; the behaviour is the same.
//
// How the file is laid out: the constants (the offsets of the hero, the pad, the camera and the
// hero's animation in game memory), the state, the helpers that read the game, the six hook
// replacements, and last the load function that checks the disc, finds the game's functions and
// takes the hook points. The decision of when and how he strafes is in strafe.h, away from game
// memory; the addresses of each disc are in discs.h.
//
// Going Commando's strafe (DESIGN.md) is in the same hooks: its turn rates, the side reversal, the
// hip twist (hero_set_joints, each tick from the animation hook) and the strafe flips (rac1's own
// state 0xB, entered from the motion input hook on X).
//
// Settings (openrac-mod.json):
//   mode   (choice: lock | hold, default lock)  Lock: he strafes always with a weapon that fires
//          ahead in hand, and as Hold otherwise. Hold: he strafes only while the button is held.
//   button (choice: both | l2 | r2, default both)  The buttons that strafe: L2 or R2, or one alone,
//          the other left to the game and to other mods (one that raises the wrench on R2).
//   feel   (choice: commando | classic, default commando)  Going Commando's, or the mod's 1.0 one.
//   twist  (bool, default on)  The hip twist: legs toward where he goes, body toward the camera.
//   flips  (bool, default on)  X with the stick pushed fully sideways or back flips that way.
//   trace  (bool, default off)  For bug reports: a line in the game's log for each pad report, with
//          what the strafe does. The environment variable OPENRAC_STRAFE_TRACE=1 switches it on too.
#include "lock_strafe.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../common/spring.h"
#include "discs.h"
#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"
#include "strafe.h"

namespace lock_strafe::rac1 {

namespace {

using openrac::gaddr;
namespace hooks = openrac::rac1;

// ---- Constants ----

// The offsets below are those of the game's Hero, PadState and moby structures, the same on the PAL
// and NTSC-U discs; the addresses that differ by disc are in discs.h. Each carries its evidence.

// The level whose program holds the game mode and the camera's words: level 0 (discs.h).
constexpr int k_main_level = 0;

// The game mode in which the player plays: not a scene, a menu or a vendor (discs.h: 0 in play).
constexpr int32_t k_mode_playing = 0;

// In the hero: the pointer to his moby (assumed).
constexpr uint32_t k_hero_moby = 0x2080u;

// In the hero: his state code (seen in game code).
constexpr uint32_t k_hero_state = 0x2084u;

// In the hero: 0 on level ground; other values turn the hero's own frame (seen in game code).
constexpr uint32_t k_hero_frame_mode = 0x20B3u;

// In the hero: his control mode, 0x16 on the hoverboard (seen in game code).
constexpr uint32_t k_hero_control_mode = 0x208Cu;

// In the hero: 0 on foot, else the vehicle he is in (seen in game code).
constexpr uint32_t k_hero_vehicle_mode = 0x20A4u;

// In the hero: item slot 0, and in the slot its state and its item id (seen in game code).
constexpr uint32_t k_hero_slot0 = 0x1090u;
constexpr uint32_t k_slot_state = 0x24u;
constexpr uint32_t k_slot_item = 0x28u;

// In the hero: his facing (rot.z) (seen in game code).
constexpr uint32_t k_hero_facing = 0x98u;

// In the hero: the move yaw, where the stick points on the screen (seen in game code).
constexpr uint32_t k_hero_move_yaw = 0x180u;

// In the hero: how far the stick is pushed, 0 to 1 (seen in game code: stick_magnitude).
constexpr uint32_t k_hero_stick = 0x229Cu;

// In the hero: the speed his velocity is built at (seen in game code: ground_speed, read by
// Hero_SetPlanarVelocity).
constexpr uint32_t k_hero_ground_speed = 0x194u;

// In the hero: the flip's drift speed along its yaw (seen in game code: high_jump_drift_speed).
constexpr uint32_t k_hero_flip_drift = 0x458u;

// In the hero: the speed the flip carries along the facing it took off with (seen in game code:
// air_target_speed, set from his speed when state 0xB is entered).
constexpr uint32_t k_hero_flip_carry = 0x454u;

// In the hero: the speed the stick asks for, 0 with the stick centred (seen in game code:
// target_speed).
constexpr uint32_t k_hero_target_speed = 0x190u;

// In the hero: his position, x and y (assumed).
constexpr uint32_t k_hero_pos_x = 0x80u;
constexpr uint32_t k_hero_pos_y = 0x84u;

// In the hero: his animation speed, and the rate of the key he plays (seen in game code).
constexpr uint32_t k_hero_anim_speed = 0xA90u;
constexpr uint32_t k_hero_anim_rate = 0xA94u;

// In the hero: two words of the animation that must read -1 and 0 for a sequence looping on itself:
// no blend from another, no sub-range or restart pending (assumed).
constexpr uint32_t k_hero_anim_blend_from = 0xAB0u;
constexpr uint32_t k_hero_anim_pending = 0xAB8u;

// In the pad: the held buttons, and the right stick sideways (PadState.analog[0]) (seen in game
// code).
constexpr uint32_t k_pad_held = 0x1A0u;
constexpr uint32_t k_pad_pressed = 0x1A4u;
constexpr uint32_t k_pad_look_x = 0x100u;

// The PadState words with button bits (pad.h: held, pressed, released, raw_held, raw_pressed,
// raw_released and the unmasked three) (seen in game code).
constexpr uint32_t k_pad_words[] =
    {0x1A0u, 0x1A4u, 0x1A8u, 0x1B0u, 0x1B4u, 0x1B8u, 0x1C0u, 0x1C4u, 0x1C8u};

// In the pad: the index of the next entry of the button history, a ring of k_pad_history_length
// words starting at k_pad_history (assumed).
constexpr uint32_t k_pad_history_index = 0x18Eu;
constexpr uint32_t k_pad_history = 0x1E0u;
constexpr int k_pad_history_length = 30;

// The size in bytes of one word: the step of the history and of the sequence tables (assumed).
constexpr unsigned k_word = 4u;

// In the hero's moby: key A and key B (the two keys being blended), the two sequence numbers (the
// one blended from, then the current one), and the blend from A to B (seen in game code).
constexpr uint32_t k_moby_key = 0x50u;
constexpr uint32_t k_moby_next_key = 0x51u;
constexpr uint32_t k_moby_seq_from = 0x52u;
constexpr uint32_t k_moby_seq = 0x53u;
constexpr uint32_t k_moby_fraction = 0x54u;

// In the hero's moby: the pointer to its animation set, whose +0x48 is the table of sequence
// pointers, one word each (assumed).
constexpr uint32_t k_moby_anim_set = 0x24u;
constexpr uint32_t k_anim_set_sequences = 0x48u;

// In the hero's moby: the pointers to the records of keys A and B, which the advance reads their
// rates from (assumed).
constexpr uint32_t k_moby_rate_a = 0x68u;
constexpr uint32_t k_moby_rate_b = 0x6Cu;

// In a sequence: the count of its keys, and the table of pointers to each key's record, whose first
// word is the key's rate (assumed).
constexpr uint32_t k_sequence_count = 0x10u;
constexpr uint32_t k_sequence_keys = 0x1Cu;

// The most keys a sequence can hold: its count is one byte (assumed).
constexpr int k_max_sequence_keys = 256;

// In the follow camera: the pointer to its data, and in the data the three pan timers the game's
// panning part sets (assumed).
constexpr uint32_t k_camera_data = 0x70u;
constexpr uint32_t k_camera_timer0 = 0x12u;
constexpr uint32_t k_camera_timer1 = 0x14u;
constexpr uint32_t k_camera_timer2 = 0x16u;

// In a camera slot: its id; 0 is the camera behind the hero, the follow camera (seen in game code:
// Camera.id, FindCameraById(0) in CameraResetBehindHero).
constexpr uint32_t k_camera_id = 0x86u;
constexpr int16_t k_camera_behind_hero = 0;

// In a moby (include/structs/moby.h) and its class, what the reticles read: as the game's aim
// (select_auto_aim_target_moby) picks its targets, a live (state below 0x80), targetable (flag
// 0x1000) creature (class type 5), aimed at its target info's height (+0x10, the first pvars word
// with flag 0x20), else half a unit above its feet. Drawn last frame: the byte at +0x31.
constexpr uint32_t k_moby_pos = 0x10u;
constexpr uint32_t k_moby_state = 0x20u;
constexpr uint32_t k_moby_class = 0x24u;
constexpr uint32_t k_moby_next = 0x28u;
constexpr uint32_t k_moby_drawn = 0x31u;
constexpr uint32_t k_moby_flags = 0x34u;
constexpr uint32_t k_moby_pvars = 0x78u;
constexpr uint32_t k_class_type = 0x46u;
constexpr int16_t k_class_creature = 5;
constexpr uint8_t k_moby_dead = 0x80u;
constexpr uint16_t k_moby_targetable = 0x1000u;
constexpr uint16_t k_moby_target_info = 0x20u;
constexpr uint32_t k_target_height = 0x10u;
constexpr float k_target_height_default = 0.5f;

// The reticle as first person queues it (the blaster's): green, sprite 0x26, not turned; the
// queue's count at +0xC0, 3 entries. A walk of the moby list stops after this many (a broken list).
constexpr uint32_t k_reticle_colour = 0xFF0FFF0Fu;
constexpr int32_t k_reticle_sprite = 0x26;
constexpr int32_t k_reticle_mode = -1;
constexpr float k_reticle_turn_rate = 90.0f;
constexpr uint32_t k_reticle_count = 0xC0u;
constexpr int32_t k_reticle_slots = 3;
constexpr int k_moby_walk_limit = 4096;

// The strafe camera's lateral spring, as the game's strafe state asks for it (seen in game code).
constexpr float k_camera_spring_a = 0.04f;
constexpr float k_camera_spring_b = 0.2f;

// How fast the camera turns round to look along a heading: 12 degrees (assumed).
constexpr float k_camera_look_rate = 0.20943952f;

// The standing turn's rates: the acceleration and the damping are these times the tick's length,
// the limit is this times the turn's step (assumed: they stand in for state 0x81's turn).
constexpr float k_standing_accel = 0.017f;
constexpr float k_standing_damping = 0.3f;
constexpr float k_standing_limit = 4.712389f;

// HeroEnterMoveState's second argument: start the state's animation (seen in game code).
constexpr int32_t k_enter_animate = 1;

// Hero_TurnToHeading's first argument for the turns that follow the move yaw (strafe.h).
constexpr int32_t k_turn_follows_move_yaw = 0;

// The size of the trace's line in bytes (assumed).
constexpr size_t k_trace_line_size = 512;

// ---- State ----

// The mod's handle, the running disc's addresses and the mode and trace settings are set in load
// and read by the hooks. The hooks all run on the game's thread, the one that runs the hero's
// update; nothing here is touched by another thread.
openrac::Mod* s_mod;
const lock_strafe_disc* s_disc;
int s_mode;                 // RAC1_STRAFE_HOLD or RAC1_STRAFE_LOCK
int s_feel;                 // RAC1_STRAFE_FEEL_COMMANDO or RAC1_STRAFE_FEEL_CLASSIC
uint32_t s_strafe_buttons;  // the pad bits that strafe (the setting "button")
bool s_twist;               // the hip twist
bool s_flips;               // the strafe flips
bool s_reticles;            // reticles on the enemies within reach
bool s_trace;               // a line each pad report

// The game's functions it calls (discs.h). Set in load.
float (*s_fast_cos)(float);
float (*s_fast_sin)(float);
void (*s_camera_slide)(int32_t);
void (*s_camera_spring)(float, float);
void (*s_camera_pan_off)();
void (*s_camera_look_along)(gaddr, float, float);
int32_t (*s_enter_state)(int32_t, int32_t);
int32_t (*s_stick_direction)();
void (*s_reticle)();  // EffectQueue_Add, called as discs.h's reticle_args says; null: no reticles

// The strafe's buttons (L2, R2 or both) as the player holds them, from the last pad report (set by
// the pad hook, read by the decision's input). A button the setting leaves to the game is not here.
uint32_t s_buttons;

// What Hold remembers from tick to tick (strafe.h); updated once a tick at the pad's report.
rac1_strafe_hold s_hold;

// Whether the walk or the run is played backwards (strafe.h); updated each tick by the animation
// hook.
int s_backpedal;

// The pad reports so far (one a tick), and the last one the side reversal stepped at.
unsigned s_tick;
unsigned s_reverse_tick;

// The side reversal's timers (strafe.h).
rac1_strafe_reverse s_reverse;

// Where his speed goes as a strafe ends: the stick's last yaw, and whether he is leaving one
// (strafe.h).
rac1_strafe_glide s_glide;

// The hip twist: the pelvis's and the spine's springs, and whether the hero has the twist's joints
// (sent, and not yet taken off).
lock_strafe_spring s_pelvis;
lock_strafe_spring s_spine;
bool s_twist_shown;

// ---- Helpers ----

// A reference to a T at a game address.
template <class T>
T& ref(gaddr address) {
    return s_mod->memory().ref<T>(address);
}

// The float at a game address.
float get_float(gaddr at) {
    return s_mod->memory().read<float>(at);
}

// Writes a float at a game address.
void put_float(gaddr at, float v) {
    s_mod->memory().write<float>(at, v);
}

// The hero's address on the running disc.
gaddr hero() {
    return s_disc->hero;
}

// The first pad's state address on the running disc.
gaddr pad() {
    return s_disc->pad;
}

// The address of a word in level 0's program as loaded now (the program moves with the level).
gaddr level0(uint32_t address) {
    return s_mod->relocate_level(k_main_level, address);
}

// The camera's yaw now.
float camera_yaw() {
    return get_float(level0(s_disc->camera_yaw));
}

// The game's g_frame_scale_sq and g_frame_time now (the turns' scales).
float frame_scale_sq() {
    return get_float(s_mod->relocate(s_disc->frame_scale));
}

float frame_time() {
    return get_float(s_mod->relocate(s_disc->turn_step));
}

// Whether the camera in use is one of the level's own, not the one behind him (camera id 0,
// FindCameraById(0): the follow camera). 0 when no camera is set up yet.
bool fixed_camera() {
    const gaddr camera = ref<gaddr>(level0(s_disc->camera_now));
    return camera != 0 && ref<int16_t>(camera + k_camera_id) != k_camera_behind_hero;
}

// The hero's moby, 0 when he has none yet.
gaddr hero_moby() {
    return ref<gaddr>(hero() + k_hero_moby);
}

// Fills the decision's input (strafe.h) from the hero, the pad and the camera.
void read_input(rac1_strafe_input* in) {
    in->playing =
        s_mod->loaded_level() >= 0 && ref<int32_t>(level0(s_disc->level_mode)) == k_mode_playing;
    in->state = ref<int32_t>(hero() + k_hero_state);
    in->frame_mode = ref<uint8_t>(hero() + k_hero_frame_mode);
    in->vehicle_mode = ref<uint8_t>(hero() + k_hero_vehicle_mode);
    in->control_mode = ref<int32_t>(hero() + k_hero_control_mode);
    in->slot_state = ref<int32_t>(hero() + k_hero_slot0 + k_slot_state);
    in->slot_item = ref<int32_t>(hero() + k_hero_slot0 + k_slot_item);
    in->held = (ref<uint32_t>(pad() + k_pad_held) & ~RAC1_STRAFE_BUTTONS) | s_buttons;
    in->facing = get_float(hero() + k_hero_facing);
    in->camera = camera_yaw();
    in->look_x = get_float(pad() + k_pad_look_x);
    in->fixed_camera = fixed_camera();
}

// What the strafe does now (strafe.h).
int strafe_kind() {
    rac1_strafe_input in;
    read_input(&in);
    return rac1_strafe_kind(s_mode, &in, &s_hold);
}

// A byte of the hero moby's animation fields (the trace's); -1 when he has no moby.
int anim_byte(unsigned at) {
    const gaddr moby = hero_moby();
    return moby != 0 ? ref<uint8_t>(moby + at) : -1;
}

// A float of the hero moby's animation fields (the trace's); 0 when he has no moby.
float anim_float(unsigned at) {
    const gaddr moby = hero_moby();
    return moby != 0 ? get_float(moby + at) : 0.0f;
}

// A float as the double the trace's format takes.
double d(float v) {
    return static_cast<double>(v);
}

// The setting "trace" (or OPENRAC_STRAFE_TRACE=1): a line each pad report in the log.
void trace() {
    // Without the setting there is nothing to log.
    if (!s_trace) {
        return;
    }

    rac1_strafe_input in;
    read_input(&in);
    char line[k_trace_line_size];
    std::snprintf(
        line,
        sizeof line,
        "mode %d kind %d playing %d state %#x vehicle %d item %#x/%d held %#x seen %#x latched "
        "%d/%d/%.3f "
        "look %.2f facing %.3f move %.3f camera %.3f pos %.3f %.3f back %d anim %d/%d %d-%d %.3f "
        "speed %.3f feel %d twist %.3f/%.3f side %d/%d fixed %d",
        s_mode,
        rac1_strafe_kind(s_mode, &in, &s_hold),
        in.playing,
        static_cast<unsigned>(in.state),
        in.vehicle_mode,
        static_cast<unsigned>(in.slot_item),
        in.slot_state,
        static_cast<unsigned>(in.held),
        ref<uint32_t>(pad() + k_pad_held),
        s_hold.latched,
        s_hold.swinging,
        d(s_hold.yaw),
        d(in.look_x),
        d(in.facing),
        d(get_float(hero() + k_hero_move_yaw)),
        d(camera_yaw()),
        d(get_float(hero() + k_hero_pos_x)),
        d(get_float(hero() + k_hero_pos_y)),
        s_backpedal,
        anim_byte(k_moby_seq_from),
        anim_byte(k_moby_seq),
        anim_byte(k_moby_key),
        anim_byte(k_moby_next_key),
        d(anim_float(k_moby_fraction)),
        d(get_float(hero() + k_hero_anim_speed)),
        s_feel,
        d(s_pelvis.angle),
        d(s_spine.angle),
        s_reverse.left,
        s_reverse.right,
        in.fixed_camera
    );
    s_mod->info(line);
}

// Hero_TurnToHeading under the camera's yaw or Hold's latched one, as state 0x81 turns him while L2
// or R2 is held: that yaw in the move yaw for the call, the stick's put back after it.
void turn_toward(float yaw, int32_t mode, float accel, float damping, float limit) {
    const gaddr move_yaw = hero() + k_hero_move_yaw;
    const float stick = get_float(move_yaw);
    put_float(move_yaw, yaw);
    s_mod->original<hooks::Hero_TurnToHeading>()(mode, accel, damping, limit);
    put_float(move_yaw, stick);
}

// Whether the hero's animation is a sequence looping on itself that the backpedal may step back:
// the walk or the run, keys A and B in it, no blend from another and no sub-range or restart
// pending.
bool is_steppable_sequence(gaddr moby, int seq) {
    return ref<uint8_t>(moby + k_moby_seq_from) == seq
           && (seq == RAC1_STRAFE_SEQ_WALK || seq == RAC1_STRAFE_SEQ_RUN)
           && ref<int32_t>(hero() + k_hero_anim_blend_from) == -1
           && ref<int32_t>(hero() + k_hero_anim_pending) == 0;
}

// Steps the hero's looping sequence back by what the animation advance would add: his key and
// blend move to the interval before, and the blend is left that much lower so that the advance,
// adding it, ends where the step back did. sequence is the sequence's address, count its keys.
void step_sequence_back(gaddr moby, gaddr sequence, int count) {
    float rates[k_max_sequence_keys];

    // Each key's rate is the first word of its record.
    for (int i = 0; i < count; ++i) {
        rates[i] =
            get_float(ref<gaddr>(sequence + k_sequence_keys + k_word * static_cast<unsigned>(i)));
    }

    const float step = get_float(hero() + k_hero_anim_speed) * get_float(hero() + k_hero_anim_rate);
    rac1_strafe_anim a =
        {ref<uint8_t>(moby + k_moby_key),
         ref<uint8_t>(moby + k_moby_next_key),
         get_float(moby + k_moby_fraction),
         get_float(hero() + k_hero_anim_rate)};
    rac1_strafe_anim_back(&a, step, count, rates);
    ref<uint8_t>(moby + k_moby_key) = static_cast<uint8_t>(a.key);
    ref<uint8_t>(moby + k_moby_next_key) = static_cast<uint8_t>(a.next_key);
    ref<gaddr>(moby + k_moby_rate_a) =
        ref<gaddr>(sequence + k_sequence_keys + k_word * static_cast<unsigned>(a.key));
    ref<gaddr>(moby + k_moby_rate_b) =
        ref<gaddr>(sequence + k_sequence_keys + k_word * static_cast<unsigned>(a.next_key));
    put_float(hero() + k_hero_anim_rate, a.rate);
    // The advance adds speed times the rate it now reads: the step of the interval stepped into.
    put_float(moby + k_moby_fraction, a.fraction - get_float(hero() + k_hero_anim_speed) * a.rate);
}

// Sends the twist's two joints to the hero: the pelvis and the spine turned about z (a yaw: his
// skeleton's joint frames stand as his model's, z up) by their springs' angles, as the game's own
// quaternion for that turn (strafe.h, rac1_strafe_joint_yaw: the sine negated). False on a port
// that does not take a mod's joints (one that takes them only from the hero's update hook's
// holder): he then runs without the twist.
bool send_twist() {
    const auto joint = [](uint32_t index, float angle) {
        openrac_joint_offset j{};
        j.joint = index;
        rac1_strafe_joint_yaw(angle, j.rotation);
        return j;
    };

    // A port without the call at all (its table has no entry): nothing to send to.
    if (!s_mod->has_api_12() || s_mod->api().hero_set_joints == nullptr) {
        return false;
    }

    return s_mod->hero_set_joints(
        {joint(RAC1_STRAFE_JOINT_PELVIS, s_pelvis.angle),
         joint(RAC1_STRAFE_JOINT_SPINE, s_spine.angle)}
    );
}

// Takes the twist off the hero and puts its springs at rest.
void clear_twist() {
    s_pelvis = lock_strafe_spring{};
    s_spine = lock_strafe_spring{};

    // Only a twist that was sent has anything to take off.
    if (s_twist_shown) {
        s_twist_shown = false;

        // A port with the call: an empty list takes the joints off.
        if (s_mod->has_api_12() && s_mod->api().hero_set_joints != nullptr) {
            s_mod->hero_set_joints({});
        }
    }
}

// One tick of the hip twist (DESIGN.md): the pelvis eases toward the move yaw from his facing and
// the spine toward minus it, on Going Commando's springs; at rest at 0, the joints are taken off.
void update_twist(int kind) {
    // The setting off: never a twist.
    if (!s_twist) {
        return;
    }

    const float target = rac1_strafe_twist_target(
        kind,
        get_float(hero() + k_hero_stick),
        s_backpedal,
        get_float(hero() + k_hero_facing),
        get_float(hero() + k_hero_move_yaw)
    );
    const float scale = frame_scale_sq();
    lock_strafe_spring_step(
        &s_pelvis, target, scale * RAC1_STRAFE_TWIST_PELVIS_ACCEL, scale * RAC1_STRAFE_TWIST_DAMPING
    );
    lock_strafe_spring_step(
        &s_spine, -target, scale * RAC1_STRAFE_TWIST_SPINE_ACCEL, scale * RAC1_STRAFE_TWIST_DAMPING
    );

    // Back at rest with nothing to aim at: take the joints off once.
    if (target == 0.0f && lock_strafe_spring_still(&s_pelvis)
        && lock_strafe_spring_still(&s_spine)) {
        clear_twist();
        return;
    }

    s_twist_shown = send_twist() || s_twist_shown;
}

// The strafe flip (DESIGN.md): X pressed this tick while strafing, standing or running, with the
// stick pushed fully and not forward enters rac1's own side or back flip (state 0xB), which picks
// its animation from the stick (0x1C left, 0x1D right, 0x1F back) as rac2's does; its drift then
// starts at Going Commando's 5 units a second along the strafe.
void try_flip(const rac1_strafe_input& in, int kind) {
    // The setting off, or not X with the stick pushed fully while strafing on the ground.
    if (!s_flips
        || !rac1_strafe_flips(
            kind, in.state, ref<uint32_t>(pad() + k_pad_pressed), get_float(hero() + k_hero_stick)
        )) {
        return;
    }

    // Forward is the plain jump, the game's.
    if (s_stick_direction() == RAC1_STRAFE_DIRECTION_FORWARD) {
        return;
    }

    // The game may refuse the state (a lock on his moves): then its own jump goes on.
    if (s_enter_state(RAC1_STRAFE_STATE_FLIP, k_enter_animate) != 0) {
        put_float(hero() + k_hero_flip_drift, frame_time() * RAC1_STRAFE_FLIP_DRIFT);
        // State 0xB adds his run speed times the sine of the drift's angle from his facing along
        // that facing: from a crouch it is 0, from a sideways strafe all of it, forward. Going
        // Commando's flip goes only along the strafe, so none is carried.
        put_float(hero() + k_hero_flip_carry, 0.0f);
    }
}

// The game's function `name` on this disc, or a refusal naming it.
template <class Fn>
Fn game_function(const char* name) {
    Fn fn = s_mod->function<Fn>(name);

    // The host has no such function on this disc: the mod cannot run without it.
    if (fn == nullptr) {
        throw openrac::Error(
            std::string("this OpenRAC has no game function ") + name + " on the "
            + std::string(s_mod->version()) + " disc"
        );
    }

    return fn;
}

// ---- Hook replacements (the game's thread) ----

// Pad_ProcessReport: one report into the pad's state, each poll, before the tick reads it. L2 and
// R2 are kept here as the player holds them; in play, on foot, they are then taken out of the pad's
// button words (and this poll's history entry), the way the game's own input locks (PadState.mode)
// take buttons out, so that the game does not look or crouch on them (strafe.h). Hold's memory is
// updated here, once a tick.
template <class Original>
void pad_process_report(Original original, gaddr pad_state, gaddr report, int32_t size) {
    original(pad_state, report, size);

    // A report for another pad is not the first pad's: leave it alone.
    if (pad_state != pad()) {
        return;
    }

    ++s_tick;
    s_buttons = ref<uint32_t>(pad() + k_pad_held) & s_strafe_buttons;
    rac1_strafe_input in;
    read_input(&in);
    rac1_strafe_hold_update(s_mode, s_feel, &in, &s_hold);

    // Off foot or out of play the animation hook does not run: the twist goes now.
    if (s_twist_shown && (!in.playing || in.vehicle_mode != 0)) {
        clear_twist();
    }

    // Out of the strafe and the run, or out of play on foot: the glide is forgotten, so the next
    // run is the game's (and a scene or a vehicle never slides along an old stick).
    if (rac1_strafe_glide_ends(
            rac1_strafe_kind(s_mode, &in, &s_hold), in.state, in.playing && in.vehicle_mode == 0
        )) {
        s_glide = rac1_strafe_glide{};
    }

    // In play on foot with the setting on, the game must not see the strafe's buttons; a button the
    // setting "button" leaves out stays the game's (and other mods').
    if (rac1_strafe_takes_buttons(s_mode, &in)) {
        // Every button word of the pad has the strafe's bits cleared.
        for (const uint32_t word : k_pad_words) {
            ref<uint32_t>(pad() + word) &= ~s_strafe_buttons;
        }

        // hist_btn[hist_idx - 1]: the entry this poll wrote, in a ring of k_pad_history_length.
        const int last = (ref<int16_t>(pad() + k_pad_history_index) + (k_pad_history_length - 1))
                         % k_pad_history_length;
        ref<uint32_t>(pad() + k_pad_history + k_word * static_cast<unsigned>(last)) &=
            ~s_strafe_buttons;
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

    const int kind = rac1_strafe_kind(s_mode, &in, &s_hold);

    // Standing in a strafe that turns him: turn him toward the camera before the game's update, at
    // Going Commando's rates or the classic ones.
    if (rac1_strafe_turns_standing(kind, in.state, &s_hold)) {
        float accel = frame_scale_sq() * k_standing_accel;
        float damping = frame_scale_sq() * k_standing_damping;
        float limit = frame_time() * k_standing_limit;

        // Going Commando's feel turns him standing as it does walking.
        if (s_feel == RAC1_STRAFE_FEEL_COMMANDO) {
            rac1_strafe_commando_rates(
                in.state, frame_scale_sq(), frame_time(), &accel, &damping, &limit
            );
        }

        turn_toward(camera_yaw(), k_turn_follows_move_yaw, accel, damping, limit);
    }

    original();
    // The stick is read: X may flip him (before his state's code sees it).
    try_flip(in, kind);
}

// Hero_TurnToHeading: eases the facing toward the move yaw (+0x180). While strafing, toward the
// camera's yaw, or while Hold's swing lasts the facing it latched.
template <class Original>
void turn_to_heading(Original original, int32_t mode, float accel, float damping, float limit) {
    const int kind = mode == k_turn_follows_move_yaw ? strafe_kind() : RAC1_STRAFE_NONE;

    // Not strafing, or a turn that does not follow the move yaw: the game's own.
    if (kind == RAC1_STRAFE_NONE) {
        original(mode, accel, damping, limit);
        return;
    }

    // Going Commando's feel turns him at its own strafe rates.
    if (s_feel == RAC1_STRAFE_FEEL_COMMANDO) {
        rac1_strafe_commando_rates(
            ref<int32_t>(hero() + k_hero_state),
            frame_scale_sq(),
            frame_time(),
            &accel,
            &damping,
            &limit
        );
    }

    turn_toward(
        rac1_strafe_turn_target(
            kind, mode, get_float(hero() + k_hero_move_yaw), camera_yaw(), &s_hold
        ),
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
    const gaddr data = ref<gaddr>(camera + k_camera_data);

    // Not strafing: the game's own camera requests.
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
    s_camera_spring(k_camera_spring_a, k_camera_spring_b);
    ref<int16_t>(data + k_camera_timer0) = 0;
    ref<int16_t>(data + k_camera_timer1) = 0;
    ref<int16_t>(data + k_camera_timer2) = 0;

    // Hold's swing is on: ask the camera to turn round behind the latched facing.
    if (rac1_strafe_swings_camera(kind, &s_hold)) {
        // The heading as the game's camera volumes give theirs: (FastCos, FastSin, 0), on the
        // game's stack, where the game reads it.
        openrac::StackFrame frame(*s_mod, 16);
        const gaddr dir = frame.address();
        put_float(dir, s_fast_cos(s_hold.yaw));
        put_float(dir + 4u, s_fast_sin(s_hold.yaw));
        put_float(dir + 8u, 0.0f);
        put_float(dir + 12u, 0.0f);
        s_camera_look_along(dir, k_camera_look_rate, 0.0f);
    }
}

// Hero_SetPlanarVelocity: the velocity at the ground speed along an angle, the facing for one
// above pi. While strafing, along the move yaw instead: where the stick points, as the game's own
// run start from a standstill asks for; and as a strafe ends, along the stick until he has turned
// to it or stopped (strafe.h, the glide).
template <class Original>
void set_planar_velocity(Original original, float angle) {
    // An angle above pi is the game's request for the facing; while strafing it becomes the move yaw.
    if (!(angle <= RAC1_STRAFE_PI)) {
        const int kind = strafe_kind();
        const int state = ref<int32_t>(hero() + k_hero_state);
        const bool running = state == RAC1_STRAFE_STATE_RUN || state == RAC1_STRAFE_STATE_RUN_ALT;
        // Along the stick while strafing, and as a strafe ends while he turns or slows (strafe.h).
        angle = rac1_strafe_glide_angle(
            &s_glide,
            kind,
            running,
            get_float(hero() + k_hero_target_speed),
            get_float(hero() + k_hero_facing),
            get_float(hero() + k_hero_move_yaw),
            angle
        );

        // Going Commando's side reversal, once a tick in the run: going from one side to the other
        // kills the speed he had (strafe.h).
        if (kind != RAC1_STRAFE_NONE && s_feel == RAC1_STRAFE_FEEL_COMMANDO && running
            && s_reverse_tick != s_tick) {
            s_reverse_tick = s_tick;
            const float f = rac1_strafe_reverse_step(
                &s_reverse,
                rac1_strafe_side(
                    get_float(hero() + k_hero_facing), get_float(hero() + k_hero_move_yaw)
                )
            );
            ref<float>(hero() + k_hero_ground_speed) *= f;
        }
    }

    original(angle);
}

// The distance from a point in game memory (x, y, z) to `b`.
float distance(gaddr a, const float b[3]) {
    const float x = get_float(a) - b[0];
    const float y = get_float(a + 4u) - b[1];
    const float z = get_float(a + 8u) - b[2];
    return std::sqrt(x * x + y * y + z * z);
}

// One reticle on the game's queue at `point` (16 bytes in game memory).
void queue_reticle(gaddr point, float size) {
    const auto owner = static_cast<int32_t>(hero_moby());
    const auto colour = static_cast<int32_t>(k_reticle_colour);

    // Each disc's build declares the parameters in its own order (discs.h).
    if (s_disc->reticle_args == LOCK_STRAFE_RETICLE_ARGS_PAL) {
        using Fn = void (*)(int32_t, int32_t, float, float, gaddr, int32_t, int32_t, float);
        reinterpret_cast<Fn>(s_reticle)(
            owner, colour, size, 0.0f, point, k_reticle_sprite, k_reticle_mode, k_reticle_turn_rate
        );
    } else {
        using Fn = void (*)(int32_t, int32_t, gaddr, int32_t, int32_t, float, float, float);
        reinterpret_cast<Fn>(s_reticle)(
            owner, colour, point, k_reticle_sprite, k_reticle_mode, size, 0.0f, k_reticle_turn_rate
        );
    }
}

// Where a moby is aimed at (its target point, as the game's aim takes it), or false when it is no
// target: dead, not targetable, not a creature or not drawn last frame.
bool target_point(gaddr moby, float point[3]) {
    const gaddr cls = ref<gaddr>(moby + k_moby_class);
    const uint16_t flags = ref<uint16_t>(moby + k_moby_flags);

    // Not one the game's aim takes, or off the screen: no reticle.
    if (ref<uint8_t>(moby + k_moby_state) >= k_moby_dead || (flags & k_moby_targetable) == 0
        || cls == 0 || ref<int16_t>(cls + k_class_type) != k_class_creature
        || ref<uint8_t>(moby + k_moby_drawn) == 0) {
        return false;
    }

    const gaddr info = (flags & k_moby_target_info) != 0 ? ref<gaddr>(moby + k_moby_pvars) : 0;
    point[0] = get_float(moby + k_moby_pos);
    point[1] = get_float(moby + k_moby_pos + 4u);
    point[2] = get_float(moby + k_moby_pos + 8u)
               + (info != 0 ? get_float(info + k_target_height) : k_target_height_default);
    return true;
}

// The reticles (strafe.h): on the nearest enemies within reach, while he strafes with a weapon
// that fires ahead, as many as the game's queue has room for.
void draw_reticles(int kind, const rac1_strafe_input& in) {
    // The setting off, not strafing with a gun, or no queue on this OpenRAC: none.
    if (!s_reticles || s_reticle == nullptr || !rac1_strafe_shows_reticles(kind, &in)) {
        return;
    }

    float reach[RAC1_STRAFE_RETICLES] = {};
    float points[RAC1_STRAFE_RETICLES][3] = {};
    int count = 0;
    gaddr moby = ref<gaddr>(level0(s_disc->mobys));

    // Keep the nearest, nearest first.
    for (int walked = 0; moby != 0 && walked < k_moby_walk_limit; ++walked) {
        float point[3];

        // A target: where it goes among the kept ones, if anywhere.
        if (target_point(moby, point)) {
            const float r = distance(hero() + k_hero_pos_x, point);
            const int at = rac1_strafe_reticle_slot(reach, count, r);

            // Nearer than one kept, or room left: in, the farther ones down one.
            if (at >= 0) {
                count = count < RAC1_STRAFE_RETICLES ? count + 1 : count;

                for (int i = count - 1; i > at; --i) {
                    reach[i] = reach[i - 1];
                    std::memcpy(points[i], points[i - 1], sizeof point);
                }

                reach[at] = r;
                std::memcpy(points[at], point, sizeof point);
            }
        }

        moby = ref<gaddr>(moby + k_moby_next);
    }

    const gaddr queued = level0(s_disc->reticles) + k_reticle_count;
    const gaddr camera = level0(s_disc->camera_pos);
    openrac::StackFrame frame(*s_mod, 16);
    const gaddr at = frame.address();

    // Queue them while the game's queue has room.
    for (int i = 0; i < count && ref<int32_t>(queued) < k_reticle_slots; ++i) {
        put_float(at, points[i][0]);
        put_float(at + 4u, points[i][1]);
        put_float(at + 8u, points[i][2]);
        put_float(at + 12u, 1.0f);
        queue_reticle(at, rac1_strafe_reticle_size(distance(camera, points[i])));
    }
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
    s_backpedal = rac1_strafe_backpedal(
        kind, s_feel, in.state, in.facing, get_float(hero() + k_hero_move_yaw), s_backpedal
    );
    update_twist(kind);
    draw_reticles(kind, in);
    const gaddr moby = hero_moby();

    // Not backpedalling, or no moby to step: the game's own advance.
    if (!s_backpedal || moby == 0) {
        original();
        return;
    }

    const int seq = ref<uint8_t>(moby + k_moby_seq);

    // Not the walk or the run looping on itself: the game's own advance.
    if (!is_steppable_sequence(moby, seq)) {
        original();
        return;
    }

    const gaddr sequence = ref<gaddr>(
        ref<gaddr>(moby + k_moby_anim_set) + k_anim_set_sequences
        + k_word * static_cast<unsigned>(seq)
    );
    const int count = ref<uint8_t>(sequence + k_sequence_count);

    // A key outside the sequence would read records that are not there: the game's own advance.
    if (ref<uint8_t>(moby + k_moby_key) >= count || ref<uint8_t>(moby + k_moby_next_key) >= count) {
        original();
        return;
    }

    step_sequence_back(moby, sequence, count);
    original();
}

}  // namespace

// ---- Entry point ----

// Called by the entry point (mod.cpp) for rac1: checks the disc, reads the settings, finds the
// game's functions and takes the hook points. Throws openrac::Error to refuse to run.
void load(openrac::Mod& mod) {
    s_mod = &mod;
    s_disc = nullptr;

    // Find the table row for the running disc by its version.
    for (const auto& disc : k_lock_strafe_discs) {
        // A matching version names the disc whose addresses to use.
        if (mod.version() == disc.version) {
            s_disc = &disc;
        }
    }

    // A disc with no table row has no known addresses; refuse to run rather than guess.
    if (s_disc == nullptr) {
        throw openrac::Error(
            "lock-strafe knows the PAL and NTSC-U discs, not " + std::string(mod.version())
        );
    }

    s_mode = mod.setting<std::string>("mode") == "hold" ? RAC1_STRAFE_HOLD : RAC1_STRAFE_LOCK;
    s_feel = mod.setting<std::string>("feel") == "classic" ? RAC1_STRAFE_FEEL_CLASSIC
                                                           : RAC1_STRAFE_FEEL_COMMANDO;
    const std::string button = mod.setting<std::string>("button");
    s_strafe_buttons = rac1_strafe_button_bits(
        button == "l2"   ? RAC1_STRAFE_BUTTON_L2
        : button == "r2" ? RAC1_STRAFE_BUTTON_R2
                         : RAC1_STRAFE_BUTTON_BOTH
    );
    s_twist = mod.setting<bool>("twist");
    s_flips = mod.setting<bool>("flips");
    s_reticles = mod.setting<bool>("reticles");
    s_trace = mod.setting<bool>("trace") || std::getenv("OPENRAC_STRAFE_TRACE") != nullptr;
    s_buttons = 0;
    s_hold = rac1_strafe_hold{};
    s_backpedal = 0;
    s_tick = 0;
    s_reverse_tick = 0;
    s_reverse = rac1_strafe_reverse{};
    s_glide = rac1_strafe_glide{};
    s_pelvis = lock_strafe_spring{};
    s_spine = lock_strafe_spring{};
    s_twist_shown = false;

    s_fast_cos = game_function<float (*)(float)>(s_disc->fast_cos);
    s_fast_sin = game_function<float (*)(float)>(s_disc->fast_sin);
    s_camera_slide = game_function<void (*)(int32_t)>(s_disc->camera_slide);
    s_camera_spring = game_function<void (*)(float, float)>(s_disc->camera_spring);
    s_camera_pan_off = game_function<void (*)()>(s_disc->camera_pan_off);
    s_camera_look_along = game_function<void (*)(gaddr, float, float)>(s_disc->camera_look_along);
    s_enter_state = game_function<int32_t (*)(int32_t, int32_t)>(s_disc->enter_state);
    s_stick_direction = game_function<int32_t (*)()>(s_disc->stick_direction);
    s_reticle = mod.function<void (*)()>(s_disc->reticle);  // none: he strafes without reticles

    // A hook point this OpenRAC lacks throws: the load is refused with its name, and the port lets
    // go of the hooks already taken.
    mod.hook<hooks::Pad_ProcessReport>(
        [](auto original, uint32_t pad_state, uint32_t report, int32_t size) {
            pad_process_report(original, pad_state, report, size);
        }
    );
    mod.hook<hooks::Hero_UpdateMotionInput>([](auto original) { update_motion_input(original); });
    mod.hook<hooks::Hero_TurnToHeading>(
        [](auto original, int32_t m, float accel, float damping, float limit) {
            turn_to_heading(original, m, accel, damping, limit);
        }
    );
    mod.hook<hooks::Hero_SetPlanarVelocity>([](auto original, float angle) {
        set_planar_velocity(original, angle);
    });
    mod.hook<hooks::FollowCamera_ApplyOverrides>([](auto original, uint32_t camera) {
        camera_overrides(original, camera);
    });
    mod.hook<hooks::Hero_AdvanceAnimation>([](auto original) { advance_animation(original); });

    mod.info(
        std::string(s_mode == RAC1_STRAFE_HOLD ? "Hold " : "Lock with weapons, ")
        + (s_strafe_buttons == RAC1_PAD_L2   ? "L2"
           : s_strafe_buttons == RAC1_PAD_R2 ? "R2"
                                             : "L2 or R2")
        + ", "
        + (s_feel == RAC1_STRAFE_FEEL_COMMANDO ? "Going Commando's feel" : "the classic feel")
        + (s_twist ? ", hip twist" : "") + (s_flips ? ", strafe flips" : "") + " on the "
        + std::string(mod.version()) + " disc" + (s_trace ? ", tracing each pad report" : "")
    );
}

}  // namespace lock_strafe::rac1
