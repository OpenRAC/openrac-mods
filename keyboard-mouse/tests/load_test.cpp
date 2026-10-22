// SPDX-License-Identifier: GPL-3.0-or-later
// The keyboard and mouse mod's library as the port loads it, against a host table made here
// (openrac_mod.h, interface 1.2): it reports its id and interface, takes the keyboard and the
// mouse, its pad callback and the follow camera's hook point, and finds the game's functions by
// each disc's names. Then, on a game memory made here, tick by tick: keys and buttons into the
// report, the mouse into the right stick, the Quick Select held by right click and pointed by the
// mouse, a number key taking a slot's item, the cursor captured in play and let go on the pause
// screen, menu keys only there, and the camera's reset asking to look along Ratchet's facing.
//
// Run with the library's path: load_test <keyboard_mouse.dylib|.so|.dll>.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../src/discs.h"
#include "kbm.hpp"
#include "openrac/openrac_mod.h"

#if defined(_WIN32)
#include <windows.h>
static void* open_library(const char* path) {
    return reinterpret_cast<void*>(LoadLibraryA(path));
}
static void* symbol(void* lib, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
}
#else
#include <dlfcn.h>
static void* open_library(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}
static void* symbol(void* lib, const char* name) {
    return dlsym(lib, name);
}
#endif

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

namespace {

constexpr std::uint32_t kMemorySize = 0x00200000u;
std::vector<std::uint8_t> g_memory(kMemorySize);
std::string g_failed;
std::string g_version = "pal";
int g_level = 0;
openrac_input g_input{};
std::uint32_t g_taken = 0;
int g_capture = -1;
openrac_pad_fn g_pad = nullptr;
void* g_pad_user = nullptr;
void* g_camera_hook = nullptr;
std::vector<std::string> g_functions;  // the names the mod looked for
int g_camera_calls = 0;
int g_look_calls = 0;
float g_look_x = 0.0f;
openrac_gaddr g_sp = 0x001F0000u;

const kbm_disc& disc() {
    return g_version == "pal" ? k_kbm_discs[0] : k_kbm_discs[1];
}

template <class T>
T& at(std::uint32_t address) {
    return *reinterpret_cast<T*>(g_memory.data() + address);
}

void game_camera(std::uint32_t) {
    ++g_camera_calls;
}
float game_cos(float a) {
    return std::cos(a);
}
float game_sin(float a) {
    return std::sin(a);
}
void game_look_along(openrac_gaddr direction, float, float) {
    ++g_look_calls;
    std::memcpy(&g_look_x, g_memory.data() + direction, 4);
}

void host_log(openrac_mod*, int level, const char* text) {
    std::printf("[%d] keyboard-mouse: %s\n", level, text);
}
void host_fail(openrac_mod*, const char* reason) {
    g_failed = reason;
}
int setting_bool(openrac_mod*, const char* key) {
    return std::strcmp(key, "capture_cursor") == 0 || std::strcmp(key, "menu_mouse") == 0 ? 1 : 0;
}
std::int64_t setting_int(openrac_mod*, const char*) {
    return 0;
}
double setting_float(openrac_mod*, const char* key) {
    if (std::strcmp(key, "mouse_sensitivity") == 0) return 1.0;
    if (std::strcmp(key, "walk_speed") == 0) return 0.5;
    if (std::strcmp(key, "stick_ramp_ms") == 0) return 0.0;  // at once, for the test
    if (std::strcmp(key, "quick_select_dead_zone") == 0) return 30.0;
    return 0.0;
}
// The bindings as the manifest's defaults, as the port hands a setting the player left alone.
const char* setting_string(openrac_mod*, const char* key) {
    for (const kbm::ActionInfo& a : kbm::kActions) {
        if (std::strcmp(a.key, key) == 0) {
            return a.fallback;
        }
    }
    return std::strcmp(key, "camera_tilt") == 0 ? "hold" : "";
}
int host_hook(openrac_mod*, const char* name, const char* signature, void* replacement, void** original) {
    CHECK(std::strcmp(name, "rac1.camera.follow.apply_overrides") == 0
          || std::strcmp(name, "FollowCamera_ApplyOverrides") == 0);
    CHECK(std::strcmp(signature, "void(u32)") == 0);
    g_camera_hook = replacement;
    *original = reinterpret_cast<void*>(&game_camera);
    return OPENRAC_HOOK_OK;
}
int host_on_frame(openrac_mod*, openrac_frame_fn, void*) {
    return 0;
}
int host_on_pad(openrac_mod*, openrac_pad_fn fn, void* user) {
    g_pad = fn;
    g_pad_user = user;
    return 0;
}
void* host_function(const char* name) {
    g_functions.emplace_back(name);
    const kbm_disc& d = disc();
    if (std::strcmp(name, d.fast_cos) == 0) return reinterpret_cast<void*>(&game_cos);
    if (std::strcmp(name, d.fast_sin) == 0) return reinterpret_cast<void*>(&game_sin);
    if (std::strcmp(name, d.camera_look_along) == 0) return reinterpret_cast<void*>(&game_look_along);
    return nullptr;
}
void* host_function_at(openrac_gaddr) {
    return nullptr;
}
openrac_gaddr host_relocate(openrac_gaddr a) {
    return a;
}
openrac_gaddr host_relocate_level(int, openrac_gaddr a) {
    return a;
}
int host_loaded_level() {
    return g_level;
}
openrac_gaddr host_stack_push(std::uint32_t size, openrac_gaddr* saved) {
    *saved = g_sp;
    g_sp -= (size + 15u) & ~15u;
    return g_sp;
}
void host_stack_pop(openrac_gaddr saved) {
    g_sp = saved;
}
const openrac_input* host_input() {
    return &g_input;
}
int host_take_input(openrac_mod*, std::uint32_t devices) {
    g_taken |= devices;
    return 0;
}
void host_capture_mouse(openrac_mod*, int capture) {
    g_capture = capture;
}

openrac_api make_api() {
    openrac_api api{};
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = "rac1";
    api.version = g_version.c_str();
    api.openrac_version = "0.0.0";
    api.platform = "test";
    api.memory = g_memory.data();
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
    const std::uint32_t t = g_input.tick + 1;
    g_input = openrac_input{};
    g_input.size = sizeof g_input;
    g_input.tick = t;
    g_input.focused = 1;
    g_input.captured = 1;
}
void hold(int key) {
    g_input.keys[key] = OPENRAC_KEY_HELD;
}
void press(int key) {
    g_input.keys[key] = OPENRAC_KEY_HELD | OPENRAC_KEY_PRESSED;
}

struct Report {
    std::uint8_t bytes[18];
    std::uint16_t buttons() const { return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8)); }
    bool held(std::uint16_t button) const { return (buttons() & button) == 0; }
};

