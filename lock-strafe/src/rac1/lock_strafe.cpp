// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Ratchet & Clank: the lock-strafe, a mod of OpenRAC's native port (README.md). Two modes, the
// setting "mode": Lock with weapons (lock) and Hold the strafe button (hold). strafe.hpp says how
// the game turns the stick into a heading, what each mode changes and why L2 and R2. The pad's
// report, the hero's motion input, two of his routines and one of the follow camera's are hook
// points (openrac/games/rac1.hpp): each replacement calls what it replaced, so whenever the hero
// is not strafing the game's routines run as they are, but for L2 and R2, which the game does not
// see while he is on foot. Without the mod the port runs the game's own.
//
// Written with the SDK's C++ layer (openrac/mod.hpp): the reference C++ mod. In OpenRAC 0.8 this
// was the port's own option (port/game/rac1/host/strafe.c in OpenRAC), and the first version of
// this mod was that file in C on the interface's table; the behaviour is the same.
//
// How the file is laid out: the constants (the offsets of the hero, the pad, the camera and the
// hero's animation in game memory), the Strafe class that holds the mod's state and reads the
// game, the six hook replacements it runs, and last the load function that checks the disc, finds
// the game's functions and takes the hook points. The decision of when and how he strafes is in
// strafe.hpp, away from game memory; the addresses of each disc are in discs.hpp.
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
//          what the strafe does. The environment variable OPENRAC_STRAFE_TRACE=1 switches it on
//          too.

#include "lock_strafe.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "../common/spring.hpp"
#include "discs.hpp"
#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"
#include "strafe.hpp"

