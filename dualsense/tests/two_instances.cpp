// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The mod and the port each have their own copy of SDL3, and both open the same controller. This
// checks that the two can live in one process: the system's SDL3 (the port's, when the port uses
// the system's) and the mod's own static SDL3, which is inside the mod's library.
//
// Neither is linked here: the system's is opened with dlopen and used through dlsym, and the mod's
// comes with the library. So the two really are two copies.
//
// It needs a controller attached.
//
//   two_instances <the mod's library>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>

void* open_library(const char* path) {
    return reinterpret_cast<void*>(LoadLibraryA(path));
}

void* symbol(void* lib, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
}
#else
#include <dlfcn.h>

void* open_library(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

void* symbol(void* lib, const char* name) {
    return dlsym(lib, name);
}
#endif

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

#include "openrac/openrac_mod.h"

namespace {

std::string g_failed;

void host_log(openrac_mod*, int, const char* text) {
    std::printf("  the mod says: %s\n", text);
}

void host_fail(openrac_mod*, const char* reason) {
    g_failed = reason;
}

int setting_bool(openrac_mod*, const char*) {
    return 1;
}

std::int64_t setting_int(openrac_mod*, const char*) {
    return 100;
}

double setting_float(openrac_mod*, const char*) {
    return 0.0;
}

const char* setting_string(openrac_mod*, const char* key) {
    if (std::strcmp(key, "touchpad") == 0) {
        return "circle";
    }

    if (std::strcmp(key, "left_paddle") == 0) {
        return "triangle";
    }

    if (std::strcmp(key, "right_paddle") == 0) {
        return "square";
    }

    return "none";
}

int host_hook(openrac_mod*, const char*, const char*, void*, void**) {
    return OPENRAC_HOOK_OK;
}

int host_on_frame(openrac_mod*, openrac_frame_fn, void*) {
    return 0;
}

int host_on_pad(openrac_mod*, openrac_pad_fn, void*) {
    return 0;
}

void* host_function(const char*) {
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
    return -1;
}

openrac_gaddr host_stack_push(std::uint32_t size, openrac_gaddr* saved) {
    *saved = 0x001F0000u;
    return 0x001F0000u - size;
}

void host_stack_pop(openrac_gaddr) {}

const openrac_input* host_input() {
    return nullptr;
}

int host_take_input(openrac_mod*, std::uint32_t) {
    return 0;
}

void host_capture_mouse(openrac_mod*, int) {}

std::vector<std::uint8_t> g_memory(0x00200000u);

openrac_api make_api() {
    openrac_api api{};
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = "rac1";
    api.version = "pal";
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

// The system's SDL3, used the way the port uses it.
struct PortSdl {
    void* lib = nullptr;
    int (*init)(std::uint32_t) = nullptr;
    void (*quit)() = nullptr;
    std::uint32_t* (*get_gamepads)(int*) = nullptr;
    void* (*open_gamepad)(std::uint32_t) = nullptr;
    void (*close_gamepad)(void*) = nullptr;
    int (*get_gamepad_type)(void*) = nullptr;
    const char* (*get_gamepad_name)(void*) = nullptr;
    std::uint16_t (*get_gamepad_vendor)(void*) = nullptr;
    std::uint16_t (*get_gamepad_product)(void*) = nullptr;
    bool (*get_gamepad_button)(void*, int) = nullptr;
    bool (*set_gamepad_led)(void*, std::uint8_t, std::uint8_t, std::uint8_t) = nullptr;
    bool (*rumble_gamepad)(void*, std::uint16_t, std::uint16_t, std::uint32_t) = nullptr;
    const char* (*get_error)() = nullptr;
    void* pad = nullptr;
};

template <class T>
void bind(void* lib, const char* name, T& into) {
    into = reinterpret_cast<T>(symbol(lib, name));
}

bool open_port_sdl(PortSdl& sdl) {
#if defined(__APPLE__)
    sdl.lib = open_library("/opt/homebrew/lib/libSDL3.dylib");

    if (sdl.lib == nullptr) {
        sdl.lib = open_library("/usr/local/lib/libSDL3.dylib");
    }
#else
    sdl.lib = open_library("libSDL3.so.0");
#endif
    if (sdl.lib == nullptr) {
        std::fprintf(stderr, "the system's SDL3 would not open\n");
        return false;
    }

    bind(sdl.lib, "SDL_Init", sdl.init);
    bind(sdl.lib, "SDL_Quit", sdl.quit);
    bind(sdl.lib, "SDL_GetGamepads", sdl.get_gamepads);
    bind(sdl.lib, "SDL_OpenGamepad", sdl.open_gamepad);
    bind(sdl.lib, "SDL_CloseGamepad", sdl.close_gamepad);
    bind(sdl.lib, "SDL_GetGamepadType", sdl.get_gamepad_type);
    bind(sdl.lib, "SDL_GetGamepadName", sdl.get_gamepad_name);
    bind(sdl.lib, "SDL_GetGamepadVendor", sdl.get_gamepad_vendor);
    bind(sdl.lib, "SDL_GetGamepadProduct", sdl.get_gamepad_product);
    bind(sdl.lib, "SDL_GetGamepadButton", sdl.get_gamepad_button);
    bind(sdl.lib, "SDL_SetGamepadLED", sdl.set_gamepad_led);
    bind(sdl.lib, "SDL_RumbleGamepad", sdl.rumble_gamepad);
    bind(sdl.lib, "SDL_GetError", sdl.get_error);
    return sdl.init != nullptr && sdl.get_gamepads != nullptr && sdl.open_gamepad != nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: two_instances <the mod's library>\n");
        return 2;
    }

    // The port's copy: the system's SDL3, opened and used here.
    PortSdl port;

    if (!open_port_sdl(port)) {
        std::fprintf(stderr, "the system's SDL3 would not open\n");
        return 3;
    }

    CHECK(port.init(0x00002000u) != 0);  // SDL_INIT_GAMEPAD
    int count = 0;
    std::uint32_t* ids = port.get_gamepads(&count);
    std::printf("the port's copy: %d gamepad(s)\n", count);
    CHECK(ids != nullptr && count > 0);

    for (int i = 0; i < count; ++i) {
        void* candidate = port.open_gamepad(ids[i]);

        if (candidate == nullptr) {
            continue;
        }

        std::printf(
            "the port's copy: %s, type %d, vendor %04x product %04x\n",
            port.get_gamepad_name(candidate),
            port.get_gamepad_type(candidate),
            port.get_gamepad_vendor(candidate),
            port.get_gamepad_product(candidate)
        );

        if (port.get_gamepad_type(candidate) == 6) {  // SDL_GAMEPAD_TYPE_PS5
            port.pad = candidate;
            break;
        }

        port.close_gamepad(candidate);
    }

    CHECK(port.pad != nullptr);

    // The mod's copy: its own static SDL3, inside the library. Loading it and running its load
    // has to find the controller as well, with the port's copy holding it open.
    void* lib = open_library(argv[1]);
    CHECK(lib != nullptr);
    auto load = reinterpret_cast<int (*)(const void*, void*)>(symbol(lib, "openrac_mod_load"));
    auto unload = reinterpret_cast<void (*)(void)>(symbol(lib, "openrac_mod_unload"));
    CHECK(load != nullptr);

    // The host table the mod is given. The mod refuses without a controller, so the load
    // succeeding is the proof that its own SDL3 found the controller.
    std::printf("\nthe mod's copy: loading it, with the port's copy holding the controller\n");
    g_failed.clear();
    openrac_api api = make_api();
    const int result = load(&api, reinterpret_cast<openrac_mod*>(1));

    if (result != 0) {
        std::fprintf(stderr, "the mod refused: %s\n", g_failed.c_str());
        std::exit(4);
    }

    CHECK(g_failed.empty());

    // The port's copy still has the controller, and it still answers.
    std::printf(
        "the port's copy still has it: %s\n",
        port.get_gamepad_button(port.pad, 20) ? "the touchpad is down" : "the touchpad is up"
    );
    std::printf(
        "the port's copy can still light the bar: %s\n",
        port.set_gamepad_led(port.pad, 0, 0, 255) ? "yes" : "no"
    );

    if (unload != nullptr) {
        unload();
    }

    port.close_gamepad(port.pad);
    port.quit();
    std::puts("two instances: all passed");
    return 0;
}
