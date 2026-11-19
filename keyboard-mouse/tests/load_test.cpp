// SPDX-License-Identifier: GPL-3.0-or-later Copyright (c) 2026 the OpenRAC contributors
//
// The keyboard and mouse mod's library as the port loads it, against a host table made here
// (openrac_mod.h, interface 1.3): it reports its id and interface, takes the keyboard and the
// mouse, its pad callback and the follow camera's hook point, and finds the game's functions by
// each disc's names. Then, on a game memory made here, tick by tick: keys and buttons into the
// report, the mouse turning the follow camera and the first-person view itself (and any other
// camera as the right stick), the Quick Select held by right click and pointed by the mouse, a
// number key taking a slot's item, the cursor captured in play and let go on the pause screen, menu
// keys only there, and the camera's reset asking to look along Ratchet's facing.
//
// Expected values are the mod's documented behaviour (README.md, openrac-mod.json) and the
// console's pad report as the game reads it; the addresses come from src/discs.hpp.
//
// Run with the library's path: load_test <keyboard_mouse.dylib|.so|.dll>.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../src/discs.hpp"
#include "kbm.hpp"
#include "openrac/openrac_mod.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

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

// How big the test's game memory is, in bytes.
constexpr std::uint32_t kMemorySize = 0x00200000u;

// The host the mod runs against: the game's memory, the input the port would hand over, what the
// mod asked of the port and what it registered. The test's host functions are C callbacks that
// carry no user pointer, so they reach it through g_host.
struct Host {
    // The game's memory.
    std::vector<std::uint8_t> memory = std::vector<std::uint8_t>(kMemorySize);

    // The reason the mod gave for refusing to load, empty when it did not.
    std::string failed;

    // The running disc's version, as the host table reports it.
    std::string version = "pal";

    // The level the host reports as loaded, -1 for none.
    int level = 0;

    // The input of the tick the port would hand the mod.
    openrac_input input{};

    // The devices the mod took from the port.
    std::uint32_t taken = 0;

    // What the mod last asked of the port for the cursor (1 captured, 0 free), -1 before it asks.
    int capture = -1;

    // The pad callback the mod registered, and the user pointer to pass back to it.
    openrac_pad_fn pad = nullptr;
    void* pad_user = nullptr;

    // The replacement the mod gave the follow camera's hook point.
    void* camera_hook = nullptr;

    // The names of the game's functions the mod looked for, in order.
    std::vector<std::string> functions;

    // How often the game's follow camera ran and its look along a heading was asked for, and the
    // x of the heading it was given.
    int camera_calls = 0;
    int look_calls = 0;
    float look_x = 0.0f;

    // The game's stack pointer, for the stack frames the mod asks for.
    openrac_gaddr stack_pointer = 0x001F0000u;
};

// The host the test's callbacks reach (see Host). Set once by main, before any callback runs; only
// the test's thread touches it.
Host* g_host = nullptr;

// Returns the running disc's addresses.
const kbm::Disc& disc() {
    return g_host->version == "pal" ? kbm::kDiscs[0] : kbm::kDiscs[1];
}

// Returns a reference to a `T` at a game address in the test's memory.
template <class T>
T& at(std::uint32_t address) {
    return *reinterpret_cast<T*>(g_host->memory.data() + address);
}

// Counts a call of the game's follow camera requests.
void game_camera(std::uint32_t) {
    ++g_host->camera_calls;
}

// Stands in for FastCos.
float game_cos(float a) {
    return std::cos(a);
}

// Stands in for FastSin.
float game_sin(float a) {
    return std::sin(a);
}

// Counts a look along a heading and keeps its x.
void game_look_along(openrac_gaddr direction, float, float) {
    ++g_host->look_calls;
    std::memcpy(&g_host->look_x, g_host->memory.data() + direction, 4);
}

// The menu widgets' updates (a widget's +0): an item grid's, a text list's, and a label's, at code
// addresses of the test's own.
constexpr std::uint32_t kGridCode = 0x00200000u;
constexpr std::uint32_t kListCode = 0x00200100u;
constexpr std::uint32_t kLabelCode = 0x00200200u;
// The map's handler (DrawMapScreen) at two places: the executable's and a level's copy.
constexpr std::uint32_t kMapCode = 0x00200400u;
constexpr std::uint32_t kMapCopyCode = 0x00270400u;

// Each returns its own value: identical bodies may be folded into one function (MSVC's /OPT:ICF),
// and the mod tells the kinds apart by the functions' addresses.
int game_grid_update(std::uint32_t) {
    return 1;
}

// Stands in for the text list's update.
int game_list_update(std::uint32_t) {
    return 2;
}

// Stands in for a label's update.
int game_label_update(std::uint32_t) {
    return 3;
}

// Stands in for the map's handler.
int game_map_screen(std::uint32_t) {
    return 4;
}

// Prints the mod's log lines.
void host_log(openrac_mod*, int level, const char* text) {
    std::printf("[%d] keyboard-mouse: %s\n", level, text);
}

// Keeps the reason the mod refused to load.
void host_fail(openrac_mod*, const char* reason) {
    g_host->failed = reason;
}

// Answers the two bool settings the mod reads: the cursor is captured and the menu mouse is on.
int setting_bool(openrac_mod*, const char* key) {
    return std::strcmp(key, "capture_cursor") == 0 || std::strcmp(key, "menu_mouse") == 0 ? 1 : 0;
}

// Answers no integer setting: the mod reads none.
std::int64_t setting_int(openrac_mod*, const char*) {
    return 0;
}

// Answers a float setting the test fixes.
double setting_float(openrac_mod*, const char* key) {
    // The tunings the test fixes: a sensitivity of 1.
    if (std::strcmp(key, "mouse_sensitivity") == 0) {
        return 1.0;
    }

    // Walk at half the stick.
    if (std::strcmp(key, "walk_speed") == 0) {
        return 0.5;
    }

    // A ramp of 0 pushes the stick at once, for the test.
    if (std::strcmp(key, "stick_ramp_ms") == 0) {
        return 0.0;  // at once, for the test
    }

    // A dead zone of 30 pixels for the Quick Select.
    if (std::strcmp(key, "quick_select_dead_zone") == 0) {
        return 30.0;
    }

    return 0.0;
}

// The bindings as the manifest's defaults, as the port hands a setting the player left alone.
const char* setting_string(openrac_mod*, const char* key) {
    // Every action is bound to its default, as the manifest has it.
    for (const kbm::ActionInfo& a : kbm::kActions) {
        // The action whose key is asked for.
        if (std::strcmp(a.key, key) == 0) {
            return a.fallback;
        }
    }

    return std::strcmp(key, "camera_tilt") == 0 ? "hold" : "";
}

