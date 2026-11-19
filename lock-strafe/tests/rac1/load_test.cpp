// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The lock-strafe's library as the port loads it, against a host table made here (openrac_mod.h):
// it reports its id and interface, refuses another game and an unknown disc, takes the six hook
// points with their signatures and finds the game's functions by each disc's names; then, on a
// game memory made here, its pad hook takes L2 out in play on foot and leaves it out of play, and
// its turn hook turns toward the camera while locked, calling the original each time; X while
// strafing with the stick pushed sideways enters the flip, and the animation hook sends the hip
// twist's two joints.
//
// Run with the library's path: load_test <lock_strafe.dylib|.so|.dll>.
//
// Expected values are worked out by hand from strafe.hpp, on a memory laid out at the discs'
// addresses in discs.hpp. The tests share one game memory and run in the order they are written:
// each starts from what the one before left.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/rac1/discs.hpp"
#include "../../src/rac1/strafe.hpp"
#include "openrac/games/rac1.h"
#include "openrac/openrac_mod.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Stops the test at the first failed condition and says where. It is a macro for the line number
// and the text of the condition, which a function cannot see.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

using namespace lock_strafe::rac1;

namespace {

// How big the test's game memory is, in bytes: enough for both discs' addresses.
constexpr std::uint32_t kMemoryBytes = 0x00200000u;

// How many hook points the test records: the mod takes six.
constexpr int kMaxHooks = 16;

// How many hook points the mod takes (seen in lock_strafe.cpp).
constexpr int kHooksTaken = 6;

// The most joints one hero_set_joints call turns (openrac_mod.h).
constexpr int kMaxJoints = 4;

// The pad report's address and size as the game's driver passes them: the mod ignores both.
constexpr openrac_gaddr kReportAddress = 0x00100000u;
constexpr std::int32_t kReportSize = 32;

// The word offsets the test lays out in game memory (seen in game code; the same as the mod's).
constexpr std::uint32_t kHeroState = 0x2084u;
constexpr std::uint32_t kHeroSlot0 = 0x1090u;
constexpr std::uint32_t kSlotState = 0x24u;
constexpr std::uint32_t kSlotItem = 0x28u;
constexpr std::uint32_t kHeroMoveYaw = 0x180u;
constexpr std::uint32_t kHeroFacing = 0x98u;
constexpr std::uint32_t kHeroStick = 0x229Cu;
constexpr std::uint32_t kHeroTargetSpeed = 0x190u;
constexpr std::uint32_t kHeroFlipDrift = 0x458u;
constexpr std::uint32_t kHeroFlipCarry = 0x454u;
constexpr std::uint32_t kPadHeld = 0x1A0u;
constexpr std::uint32_t kPadPressed = 0x1A4u;

// The pad bit of L1, which the mod leaves to the game.
constexpr std::uint32_t kPadL1 = 0x4u;

// The item ids the test puts in slot 0: the Blaster (Lock's weapon) and the wrench.
constexpr std::uint32_t kItemBlaster = 0x0F;
constexpr std::uint32_t kItemWrench = 0x08;

// The angle Hero_SetPlanarVelocity is asked for when the game wants the hero's own facing.
constexpr float kFacingRequest = 99999.0f;

// One hook point the mod took.
struct TakenHook {
    const char* name;
    const char* signature;
    void* replacement;
};

// The host the mod runs against: the game's memory, what the mod took and sent, and the knobs the
// test turns. The test's host functions are C callbacks that carry no user pointer, so they reach
// it through g_host.
struct Host {
    // The game's memory.
    std::vector<std::uint8_t> memory = std::vector<std::uint8_t>(kMemoryBytes);

    // The reason the mod gave for refusing to load, empty when it did not.
    std::string failed;

    // The level the host reports as loaded, -1 for none.
    int level = -1;

    // The running disc's version, as the host table reports it.
    const char* disc = "pal";

    // The values of the settings the mod reads.
    const char* setting_mode = "lock";
    const char* setting_button = "both";
    bool setting_twist = true;
    bool setting_flips = true;

    // The hook points the mod took, in order.
    TakenHook hooks[kMaxHooks] = {};
    int hook_count = 0;

    // What the hook points go on to: the game's, here counting calls.
    int original_calls = 0;

    // The move yaw the game's Hero_TurnToHeading saw, and the angle its Hero_SetPlanarVelocity was
    // asked for last.
    float move_yaw_seen = 0.0f;
    float planar_seen = 0.0f;