namespace lock_strafe::rac1 {
namespace {

using openrac::gaddr;
namespace hooks = openrac::rac1;

// ---- Constants ----

// The offsets below are those of the game's Hero, PadState and moby structures, the same on the PAL
// and NTSC-U discs; the addresses that differ by disc are in discs.hpp. Each carries its evidence.

// The level whose program holds the game mode and the camera's words: level 0 (discs.hpp).
constexpr int kMainLevel = 0;

// The game mode in which the player plays: not a scene, a menu or a vendor (discs.hpp: 0 in play).
constexpr std::int32_t kModePlaying = 0;

// In the hero: the pointer to his moby (assumed).
constexpr std::uint32_t kHeroMoby = 0x2080u;

// In the hero: his state code (seen in game code).
constexpr std::uint32_t kHeroState = 0x2084u;

// In the hero: 0 on level ground; other values turn the hero's own frame (seen in game code).
constexpr std::uint32_t kHeroFrameMode = 0x20B3u;

// In the hero: his control mode, 0x16 on the hoverboard (seen in game code).
constexpr std::uint32_t kHeroControlMode = 0x208Cu;

// In the hero: 0 on foot, else the vehicle he is in (seen in game code).
constexpr std::uint32_t kHeroVehicleMode = 0x20A4u;

// In the hero: item slot 0, and in the slot its state and its item id (seen in game code).
constexpr std::uint32_t kHeroSlot0 = 0x1090u;
constexpr std::uint32_t kSlotState = 0x24u;
constexpr std::uint32_t kSlotItem = 0x28u;

// In the hero: his facing (rot.z) (seen in game code).
constexpr std::uint32_t kHeroFacing = 0x98u;

// In the hero: the move yaw, where the stick points on the screen (seen in game code).
constexpr std::uint32_t kHeroMoveYaw = 0x180u;

// In the hero: how far the stick is pushed, 0 to 1 (seen in game code: stick_magnitude).
constexpr std::uint32_t kHeroStick = 0x229Cu;

// In the hero: the speed his velocity is built at (seen in game code: ground_speed, read by
// Hero_SetPlanarVelocity).
constexpr std::uint32_t kHeroGroundSpeed = 0x194u;

// In the hero: the flip's drift speed along its yaw (seen in game code: high_jump_drift_speed).
constexpr std::uint32_t kHeroFlipDrift = 0x458u;

// In the hero: the speed the flip carries along the facing it took off with (seen in game code:
// air_target_speed, set from his speed when state 0xB is entered).
constexpr std::uint32_t kHeroFlipCarry = 0x454u;

// In the hero: the speed the stick asks for, 0 with the stick centred (seen in game code:
// target_speed).
constexpr std::uint32_t kHeroTargetSpeed = 0x190u;

// In the hero: his position, x and y (assumed).
constexpr std::uint32_t kHeroPosX = 0x80u;
constexpr std::uint32_t kHeroPosY = 0x84u;

// In the hero: his animation speed, and the rate of the key he plays (seen in game code).
constexpr std::uint32_t kHeroAnimSpeed = 0xA90u;
constexpr std::uint32_t kHeroAnimRate = 0xA94u;

// In the hero: two words of the animation that must read -1 and 0 for a sequence looping on itself:
// no blend from another, no sub-range or restart pending (assumed).
constexpr std::uint32_t kHeroAnimBlendFrom = 0xAB0u;
constexpr std::uint32_t kHeroAnimPending = 0xAB8u;

// In the pad: the held buttons, and the right stick sideways (PadState.analog[0]) (seen in game
// code).
constexpr std::uint32_t kPadHeld = 0x1A0u;
constexpr std::uint32_t kPadPressed = 0x1A4u;
constexpr std::uint32_t kPadLookX = 0x100u;

// The PadState words with button bits (pad.h: held, pressed, released, raw_held, raw_pressed,
// raw_released and the unmasked three) (seen in game code).
constexpr std::uint32_t kPadWords[] =
    {0x1A0u, 0x1A4u, 0x1A8u, 0x1B0u, 0x1B4u, 0x1B8u, 0x1C0u, 0x1C4u, 0x1C8u};

// In the pad: the index of the next entry of the button history, a ring of kPadHistoryLength
// words starting at kPadHistory (assumed).
constexpr std::uint32_t kPadHistoryIndex = 0x18Eu;
constexpr std::uint32_t kPadHistory = 0x1E0u;
constexpr int kPadHistoryLength = 30;

// The size in bytes of one word: the step of the history and of the sequence tables (assumed).
constexpr std::uint32_t kWord = 4u;

// In the hero's moby: key A and key B (the two keys being blended), the two sequence numbers (the
// one blended from, then the current one), and the blend from A to B (seen in game code).
constexpr std::uint32_t kMobyKey = 0x50u;
constexpr std::uint32_t kMobyNextKey = 0x51u;
constexpr std::uint32_t kMobySeqFrom = 0x52u;
constexpr std::uint32_t kMobySeq = 0x53u;
constexpr std::uint32_t kMobyFraction = 0x54u;

// In the hero's moby: the pointer to its animation set, whose +0x48 is the table of sequence
// pointers, one word each (assumed).
constexpr std::uint32_t kMobyAnimSet = 0x24u;
constexpr std::uint32_t kAnimSetSequences = 0x48u;

// In the hero's moby: the pointers to the records of keys A and B, which the advance reads their
// rates from (assumed).
constexpr std::uint32_t kMobyRateA = 0x68u;
constexpr std::uint32_t kMobyRateB = 0x6Cu;

// In a sequence: the count of its keys, and the table of pointers to each key's record, whose first
// word is the key's rate (assumed).
constexpr std::uint32_t kSequenceCount = 0x10u;
constexpr std::uint32_t kSequenceKeys = 0x1Cu;

// The most keys a sequence can hold: its count is one byte (assumed).
constexpr int kMaxSequenceKeys = 256;

// In the follow camera: the pointer to its data, and in the data the three pan timers the game's
// panning part sets (assumed).
constexpr std::uint32_t kCameraData = 0x70u;
constexpr std::uint32_t kCameraTimer0 = 0x12u;
constexpr std::uint32_t kCameraTimer1 = 0x14u;
constexpr std::uint32_t kCameraTimer2 = 0x16u;

// In a camera slot: its id; 0 is the camera behind the hero, the follow camera (seen in game code:
// Camera.id, FindCameraById(0) in CameraResetBehindHero).
constexpr std::uint32_t kCameraId = 0x86u;
constexpr std::int16_t kCameraBehindHero = 0;

// The strafe camera's lateral spring, as the game's strafe state asks for it (seen in game code).
constexpr float kCameraSpringA = 0.04f;
constexpr float kCameraSpringB = 0.2f;

// How fast the camera turns round to look along a heading: 12 degrees (assumed).
constexpr float kCameraLookRate = 0.20943952f;

// The standing turn's rates: the acceleration and the damping are these times the tick's length,
// the limit is this times the turn's step (assumed: they stand in for state 0x81's turn).
constexpr float kStandingAccel = 0.017f;
constexpr float kStandingDamping = 0.3f;
constexpr float kStandingLimit = 4.712389f;

// HeroEnterMoveState's second argument: start the state's animation (seen in game code).
constexpr std::int32_t kEnterAnimate = 1;

// Hero_TurnToHeading's first argument for the turns that follow the move yaw (strafe.hpp).
constexpr std::int32_t kTurnFollowsMoveYaw = 0;

// The size of the trace's line in bytes (assumed).
constexpr std::size_t kTraceLineSize = 512;

// ---- Types ----

// What the player chose in the mod's settings, read once at load.
struct Settings {
    Mode mode = Mode::Lock;                         // Setting "mode": Hold or Lock.
    Feel feel = Feel::Commando;                     // Setting "feel": Going Commando's or classic.
    std::uint32_t strafe_buttons = kStrafeButtons;  // The pad bits that strafe (setting "button").
    bool twist = false;                             // Setting "twist": the hip twist.
    bool flips = false;                             // Setting "flips": the strafe flips.
    bool trace = false;                             // Setting "trace": a line each pad report.
};

// The game's functions the mod calls (discs.hpp), found once at load.
struct GameFunctions {
    float (*fast_cos)(float) = nullptr;
    float (*fast_sin)(float) = nullptr;
    void (*camera_slide)(std::int32_t) = nullptr;
    void (*camera_spring)(float, float) = nullptr;
    void (*camera_pan_off)() = nullptr;
    void (*camera_look_along)(gaddr, float, float) = nullptr;
    std::int32_t (*enter_state)(std::int32_t, std::int32_t) = nullptr;
    std::int32_t (*stick_direction)() = nullptr;
};

// Returns a float as the double the trace's format takes.
double to_double(float v) {
    return static_cast<double>(v);
}

// Returns the game's function `name` on the running disc, or refuses the load naming it.
template <class Fn>
Fn game_function(openrac::Mod& mod, const char* name) {
    Fn fn = mod.function<Fn>(name);

    // The host has no such function on this disc: the mod cannot run without it.
    if (fn == nullptr) {
        throw openrac::Error(
            std::string("this OpenRAC has no game function ") + name + " on the "
            + std::string(mod.version()) + " disc"
        );
    }

    return fn;
}

// Finds the game's functions `disc` names, or refuses the load.
GameFunctions find_functions(openrac::Mod& mod, const Disc& disc) {
    GameFunctions functions;

    functions.fast_cos = game_function<float (*)(float)>(mod, disc.fast_cos);
    functions.fast_sin = game_function<float (*)(float)>(mod, disc.fast_sin);
    functions.camera_slide = game_function<void (*)(std::int32_t)>(mod, disc.camera_slide);
    functions.camera_spring = game_function<void (*)(float, float)>(mod, disc.camera_spring);
    functions.camera_pan_off = game_function<void (*)()>(mod, disc.camera_pan_off);
    functions.camera_look_along =
        game_function<void (*)(gaddr, float, float)>(mod, disc.camera_look_along);
    functions.enter_state =
        game_function<std::int32_t (*)(std::int32_t, std::int32_t)>(mod, disc.enter_state);
    functions.stick_direction = game_function<std::int32_t (*)()>(mod, disc.stick_direction);

    return functions;
}

// Returns the table row for the disc `mod` runs on, or null when the mod has none.
const Disc* find_disc(const openrac::Mod& mod) {
    // The first row whose version names the running disc holds its addresses.
    for (const Disc& disc : kDiscs) {
        // A matching version names the disc whose addresses to use.
        if (mod.version() == disc.version) {
            return &disc;
        }
    }

    return nullptr;
}

// Reads the mod's settings, and the environment variable that switches the trace on too.
Settings read_settings(const openrac::Mod& mod) {
    Settings settings;
    const std::string button = mod.setting<std::string>("button");

    settings.mode = mod.setting<std::string>("mode") == "hold" ? Mode::Hold : Mode::Lock;
    settings.feel = mod.setting<std::string>("feel") == "classic" ? Feel::Classic : Feel::Commando;
    settings.strafe_buttons = button_bits(
        button == "l2"   ? Button::L2
        : button == "r2" ? Button::R2
                         : Button::Both
    );
    settings.twist = mod.setting<bool>("twist");
    settings.flips = mod.setting<bool>("flips");
    settings.trace = mod.setting<bool>("trace") || std::getenv("OPENRAC_STRAFE_TRACE") != nullptr;

    return settings;
}

// The strafe: the mod's state, and the code that reads the hero, the pad and the camera out of
// game memory around the decision of strafe.hpp. The hook replacements are its public functions.
//
// One object lives from load on (g_strafe). The hooks all run on the game's thread, the one that
// runs the hero's update; nothing here is touched by another thread.
class Strafe {
public:
    // Makes the strafe for `mod` on `disc`, with the settings and game functions it found.
    Strafe(
        openrac::Mod& mod,
        const Disc& disc,
        const Settings& settings,
        const GameFunctions& functions
    )
        : m_mod(mod),
          m_disc(disc),
          m_settings(settings),
          m_functions(functions) {}