int host_hook(
    openrac_mod*, const char* name, const char* signature, void* replacement, void** original
) {
    CHECK(
        std::strcmp(name, "rac1.camera.follow.apply_overrides") == 0
        || std::strcmp(name, "FollowCamera_ApplyOverrides") == 0
    );
    CHECK(std::strcmp(signature, "void(u32)") == 0);
    g_host->camera_hook = replacement;
    *original = reinterpret_cast<void*>(&game_camera);
    return OPENRAC_HOOK_OK;
}

// Accepts a frame callback and ignores it.
int host_on_frame(openrac_mod*, openrac_frame_fn, void*) {
    return 0;
}

// Keeps the pad callback and its user pointer.
int host_on_pad(openrac_mod*, openrac_pad_fn fn, void* user) {
    g_host->pad = fn;
    g_host->pad_user = user;
    return 0;
}

// Returns the game's function by name: only the running disc's own names are found.
void* host_function(const char* name) {
    g_host->functions.emplace_back(name);
    const kbm::Disc& d = disc();

    // The camera reset's cosine.
    if (std::strcmp(name, d.fast_cos) == 0) {
        return reinterpret_cast<void*>(&game_cos);
    }

    // The camera reset's sine.
    if (std::strcmp(name, d.fast_sin) == 0) {
        return reinterpret_cast<void*>(&game_sin);
    }

    // The camera's look along a heading.
    if (std::strcmp(name, d.camera_look_along) == 0) {
        return reinterpret_cast<void*>(&game_look_along);
    }

    // The item grid's update.
    if (std::strcmp(name, d.grid_update) == 0) {
        return reinterpret_cast<void*>(&game_grid_update);
    }

    // The text list's update.
    if (std::strcmp(name, d.list_update) == 0) {
        return reinterpret_cast<void*>(&game_list_update);
    }

    // The map's handler.
    if (std::strcmp(name, d.map_screen) == 0) {
        return reinterpret_cast<void*>(&game_map_screen);
    }

    return nullptr;
}

// Returns the function at a code address, or null.
void* host_function_at(openrac_gaddr address) {
    // The test's code addresses stand for the widgets' updates and the map's handler.
    switch (address) {
        case kGridCode:
            return reinterpret_cast<void*>(&game_grid_update);
        case kListCode:
            return reinterpret_cast<void*>(&game_list_update);
        case kLabelCode:
            return reinterpret_cast<void*>(&game_label_update);
        case kMapCode:
        case kMapCopyCode:
            return reinterpret_cast<void*>(&game_map_screen);
        default:
            return nullptr;
    }
}

// Returns the executable's address as it is.
openrac_gaddr host_relocate(openrac_gaddr a) {
    return a;
}

// Returns level 0's address as it is.
openrac_gaddr host_relocate_level(int, openrac_gaddr a) {
    return a;
}

// Returns the level the test says is loaded.
int host_loaded_level() {
    return g_host->level;
}

// Gives the mod a stack frame of `size` bytes.
openrac_gaddr host_stack_push(std::uint32_t size, openrac_gaddr* saved) {
    *saved = g_host->stack_pointer;
    g_host->stack_pointer -= (size + 15u) & ~15u;
    return g_host->stack_pointer;
}

// Takes back the stack frame the mod gave up.
void host_stack_pop(openrac_gaddr saved) {
    g_host->stack_pointer = saved;
}

// Returns the input of the tick under test.
const openrac_input* host_input() {
    return &g_host->input;
}

// Keeps the devices the mod took.
int host_take_input(openrac_mod*, std::uint32_t devices) {
    g_host->taken |= devices;
    return 0;
}

// Keeps what the mod asked of the port for the cursor.
void host_capture_mouse(openrac_mod*, int capture) {
    g_host->capture = capture;
}

// Returns the host table over the test's memory.
openrac_api make_api() {
    openrac_api api{};
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = "rac1";
    api.version = g_host->version.c_str();
    api.openrac_version = "0.0.0";
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
    api.relocate_data = host_relocate;
    api.relocate_level_data = host_relocate_level;
    api.loaded_level = host_loaded_level;
    api.stack_push = host_stack_push;
    api.stack_pop = host_stack_pop;
    api.input = host_input;
    api.take_input = host_take_input;
    api.capture_mouse = host_capture_mouse;
    return api;
}

// A new tick: nothing held, nothing pressed, no motion; then the test's keys.
void tick() {
    const std::uint32_t t = g_host->input.tick + 1;
    g_host->input = openrac_input{};
    g_host->input.size = sizeof g_host->input;
    g_host->input.tick = t;
    g_host->input.focused = 1;
    g_host->input.captured = 1;
}

// Holds a key down this tick.
void hold(int key) {
    g_host->input.keys[key] = OPENRAC_KEY_HELD;
}

// Presses a key this tick (down now, not held before).
void press(int key) {
    g_host->input.keys[key] = OPENRAC_KEY_HELD | OPENRAC_KEY_PRESSED;
}

// A pad report as the game reads it: 18 bytes, the buttons active low.
struct Report {
    std::uint8_t bytes[18];

    std::uint16_t buttons() const { return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8)); }

    bool held(std::uint16_t button) const { return (buttons() & button) == 0; }
};

// The pad's report as the port hands it with nothing held (or its right stick's x off centre),
// through the mod's callback.
Report read_pad(std::uint8_t right_x = 0x80) {
    Report r{};
    r.bytes[0] = r.bytes[1] = 0xFF;
    r.bytes[2] = r.bytes[3] = r.bytes[4] = r.bytes[5] = 0x80;
    r.bytes[2] = right_x;
    g_host->pad(g_host->pad_user, 0, r.bytes, 18);
    return r;
}

// Returns three floats from the test's memory as a vector.
kbm::Vec3 vec_at(std::uint32_t address) {
    return {at<float>(address), at<float>(address + 4u), at<float>(address + 8u)};
}

// Writes a vector as three floats into the test's memory.
void set_vec(std::uint32_t address, kbm::Vec3 v) {
    at<float>(address) = v.x;
    at<float>(address + 4u) = v.y;
    at<float>(address + 8u) = v.z;
}

// Returns whether two floats are within `within` of each other.
bool close_to(float a, float b, float within = 1e-4f) {
    return std::fabs(a - b) <= within;
}

// The camera in use (0 for none) and its id, as the game's camera block has them.
// The addresses of the test's camera slot and of its type's state, in the test's memory.
constexpr std::uint32_t kCameraBlock = 0x00180000u;
constexpr std::uint32_t kCameraStateBlock = 0x00181000u;

