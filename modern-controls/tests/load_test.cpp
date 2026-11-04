// SPDX-License-Identifier: GPL-3.0-or-later
// Load the real binary into a synthetic host: no game assets or running save needed.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

#include "openrac/openrac_mod.h"
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
std::vector<uint8_t> memory(0x2000000);
openrac_input input{};
openrac_pad_fn pad;
void* pad_user;
openrac_frame_fn frame;
void* frame_user;
void (*camera_hook)(uint32_t);
void (*speed_hook)(float, float);
int phase = OPENRAC_PLAY_PLAYING;
float speed_up, speed_down;
std::string failure;

template <class T>
T& at(uint32_t p) {
    return *reinterpret_cast<T*>(memory.data() + p);
}

struct V {
    float x, y, z;
};

void camera_original(uint32_t) {}

void speed_original(float up, float down) {
    speed_up = up;
    speed_down = down;
}

int hook(openrac_mod*, const char* id, const char*, void* fn, void** original) {
    if (std::strcmp(id, "rac1.camera.follow.apply_overrides") == 0) {
        camera_hook = reinterpret_cast<decltype(camera_hook)>(fn);
        *original = reinterpret_cast<void*>(camera_original);
    } else if (std::strcmp(id, "rac1.player.approach_speed") == 0) {
        speed_hook = reinterpret_cast<decltype(speed_hook)>(fn);
        *original = reinterpret_cast<void*>(speed_original);
    } else {
        return OPENRAC_HOOK_UNKNOWN;
    }
    return 0;
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    auto lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(lib);
    auto load =
        reinterpret_cast<int (*)(const openrac_api*, openrac_mod*)>(dlsym(lib, "openrac_mod_load"));
    auto unload = reinterpret_cast<void (*)()>(dlsym(lib, "openrac_mod_unload"));
    CHECK(load && unload);
    for (bool pal : {true, false}) {
        std::fill(memory.begin(), memory.end(), 0);
        failure.clear();
        input = {};
        input.focused = 1;
        input.captured = 1;
        openrac_api api{};
        api.size = offsetof(openrac_api, data_dir);
        api.api_major = 1;
        api.api_minor = 10;
        api.game = "rac1";
        api.version = pal ? "pal" : "ntsc";
        api.memory = memory.data();
        api.log = [](openrac_mod*, int, const char*) {
        };
        api.fail = [](openrac_mod*, const char* msg) {
            failure = msg;
        };
        api.setting_bool = [](openrac_mod*, const char* key) -> int {
            return std::strcmp(key, "invert_y") != 0;
        };
        api.setting_int = [](openrac_mod*, const char* key) -> int64_t {
            if (!std::strcmp(key, "mouse_x") || !std::strcmp(key, "mouse_y")) {
                return 150;
            }
            if (!std::strcmp(key, "stick_x")) {
                return 240;
            }
            if (!std::strcmp(key, "stick_y")) {
                return 150;
            }
            if (!std::strcmp(key, "deadzone")) {
                return 10;
            }
            return 175;
        };
        api.hook = hook;
        api.relocate_data = [](uint32_t p) {
            return p;
        };
        api.relocate_level_data = [](int, uint32_t p) {
            return p;
        };
        api.loaded_level = []() {
            return 0;
        };
        api.input = []() -> const openrac_input* {
            return &input;
        };
        api.take_input = [](openrac_mod*, uint32_t) {
            return 0;
        };
        api.capture_mouse = [](openrac_mod*, int value) {
            input.captured = value;
        };
        api.on_pad = [](openrac_mod*, openrac_pad_fn f, void* u) {
            pad = f;
            pad_user = u;
            return 0;
        };
        api.on_frame = [](openrac_mod*, openrac_frame_fn f, void* u) {
            frame = f;
            frame_user = u;
            return 0;
        };
        api.play_status = [](openrac_play_status* s) {
            s->phase = phase;
            s->level = 0;
            return 0;
        };
        CHECK(load(&api, reinterpret_cast<openrac_mod*>(1)) == 0);
        CHECK(failure.empty());
        const uint32_t cameras = pal ? 0x166D80 : 0x166C80;
        constexpr uint32_t camera = 0x100000, state = 0x110000;
        at<uint32_t>(cameras + 0x180) = camera;
        at<V>(cameras + 0x1b0) = {0, 0, 1};
        at<uint32_t>(camera + 0x70) = state;
        at<V>(state + 0x130) = {4, 0, 0};
        at<V>(state + 0x140) = {4, 0, 0};
        at<V>(state) = {4, 0, 0};
        uint8_t report[6] = {255, 255, 255, 128, 128, 128};
        input.tick = 1;
        pad(pad_user, 0, report, 6);
        CHECK(report[2] == 128);
        camera_hook(camera);
        CHECK(at<V>(state + 0x130).y < -.1f);
        CHECK(
            std::abs(
                at<V>(state + 0x130).x * at<V>(state + 0x130).x
                + at<V>(state + 0x130).y * at<V>(state + 0x130).y - 16
            )
            < .001f
        );
        const float once = at<V>(state + 0x130).y;
        camera_hook(camera);
        CHECK(at<V>(state + 0x130).y == once);
        // Mouse deltas are proportional, consumed once even when the pad is polled twice.
        input.tick++;
        input.mouse_dx = 100;
        pad(pad_user, 0, report, 6);
        pad(pad_user, 0, report, 6);
        camera_hook(camera);
        CHECK(at<V>(state + 0x130).y < once);
        input.tick++;
        input.mouse_dx = 0;
        input.mouse_dy = 30;
        pad(pad_user, 0, report, 6);
        camera_hook(camera);
        CHECK(at<V>(state + 0x130).z > 0);
        speed_hook(.2f, .3f);
        CHECK(std::abs(speed_up - .35f) < .0001f && std::abs(speed_down - .525f) < .0001f);
        // Camera-volume locks suppress both axes, even with large mouse motion.
        at<int32_t>(state + 0x1b8) = 3;
        input.tick++;
        input.mouse_dx = 100;
        input.mouse_dy = 100;
        const V locked = at<V>(state + 0x130);
        pad(pad_user, 0, report, 6);
        camera_hook(camera);
        CHECK(
            at<V>(state + 0x130).x == locked.x && at<V>(state + 0x130).y == locked.y
            && at<V>(state + 0x130).z == locked.z
        );
        at<int32_t>(state + 0x1b8) = 0;
        // Menus and lost focus leave physical reports and acceleration unchanged.
        phase = OPENRAC_PLAY_MENU;
        input.tick++;
        report[2] = 255;
        pad(pad_user, 0, report, 6);
        CHECK(report[2] == 255);
        speed_hook(.2f, .3f);
        CHECK(speed_up == .2f);
        frame(frame_user);
        CHECK(!input.captured);
        phase = OPENRAC_PLAY_PLAYING;
        input.focused = 0;
        input.tick++;
        pad(pad_user, 0, report, 6);
        CHECK(report[2] == 255);
        input.focused = 1;
        // Weapon selection and special camera modes retain their stick input.
        report[1] = 0xef;
        input.tick++;
        pad(pad_user, 0, report, 6);
        CHECK(report[2] == 255);
        report[1] = 255;
        at<int16_t>(camera + 0x86) = 7;
        input.tick++;
        pad(pad_user, 0, report, 6);
        CHECK(report[2] == 255);
        // First-person tilt, with bounded pitch, uses the direct path too.
        at<int16_t>(camera + 0x86) = 4;
        at<V>(camera) = {1, 0, 0};
        input.captured = 1;
        input.mouse_dy = 100000;
        input.tick++;
        pad(pad_user, 0, report, 6);
        CHECK(at<float>(state + 0x10) < 1.118f && at<float>(state + 0x10) > 1.116f);
        unload();
        phase = OPENRAC_PLAY_PLAYING;
    }
}