// The pad's report as the port hands it with nothing held, through the mod's callback.
Report read_pad() {
    Report r{};
    r.bytes[0] = r.bytes[1] = 0xFF;
    r.bytes[2] = r.bytes[3] = r.bytes[4] = r.bytes[5] = 0x80;
    g_pad(g_pad_user, 0, r.bytes, 18);
    return r;
}

void run(const char* library) {
    void* lib = open_library(library);
    CHECK(lib != nullptr);
    auto query = reinterpret_cast<openrac_mod_query_fn>(symbol(lib, OPENRAC_MOD_QUERY));
    auto load = reinterpret_cast<openrac_mod_load_fn>(symbol(lib, OPENRAC_MOD_LOAD));
    CHECK(query != nullptr && load != nullptr);
    openrac_mod_info info{};
    info.size = sizeof info;
    query(&info);
    CHECK(std::strcmp(info.id, "keyboard-mouse") == 0 && info.api_major == 1 && info.api_minor == 3);

    for (const char* version : {"ntsc", "pal"}) {
        g_version = version;
        g_functions.clear();
        g_taken = 0;
        const openrac_api api = make_api();
        CHECK(load(&api, reinterpret_cast<openrac_mod*>(1)) == 0);
        CHECK(g_failed.empty());
        CHECK(g_taken == (OPENRAC_INPUT_KEYBOARD | OPENRAC_INPUT_MOUSE));
        CHECK(g_pad != nullptr && g_camera_hook != nullptr);
        CHECK(g_functions.size() == 3 && g_functions[2] == disc().camera_look_along);
    }
    // Another game is refused.
    {
        openrac_api other = make_api();
        other.game = "rac2";
        g_failed.clear();
        CHECK(load(&other, reinterpret_cast<openrac_mod*>(1)) != 0 && !g_failed.empty());
        g_failed.clear();
        const openrac_api api = make_api();
        CHECK(load(&api, reinterpret_cast<openrac_mod*>(1)) == 0);
    }

    const kbm_disc& d = disc();
    at<std::int32_t>(d.level_mode) = 0;  // in play
    const std::uint32_t hero = d.hero;

    // W and Space: the left stick up, Cross held; the cursor captured.
    tick();
    hold(26);
    hold(44);
    Report r = read_pad();
    CHECK(kbm::game_axis(r.bytes[5]) == -1.0f && r.bytes[4] == 0x80);
    CHECK(r.held(kbm::kCross) && !r.held(kbm::kCircle));
    CHECK(g_capture == 1);
    // The same tick read again (the second port, a second read): the same report.
    Report again = read_pad();
    CHECK(std::memcmp(again.bytes, r.bytes, 18) == 0);

    // Left click fires; the mouse moving right turns the camera right.
    tick();
    g_input.mouse_held = OPENRAC_MOUSE_LEFT;
    g_input.mouse_dx = 24.0f;
    r = read_pad();
    tick();
    g_input.mouse_held = OPENRAC_MOUSE_LEFT;
    g_input.mouse_dx = 24.0f;
    r = read_pad();
    CHECK(r.held(kbm::kCircle));
    CHECK(kbm::game_axis(r.bytes[2]) == 1.0f);
    // Not captured (the window just got focus back): the mouse does nothing.
    tick();
    g_input.captured = 0;
    g_input.mouse_held = OPENRAC_MOUSE_LEFT;
    g_input.mouse_dx = 50.0f;
    r = read_pad();
    CHECK(!r.held(kbm::kCircle) && r.bytes[2] == 0x80);

    // Right click held: Triangle held, the mouse pointing the ring's slot by the left stick.
    tick();
    g_input.mouse_held = OPENRAC_MOUSE_RIGHT;
    g_input.mouse_pressed = OPENRAC_MOUSE_RIGHT;
    hold(26);  // W held: the ring takes the left stick over
    r = read_pad();
    CHECK(r.held(kbm::kTriangle) && r.bytes[4] == 0x80 && r.bytes[5] == 0x80);
    tick();
    g_input.mouse_held = OPENRAC_MOUSE_RIGHT;
    g_input.mouse_dx = 45.0f;
    g_input.mouse_dy = 45.0f;
    r = read_pad();
    CHECK(r.held(kbm::kTriangle));
    CHECK(kbm::ring_slot(kbm::game_axis(r.bytes[4]), kbm::game_axis(r.bytes[5])) == 3);
    CHECK(r.bytes[2] == 0x80);  // the camera stays still while the ring is open
    tick();  // let go: Triangle up, the game takes the slot
    r = read_pad();
    CHECK(!r.held(kbm::kTriangle) && r.bytes[4] == 0x80);

    // 3: the item in the Quick Select's third slot, put in hand; the wheel goes on from it.
    for (int i = 0; i < 8; ++i) {
        at<std::int32_t>(d.quick_select + 4u * static_cast<std::uint32_t>(i)) = i == 2 ? 0x0B : i == 5 ? 0x15 : 0;
    }
    at<std::int32_t>(hero + KBM_HERO_ITEM) = 0x08;
    at<std::int32_t>(hero + KBM_HERO_PENDING) = 0;
    tick();
    press(32);
    read_pad();
    CHECK(at<std::int32_t>(hero + KBM_HERO_PENDING) == 0x0B);
    tick();
    g_input.wheel_y = -1.0f;  // down: the next
    read_pad();
    CHECK(at<std::int32_t>(hero + KBM_HERO_PENDING) == 0x15);
    // In a vehicle the number keys do nothing.
    at<std::int32_t>(hero + KBM_HERO_PENDING) = 0;
    at<std::uint8_t>(hero + KBM_HERO_VEHICLE) = 1;
    tick();
    press(32);
    read_pad();
    CHECK(at<std::int32_t>(hero + KBM_HERO_PENDING) == 0);
    at<std::uint8_t>(hero + KBM_HERO_VEHICLE) = 0;

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
    CHECK(!r.held(kbm::kTriangle) && !r.held(kbm::kStart) && g_capture == 0);
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
    g_input.captured = 0;
    g_input.pointer_inside = 1;
    g_input.mouse_held = g_input.mouse_pressed = OPENRAC_MOUSE_LEFT;
    r = read_pad();
    CHECK(r.held(kbm::kCross) && !r.held(kbm::kCircle));
    tick();
    g_input.captured = 0;
    g_input.mouse_held = OPENRAC_MOUSE_LEFT;  // the cursor off the game's picture: nothing
    g_input.pointer_inside = 0;
    r = read_pad();
    CHECK(!r.held(kbm::kCross));
    // A key tapped between two ticks (down and up again) still presses for one.
    tick();
    g_input.keys[40] = OPENRAC_KEY_PRESSED;
    r = read_pad();
    CHECK(r.held(kbm::kCross));
    // A held arrow repeats: let go one tick in six after 24.
    int ups = 0;
    bool was = false;
    for (int i = 0; i < 60; ++i) {
        tick();
        hold(82);
        r = read_pad();
        if (r.held(kbm::kUp) && !was) {
            ++ups;
        }
        was = r.held(kbm::kUp);
    }
    CHECK(ups == 6);  // the press, then once after each tick let go (at 30, 36, 42, 48 and 54)
    // The wheel: each notch one press, let go between.
    tick();
    g_input.pointer_inside = 1;
    g_input.captured = 0;
    g_input.wheel_y = -1.0f;
    r = read_pad();
    CHECK(r.held(kbm::kDown));
    tick();
    g_input.pointer_inside = 1;
    g_input.captured = 0;
    g_input.wheel_y = -1.0f;
    r = read_pad();
    CHECK(!r.held(kbm::kDown));  // let go between
    tick();
    r = read_pad();
    CHECK(r.held(kbm::kDown));

    // The pause menu's page under the mouse: over an item not focused, the focus moves there (the
    // page's pending focus, as the d-pad asks for it); over the focused list's third row, the d-pad
    // steps the selection down to it.
    {
        const std::uint32_t menus = d.page_menu, page = 0x00180000u, w0 = 0x00181000u, w1 = 0x00182000u,
                            items = 0x00183000u;
        at<std::uint32_t>(menus + 4u) = page;
        at<std::uint32_t>(page + KBM_PAGE_FOCUS) = w0;
        at<std::uint32_t>(page + KBM_PAGE_WIDGETS) = w0;
        at<std::uint32_t>(page + KBM_PAGE_WIDGETS + 4u) = w1;
        at<std::uint32_t>(page + KBM_PAGE_PENDING) = 0;
        const std::int32_t r0[4] = {100, 100, 200, 100};  // a list of three rows: 25 apart from y 125
        const std::int32_t r1[4] = {100, 250, 200, 40};
        std::memcpy(g_memory.data() + w0 + KBM_WIDGET_RECT, r0, 16);
        std::memcpy(g_memory.data() + w1 + KBM_WIDGET_RECT, r1, 16);
        at<std::uint32_t>(w0 + KBM_LIST_ITEMS) = items;
        at<std::int32_t>(w0 + KBM_LIST_SELECTED) = 0;
        for (int i = 0; i < 3; ++i) {
            at<std::int16_t>(items + KBM_LIST_ITEM_SIZE * static_cast<std::uint32_t>(i)) = static_cast<std::int16_t>(10 + i);
        }
        const auto point = [](float gx, float gy) {
            tick();
            g_input.captured = 0;
            g_input.pointer_inside = 1;
            g_input.pointer_x = gx / kbm::kScreenWidth;
            g_input.pointer_y = gy / kbm::kScreenHeight;
            g_input.mouse_dx = 1.0f;
        };
        point(150.0f, 270.0f);  // over the second item
        read_pad();
        CHECK(at<std::uint32_t>(page + KBM_PAGE_PENDING) == w1);
        at<std::uint32_t>(page + KBM_PAGE_PENDING) = 0;
        point(150.0f, 176.0f);  // over the list's third row
        r = read_pad();
        CHECK(r.held(kbm::kDown));
        at<std::int32_t>(w0 + KBM_LIST_SELECTED) = 1;
        tick();
        g_input.captured = 0;
        r = read_pad();  // let go
        point(150.0f, 176.0f);
        r = read_pad();
        CHECK(r.held(kbm::kDown));
        at<std::int32_t>(w0 + KBM_LIST_SELECTED) = 2;
        tick();
        r = read_pad();
        point(150.0f, 177.0f);  // on it: nothing more
        r = read_pad();
        CHECK(!r.held(kbm::kDown) && !r.held(kbm::kUp));
        // A click on the other item: focus first, then Cross.
        point(150.0f, 270.0f);
        g_input.mouse_held = g_input.mouse_pressed = OPENRAC_MOUSE_LEFT;
        r = read_pad();
        CHECK(!r.held(kbm::kCross) && at<std::uint32_t>(page + KBM_PAGE_PENDING) == w1);
        at<std::uint32_t>(page + KBM_PAGE_FOCUS) = w1;
        at<std::uint32_t>(page + KBM_PAGE_PENDING) = 0;
        bool crossed = false;
        for (int i = 0; i < 6 && !crossed; ++i) {
            tick();
            g_input.captured = 0;
            crossed = read_pad().held(kbm::kCross);
        }
        CHECK(crossed);
        // The map focused (its handler DrawMapScreen): Enter and a click travel (Circle), Space
        // turns the page (Cross), Escape goes back; E is Circle on any page.
        at<std::uint32_t>(w1 + 4u) = d.map_screen;
        tick();
        g_input.captured = 0;
        read_pad();
        tick();
        g_input.captured = 0;
        press(40);
        r = read_pad();
        CHECK(r.held(kbm::kCircle) && !r.held(kbm::kCross));
        tick();
        g_input.captured = 0;
        press(44);
        r = read_pad();
        CHECK(r.held(kbm::kCross) && !r.held(kbm::kCircle));
        tick();
        g_input.captured = 0;
        g_input.pointer_inside = 1;
        g_input.mouse_held = g_input.mouse_pressed = OPENRAC_MOUSE_LEFT;
        r = read_pad();
        CHECK(r.held(kbm::kCircle));
        tick();
        r = read_pad();
        tick();
        press(41);
        r = read_pad();
        CHECK(r.held(kbm::kTriangle));
        at<std::uint32_t>(w1 + 4u) = 0;
        tick();
        r = read_pad();
        tick();
        press(8);  // E
        r = read_pad();
        CHECK(r.held(kbm::kCircle) && !r.held(kbm::kCross));
        at<std::uint32_t>(menus + 4u) = 0;
    }

    // A cutscene (mode 2), a movie (1), a slideshow (7): Space, Enter, Escape and a click are Start.
    for (int mode : {2, 1, 7}) {
        at<std::int32_t>(d.level_mode) = mode;
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

    // The title, with no level (the boot program's mode word): Space, Enter or Escape is Start;
    // its main menu (mode 3 there) has the menu's keys; WASD is the left stick everywhere.
    g_level = -1;
    at<std::int32_t>(d.boot_mode) = 0;
    tick();
    r = read_pad();
    tick();
    press(40);
    r = read_pad();
    CHECK(r.held(kbm::kStart) && g_capture == 0);
    at<std::int32_t>(d.boot_mode) = 3;
    tick();
    r = read_pad();
    tick();
    press(42);
    hold(22);  // S
    r = read_pad();
    CHECK(r.held(kbm::kTriangle) && kbm::game_axis(r.bytes[5]) > 0.0f);
    g_level = 0;
    at<std::int32_t>(d.level_mode) = 0;
    tick();
    hold(82);  // in play the menu's arrows do nothing
    r = read_pad();
    CHECK(!r.held(kbm::kUp) && g_capture == 1);
    // The port's overlay open: the keys are left to it.
    tick();
    g_input.overlay = 1;
    press(44);
    r = read_pad();
    CHECK(!r.held(kbm::kCross) && g_capture == 0);

    // Q: the camera swings behind Ratchet, through the follow camera's hook, for a while.
    at<float>(hero + KBM_HERO_FACING) = 0.0f;
    tick();
    press(20);
    read_pad();
    auto camera = reinterpret_cast<void (*)(std::uint32_t)>(g_camera_hook);
    g_camera_calls = g_look_calls = 0;
    camera(0x1000);
    CHECK(g_camera_calls == 1 && g_look_calls == 1 && g_look_x == 1.0f);
    for (int i = 0; i < 40; ++i) {
        camera(0x1000);
    }
    CHECK(g_camera_calls == 41 && g_look_calls == 30);
    CHECK(g_sp == 0x001F0000u);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: load_test <library>\n");
        return 2;
    }
    run(argv[1]);
    std::puts("keyboard-mouse load: all passed");
    return 0;
}