// Makes the camera at `camera` the one in use, with the id `id`.
void use_camera(std::uint32_t camera, int id) {
    at<std::uint32_t>(disc().cameras + kbm::kCamerasCurrent) = camera;
    at<std::int16_t>(kCameraBlock + kbm::kCameraId) = static_cast<std::int16_t>(id);
    at<std::uint32_t>(kCameraBlock + kbm::kCameraState) = kCameraStateBlock;
}

// The follow camera and the first-person view turned by the mouse itself: at once and in
// proportion, mouse right to the right and mouse up looking up whatever the game's invert option,
// the tilt held through the game's own target, the pad's stick and the game's locks left alone.
void direct_camera() {
    const kbm::Disc& d = disc();
    auto camera = reinterpret_cast<void (*)(std::uint32_t)>(g_host->camera_hook);
    const kbm::Vec3 target{10.0f, 20.0f, 5.0f};
    const kbm::Vec3 orbit{
        -4.0f, 0.0f, 1.0f
    };  // the eye west of Ratchet and above him: it looks east
    use_camera(kCameraBlock, 0);
    set_vec(d.cameras + kbm::kCamerasUp, {0.0f, 0.0f, 1.0f});
    set_vec(kCameraStateBlock + kbm::kFollowTarget, target);
    set_vec(kCameraStateBlock + kbm::kFollowOrbit, orbit);
    set_vec(kCameraStateBlock + kbm::kFollowOrbitSmooth, orbit);
    set_vec(kCameraStateBlock + kbm::kFollowEye, kbm::plus(target, orbit));
    at<std::int32_t>(kCameraStateBlock + kbm::kFollowLocks) = 0;
    at<std::int32_t>(d.tilt_invert) = 0;
    at<float>(d.pad_sticks + 4u) = 0.0f;

    // Mouse right: no stick; the hook point turns the orbit, its smoothed copy and the eye's last
    // place by the tick's motion, about up, the view to the right (south).
    tick();
    g_host->input.mouse_dx = 30.0f;
    Report r = read_pad();
    CHECK(r.bytes[2] == 0x80 && r.bytes[3] == 0x80);
    camera(kCameraBlock);
    kbm::LookTuning tuning;
    const kbm::Vec3 turned =
        kbm::rotate(orbit, kbm::kWorldUp, kbm::DirectLook::yaw(30.0f, 0.0f, tuning));
    const kbm::Vec3 o = vec_at(kCameraStateBlock + kbm::kFollowOrbit);
    CHECK(close_to(o.x, turned.x) && close_to(o.y, turned.y) && close_to(o.z, turned.z));
    CHECK(-o.y < -0.1f);  // the view (eye to Ratchet) turned south: right
    CHECK(close_to(vec_at(kCameraStateBlock + kbm::kFollowOrbitSmooth).y, turned.y));
    CHECK(close_to(vec_at(kCameraStateBlock + kbm::kFollowEye).y, target.y + turned.y));
    CHECK(at<float>(d.pad_sticks + 4u) == 0.0f);  // no tilt asked for: the stick's y untouched
    // The motion is the tick's: the next call has none.
    camera(kCameraBlock);
    CHECK(close_to(vec_at(kCameraStateBlock + kbm::kFollowOrbit).y, turned.y));

    // Mouse up: the eye goes down (the view looks up), and the game is asked to hold it there.
    const float before =
        kbm::elevation(vec_at(kCameraStateBlock + kbm::kFollowOrbit), kbm::kWorldUp);
    tick();
    g_host->input.mouse_dy = -40.0f;
    r = read_pad();
    CHECK(r.bytes[3] == 0x80);
    camera(kCameraBlock);
    const float after =
        kbm::elevation(vec_at(kCameraStateBlock + kbm::kFollowOrbit), kbm::kWorldUp);
    CHECK(close_to(after, before + kbm::DirectLook::pitch(0.0f, -40.0f, tuning)) && after < before);
    const float w = at<float>(kCameraStateBlock + kbm::kFollowTilt);
    CHECK(
        close_to(w, kbm::follow_target_of(after))
        && at<float>(kCameraStateBlock + kbm::kFollowTiltRate) == 0.0f
    );
    CHECK(close_to(at<float>(d.pad_sticks + 4u), kbm::follow_stick_for(w, false)));
    // The game's vertical invert on: the same mouse, the same way; only the stick's y written
    // flips.
    at<std::int32_t>(d.tilt_invert) = 1;
    tick();
    g_host->input.mouse_dy = -40.0f;
    read_pad();
    camera(kCameraBlock);
    const float again =
        kbm::elevation(vec_at(kCameraStateBlock + kbm::kFollowOrbit), kbm::kWorldUp);
    CHECK(again < after - 0.01f);
    CHECK(close_to(
        at<float>(d.pad_sticks + 4u), kbm::follow_stick_for(kbm::follow_target_of(again), true)
    ));
    // At rest the tilt stays (Camera tilt: Stays): the target asked for again each tick.
    at<float>(d.pad_sticks + 4u) = 0.0f;
    tick();
    read_pad();
    camera(kCameraBlock);
    CHECK(close_to(
        at<float>(d.pad_sticks + 4u), kbm::follow_stick_for(kbm::follow_target_of(again), true)
    ));
    CHECK(close_to(
        kbm::elevation(vec_at(kCameraStateBlock + kbm::kFollowOrbit), kbm::kWorldUp), again
    ));
    at<std::int32_t>(d.tilt_invert) = 0;

    // The pad's right stick off centre: the camera is the pad's; the mouse's hold lets go.
    const kbm::Vec3 still = vec_at(kCameraStateBlock + kbm::kFollowOrbit);
    at<float>(d.pad_sticks + 4u) = 0.25f;
    tick();
    g_host->input.mouse_dx = 30.0f;
    r = read_pad(0xF0);
    CHECK(r.bytes[2] == 0xF0);
    camera(kCameraBlock);
    CHECK(
        vec_at(kCameraStateBlock + kbm::kFollowOrbit).y == still.y
        && at<float>(d.pad_sticks + 4u) == 0.25f
    );
    tick();
    read_pad();
    camera(kCameraBlock);
    CHECK(at<float>(d.pad_sticks + 4u) == 0.25f);  // not held any more
    // The game's lock on turning (a camera volume's) stops the mouse's turn too.
    at<std::int32_t>(kCameraStateBlock + kbm::kFollowLocks) = 1;
    tick();
    g_host->input.mouse_dx = 30.0f;
    read_pad();
    camera(kCameraBlock);
    CHECK(vec_at(kCameraStateBlock + kbm::kFollowOrbit).y == still.y);
    at<std::int32_t>(kCameraStateBlock + kbm::kFollowLocks) = 0;

    // First person (camera 4): the way it looks turned and tilted before the game's update, its
    // tilt kept where the game keeps it; no stick.
    use_camera(kCameraBlock, kbm::kCameraFirstPerson);
    set_vec(kCameraBlock, {1.0f, 0.0f, 0.0f});  // looking east
    at<float>(kCameraStateBlock + kbm::kFirstPersonTilt) = 0.0f;
    at<std::uint32_t>(d.hero + kbm::kHeroMoby) = 0;  // no moby: up is the world's
    tick();
    g_host->input.mouse_dx = 30.0f;
    g_host->input.mouse_dy = -30.0f;
    r = read_pad();
    CHECK(r.bytes[2] == 0x80 && r.bytes[3] == 0x80);
    const kbm::Vec3 look = vec_at(kCameraBlock);
    CHECK(look.y < -0.01f && look.z > 0.01f);  // right, and up
    CHECK(close_to(kbm::length(look), 1.0f));
    const float tilt = at<float>(kCameraStateBlock + kbm::kFirstPersonTilt);
    CHECK(close_to(tilt, kbm::DirectLook::pitch(30.0f, -30.0f, tuning)) && tilt < 0.0f);
    CHECK(close_to(kbm::elevation(look, kbm::kWorldUp), -tilt));

    // It stops at the game's 64 degrees.
    for (int i = 0; i < 40; ++i) {
        tick();
        g_host->input.mouse_dy = -100.0f;
        read_pad();
    }

    CHECK(
        close_to(at<float>(kCameraStateBlock + kbm::kFirstPersonTilt), -kbm::kFirstPersonTiltLimit)
    );
    CHECK(close_to(
        kbm::elevation(vec_at(kCameraBlock), kbm::kWorldUp), kbm::kFirstPersonTiltLimit, 1e-3f
    ));

    // Any other camera (a scene's own): the mouse as the right stick, as before.
    use_camera(kCameraBlock, 7);

    // The follow camera's hook does not run for a few ticks.
    for (int i = 0; i < 4; ++i) {
        tick();
        read_pad();
    }

    tick();
    g_host->input.mouse_dx = 24.0f;
    read_pad();
    tick();
    g_host->input.mouse_dx = 24.0f;
    r = read_pad();
    CHECK(kbm::game_axis(r.bytes[2]) == 1.0f);
    use_camera(0, 0);
}