    // Pad_ProcessReport: one report into the pad's state, each poll, before the tick reads it. L2
    // and R2 are kept here as the player holds them; in play, on foot, they are then taken out of
    // the pad's button words (and this poll's history entry), the way the game's own input locks
    // (PadState.mode) take buttons out, so that the game does not look or crouch on them
    // (strafe.hpp). Hold's memory is updated here, once a tick.
    template <class Original>
    void pad_process_report(Original original, gaddr pad_state, gaddr report, std::int32_t size);

    // Hero_UpdateMotionInput: the stick read into the hero, early in his update each tick.
    // Standing, Lock and Hold after its swing turn him toward the camera first, where the game's
    // standing state does not turn him: state 0x81's turn toward the camera with L2 or R2 held.
    template <class Original>
    void update_motion_input(Original original);

    // Hero_TurnToHeading: eases the facing toward the move yaw (+0x180). While strafing, toward
    // the camera's yaw, or while Hold's swing lasts the facing it latched.
    template <class Original>
    void turn_to_heading(
        Original original, std::int32_t mode, float accel, float damping, float limit
    );

    // The follow camera's per-tick requests from the hero's state (the camera moby's update calls
    // it before it places the eye). In state 0x81 with L2 or R2 held, the game's strafe, it asks
    // for its strafe camera and returns before its own panning and shake: the camera slides along
    // with the hero (+0x116), held behind him by its lateral spring at 0.04 / 0.2, and the pan
    // timers are cleared. While strafing the same is asked for: the game's routine runs, then what
    // its panning part changed is put as that branch leaves it (the pan weight as it was, the
    // timers 0, the spring and the slide asked for). Hold's swing turns the camera round behind
    // the latched facing, as the game's own requests to look along a heading do (a turn standing
    // in for the right stick's, which the stick overrides when moved and the camera's reset puts
    // back to 0 at the end of each camera update). It is asked for only while the swing lasts
    // (strafe.hpp): after it nothing turns the camera back toward that facing, and the right
    // stick's turn stays where the player leaves it.
    template <class Original>
    void camera_overrides(Original original, gaddr camera);