    // HeroEnterMoveState's state asked for (taken), and stick_direction_vs_facing's answer: a
    // direction set by the test.
    std::int32_t entered = -1;
    std::int32_t direction = 1;

    // What hero_set_joints was given last, and how many joints (-1 before the first call).
    openrac_joint_offset joints[kMaxJoints] = {};
    int joint_count = -1;

    // The game's stack pointer, for the stack frames the mod asks for.
    openrac_gaddr stack_pointer = 0x001F0000u;
};

// The host the test's callbacks reach (see Host). Set once by main, before any callback runs; only
// the test's thread touches it.
Host* g_host = nullptr;

// ---- The game's functions ----

// Counts a call of the game's Pad_ProcessReport.
void game_pad(openrac_gaddr /*pad*/, openrac_gaddr /*report*/, std::int32_t /*size*/) {
    ++g_host->original_calls;
}

// Counts a call of one of the game's functions with no arguments.
void game_void() {
    ++g_host->original_calls;
}

// Counts a call of the game's Hero_TurnToHeading, and keeps the move yaw it saw.
void game_turn(std::int32_t /*mode*/, float /*accel*/, float /*damping*/, float /*limit*/) {
    std::memcpy(&g_host->move_yaw_seen, g_host->memory.data() + kDiscs[0].hero + kHeroMoveYaw, 4);
    ++g_host->original_calls;
}

// Counts a call of the game's Hero_SetPlanarVelocity, and keeps the angle it was asked for.
void game_float(float angle) {
    g_host->planar_seen = angle;
    ++g_host->original_calls;
}

// Counts a call of the game's follow camera requests.
void game_camera(openrac_gaddr /*camera*/) {
    ++g_host->original_calls;
}

// Stands in for FastCos and FastSin: the angle as it is.
float game_cos(float a) {
    return a;
}

// Stands in for the camera's slide: does nothing.
void game_int(std::int32_t /*a*/) {}

// Stands in for the camera's lateral spring: does nothing.
void game_two(float /*a*/, float /*b*/) {}

// Stands in for the camera's look along a heading: does nothing.
void game_look(openrac_gaddr /*direction*/, float /*a*/, float /*b*/) {}

// Stands in for HeroEnterMoveState: keeps the state asked for and takes it.
std::int32_t game_enter(std::int32_t state, std::int32_t animate) {
    CHECK(animate == 1);
    g_host->entered = state;

    return 1;
}

// Stands in for stick_direction_vs_facing: the direction the test set.
std::int32_t game_direction() {
    return g_host->direction;
}

// ---- The host table ----

// Keeps what the mod sent to hero_set_joints.
int host_set_joints(openrac_mod* /*mod*/, const openrac_joint_offset* joints, int count) {
    CHECK(count >= 0 && count <= kMaxJoints);
    g_host->joint_count = count;

    // An empty list takes the joints off; there is nothing to copy then.
    if (count > 0) {
        std::memcpy(
            g_host->joints, joints, sizeof(openrac_joint_offset) * static_cast<std::size_t>(count)
        );
    }

    return OPENRAC_HOOK_OK;
}

// Prints a log line.
void host_log(openrac_mod* /*mod*/, int level, const char* text) {
    std::printf("[%d] lock-strafe: %s\n", level, text);
}

// Keeps the reason the mod refused to load.
void host_fail(openrac_mod* /*mod*/, const char* reason) {
    g_host->failed = reason;
}

// Answers the two bool settings the mod reads.
int setting_bool(openrac_mod* /*mod*/, const char* key) {
    // Twist and flips are the test's knobs; any other key is off.
    if (std::strcmp(key, "twist") == 0) {
        return g_host->setting_twist ? 1 : 0;
    }

    return std::strcmp(key, "flips") == 0 && g_host->setting_flips ? 1 : 0;
}

// Answers no integer setting: the mod reads none.
std::int64_t setting_int(openrac_mod* /*mod*/, const char* /*key*/) {
    return 0;
}

// Answers no float setting: the mod reads none.
double setting_float(openrac_mod* /*mod*/, const char* /*key*/) {
    return 0.0;
}

// Answers the three choice settings the mod reads.
const char* setting_string(openrac_mod* /*mod*/, const char* key) {
    return std::strcmp(key, "mode") == 0     ? g_host->setting_mode
           : std::strcmp(key, "button") == 0 ? g_host->setting_button
           : std::strcmp(key, "feel") == 0   ? "commando"
                                             : "";
}

// Returns what a hook point goes on to: the game's function, here a counting stand-in.
void* original_for(const char* name) {
    // One stand-in per signature the six hook points have.
    if (std::strcmp(name, "Pad_ProcessReport") == 0) {
        return reinterpret_cast<void*>(game_pad);
    }

    // Hero_TurnToHeading's stand-in records the move yaw it sees.
    if (std::strcmp(name, "Hero_TurnToHeading") == 0) {
        return reinterpret_cast<void*>(game_turn);
    }

    // Hero_SetPlanarVelocity's stand-in records the angle it is asked for.
    if (std::strcmp(name, "Hero_SetPlanarVelocity") == 0) {
        return reinterpret_cast<void*>(game_float);
    }

    // The follow camera's requests have a stand-in of their own.
    if (std::strcmp(name, "FollowCamera_ApplyOverrides") == 0) {
        return reinterpret_cast<void*>(game_camera);
    }

    return reinterpret_cast<void*>(game_void);
}

// Returns the name the port takes for a hook point's permanent id (the SDK's C++ layer asks by
// it), as the port's loader resolves ids and aliases (OpenRAC's docs/port/MODS.md, section 4).
const char* canonical(const char* name) {
    static const char* const kIds[][2] = {
        {"rac1.pad.process_report", "Pad_ProcessReport"},
        {"rac1.player.update_motion_input", "Hero_UpdateMotionInput"},
        {"rac1.player.turn_to_heading", "Hero_TurnToHeading"},
        {"rac1.player.set_planar_velocity", "Hero_SetPlanarVelocity"},
        {"rac1.camera.follow.apply_overrides", "FollowCamera_ApplyOverrides"},
        {"rac1.player.advance_animation", "Hero_AdvanceAnimation"},
    };

    // An id in the table is an alias of the name beside it.
    for (const auto& id : kIds) {
        // An id in the table is an alias of the name beside it.
        if (std::strcmp(name, id[0]) == 0) {
            return id[1];
        }
    }

    return name;
}

// Records a hook point the mod takes and gives it the game's stand-in to call on.
int host_hook(
    openrac_mod* /*mod*/,
    const char* name,
    const char* signature,
    void* replacement,
    void** original
) {
    name = canonical(name);
    CHECK(g_host->hook_count < kMaxHooks);
    g_host->hooks[g_host->hook_count] = {name, signature, replacement};
    ++g_host->hook_count;
    *original = original_for(name);

    return OPENRAC_HOOK_OK;
}

// Accepts a frame callback and ignores it.
int host_on_frame(openrac_mod* /*mod*/, openrac_frame_fn /*fn*/, void* /*user*/) {
    return 0;
}

// Accepts a pad callback and ignores it.
int host_on_pad(openrac_mod* /*mod*/, openrac_pad_fn /*fn*/, void* /*user*/) {
    return 0;
}

// Returns the game's function by name: only the running disc's own names are found.
void* host_function(const char* name) {
    const Disc& disc = std::strcmp(g_host->disc, "pal") == 0 ? kDiscs[0] : kDiscs[1];

    // FastCos and FastSin are the same stand-in.
    if (std::strcmp(name, disc.fast_cos) == 0 || std::strcmp(name, disc.fast_sin) == 0) {
        return reinterpret_cast<void*>(game_cos);
    }

    // The camera's slide.
    if (std::strcmp(name, disc.camera_slide) == 0) {
        return reinterpret_cast<void*>(game_int);
    }

    // The camera's lateral spring.
    if (std::strcmp(name, disc.camera_spring) == 0) {
        return reinterpret_cast<void*>(game_two);
    }

    // The camera's panning cleared, which counts as a call.
    if (std::strcmp(name, disc.camera_pan_off) == 0) {
        return reinterpret_cast<void*>(game_void);
    }

    // The camera's look along a heading.
    if (std::strcmp(name, disc.camera_look_along) == 0) {
        return reinterpret_cast<void*>(game_look);
    }

    // HeroEnterMoveState.
    if (std::strcmp(name, disc.enter_state) == 0) {
        return reinterpret_cast<void*>(game_enter);
    }

    // stick_direction_vs_facing.
    if (std::strcmp(name, disc.stick_direction) == 0) {
        return reinterpret_cast<void*>(game_direction);
    }

    return nullptr;
}

// Finds no game function by address.
void* host_function_at(openrac_gaddr /*address*/) {
    return nullptr;
}

// Returns the executable's address as it is: the test does not move the executable.
openrac_gaddr identity(openrac_gaddr address) {
    return address;
}

// Returns level 0's address as it is: the test does not move the level's program either.
openrac_gaddr identity_level(int /*level*/, openrac_gaddr address) {
    return address;
}

// Returns the level the test says is loaded.
int loaded_level() {
    return g_host->level;
}

// Gives the mod a stack frame of `size` bytes, 16-byte aligned, as the game's stack would.
openrac_gaddr stack_push(std::uint32_t size, openrac_gaddr* saved) {
    *saved = g_host->stack_pointer;
    g_host->stack_pointer -= (size + 15u) & ~15u;

    return g_host->stack_pointer;
}

// Takes back the stack frame the mod gave up.
void stack_pop(openrac_gaddr saved) {
    g_host->stack_pointer = saved;
}

// Returns the host table for a game and a disc, over the test's memory.
openrac_api make_api(const char* game, const char* version) {
    openrac_api api;

    std::memset(&api, 0, sizeof api);
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = game;
    api.version = version;
    api.openrac_version = "test";
    api.platform = "test";
    api.memory = g_host->memory.data();
    api.log = host_log;
    api.fail = host_fail;
    api.setting_bool = setting_bool;
    api.setting_int = setting_int;
    api.setting_float = setting_float;
    api.setting_string = setting_string;
    api.hook = host_hook;
    api.on_frame = host_on_frame;
    api.on_pad = host_on_pad;
    api.function = host_function;
    api.function_at = host_function_at;
    api.relocate_data = identity;
    api.relocate_level_data = identity_level;
    api.loaded_level = loaded_level;
    api.stack_push = stack_push;
    api.stack_pop = stack_pop;
    api.hero_set_joints = host_set_joints;

    return api;
}

// ---- The library ----

// Opens the library at `path`, or returns null.
void* open_library(const char* path) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(path));
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