// Loads the library on each disc, then checks that another game is refused.
void test_the_library_loads_on_each_disc_and_refuses_another_game(const char* library) {
    void* lib = open_library(library);
    CHECK(lib != nullptr);
    auto query = reinterpret_cast<openrac_mod_query_fn>(symbol(lib, OPENRAC_MOD_QUERY));
    auto load = reinterpret_cast<openrac_mod_load_fn>(symbol(lib, OPENRAC_MOD_LOAD));
    CHECK(query != nullptr && load != nullptr);
    openrac_mod_info info{};
    info.size = sizeof info;
    query(&info);
    CHECK(
        std::strcmp(info.id, "keyboard-mouse") == 0 && info.api_major == 1 && info.api_minor == 3
    );

    // Load once on each disc: the NTSC-U one, then the PAL one.
    for (const char* version : {"ntsc", "pal"}) {
        g_host->version = version;
        g_host->functions.clear();
        g_host->taken = 0;
        const openrac_api api = make_api();
        CHECK(load(&api, reinterpret_cast<openrac_mod*>(1)) == 0);
        CHECK(g_host->failed.empty());
        CHECK(g_host->taken == (OPENRAC_INPUT_KEYBOARD | OPENRAC_INPUT_MOUSE));
        CHECK(g_host->pad != nullptr && g_host->camera_hook != nullptr);
        CHECK(g_host->functions.size() == 6 && g_host->functions[2] == disc().camera_look_along);
        CHECK(
            g_host->functions[3] == disc().grid_update && g_host->functions[4] == disc().list_update
        );
        CHECK(g_host->functions[5] == disc().map_screen);
    }

    // Another game is refused.
    {
        openrac_api other = make_api();
        other.game = "rac2";
        g_host->failed.clear();
        CHECK(load(&other, reinterpret_cast<openrac_mod*>(1)) != 0 && !g_host->failed.empty());
        g_host->failed.clear();
        const openrac_api api = make_api();
        CHECK(load(&api, reinterpret_cast<openrac_mod*>(1)) == 0);
    }
}

// Checks W and Space, the left click and the mouse's turn of the camera, and that an uncaptured
// mouse does nothing.
void test_keys_and_the_mouse_press_the_pad_and_turn_the_camera() {
    const kbm::Disc& d = disc();
    at<std::int32_t>(d.level_mode) = 0;  // in play

    // W and Space: the left stick up, Cross held; the cursor captured.
    tick();
    hold(26);
    hold(44);
    Report r = read_pad();
    CHECK(kbm::game_axis(r.bytes[5]) == -1.0f && r.bytes[4] == 0x80);
    CHECK(r.held(kbm::kCross) && !r.held(kbm::kCircle));
    CHECK(g_host->capture == 1);
    // The same tick read again (the second port, a second read): the same report.
    Report again = read_pad();
    CHECK(std::memcmp(again.bytes, r.bytes, 18) == 0);

    // Left click fires; the mouse moving right turns the camera right.
    tick();
    g_host->input.mouse_held = OPENRAC_MOUSE_LEFT;
    g_host->input.mouse_dx = 24.0f;
    r = read_pad();
    tick();
    g_host->input.mouse_held = OPENRAC_MOUSE_LEFT;
    g_host->input.mouse_dx = 24.0f;
    r = read_pad();
    CHECK(r.held(kbm::kCircle));
    CHECK(kbm::game_axis(r.bytes[2]) == 1.0f);
    // Not captured (the window got focus back): the mouse does nothing.
    tick();
    g_host->input.captured = 0;
    g_host->input.mouse_held = OPENRAC_MOUSE_LEFT;
    g_host->input.mouse_dx = 50.0f;
    r = read_pad();
    CHECK(!r.held(kbm::kCircle) && r.bytes[2] == 0x80);

    direct_camera();
}