    // Hero_SetPlanarVelocity: the velocity at the ground speed along an angle, the facing for one
    // above pi. While strafing, along the move yaw instead: where the stick points, as the game's
    // own run start from a standstill asks for; and as a strafe ends, along the stick until he
    // has turned to it or stopped (strafe.hpp, the glide).
    template <class Original>
    void set_planar_velocity(Original original, float angle);

    // Ratchet's animation advance (once a tick after his state's update): the key and blend of
    // his moby moved on by his animation speed (+0xA90) times the key's rate (+0xA94). While he
    // strafes away from his facing in the walk or the run (strafe.hpp, the backpedal), the
    // sequence is stepped back here by what the advance would add, and the blend left that much
    // lower so that the advance, adding it, ends where the step back did: the rest of what it
    // does (the sounds, the items that follow his speed, the distance moved) is the game's. Only
    // a sequence looping on itself (keys A and B in it, no blend from another, no sub-range or
    // restart pending).
    template <class Original>
    void advance_animation(Original original);

private:
    // Returns a reference to a T at a game address.
    template <class T>
    T& ref(gaddr address) const {
        return m_mod.memory().ref<T>(address);
    }

    // Returns the float at a game address.
    float get_float(gaddr at) const;

    // Writes a float at a game address.
    void put_float(gaddr at, float v) const;

    // Returns the hero's address on the running disc.
    gaddr hero() const;

    // Returns the first pad's state address on the running disc.
    gaddr pad() const;

    // Returns the address of a word in level 0's program as loaded now (the program moves with the
    // level).
    gaddr level0(std::uint32_t address) const;

    // Returns the camera's yaw now.
    float camera_yaw() const;

    // Returns the game's g_frame_scale_sq now (the turns' scale).
    float frame_scale_sq() const;

    // Returns the game's g_frame_time now (the turns' limit's scale).
    float frame_time() const;

    // Returns whether the camera in use is one of the level's own, not the one behind him (camera
    // id 0, FindCameraById(0): the follow camera). False when no camera is set up yet.
    bool has_fixed_camera() const;

    // Returns the hero's moby, 0 when he has none yet.
    gaddr hero_moby() const;

    // Returns the decision's input (strafe.hpp), read from the hero, the pad and the camera.
    HeroInput read_input() const;

    // Returns what the strafe does now (strafe.hpp).
    Kind current_kind() const;

    // Returns a byte of the hero moby's animation fields (the trace's); -1 when he has no moby.
    int anim_byte(std::uint32_t at) const;

    // Returns a float of the hero moby's animation fields (the trace's); 0 when he has no moby.
    float anim_float(std::uint32_t at) const;

    // Logs a line for this pad report when the setting "trace" (or OPENRAC_STRAFE_TRACE=1) is on.
    void trace() const;

    // Runs Hero_TurnToHeading under the camera's yaw or Hold's latched one, as state 0x81 turns
    // him while L2 or R2 is held: that yaw in the move yaw for the call, the stick's put back
    // after it.
    void turn_toward(float yaw, std::int32_t mode, float accel, float damping, float limit) const;

    // Returns whether the hero's animation is a sequence looping on itself that the backpedal may
    // step back: the walk or the run, keys A and B in it, no blend from another and no sub-range
    // or restart pending.
    bool is_steppable_sequence(gaddr moby, int seq) const;

    // Steps the hero's looping sequence back by what the animation advance would add: his key and
    // blend move to the interval before, and the blend is left that much lower so that the
    // advance, adding it, ends where the step back did. `sequence` is the sequence's address,
    // `count` its keys.
    void step_sequence_back(gaddr moby, gaddr sequence, int count) const;

    // Sends the twist's two joints to the hero: the pelvis and the spine turned about z (a yaw:
    // his skeleton's joint frames stand as his model's, z up) by their springs' angles, as the
    // game's own quaternion for that turn (strafe.hpp, joint_yaw: the sine negated). Returns false
    // on a port that does not take a mod's joints (one that takes them only from the hero's
    // update hook's holder): he then runs without the twist.
    bool send_twist() const;

    // Takes the twist off the hero and puts its springs at rest.
    void clear_twist();

    // Steps one tick of the hip twist (DESIGN.md): the pelvis eases toward the move yaw from his
    // facing and the spine toward minus it, on Going Commando's springs; at rest at 0, the joints
    // are taken off.
    void update_twist(Kind kind);

    // Tries the strafe flip (DESIGN.md): X pressed this tick while strafing, standing or running,
    // with the stick pushed fully and not forward enters rac1's own side or back flip (state
    // 0xB), which picks its animation from the stick (0x1C left, 0x1D right, 0x1F back) as
    // rac2's does; its drift then starts at Going Commando's 5 units a second along the strafe.
    void try_flip(const HeroInput& in, Kind kind) const;

    // The host's table and this mod's handle. Not owned; the mod lives until the program ends.
    openrac::Mod& m_mod;

    // The running disc's addresses. Not owned; a row of kDiscs.
    const Disc& m_disc;

    // The player's settings, from load.
    Settings m_settings;

    // The game's functions the mod calls, from load.
    GameFunctions m_functions;

    // The strafe's buttons (L2, R2 or both) as the player holds them, from the last pad report
    // (set by the pad hook, read by the decision's input). A button the setting leaves to the
    // game is not here.
    std::uint32_t m_buttons = 0;

    // What Hold remembers from tick to tick (strafe.hpp); updated once a tick at the pad's report.
    HoldState m_hold{};