// Returns the library's exported `name`, or null.
void* symbol(void* library, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

// The library under test and how to load it again: the entry point, the handle and the table.
struct Session {
    openrac_mod_load_fn load;
    openrac_mod* mod;
    openrac_api api;

    // The pad and turn hooks of the last load, set by take_hooks.
    openrac_rac1_Pad_ProcessReport pad;
    openrac_rac1_Hero_TurnToHeading turn;
};

// Loads the library again on the session's table, and returns the load's answer (0 to run).
int load_again(Session& session) {
    return session.load(&session.api, session.mod);
}

// ---- Game memory ----

// Returns the replacement of the hook point called `name`, or null when the mod did not take it.
void* hook_named(const char* name) {
    // The hooks are recorded under the port's own names.
    for (int n = 0; n < g_host->hook_count; ++n) {
        // The hooks are recorded under the port's own names.
        if (std::strcmp(g_host->hooks[n].name, name) == 0) {
            return g_host->hooks[n].replacement;
        }
    }

    return nullptr;
}

// Takes the pad and turn hooks of the load that ran last.
void take_hooks(Session& session) {
    session.pad = reinterpret_cast<openrac_rac1_Pad_ProcessReport>(hook_named("Pad_ProcessReport"));
    session.turn =
        reinterpret_cast<openrac_rac1_Hero_TurnToHeading>(hook_named("Hero_TurnToHeading"));
}

// Writes a 32-bit word into the game's memory.
void put_u32(openrac_gaddr address, std::uint32_t value) {
    std::memcpy(g_host->memory.data() + address, &value, 4);
}

// Reads a 32-bit word from the game's memory.
std::uint32_t get_u32(openrac_gaddr address) {
    std::uint32_t value;

    std::memcpy(&value, g_host->memory.data() + address, 4);

    return value;
}

// Writes a float into the game's memory.
void put_f32(openrac_gaddr address, float value) {
    std::memcpy(g_host->memory.data() + address, &value, 4);
}

// Reads a float from the game's memory.
float get_f32(openrac_gaddr address) {
    float value;

    std::memcpy(&value, g_host->memory.data() + address, 4);

    return value;
}

// Runs one pad report through the mod's pad hook, as the game's driver would.
void report(Session& session, const Disc& disc) {
    session.pad(disc.pad, kReportAddress, kReportSize);
}

// ---- Tests ----

// Checks that another game and an unknown disc are refused with a reason, before any hook.
void test_another_game_and_an_unknown_disc_are_refused(Session& session) {
    session.api = make_api("rac2", "ntsc");
    CHECK(
        load_again(session) != 0 && g_host->failed.find("rac1") != std::string::npos
        && g_host->hook_count == 0
    );
    session.api = make_api("rac1", "jp");
    g_host->failed.clear();
    CHECK(
        load_again(session) != 0 && g_host->failed.find("jp") != std::string::npos
        && g_host->hook_count == 0
    );
}

// Checks that each disc's own function names are found and the six hook points are taken with
// games/rac1.h's signatures.
void test_each_disc_takes_the_six_hook_points(Session& session) {
    // NTSC-U: its own function names (host_function knows only those).
    g_host->disc = "ntsc";
    session.api = make_api("rac1", "ntsc");
    g_host->failed.clear();
    CHECK(load_again(session) == 0 && g_host->failed.empty() && g_host->hook_count == kHooksTaken);
    g_host->hook_count = 0;

    // PAL: the six hook points with games/rac1.h's signatures.
    g_host->disc = "pal";
    session.api = make_api("rac1", "pal");
    CHECK(load_again(session) == 0 && g_host->failed.empty());
    CHECK(g_host->hook_count == kHooksTaken);

    static const char* const kPoints[][2] = {
        {OPENRAC_RAC1_HOOK_Pad_ProcessReport, OPENRAC_RAC1_SIG_Pad_ProcessReport},
        {OPENRAC_RAC1_HOOK_Hero_UpdateMotionInput, OPENRAC_RAC1_SIG_Hero_UpdateMotionInput},
        {OPENRAC_RAC1_HOOK_Hero_TurnToHeading, OPENRAC_RAC1_SIG_Hero_TurnToHeading},
        {OPENRAC_RAC1_HOOK_Hero_SetPlanarVelocity, OPENRAC_RAC1_SIG_Hero_SetPlanarVelocity},
        {OPENRAC_RAC1_HOOK_FollowCamera_ApplyOverrides,
         OPENRAC_RAC1_SIG_FollowCamera_ApplyOverrides},
        {OPENRAC_RAC1_HOOK_Hero_AdvanceAnimation, OPENRAC_RAC1_SIG_Hero_AdvanceAnimation},
    };

    // Each point is taken under its id and with its signature, in the order the mod takes them.
    for (int n = 0; n < kHooksTaken; ++n) {
        CHECK(
            std::strcmp(g_host->hooks[n].name, kPoints[n][0]) == 0
            && std::strcmp(g_host->hooks[n].signature, kPoints[n][1]) == 0
        );
    }
}

// Checks that the pad hook takes L2 out in play on foot only, and that the turn hook eases toward
// the camera while locked and leaves another kind of turn to the game.
void test_the_pad_hook_takes_l2_and_the_turn_hook_follows_the_camera(Session& session) {
    const Disc& disc = kDiscs[0];

    // The hero running on level ground in play, the Blaster ready, L2 held.
    put_u32(disc.hero + kHeroState, kStateRun);
    put_u32(disc.hero + kHeroSlot0 + kSlotState, kSlotReady);
    put_u32(disc.hero + kHeroSlot0 + kSlotItem, kItemBlaster);
    put_u32(disc.level_mode, 0);
    put_f32(disc.camera_yaw, 1.25f);
    put_f32(disc.hero + kHeroMoveYaw, -0.5f);
    take_hooks(session);
    CHECK(session.pad != nullptr && session.turn != nullptr);

    // Out of play (no level): the game sees L2.
    g_host->level = -1;
    put_u32(disc.pad + kPadHeld, kPadL2 | kPadL1);
    g_host->original_calls = 0;
    report(session, disc);
    CHECK(g_host->original_calls == 1 && get_u32(disc.pad + kPadHeld) == (kPadL2 | kPadL1));

    // In play on foot: L2 taken out, L1 kept.
    g_host->level = 0;
    report(session, disc);
    CHECK(g_host->original_calls == 2 && get_u32(disc.pad + kPadHeld) == kPadL1);

    // Locked: the turn eases toward the camera's yaw, and the stick's move yaw is put back.
    session.turn(0, 0.1f, 0.2f, 0.3f);
    CHECK(
        g_host->original_calls == 3 && g_host->move_yaw_seen == 1.25f
        && get_f32(disc.hero + kHeroMoveYaw) == -0.5f
    );

    // A turn of another kind is the game's, untouched.
    session.turn(1, 0.1f, 0.2f, 0.3f);
    CHECK(g_host->original_calls == 4 && g_host->move_yaw_seen == -0.5f);
}

// Checks that X while strafing with the stick pushed fully sideways enters the flip after the
// game's motion input, and that forward and a half-pushed stick leave the game's own jump.
void test_x_while_strafing_enters_the_flip() {
    const Disc& disc = kDiscs[0];

    // The flip: standing, strafing (the Blaster, Lock), X pressed with the stick pushed fully to
    // the right enters state 0xB after the game's motion input, and starts the drift at 5 u/s.
    put_f32(disc.frame_scale, 1.0f);
    put_f32(disc.turn_step, 1.0f / 60.0f);
    put_u32(disc.hero + kHeroState, kStateStand);
    put_f32(disc.hero + kHeroStick, 1.0f);
    put_u32(disc.pad + kPadPressed, kPadX);

    auto motion =
        reinterpret_cast<openrac_rac1_Hero_UpdateMotionInput>(hook_named("Hero_UpdateMotionInput"));

    CHECK(motion != nullptr);
    g_host->original_calls = 0;
    motion();
    CHECK(g_host->original_calls >= 1 && g_host->entered == kStateFlip);
    CHECK(get_f32(disc.hero + kHeroFlipDrift) == (1.0f / 60.0f) * kFlipDrift);

    // No run speed carried along his facing: the flip goes only along the strafe.
    CHECK(get_f32(disc.hero + kHeroFlipCarry) == 0.0f);

    // Forward is the game's own jump.
    g_host->entered = -1;
    g_host->direction = 2;
    motion();
    CHECK(g_host->entered == -1);

    // The stick not past 0.9: the game's too.
    g_host->direction = 0;
    put_f32(disc.hero + kHeroStick, 0.5f);
    motion();
    CHECK(g_host->entered == -1);
    put_u32(disc.pad + kPadPressed, 0);
}

// Checks that the animation hook sends the pelvis turned toward where he goes and the spine back,
// as the game builds a yaw about z.
void test_the_animation_hook_sends_the_hip_twist() {
    const Disc& disc = kDiscs[0];

    // The twist: running right with the stick pushed (move yaw -0.5 from facing 1.07 is about -90
    // degrees), the animation hook sends the pelvis turned toward it and the spine back.
    put_u32(disc.hero + kHeroState, kStateRun);
    put_f32(disc.hero + kHeroStick, 1.0f);
    put_f32(disc.hero + kHeroFacing, 1.0708f);

    auto advance =
        reinterpret_cast<openrac_rac1_Hero_AdvanceAnimation>(hook_named("Hero_AdvanceAnimation"));

    CHECK(advance != nullptr);
    advance();
    CHECK(g_host->joint_count == 2 && g_host->joints[0].joint == kJointPelvis);
    CHECK(g_host->joints[1].joint == kJointSpine);

    // A yaw about z as the game builds one (the sine negated, strafe.hpp): the pelvis turned
    // right, so its z is positive and its front (the rotation's first row, as the game's joint
    // matrices take it: 2(xy - zw) sideways) to his right; the spine turned back, its z negative.
    // 1.1.0 had both the other way round, the hips turned away from where he went.
    CHECK(g_host->joints[0].rotation[0] == 0.0f && g_host->joints[0].rotation[1] == 0.0f);
    CHECK(g_host->joints[0].rotation[2] > 0.0f && g_host->joints[1].rotation[2] < 0.0f);
    CHECK(
        2.0f
            * (g_host->joints[0].rotation[0] * g_host->joints[0].rotation[1]
               - g_host->joints[0].rotation[2] * g_host->joints[0].rotation[3])
        < 0.0f
    );
}

// Checks the velocity's angle while a strafe slides to a stop, while one ends as he runs on, and
// that the glide is forgotten when he stands, when a scene starts, and out of play.
void test_the_velocity_follows_the_stick_as_a_strafe_ends(Session& session) {
    const Disc& disc = kDiscs[0];

    // The slide to a stop: running right, the velocity along the stick's yaw; the stick let go
    // (the game sets the move yaw to his facing and the target speed to 0), still along it.
    auto planar =
        reinterpret_cast<openrac_rac1_Hero_SetPlanarVelocity>(hook_named("Hero_SetPlanarVelocity"));

    CHECK(planar != nullptr);
    put_f32(disc.hero + kHeroTargetSpeed, 0.095f);
    planar(kFacingRequest);
    CHECK(g_host->planar_seen == -0.5f);
    put_f32(disc.hero + kHeroTargetSpeed, 0.0f);
    put_f32(disc.hero + kHeroMoveYaw, 1.0708f);
    planar(kFacingRequest);
    CHECK(g_host->planar_seen == -0.5f);

    // An angle of the caller's own is kept.
    planar(0.25f);
    CHECK(g_host->planar_seen == 0.25f);
    put_f32(disc.hero + kHeroMoveYaw, -0.5f);

    // The strafe ends while he runs on (Lock: the wrench taken in hand), the stick still right:
    // along the stick until his facing has turned to it, then the game's facing.
    put_f32(disc.hero + kHeroTargetSpeed, 0.095f);
    planar(kFacingRequest);
    CHECK(g_host->planar_seen == -0.5f);
    put_u32(disc.hero + kHeroSlot0 + kSlotItem, kItemWrench);
    put_u32(disc.pad + kPadHeld, 0);
    report(session, disc);
    planar(kFacingRequest);
    CHECK(g_host->planar_seen == -0.5f);
    put_f32(disc.hero + kHeroFacing, -0.49f);
    planar(kFacingRequest);
    CHECK(g_host->planar_seen == kFacingRequest);

    // Standing at a pad report forgets it: running again, the game's from the start.
    put_f32(disc.hero + kHeroFacing, 1.0708f);
    put_u32(disc.hero + kHeroState, kStateStand);
    report(session, disc);
    put_u32(disc.hero + kHeroState, kStateRun);
    planar(kFacingRequest);
    CHECK(g_host->planar_seen == kFacingRequest);

    // A scene starting mid-run, the stick let go: forgotten at the pad's report, not slid along.
    put_u32(disc.hero + kHeroSlot0 + kSlotItem, kItemBlaster);
    planar(kFacingRequest);
    put_u32(disc.hero + kHeroSlot0 + kSlotItem, kItemWrench);
    g_host->level = -1;
    report(session, disc);
    g_host->level = 0;
    put_f32(disc.hero + kHeroTargetSpeed, 0.0f);
    planar(kFacingRequest);
    CHECK(g_host->planar_seen == kFacingRequest);
    put_u32(disc.hero + kHeroSlot0 + kSlotItem, kItemBlaster);

    // Out of play the twist is taken off at the pad's report.
    g_host->level = -1;
    report(session, disc);
    CHECK(g_host->joint_count == 0);
    g_host->level = 0;
}

// Checks that in Hold with nothing latched and the wrench in hand, the turn is the game's.
void test_hold_with_nothing_latched_leaves_the_turn_to_the_game(Session& session) {
    const Disc& disc = kDiscs[0];

    g_host->setting_mode = "hold";
    g_host->hook_count = 0;
    CHECK(load_again(session) == 0);
    take_hooks(session);
    put_u32(disc.hero + kHeroSlot0 + kSlotItem, kItemWrench);
    put_u32(disc.pad + kPadHeld, 0);
    report(session, disc);
    session.turn(0, 0.1f, 0.2f, 0.3f);
    CHECK(g_host->move_yaw_seen == -0.5f);
}

// Checks that with the strafe button R2 only, R2 is taken out in play on foot and L2 and L1 reach
// the game, and that R2 latches Hold.
void test_the_r2_setting_takes_r2_and_leaves_l2_to_the_game(Session& session) {
    const Disc& disc = kDiscs[0];

    // Strafe button R2 only: in play on foot R2 is taken out, L2 (and L1) reach the game, for the
    // game's look and for other mods.
    g_host->setting_button = "r2";
    g_host->hook_count = 0;
    CHECK(load_again(session) == 0);
    take_hooks(session);
    put_u32(disc.pad + kPadHeld, kPadL2 | kPadR2 | kPadL1);
    report(session, disc);
    CHECK(get_u32(disc.pad + kPadHeld) == (kPadL2 | kPadL1));

    // L2 alone does not latch Hold: the game's turn.
    put_u32(disc.pad + kPadHeld, kPadL2);
    report(session, disc);
    session.turn(0, 0.1f, 0.2f, 0.3f);
    CHECK(g_host->move_yaw_seen == -0.5f);

    // R2 latches it: the turn eases toward the camera's yaw.
    put_u32(disc.pad + kPadHeld, kPadR2);
    report(session, disc);
    CHECK(get_u32(disc.pad + kPadHeld) == 0);
    session.turn(0, 0.1f, 0.2f, 0.3f);
    CHECK(g_host->move_yaw_seen == 1.25f);
}

// Checks that under a camera of the level's own R2 stays taken but the turn is the game's, and
// that the camera behind him (id 0, or no camera yet) gives the strafe back.
void test_a_camera_of_the_levels_own_leaves_the_turn_to_the_game(Session& session) {
    const Disc& disc = kDiscs[0];

    // A camera of the level's own (id not 0, Kalebo III's wall camera): R2 stays taken (given
    // back it put him in first person) but the turn is the game's; the camera behind him (id 0)
    // gives the strafe back, R2 still held. A slot pointer of 0 (no camera yet) is behind him.
    const openrac_gaddr slot = 0x001F0000u;

    put_u32(disc.camera_now, slot);

    // The camera's id is the s16 at +0x86, the high half of the word at +0x84.
    put_u32(slot + 0x84u, 0x00070000u);
    put_u32(disc.pad + kPadHeld, 0);
    report(session, disc);
    put_u32(disc.pad + kPadHeld, kPadR2);
    report(session, disc);
    CHECK(get_u32(disc.pad + kPadHeld) == 0);
    session.turn(0, 0.1f, 0.2f, 0.3f);
    CHECK(g_host->move_yaw_seen == -0.5f);

    // Id -1: not behind him either.
    put_u32(slot + 0x84u, 0xFFFF0000u);
    put_u32(disc.pad + kPadHeld, kPadR2);
    report(session, disc);
    session.turn(0, 0.1f, 0.2f, 0.3f);
    CHECK(get_u32(disc.pad + kPadHeld) == 0 && g_host->move_yaw_seen == -0.5f);

    // Id 0: the camera behind him.
    put_u32(slot + 0x84u, 0);
    put_u32(disc.pad + kPadHeld, kPadR2);
    report(session, disc);
    CHECK(get_u32(disc.pad + kPadHeld) == 0);
    session.turn(0, 0.1f, 0.2f, 0.3f);
    CHECK(g_host->move_yaw_seen == 1.25f);

    // No camera slot at all.
    put_u32(disc.camera_now, 0);
    put_u32(disc.pad + kPadHeld, kPadR2);
    report(session, disc);
    session.turn(0, 0.1f, 0.2f, 0.3f);
    CHECK(g_host->move_yaw_seen == 1.25f);
}

// Checks that with the strafe button L2 only, R2 reaches the game.
void test_the_l2_setting_leaves_r2_to_the_game(Session& session) {
    const Disc& disc = kDiscs[0];

    g_host->setting_button = "l2";
    g_host->hook_count = 0;
    CHECK(load_again(session) == 0);
    take_hooks(session);
    put_u32(disc.pad + kPadHeld, kPadL2 | kPadR2);
    report(session, disc);
    CHECK(get_u32(disc.pad + kPadHeld) == kPadR2);
}

}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);

    Host host;

    g_host = &host;

    void* library = open_library(argv[1]);

    CHECK(library != nullptr);

    Session session{};

    auto query = reinterpret_cast<openrac_mod_query_fn>(symbol(library, OPENRAC_MOD_QUERY));

    session.load = reinterpret_cast<openrac_mod_load_fn>(symbol(library, OPENRAC_MOD_LOAD));
    CHECK(query != nullptr && session.load != nullptr);

    openrac_mod_info info;

    std::memset(&info, 0, sizeof info);
    info.size = sizeof info;
    query(&info);

    // The interface its manifest names (1.12: the hero's joints), whatever the SDK it was built
    // with.
    CHECK(info.api_major == OPENRAC_MOD_API_MAJOR && info.api_minor == 12);
    CHECK(std::strcmp(info.id, "lock-strafe") == 0 && std::strcmp(info.version, "1.2.1") == 0);

    // Any handle will do: the mod only passes it back.
    session.mod = reinterpret_cast<openrac_mod*>(&info);

    test_another_game_and_an_unknown_disc_are_refused(session);
    test_each_disc_takes_the_six_hook_points(session);
    test_the_pad_hook_takes_l2_and_the_turn_hook_follows_the_camera(session);
    test_x_while_strafing_enters_the_flip();
    test_the_animation_hook_sends_the_hip_twist();
    test_the_velocity_follows_the_stick_as_a_strafe_ends(session);
    test_hold_with_nothing_latched_leaves_the_turn_to_the_game(session);
    test_the_r2_setting_takes_r2_and_leaves_l2_to_the_game(session);
    test_a_camera_of_the_levels_own_leaves_the_turn_to_the_game(session);
    test_the_l2_setting_leaves_r2_to_the_game(session);
    std::printf("load_test: ok\n");

    return 0;
}