// Checks that the right click holds the Quick Select open and the mouse points its slot.
void test_the_quick_select_ring_follows_the_mouse() {
    Report r{};

    // Right click held: Triangle held, the mouse pointing the ring's slot by the left stick.
    tick();
    g_host->input.mouse_held = OPENRAC_MOUSE_RIGHT;
    g_host->input.mouse_pressed = OPENRAC_MOUSE_RIGHT;
    hold(26);  // W held: the ring takes the left stick over
    r = read_pad();
    CHECK(r.held(kbm::kTriangle) && r.bytes[4] == 0x80 && r.bytes[5] == 0x80);
    tick();
    g_host->input.mouse_held = OPENRAC_MOUSE_RIGHT;
    g_host->input.mouse_dx = 45.0f;
    g_host->input.mouse_dy = 45.0f;
    r = read_pad();
    CHECK(r.held(kbm::kTriangle));
    CHECK(kbm::ring_slot(kbm::game_axis(r.bytes[4]), kbm::game_axis(r.bytes[5])) == 3);
    CHECK(r.bytes[2] == 0x80);  // the camera stays still while the ring is open
    tick();                     // let go: Triangle up, the game takes the slot
    r = read_pad();
    CHECK(!r.held(kbm::kTriangle) && r.bytes[4] == 0x80);
}

// Checks that the number keys and the wheel take the Quick Select's items, and do nothing in a
// vehicle.
void test_number_keys_put_the_quick_selects_items_in_hand() {
    const kbm::Disc& d = disc();
    const std::uint32_t hero = disc().hero;

    // 3: the item in the Quick Select's third slot, put in hand; the wheel goes on from it.
    for (int i = 0; i < 8; ++i) {
        at<std::int32_t>(d.quick_select + 4u * static_cast<std::uint32_t>(i)) = i == 2   ? 0x0B
                                                                                : i == 5 ? 0x15
                                                                                         : 0;
    }

    at<std::int32_t>(hero + kbm::kHeroItem) = 0x08;
    at<std::int32_t>(hero + kbm::kHeroPending) = 0;
    tick();
    press(32);
    read_pad();
    CHECK(at<std::int32_t>(hero + kbm::kHeroPending) == 0x0B);
    tick();
    g_host->input.wheel_y = -1.0f;  // down: the next
    read_pad();
    CHECK(at<std::int32_t>(hero + kbm::kHeroPending) == 0x15);
    // In a vehicle the number keys do nothing.
    at<std::int32_t>(hero + kbm::kHeroPending) = 0;
    at<std::uint8_t>(hero + kbm::kHeroVehicle) = 1;
    tick();
    press(32);
    read_pad();
    CHECK(at<std::int32_t>(hero + kbm::kHeroPending) == 0);
    at<std::uint8_t>(hero + kbm::kHeroVehicle) = 0;
}

// Checks Escape, Enter, Space, Backspace, the arrows and a click on the pause screen.
void test_the_pause_screen_keys_and_clicks() {
    const kbm::Disc& d = disc();
    Report r{};

    // Escape pauses (Start). Still held as the pause menu opens, it does not also go back there;
    // let go and pressed again, it does (Triangle). On the pause screen the cursor goes, Enter and
    // Space confirm (Cross), Backspace goes back, the arrows are the d-pad, and a left click over
    // the game is Cross; the play binds of the mouse (Fire's Circle) do nothing there.
    tick();
    press(41);
    r = read_pad();
    CHECK(r.held(kbm::kStart));
    at<std::int32_t>(d.level_mode) = 3;
    tick();
    hold(41);
    r = read_pad();
    CHECK(!r.held(kbm::kTriangle) && !r.held(kbm::kStart) && g_host->capture == 0);
    tick();
    r = read_pad();
    tick();
    press(41);
    r = read_pad();
    CHECK(r.held(kbm::kTriangle) && !r.held(kbm::kStart));
    tick();
    press(40);
    hold(82);
    r = read_pad();
    CHECK(r.held(kbm::kCross) && r.held(kbm::kUp) && !r.held(kbm::kCircle));
    tick();
    press(44);
    r = read_pad();
    CHECK(r.held(kbm::kCross));
    tick();
    press(42);
    r = read_pad();
    CHECK(r.held(kbm::kTriangle));
    tick();
    g_host->input.captured = 0;
    g_host->input.pointer_inside = 1;
    g_host->input.mouse_held = g_host->input.mouse_pressed = OPENRAC_MOUSE_LEFT;
    r = read_pad();
    CHECK(r.held(kbm::kCross) && !r.held(kbm::kCircle));
    tick();
    g_host->input.captured = 0;
    g_host->input.mouse_held = OPENRAC_MOUSE_LEFT;  // the cursor off the game's picture: nothing
    g_host->input.pointer_inside = 0;
    r = read_pad();
    CHECK(!r.held(kbm::kCross));
}

// Checks that a tap between ticks presses once, a held arrow repeats and the wheel presses per
// notch.
void test_taps_key_repeat_and_the_wheel_in_menus() {
    Report r{};

    // A key tapped between two ticks (down and up again) still presses for one.
    tick();
    g_host->input.keys[40] = OPENRAC_KEY_PRESSED;
    r = read_pad();
    CHECK(r.held(kbm::kCross));
    // A held arrow repeats: let go one tick in six after 24.
    int ups = 0;
    bool was = false;

    // Hold the arrow for sixty ticks and count the presses.
    for (int i = 0; i < 60; ++i) {
        tick();
        hold(82);
        r = read_pad();

        // Count each press that follows a tick with Up let go.
        if (r.held(kbm::kUp) && !was) {
            ++ups;
        }

        was = r.held(kbm::kUp);
    }

    CHECK(ups == 6);  // the press, then once after each tick let go (at 30, 36, 42, 48 and 54)
    // The wheel: each notch one press, let go between.
    tick();
    g_host->input.pointer_inside = 1;
    g_host->input.captured = 0;
    g_host->input.wheel_y = -1.0f;
    r = read_pad();
    CHECK(r.held(kbm::kDown));
    tick();
    g_host->input.pointer_inside = 1;
    g_host->input.captured = 0;
    g_host->input.wheel_y = -1.0f;
    r = read_pad();
    CHECK(!r.held(kbm::kDown));  // let go between
    tick();
    r = read_pad();
    CHECK(r.held(kbm::kDown));
}