    // Whether the walk or the run is played backwards (strafe.hpp); updated each tick by the
    // animation hook.
    bool m_backpedal = false;

    // The pad reports so far (one a tick).
    unsigned m_tick = 0;

    // The last pad report the side reversal stepped at.
    unsigned m_reverse_tick = 0;

    // The side reversal's timers (strafe.hpp).
    ReverseTimers m_reverse{};

    // Where his speed goes as a strafe ends: the stick's last yaw, and whether he is leaving one
    // (strafe.hpp).
    GlideState m_glide{};

    // The hip twist's springs: the pelvis's and the spine's.
    Spring m_pelvis{};
    Spring m_spine{};

    // Whether the hero has the twist's joints (sent, and not yet taken off).
    bool m_twist_shown = false;
};

// The one strafe, made by load. The hook replacements are lambdas that carry no state, so they
// reach it through this pointer; load sets it before it takes the first hook, and only the game's
// thread uses it after that.
std::unique_ptr<Strafe> g_strafe;

float Strafe::get_float(gaddr at) const {
    return m_mod.memory().read<float>(at);
}

void Strafe::put_float(gaddr at, float v) const {
    m_mod.memory().write<float>(at, v);
}

gaddr Strafe::hero() const {
    return m_disc.hero;
}

gaddr Strafe::pad() const {
    return m_disc.pad;
}

gaddr Strafe::level0(std::uint32_t address) const {
    return m_mod.relocate_level(kMainLevel, address);
}

float Strafe::camera_yaw() const {
    return get_float(level0(m_disc.camera_yaw));
}

float Strafe::frame_scale_sq() const {
    return get_float(m_mod.relocate(m_disc.frame_scale));
}

float Strafe::frame_time() const {
    return get_float(m_mod.relocate(m_disc.turn_step));
}

bool Strafe::has_fixed_camera() const {
    const gaddr camera = ref<gaddr>(level0(m_disc.camera_now));

    return camera != 0 && ref<std::int16_t>(camera + kCameraId) != kCameraBehindHero;
}

gaddr Strafe::hero_moby() const {
    return ref<gaddr>(hero() + kHeroMoby);
}

HeroInput Strafe::read_input() const {
    HeroInput in{};

    in.playing =
        m_mod.loaded_level() >= 0 && ref<std::int32_t>(level0(m_disc.level_mode)) == kModePlaying;
    in.state = ref<std::int32_t>(hero() + kHeroState);
    in.frame_mode = ref<std::uint8_t>(hero() + kHeroFrameMode);
    in.vehicle_mode = ref<std::uint8_t>(hero() + kHeroVehicleMode);
    in.control_mode = ref<std::int32_t>(hero() + kHeroControlMode);
    in.slot_state = ref<std::int32_t>(hero() + kHeroSlot0 + kSlotState);
    in.slot_item = ref<std::int32_t>(hero() + kHeroSlot0 + kSlotItem);
    in.held = (ref<std::uint32_t>(pad() + kPadHeld) & ~kStrafeButtons) | m_buttons;
    in.facing = get_float(hero() + kHeroFacing);
    in.camera = camera_yaw();
    in.look_x = get_float(pad() + kPadLookX);
    in.fixed_camera = has_fixed_camera();

    return in;
}

Kind Strafe::current_kind() const {
    return strafe_kind(m_settings.mode, read_input(), m_hold);
}

int Strafe::anim_byte(std::uint32_t at) const {
    const gaddr moby = hero_moby();

    return moby != 0 ? ref<std::uint8_t>(moby + at) : -1;
}

float Strafe::anim_float(std::uint32_t at) const {
    const gaddr moby = hero_moby();

    return moby != 0 ? get_float(moby + at) : 0.0f;
}

void Strafe::trace() const {
    // Without the setting there is nothing to log.
    if (!m_settings.trace) {
        return;
    }

    const HeroInput in = read_input();

    // The line's fields are many and mixed, which only a format string reads well; C++17 has no
    // std::format, so snprintf fills a fixed buffer for the SDK's log.
    char line[kTraceLineSize];

    std::snprintf(
        line,
        sizeof line,
        "mode %d kind %d playing %d state %#x vehicle %d item %#x/%d held %#x seen %#x latched "
        "%d/%d/%.3f "
        "look %.2f facing %.3f move %.3f camera %.3f pos %.3f %.3f back %d anim %d/%d %d-%d %.3f "
        "speed %.3f feel %d twist %.3f/%.3f side %d/%d fixed %d",
        static_cast<int>(m_settings.mode),
        static_cast<int>(strafe_kind(m_settings.mode, in, m_hold)),
        in.playing,
        static_cast<unsigned>(in.state),
        in.vehicle_mode,
        static_cast<unsigned>(in.slot_item),
        in.slot_state,
        static_cast<unsigned>(in.held),
        ref<std::uint32_t>(pad() + kPadHeld),
        m_hold.latched,
        m_hold.swinging,
        to_double(m_hold.yaw),
        to_double(in.look_x),
        to_double(in.facing),
        to_double(get_float(hero() + kHeroMoveYaw)),
        to_double(camera_yaw()),
        to_double(get_float(hero() + kHeroPosX)),
        to_double(get_float(hero() + kHeroPosY)),
        m_backpedal,
        anim_byte(kMobySeqFrom),
        anim_byte(kMobySeq),
        anim_byte(kMobyKey),
        anim_byte(kMobyNextKey),
        to_double(anim_float(kMobyFraction)),
        to_double(get_float(hero() + kHeroAnimSpeed)),
        static_cast<int>(m_settings.feel),
        to_double(m_pelvis.angle),
        to_double(m_spine.angle),
        m_reverse.left,
        m_reverse.right,
        in.fixed_camera
    );
    m_mod.info(line);
}

void Strafe::turn_toward(
    float yaw, std::int32_t mode, float accel, float damping, float limit
) const {
    const gaddr move_yaw = hero() + kHeroMoveYaw;
    const float stick = get_float(move_yaw);

    put_float(move_yaw, yaw);
    m_mod.original<hooks::Hero_TurnToHeading>()(mode, accel, damping, limit);
    put_float(move_yaw, stick);
}

bool Strafe::is_steppable_sequence(gaddr moby, int seq) const {
    return ref<std::uint8_t>(moby + kMobySeqFrom) == seq && (seq == kSeqWalk || seq == kSeqRun)
           && ref<std::int32_t>(hero() + kHeroAnimBlendFrom) == -1
           && ref<std::int32_t>(hero() + kHeroAnimPending) == 0;
}

void Strafe::step_sequence_back(gaddr moby, gaddr sequence, int count) const {
    float rates[kMaxSequenceKeys];

    // Each key's rate is the first word of its record.
    for (int n = 0; n < count; ++n) {
        rates[n] =
            get_float(ref<gaddr>(sequence + kSequenceKeys + kWord * static_cast<std::uint32_t>(n)));
    }

    const float step = get_float(hero() + kHeroAnimSpeed) * get_float(hero() + kHeroAnimRate);
    AnimationKeys keys = {
        ref<std::uint8_t>(moby + kMobyKey),
        ref<std::uint8_t>(moby + kMobyNextKey),
        get_float(moby + kMobyFraction),
        get_float(hero() + kHeroAnimRate),
    };

    step_animation_back(keys, step, count, rates);
    ref<std::uint8_t>(moby + kMobyKey) = static_cast<std::uint8_t>(keys.key);
    ref<std::uint8_t>(moby + kMobyNextKey) = static_cast<std::uint8_t>(keys.next_key);
    ref<gaddr>(moby + kMobyRateA) =
        ref<gaddr>(sequence + kSequenceKeys + kWord * static_cast<std::uint32_t>(keys.key));
    ref<gaddr>(moby + kMobyRateB) =
        ref<gaddr>(sequence + kSequenceKeys + kWord * static_cast<std::uint32_t>(keys.next_key));
    put_float(hero() + kHeroAnimRate, keys.rate);

    // The advance adds speed times the rate it now reads: the step of the interval stepped into.
    put_float(moby + kMobyFraction, keys.fraction - get_float(hero() + kHeroAnimSpeed) * keys.rate);
}

bool Strafe::send_twist() const {
    const auto joint = [](std::uint32_t index, float angle) {
        openrac_joint_offset j{};

        j.joint = index;
        joint_yaw(angle, j.rotation);

        return j;
    };

    // A port without the call at all (its table has no entry): nothing to send to.
    if (!m_mod.has_api_12() || m_mod.api().hero_set_joints == nullptr) {
        return false;
    }

    return m_mod.hero_set_joints(
        {joint(kJointPelvis, m_pelvis.angle), joint(kJointSpine, m_spine.angle)}
    );
}

void Strafe::clear_twist() {
    m_pelvis = Spring{};
    m_spine = Spring{};

    // Only a twist that was sent has anything to take off.
    if (m_twist_shown) {
        m_twist_shown = false;

        // A port with the call: an empty list takes the joints off.
        if (m_mod.has_api_12() && m_mod.api().hero_set_joints != nullptr) {
            m_mod.hero_set_joints({});
        }
    }
}

void Strafe::update_twist(Kind kind) {
    // The setting off: never a twist.
    if (!m_settings.twist) {
        return;
    }

    const float target = twist_target(
        kind,
        get_float(hero() + kHeroStick),
        m_backpedal,
        get_float(hero() + kHeroFacing),
        get_float(hero() + kHeroMoveYaw)
    );
    const float scale = frame_scale_sq();

    spring_step(m_pelvis, target, scale * kTwistPelvisAccel, scale * kTwistDamping);
    spring_step(m_spine, -target, scale * kTwistSpineAccel, scale * kTwistDamping);

    // Back at rest with nothing to aim at: take the joints off once.
    if (target == 0.0f && spring_is_still(m_pelvis) && spring_is_still(m_spine)) {
        clear_twist();

        return;
    }

    m_twist_shown = send_twist() || m_twist_shown;
}

void Strafe::try_flip(const HeroInput& in, Kind kind) const {
    // The setting off, or not X with the stick pushed fully while strafing on the ground.
    if (!m_settings.flips
        || !starts_flip(
            kind, in.state, ref<std::uint32_t>(pad() + kPadPressed), get_float(hero() + kHeroStick)
        )) {
        return;
    }

    // Forward is the plain jump, the game's.
    if (m_functions.stick_direction() == kDirectionForward) {
        return;
    }

    // The game may refuse the state (a lock on his moves): then its own jump goes on.
    if (m_functions.enter_state(kStateFlip, kEnterAnimate) != 0) {
        put_float(hero() + kHeroFlipDrift, frame_time() * kFlipDrift);

        // State 0xB adds his run speed times the sine of the drift's angle from his facing along
        // that facing: from a crouch it is 0, from a sideways strafe all of it, forward. Going
        // Commando's flip goes only along the strafe, so none is carried.
        put_float(hero() + kHeroFlipCarry, 0.0f);
    }
}

// ---- Hook replacements (the game's thread) ----

template <class Original>
void Strafe::pad_process_report(
    Original original, gaddr pad_state, gaddr report, std::int32_t size
) {
    original(pad_state, report, size);

    // A report for another pad is not the first pad's: leave it alone.
    if (pad_state != pad()) {
        return;
    }

    ++m_tick;
    m_buttons = ref<std::uint32_t>(pad() + kPadHeld) & m_settings.strafe_buttons;

    const HeroInput in = read_input();

    update_hold(m_settings.mode, m_settings.feel, in, m_hold);

    // Off foot or out of play the animation hook does not run: the twist goes now.
    if (m_twist_shown && (!in.playing || in.vehicle_mode != 0)) {
        clear_twist();
    }

    // Out of the strafe and the run, or out of play on foot: the glide is forgotten, so the next
    // run is the game's (and a scene or a vehicle never slides along an old stick).
    if (glide_ends(
            strafe_kind(m_settings.mode, in, m_hold), in.state, in.playing && in.vehicle_mode == 0
        )) {
        m_glide = GlideState{};
    }

    // In play on foot with the setting on, the game must not see the strafe's buttons; a button
    // the setting "button" leaves out stays the game's (and other mods').
    if (takes_buttons(m_settings.mode, in)) {
        // Every button word of the pad has the strafe's bits cleared.
        for (const std::uint32_t word : kPadWords) {
            ref<std::uint32_t>(pad() + word) &= ~m_settings.strafe_buttons;
        }

        // The entry this poll wrote is hist_btn[hist_idx - 1], in a ring of kPadHistoryLength.
        const int last = (ref<std::int16_t>(pad() + kPadHistoryIndex) + (kPadHistoryLength - 1))
                         % kPadHistoryLength;

        ref<std::uint32_t>(pad() + kPadHistory + kWord * static_cast<std::uint32_t>(last)) &=
            ~m_settings.strafe_buttons;
    }

    trace();
}

template <class Original>
void Strafe::update_motion_input(Original original) {
    const HeroInput in = read_input();
    const Kind kind = strafe_kind(m_settings.mode, in, m_hold);

    // Standing in a strafe that turns him: turn him toward the camera before the game's update, at
    // Going Commando's rates or the classic ones.
    if (turns_standing(kind, in.state, m_hold)) {
        float accel = frame_scale_sq() * kStandingAccel;
        float damping = frame_scale_sq() * kStandingDamping;
        float limit = frame_time() * kStandingLimit;

        // Going Commando's feel turns him standing as it does walking.
        if (m_settings.feel == Feel::Commando) {
            commando_rates(in.state, frame_scale_sq(), frame_time(), accel, damping, limit);
        }

        turn_toward(camera_yaw(), kTurnFollowsMoveYaw, accel, damping, limit);
    }

    original();

    // The stick is read: X may flip him (before his state's code sees it).
    try_flip(in, kind);
}

template <class Original>
void Strafe::turn_to_heading(
    Original original, std::int32_t mode, float accel, float damping, float limit
) {
    const Kind kind = mode == kTurnFollowsMoveYaw ? current_kind() : Kind::None;

    // Not strafing, or a turn that does not follow the move yaw: the game's own.
    if (kind == Kind::None) {
        original(mode, accel, damping, limit);

        return;
    }

    // Going Commando's feel turns him at its own strafe rates.
    if (m_settings.feel == Feel::Commando) {
        commando_rates(
            ref<std::int32_t>(hero() + kHeroState),
            frame_scale_sq(),
            frame_time(),
            accel,
            damping,
            limit
        );
    }

    turn_toward(
        turn_target(kind, mode, get_float(hero() + kHeroMoveYaw), camera_yaw(), m_hold),
        mode,
        accel,
        damping,
        limit
    );
}

template <class Original>
void Strafe::camera_overrides(Original original, gaddr camera) {
    const Kind kind = current_kind();
    const gaddr data = ref<gaddr>(camera + kCameraData);

    // Not strafing: the game's own camera requests.
    if (kind == Kind::None) {
        original(camera);

        return;
    }

    const gaddr pan = level0(m_disc.camera_pan);
    const float pan_before = get_float(pan);

    original(camera);
    put_float(pan, pan_before);
    m_functions.camera_slide(0);
    m_functions.camera_pan_off();
    m_functions.camera_spring(kCameraSpringA, kCameraSpringB);
    ref<std::int16_t>(data + kCameraTimer0) = 0;
    ref<std::int16_t>(data + kCameraTimer1) = 0;
    ref<std::int16_t>(data + kCameraTimer2) = 0;

    // Hold's swing is on: ask the camera to turn round behind the latched facing.
    if (swings_camera(kind, m_hold)) {
        // The heading as the game's camera volumes give theirs: (FastCos, FastSin, 0), on the
        // game's stack, where the game reads it.
        openrac::StackFrame frame(m_mod, 16);
        const gaddr dir = frame.address();

        put_float(dir, m_functions.fast_cos(m_hold.yaw));
        put_float(dir + 4u, m_functions.fast_sin(m_hold.yaw));
        put_float(dir + 8u, 0.0f);
        put_float(dir + 12u, 0.0f);
        m_functions.camera_look_along(dir, kCameraLookRate, 0.0f);
    }
}

template <class Original>
void Strafe::set_planar_velocity(Original original, float angle) {
    // An angle above pi is the game's request for the facing; while strafing it becomes the move
    // yaw.
    if (!(angle <= kPi)) {
        const Kind kind = current_kind();
        const std::int32_t state = ref<std::int32_t>(hero() + kHeroState);
        const bool running = state == kStateRun || state == kStateRunAlt;

        // Along the stick while strafing, and as a strafe ends while he turns or slows
        // (strafe.hpp).
        angle = glide_angle(
            m_glide,
            kind,
            running,
            get_float(hero() + kHeroTargetSpeed),
            get_float(hero() + kHeroFacing),
            get_float(hero() + kHeroMoveYaw),
            angle
        );

        // Going Commando's side reversal, once a tick in the run: going from one side to the other
        // kills the speed he had (strafe.hpp).
        if (kind != Kind::None && m_settings.feel == Feel::Commando && running
            && m_reverse_tick != m_tick) {
            m_reverse_tick = m_tick;

            const float f = reverse_step(
                m_reverse,
                side_of(get_float(hero() + kHeroFacing), get_float(hero() + kHeroMoveYaw))
            );

            ref<float>(hero() + kHeroGroundSpeed) *= f;
        }
    }

    original(angle);
}

template <class Original>
void Strafe::advance_animation(Original original) {
    const HeroInput in = read_input();
    const Kind kind = strafe_kind(m_settings.mode, in, m_hold);

    m_backpedal = backpedals(
        kind, m_settings.feel, in.state, in.facing, get_float(hero() + kHeroMoveYaw), m_backpedal
    );
    update_twist(kind);

    const gaddr moby = hero_moby();

    // Not backpedalling, or no moby to step: the game's own advance.
    if (!m_backpedal || moby == 0) {
        original();

        return;
    }

    const int seq = ref<std::uint8_t>(moby + kMobySeq);

    // Not the walk or the run looping on itself: the game's own advance.
    if (!is_steppable_sequence(moby, seq)) {
        original();

        return;
    }

    const gaddr sequence = ref<gaddr>(
        ref<gaddr>(moby + kMobyAnimSet) + kAnimSetSequences
        + kWord * static_cast<std::uint32_t>(seq)
    );
    const int count = ref<std::uint8_t>(sequence + kSequenceCount);

    // A key outside the sequence would read records that are not there: the game's own advance.
    if (ref<std::uint8_t>(moby + kMobyKey) >= count
        || ref<std::uint8_t>(moby + kMobyNextKey) >= count) {
        original();

        return;
    }

    step_sequence_back(moby, sequence, count);
    original();
}

}  // namespace

// ---- Entry point ----

void load(openrac::Mod& mod) {
    const Disc* disc = find_disc(mod);

    // A disc with no table row has no known addresses; refuse to run rather than guess.
    if (disc == nullptr) {
        throw openrac::Error(
            "lock-strafe knows the PAL and NTSC-U discs, not " + std::string(mod.version())
        );
    }

    const Settings settings = read_settings(mod);

    g_strafe = std::make_unique<Strafe>(mod, *disc, settings, find_functions(mod, *disc));

    // A hook point this OpenRAC lacks throws: the load is refused with its name, and the port lets
    // go of the hooks already taken.
    mod.hook<hooks::Pad_ProcessReport>(
        [](auto original, std::uint32_t pad_state, std::uint32_t report, std::int32_t size) {
            g_strafe->pad_process_report(original, pad_state, report, size);
        }
    );
    mod.hook<hooks::Hero_UpdateMotionInput>([](auto original) {
        g_strafe->update_motion_input(original);
    });
    mod.hook<hooks::Hero_TurnToHeading>(
        [](auto original, std::int32_t m, float accel, float damping, float limit) {
            g_strafe->turn_to_heading(original, m, accel, damping, limit);
        }
    );
    mod.hook<hooks::Hero_SetPlanarVelocity>([](auto original, float angle) {
        g_strafe->set_planar_velocity(original, angle);
    });
    mod.hook<hooks::FollowCamera_ApplyOverrides>([](auto original, std::uint32_t camera) {
        g_strafe->camera_overrides(original, camera);
    });
    mod.hook<hooks::Hero_AdvanceAnimation>([](auto original) {
        g_strafe->advance_animation(original);
    });

    mod.info(
        std::string(settings.mode == Mode::Hold ? "Hold " : "Lock with weapons, ")
        + (settings.strafe_buttons == kPadL2   ? "L2"
           : settings.strafe_buttons == kPadR2 ? "R2"
                                               : "L2 or R2")
        + ", " + (settings.feel == Feel::Commando ? "Going Commando's feel" : "the classic feel")
        + (settings.twist ? ", hip twist" : "") + (settings.flips ? ", strafe flips" : "")
        + " on the " + std::string(mod.version()) + " disc"
        + (settings.trace ? ", tracing each pad report" : "")
    );
}

}  // namespace lock_strafe::rac1