// Checks that the mouse moves the pause page's focus and list selection to the item under it.
void test_the_mouse_over_the_pause_menus_page() {
    const kbm::Disc& d = disc();
    Report r{};

    // The pause menu's page under the mouse: over an item not focused, the focus moves there (the
    // page's pending focus, as the d-pad asks for it); over the focused list's third row, the d-pad
    // steps the selection down to it.
    {
        const std::uint32_t menus = d.page_menu;
        const std::uint32_t page = 0x00180000u;
        const std::uint32_t w0 = 0x00181000u;
        const std::uint32_t w1 = 0x00182000u;
        const std::uint32_t items = 0x00183000u;
        at<std::uint32_t>(menus + 4u) = page;
        at<std::uint32_t>(page + kbm::kPageFocus) = w0;
        at<std::uint32_t>(page + kbm::kPageWidgets) = w0;
        at<std::uint32_t>(page + kbm::kPageWidgets + 4u) = w1;
        at<std::uint32_t>(page + kbm::kPagePending) = 0;
        const std::int32_t r0[4] =
            {100, 100, 200, 100};  // a list of three rows: 25 apart from y 125
        const std::int32_t r1[4] = {100, 250, 200, 40};
        std::memcpy(g_host->memory.data() + w0 + kbm::kWidgetRect, r0, 16);
        std::memcpy(g_host->memory.data() + w1 + kbm::kWidgetRect, r1, 16);
        at<std::uint32_t>(w0 + kbm::kListItems) = items;
        at<std::int32_t>(w0 + kbm::kListSelected) = 0;
        // Two text lists, the second below the first (the first's down link).
        at<std::uint32_t>(w0) = kListCode;
        at<std::uint32_t>(w0 + kbm::kListDown) = w1;
        at<std::uint32_t>(w1) = kListCode;
        at<std::uint32_t>(w1 + kbm::kListUp) = w0;

        // Three rows of text in the list, the fourth empty.
        for (int i = 0; i < 3; ++i) {
            at<std::int16_t>(items + kbm::kListItemSize * static_cast<std::uint32_t>(i)) =
                static_cast<std::int16_t>(10 + i);
        }

        const auto point = [](float gx, float gy) {
            tick();
            g_host->input.captured = 0;
            g_host->input.pointer_inside = 1;
            g_host->input.pointer_x = gx / kbm::kScreenWidth;
            g_host->input.pointer_y = gy / kbm::kScreenHeight;
            g_host->input.mouse_dx = 1.0f;
        };
        point(150.0f, 270.0f);  // over the second item
        read_pad();
        CHECK(at<std::uint32_t>(page + kbm::kPagePending) == w1);
        at<std::uint32_t>(page + kbm::kPagePending) = 0;
        point(150.0f, 176.0f);  // over the list's third row
        r = read_pad();
        CHECK(r.held(kbm::kDown));
        at<std::int32_t>(w0 + kbm::kListSelected) = 1;
        tick();
        g_host->input.captured = 0;
        r = read_pad();  // let go
        point(150.0f, 176.0f);
        r = read_pad();
        CHECK(r.held(kbm::kDown));
        at<std::int32_t>(w0 + kbm::kListSelected) = 2;
        tick();
        r = read_pad();
        point(150.0f, 177.0f);  // on it: nothing more
        r = read_pad();
        CHECK(!r.held(kbm::kDown) && !r.held(kbm::kUp));
        // A click on the other item: focus first, then Cross.
        point(150.0f, 270.0f);
        g_host->input.mouse_held = g_host->input.mouse_pressed = OPENRAC_MOUSE_LEFT;
        r = read_pad();
        CHECK(!r.held(kbm::kCross) && at<std::uint32_t>(page + kbm::kPagePending) == w1);
        at<std::uint32_t>(page + kbm::kPageFocus) = w1;
        at<std::uint32_t>(page + kbm::kPagePending) = 0;
        bool crossed = false;

        // Wait up to six ticks for the click's Cross.
        for (int i = 0; i < 6 && !crossed; ++i) {
            tick();
            g_host->input.captured = 0;
            crossed = read_pad().held(kbm::kCross);
        }

        CHECK(crossed);
        // The map focused (its handler DrawMapScreen): Enter and a click travel (Circle), Space
        // turns the page (Cross), Escape goes back; E is Circle on any page.
        at<std::uint32_t>(w1 + 4u) = kMapCode;
        tick();
        g_host->input.captured = 0;
        read_pad();
        tick();
        g_host->input.captured = 0;
        press(40);
        r = read_pad();
        CHECK(r.held(kbm::kCircle) && !r.held(kbm::kCross));
        tick();
        g_host->input.captured = 0;
        press(44);
        r = read_pad();
        CHECK(r.held(kbm::kCross) && !r.held(kbm::kCircle));
        tick();
        g_host->input.captured = 0;
        g_host->input.pointer_inside = 1;
        g_host->input.mouse_held = g_host->input.mouse_pressed = OPENRAC_MOUSE_LEFT;
        r = read_pad();
        CHECK(r.held(kbm::kCircle));
        tick();
        r = read_pad();
        tick();
        press(41);
        r = read_pad();
        CHECK(r.held(kbm::kTriangle));
        // A level's copy of the handler, at another address, is the map too.
        at<std::uint32_t>(w1 + 4u) = kMapCopyCode;
        tick();
        r = read_pad();
        tick();
        g_host->input.captured = 0;
        press(40);
        r = read_pad();
        CHECK(r.held(kbm::kCircle) && !r.held(kbm::kCross));
        tick();
        r = read_pad();
        at<std::uint32_t>(w1 + 4u) = 0;
        tick();
        r = read_pad();
        tick();
        press(8);  // E
        r = read_pad();
        CHECK(r.held(kbm::kCircle) && !r.held(kbm::kCross));
        at<std::uint32_t>(menus + 4u) = 0;
    }
}

// Checks that the mouse never focuses the Quick Select page's label or preview.
void test_the_mouse_over_the_quick_select_page() {
    const kbm::Disc& d = disc();

    // The Quick Select page: two item grids (the second below the first), a label and the item
    // preview, which reads the focused widget as a grid. The mouse never focuses the label or the
    // preview, which the d-pad cannot reach (focusing one handed the preview a garbage item and
    // the game crashed); over the label where it lies on the second grid, the grid is focused.
    {
        const std::uint32_t menus = d.page_menu;
        const std::uint32_t page = 0x00188000u;
        const std::uint32_t g0 = 0x00189000u;
        const std::uint32_t g1 = 0x0018A000u;
        const std::uint32_t label = 0x0018B000u;
        const std::uint32_t preview = 0x0018C000u;
        at<std::uint32_t>(menus + 4u) = page;
        at<std::uint32_t>(page + kbm::kPageFocus) = g0;
        at<std::uint32_t>(page + kbm::kPageWidgets) = label;
        at<std::uint32_t>(page + kbm::kPageWidgets + 4u) = preview;
        at<std::uint32_t>(page + kbm::kPageWidgets + 8u) = g0;
        at<std::uint32_t>(page + kbm::kPageWidgets + 12u) = g1;
        at<std::uint32_t>(page + kbm::kPagePending) = 0;
        at<std::uint32_t>(g0) = kGridCode;
        at<std::uint32_t>(g0 + kbm::kGridLinks + 4u) = g1;  // down
        at<std::uint32_t>(g1) = kGridCode;
        at<std::uint32_t>(g1 + kbm::kGridLinks) = g0;  // up
        at<std::uint32_t>(label) = kLabelCode;
        at<std::uint32_t>(preview) = 0x00200300u;  // a function the port does not know
        const std::int32_t rg0[4] = {50, 50, 200, 100};
        const std::int32_t rg1[4] = {50, 160, 200, 100};
        const std::int32_t rlabel[4] = {50, 200, 400, 150};
        const std::int32_t rpreview[4] = {300, 50, 150, 100};
        std::memcpy(g_host->memory.data() + g0 + kbm::kWidgetRect, rg0, 16);
        std::memcpy(g_host->memory.data() + g1 + kbm::kWidgetRect, rg1, 16);
        std::memcpy(g_host->memory.data() + label + kbm::kWidgetRect, rlabel, 16);
        std::memcpy(g_host->memory.data() + preview + kbm::kWidgetRect, rpreview, 16);

        const auto point = [](float gx, float gy, bool click) {
            tick();
            g_host->input.captured = 0;
            g_host->input.pointer_inside = 1;
            g_host->input.pointer_x = gx / kbm::kScreenWidth;
            g_host->input.pointer_y = gy / kbm::kScreenHeight;
            g_host->input.mouse_dx = 1.0f;
            g_host->input.mouse_held = g_host->input.mouse_pressed = click ? OPENRAC_MOUSE_LEFT : 0;
        };

        point(375.0f, 100.0f, false);  // over the preview
        read_pad();
        CHECK(at<std::uint32_t>(page + kbm::kPagePending) == 0);
        point(375.0f, 100.0f, true);
        read_pad();
        CHECK(at<std::uint32_t>(page + kbm::kPagePending) == 0);
        point(350.0f, 300.0f, true);  // over the label alone
        read_pad();
        CHECK(at<std::uint32_t>(page + kbm::kPagePending) == 0);
        point(100.0f, 220.0f, false);  // over the label where it lies on the second grid
        read_pad();
        CHECK(at<std::uint32_t>(page + kbm::kPagePending) == g1);
        at<std::uint32_t>(page + kbm::kPagePending) = 0;
        // Without the link the second grid is out of reach too.
        at<std::uint32_t>(g0 + kbm::kGridLinks + 4u) = 0;
        point(100.0f, 220.0f, false);
        read_pad();
        CHECK(at<std::uint32_t>(page + kbm::kPagePending) == 0);
        tick();
        g_host->input.mouse_held = g_host->input.mouse_pressed = 0;
        read_pad();
        at<std::uint32_t>(menus + 4u) = 0;
    }
}

// Checks that the mouse moves an item grid's selection to the cell under it and a click steps there
// first.
void test_the_mouse_over_an_item_grid() {
    const kbm::Disc& d = disc();
    Report r{};

    // An item grid focused (the Quick Select's two rows of three): the selection goes to the cell
    // under the mouse, a row at a time and then a column, wherever the mouse came from; a click on
    // another cell steps there first, then confirms. The layout: a 240 by 160 rectangle at
    // (60, 120), its frame 3 by 2 units (80 pixels a unit), the icons 0.8 units, the margins 0.1:
    // the cells' icons are 64 pixels, 80 apart, from (68, 128).
    {
        const std::uint32_t menus = d.page_menu;
        const std::uint32_t page = 0x0018D000u;
        const std::uint32_t grid = 0x0018E000u;
        const std::uint32_t moby = 0x0018F000u;
        const std::uint32_t frame = 0x00190000u;
        at<std::uint32_t>(menus + 4u) = page;
        at<std::uint32_t>(page + kbm::kPageFocus) = grid;
        at<std::uint32_t>(page + kbm::kPageWidgets) = grid;
        at<std::uint32_t>(page + kbm::kPagePending) = 0;
        at<std::uint32_t>(grid) = kGridCode;
        const std::int32_t rect[4] = {60, 120, 240, 160};
        std::memcpy(g_host->memory.data() + grid + kbm::kWidgetRect, rect, 16);
        at<std::uint32_t>(grid + kbm::kWidgetMoby) = moby;
        at<std::uint32_t>(moby + kbm::kMenuMobyFrame) = frame;
        at<float>(frame + kbm::kFrameSize) = 3.0f;
        at<float>(frame + kbm::kFrameSize + 4u) = 2.0f;
        at<float>(d.grid_icon_size) = 0.8f;
        at<float>(d.grid_icon_size + 4u) = 0.8f;
        at<std::uint32_t>(grid + kbm::kGridFlags) = 0;
        at<float>(grid + kbm::kGridMargins) = 0.1f;
        at<float>(grid + kbm::kGridMargins + 4u) = 0.1f;
        at<std::int32_t>(grid + kbm::kGridSelected) = 0;
        at<std::int32_t>(grid + kbm::kGridRows) = 2;
        at<std::int32_t>(grid + kbm::kGridCols) = 3;

        // The mouse at (gx, gy), moved this tick or at rest; the d-pad presses of the next ticks.
        const auto presses = [](float gx, float gy, bool moved, bool click, int ticks) {
            std::vector<std::uint16_t> seen;

            // Run the ticks of the hold.
            for (int i = 0; i < ticks; ++i) {
                tick();
                g_host->input.captured = 0;
                g_host->input.pointer_inside = 1;
                g_host->input.pointer_x = gx / kbm::kScreenWidth;
                g_host->input.pointer_y = gy / kbm::kScreenHeight;
                g_host->input.mouse_dx = i == 0 && moved ? 1.0f : 0.0f;
                g_host->input.mouse_held = i == 0 && click ? OPENRAC_MOUSE_LEFT : 0;
                g_host->input.mouse_pressed = i == 0 && click ? OPENRAC_MOUSE_LEFT : 0;
                const Report r = read_pad();

                // Note each d-pad or Cross button held.
                for (std::uint16_t b :
                     {kbm::kUp, kbm::kDown, kbm::kLeft, kbm::kRight, kbm::kCross}) {
                    // The button is held this tick.
                    if (r.held(b)) {
                        seen.push_back(b);
                    }
                }
            }

            return seen;
        };

        // Over the last cell (the second row's third, its icon from (228, 208)): down, right,
        // right.
        std::vector<std::uint16_t> want = {kbm::kDown, kbm::kRight, kbm::kRight};
        CHECK(presses(260.0f, 240.0f, true, false, 8) == want);
        // There (the game moved the selection): nothing more, however far the mouse moves on it.
        at<std::int32_t>(grid + kbm::kGridSelected) = 5;
        CHECK(presses(290.0f, 210.0f, true, false, 6).empty());
        // In the gap between the first row's first two cells, nearer the second: one step there
        // from the first.
        at<std::int32_t>(grid + kbm::kGridSelected) = 0;
        want = {kbm::kRight};
        CHECK(presses(145.0f, 160.0f, true, false, 6) == want);
        // A click on the first cell from the last: up, left, left, then Cross.
        at<std::int32_t>(grid + kbm::kGridSelected) = 5;
        want = {kbm::kUp, kbm::kLeft, kbm::kLeft, kbm::kCross};
        CHECK(presses(90.0f, 150.0f, true, true, 12) == want);
        at<std::uint32_t>(menus + 4u) = 0;
        tick();
        read_pad();
    }
}

// Checks that Space, Enter, Escape and a click skip a scene, a movie or a slideshow, and press in
// the ship.
void test_scenes_movies_and_the_ship_skip() {
    const kbm::Disc& d = disc();
    Report r{};

    // A cutscene (mode 2), a movie (1), a slideshow (7): Space, Enter, Escape and a click are
    // Start.
    for (int mode : {2, 1, 7}) {
        at<std::int32_t>(d.level_mode) = mode;

        // Space, Enter and Escape in turn.
        for (int key : {44, 40, 41}) {
            tick();
            r = read_pad();
            tick();
            press(key);
            r = read_pad();
            CHECK(r.held(kbm::kStart) && !r.held(kbm::kCross));
        }
    }

    // The ship (mode 6): Start and Cross.
    at<std::int32_t>(d.level_mode) = 6;
    tick();
    r = read_pad();
    tick();
    press(44);
    r = read_pad();
    CHECK(r.held(kbm::kStart) && r.held(kbm::kCross));
}

// Checks that Escape goes on and Backspace quits in the "Quit?" dialog.
void test_a_quit_dialog_goes_on_or_quits() {
    const kbm::Disc& d = disc();
    Report r{};

    // A "Quit?" dialog (mode 4, kind 1): Escape goes on (Cross), Backspace quits (Triangle).
    at<std::int32_t>(d.level_mode) = 4;
    at<std::int32_t>(d.dialog) = 1;
    tick();
    r = read_pad();
    tick();
    press(41);
    r = read_pad();
    CHECK(r.held(kbm::kCross) && !r.held(kbm::kTriangle));
    tick();
    press(42);
    r = read_pad();
    CHECK(r.held(kbm::kTriangle));
}

// Checks the title's and the main menu's keys, and WASD as the left stick everywhere.
void test_the_title_and_its_main_menu() {
    const kbm::Disc& d = disc();
    Report r{};

    // The title, with no level (the boot program's mode word): Space, Enter or Escape is Start;
    // its main menu (mode 3 there) has the menu's keys; WASD is the left stick everywhere.
    g_host->level = -1;
    at<std::int32_t>(d.boot_mode) = 0;
    tick();
    r = read_pad();
    tick();
    press(40);
    r = read_pad();
    CHECK(r.held(kbm::kStart) && g_host->capture == 0);
    at<std::int32_t>(d.boot_mode) = 3;
    tick();
    r = read_pad();
    tick();
    press(42);
    hold(22);  // S
    r = read_pad();
    CHECK(r.held(kbm::kTriangle) && kbm::game_axis(r.bytes[5]) > 0.0f);
    g_host->level = 0;
    at<std::int32_t>(d.level_mode) = 0;
    tick();
    hold(82);  // in play the menu's arrows do nothing
    r = read_pad();
    CHECK(!r.held(kbm::kUp) && g_host->capture == 1);
}

// Checks that the keys are left to the port's overlay while it is open.
void test_the_overlay_takes_the_keys() {
    Report r{};

    // The port's overlay open: the keys are left to it.
    tick();
    g_host->input.overlay = 1;
    press(44);
    r = read_pad();
    CHECK(!r.held(kbm::kCross) && g_host->capture == 0);
}

// Checks that Q swings the camera behind Ratchet through the follow camera's hook for a while.
void test_q_swings_the_camera_behind_ratchet() {
    const std::uint32_t hero = disc().hero;

    // Q: the camera swings behind Ratchet, through the follow camera's hook, for a while.
    at<float>(hero + kbm::kHeroFacing) = 0.0f;
    tick();
    press(20);
    read_pad();
    auto camera = reinterpret_cast<void (*)(std::uint32_t)>(g_host->camera_hook);
    g_host->camera_calls = 0;
    g_host->look_calls = 0;
    camera(0x1000);
    CHECK(g_host->camera_calls == 1 && g_host->look_calls == 1 && g_host->look_x == 1.0f);

    // Forty more calls: the swing lasts thirty.
    for (int i = 0; i < 40; ++i) {
        camera(0x1000);
    }

    CHECK(g_host->camera_calls == 41 && g_host->look_calls == 30);
    CHECK(g_host->stack_pointer == 0x001F0000u);
}

// Runs every check against the library at `library`, in order: each starts from the game state
// the one before left.
void run(const char* library) {
    test_the_library_loads_on_each_disc_and_refuses_another_game(library);
    test_keys_and_the_mouse_press_the_pad_and_turn_the_camera();
    test_the_quick_select_ring_follows_the_mouse();
    test_number_keys_put_the_quick_selects_items_in_hand();
    test_the_pause_screen_keys_and_clicks();
    test_taps_key_repeat_and_the_wheel_in_menus();
    test_the_mouse_over_the_pause_menus_page();
    test_the_mouse_over_the_quick_select_page();
    test_the_mouse_over_an_item_grid();
    test_scenes_movies_and_the_ship_skip();
    test_a_quit_dialog_goes_on_or_quits();
    test_the_title_and_its_main_menu();
    test_the_overlay_takes_the_keys();
    test_q_swings_the_camera_behind_ratchet();
}

}  // namespace

int main(int argc, char** argv) {
    // Without a library path, say how to run the test.
    if (argc < 2) {
        std::fprintf(stderr, "usage: load_test <library>\n");
        return 2;
    }

    Host host;

    g_host = &host;
    run(argv[1]);
    std::puts("keyboard-mouse load: all passed");
    return 0;
}
