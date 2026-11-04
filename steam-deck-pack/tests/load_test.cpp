// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual native library through the SDK, including input ownership and UI events.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "font_bank.h"
#include "openrac/openrac_mod.h"
#include "paint.h"
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
            std::fprintf(stderr, "%d: %s\n", __LINE__, #c);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

namespace {
std::vector<std::uint8_t> memory(CONTROLLER_ICONS_RAM);
openrac_input input{};
openrac_pad_fn pad = nullptr;
openrac_frame_fn frame = nullptr;
void* pad_user = nullptr;
void* frame_user = nullptr;
int phase = OPENRAC_PLAY_PLAYING;
int capture = -1;
std::uint32_t taken = 0;
std::string failure;
std::string directory;

struct Page {
    openrac_ui_fn fn;
    void* user;
    std::vector<std::string> labels;
    std::vector<std::uint32_t> values;
};

std::vector<Page> pages;
bool reject_pause = false;
int warnings = 0;

void log(openrac_mod*, int level, const char*) {
    if (level == OPENRAC_LOG_WARN) {
        ++warnings;
    }
}

void fail(openrac_mod*, const char* why) {
    failure = why;
}

int setting_bool(openrac_mod*, const char* key) {
    return std::strcmp(key, "enabled") == 0;
}

std::int64_t setting_int(openrac_mod*, const char*) {
    return 100;
}

const char* data_dir(openrac_mod*) {
    return directory.c_str();
}

const openrac_input* get_input() {
    return &input;
}

bool bad_relocation = false;

std::uint32_t relocate(std::uint32_t address) {
    return bad_relocation ? CONTROLLER_ICONS_RAM : address;
}

// A generated font bank, not a game asset. FNV is a font fingerprint, not authentication:
// adjusting five unused palette bytes lets this fixture pass the same known-font check as the
// actual library, so its real on_frame path is tested without bundling disc textures.
constexpr std::uint32_t kBank = 0x01000000u;

controller_icons_fonts generated_fonts(const char* version) {
    const auto* disc = controller_icons_disc_for(version);
    CHECK(disc != nullptr);
    std::fill(memory.begin() + kBank, memory.begin() + kBank + 0x20000, 0);
    std::memcpy(memory.data() + disc->bank, &kBank, sizeof kBank);
    for (unsigned f = 0; f < 3; ++f) {
        const std::uint16_t texels = static_cast<std::uint16_t>((0xC00 + f * 0x8000) / 16);
        const std::uint16_t palette = 0x800 / 16;
        std::memcpy(memory.data() + disc->table + 16 * (f + 1) + 8, &texels, 2);
        std::memcpy(memory.data() + disc->table + 16 * (f + 1) + 10, &palette, 2);
    }
    controller_icons_fonts fonts{};
    CHECK(controller_icons_find_fonts(memory.data(), relocate, disc, &fonts));
    const std::uint8_t dark[4] = {40, 40, 40, 128};
    std::memcpy(fonts.palette, dark, 4);
    constexpr std::uint32_t prime = 16777619u, inverse = 0x359C449Bu;
    CHECK(prime * inverse == 1u);
    auto prefix = controller_icons_hash(fonts.fonts, fonts.palette);
    for (int i = 0; i < 5; ++i) {
        prefix *= inverse;
    }  // Current last five bytes are zero.
    const auto target = k_controller_icons_game_fonts[0];
    bool found = false;
    for (unsigned seed = 0; seed < 256 && !found; ++seed) {
        const auto first = (prefix ^ seed) * prime;
        std::unordered_map<std::uint32_t, unsigned> front;
        front.reserve(65536);
        for (unsigned pair = 0; pair < 65536; ++pair) {
            front.emplace((((first ^ (pair >> 8)) * prime) ^ (pair & 255)) * prime, pair);
        }
        for (unsigned pair = 0; pair < 65536; ++pair) {
            const auto back = (((target * inverse) ^ (pair & 255)) * inverse) ^ (pair >> 8);
            const auto it = front.find(back);
            if (it != front.end()) {
                const std::uint8_t patch[5] =
                    {static_cast<std::uint8_t>(seed),
                     static_cast<std::uint8_t>(it->second >> 8),
                     static_cast<std::uint8_t>(it->second),
                     static_cast<std::uint8_t>(pair >> 8),
                     static_cast<std::uint8_t>(pair)};
                std::memcpy(fonts.palette + 1019, patch, sizeof patch);
                found = true;
                break;
            }
        }
    }
    CHECK(found && controller_icons_hash(fonts.fonts, fonts.palette) == target);
    return fonts;
}

void check_deck_letters(const controller_icons_fonts& fonts) {
    for (int f = 0; f < 3; ++f) {
        for (unsigned cell = 0; cell < 192; cell += 24) {
            unsigned white = 0;
            for (unsigned y = 96; y < 112; ++y) {
                for (unsigned x = cell; x < cell + 24; ++x) {
                    const auto index = fonts.fonts[f][y * 256 + x];
                    const auto csm1 =
                        (index & ~0x18u) | ((index & 8u) << 1) | ((index & 0x10u) >> 1);
                    const auto* c = fonts.palette + 4 * csm1;
                    white += c[0] == 225 && c[1] == 225 && c[2] == 225;
                }
            }
            CHECK(white > 15);
        }
        CHECK(fonts.fonts[f][0] == 0);  // Ordinary text outside the icon cells stays unchanged.
    }
    CHECK(fonts.palette[0] == 40 && fonts.palette[1] == 40 && fonts.palette[2] == 40);
}

int take(openrac_mod*, std::uint32_t devices) {
    taken = devices;
    return 0;
}

void set_capture(openrac_mod*, int value) {
    capture = value;
    input.captured = value;
}

int on_pad(openrac_mod*, openrac_pad_fn fn, void* user) {
    pad = fn;
    pad_user = user;
    return 0;
}

int on_frame(openrac_mod*, openrac_frame_fn fn, void* user) {
    frame = fn;
    frame_user = user;
    return 0;
}

int play_status(openrac_play_status* out) {
    out->phase = phase;
    out->level = 1;
    return 0;
}

int ui_page(openrac_mod*, std::uint32_t, openrac_ui_fn fn, void* user) {
    pages.push_back({fn, user, {}});
    return static_cast<int>(pages.size()) - 1;
}

void check_text(const char* value) {
    CHECK(value != nullptr && std::strlen(value) <= OPENRAC_UI_TEXT_MAX);
}

int ui_set(openrac_mod*, int page, const openrac_ui_content* content) {
    CHECK(page >= 0 && static_cast<std::size_t>(page) < pages.size());
    CHECK(content->item_count <= OPENRAC_UI_ITEMS_MAX);
    check_text(content->title);
    pages[page].labels.clear();
    pages[page].values.clear();
    for (std::uint32_t i = 0; i < content->item_count; ++i) {
        const auto& item = content->items[i];
        check_text(item.label);
        pages[page].labels.push_back(item.label);
        pages[page].values.push_back(item.choice);
        for (std::uint32_t j = 0; j < item.choice_count; ++j) {
            check_text(item.choices[j]);
        }
    }
    return 0;
}

int ui_main(openrac_mod*, const char*, const char*, int) {
    CHECK(false);  // The pack must not add a top-level main-menu entry.
    return OPENRAC_HOOK_UNKNOWN;
}

int ui_pause(openrac_mod*, const char* label, int page) {
    CHECK(std::strcmp(label, "Steam Deck") == 0 && page == 0);
    return reject_pause ? OPENRAC_HOOK_UNKNOWN : 0;
}

int ui_open(openrac_mod*, int) {
    CHECK(false);  // There is no setup/help sub-page any more.
    return OPENRAC_HOOK_UNKNOWN;
}

void event(int page, std::uint32_t kind, int item = -1, int value = 0) {
    openrac_ui_event e{};
    e.size = sizeof e;
    e.kind = kind;
    e.item = item;
    e.value = value;
    pages[page].fn(pages[page].user, page, &e);
}

std::array<std::uint8_t, 8> report() {
    return {0xff, 0xff, 128, 128, 128, 128, 0, 0};
}
}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    auto* lib = open_library(argv[1]);
    CHECK(lib != nullptr);
    auto query = reinterpret_cast<void (*)(openrac_mod_info*)>(symbol(lib, "openrac_mod_query"));
    auto load = reinterpret_cast<int (*)(const openrac_api*, openrac_mod*)>(
        symbol(lib, "openrac_mod_load")
    );
    CHECK(query != nullptr && load != nullptr);
    openrac_mod_info info{};
    info.size = sizeof info;
    query(&info);
    CHECK(std::strcmp(info.id, "steam-deck-pack") == 0 && info.api_minor == 9);
    const auto temp =
        std::filesystem::temp_directory_path()
        / ("openrac-deck-test-" + std::to_string(reinterpret_cast<std::uintptr_t>(lib)));
    std::filesystem::create_directory(temp);
    directory = temp.string();
    openrac_api api{};
    api.size = sizeof api;
    api.api_major = 1;
    api.api_minor = 11;
    api.game = "rac1";
    api.version = "pal";
    api.openrac_version = "test";
    api.platform = "test";
    api.memory = memory.data();
    api.relocate_data = relocate;
    api.log = log;
    api.fail = fail;
    api.setting_bool = setting_bool;
    api.setting_int = setting_int;
    api.data_dir = data_dir;
    api.input = get_input;
    api.take_input = take;
    api.capture_mouse = set_capture;
    api.on_pad = on_pad;
    api.on_frame = on_frame;
    api.play_status = play_status;
    api.ui_page = ui_page;
    api.ui_set = ui_set;
    api.ui_main_item = ui_main;
    api.ui_pause_item = ui_pause;
    api.ui_open = ui_open;
    input.size = sizeof input;
    input.focused = 1;
    input.tick = 1;
    input.mouse_dx = 8;
    auto* handle = reinterpret_cast<openrac_mod*>(1);
    CHECK(load(&api, handle) == 0 && failure.empty());
    CHECK(taken == OPENRAC_INPUT_MOUSE && pad != nullptr && frame != nullptr);
    auto r = report();
    pad(pad_user, 0, r.data(), r.size());
    CHECK(capture == 1 && r[2] > 128 && r[0] == 255 && r[4] == 128);
    input.captured = 0;  // The window system may release capture independently.
    frame(frame_user);
    CHECK(input.captured == 1);
    r = report();
    pad(pad_user, 1, r.data(), r.size());
    CHECK(r[2] == 128);
    ++input.tick;
    input.mouse_dx = 0;
    r = report();
    pad(pad_user, 0, r.data(), r.size());
    CHECK(r[2] == 128);
    input.mouse_dx = 100;
    ++input.tick;
    r = report();
    r[2] = 0;
    pad(pad_user, 0, r.data(), r.size());
    CHECK(r[2] == 0);
    for (int blocked :
         {OPENRAC_PLAY_FRONT_END, OPENRAC_PLAY_LOADING, OPENRAC_PLAY_SCENE, OPENRAC_PLAY_MENU}) {
        phase = blocked;
        ++input.tick;
        r = report();
        pad(pad_user, 0, r.data(), r.size());
        CHECK(capture == 0 && r[2] == 128);
    }
    phase = OPENRAC_PLAY_PLAYING;
    input.overlay = 1;
    ++input.tick;
    r = report();
    pad(pad_user, 0, r.data(), r.size());
    CHECK(capture == 0 && r[2] == 128);
    input.overlay = 0;
    input.focused = 0;
    frame(frame_user);
    CHECK(capture == 0);
    input.focused = 1;
    auto fonts = generated_fonts("pal");
    event(0, OPENRAC_UI_CHANGE, 0, 0);  // Disabling gyro does not remove the built-in buttons.
    frame(frame_user);
    check_deck_letters(fonts);
    const auto painted = controller_icons_hash(fonts.fonts, fonts.palette);
    frame(frame_user);
    CHECK(controller_icons_hash(fonts.fonts, fonts.palette) == painted);  // Paint once per bank.
    fonts = generated_fonts("pal");
    fonts.fonts[0][96 * 256] = 1;  // An unknown replacement font must not be overwritten.
    const auto unknown = controller_icons_hash(fonts.fonts, fonts.palette);
    frame(frame_user);
    CHECK(controller_icons_hash(fonts.fonts, fonts.palette) == unknown);
    bad_relocation = true;
    frame(frame_user);  // Relocation outside the 32 MB must be ignored safely.
    bad_relocation = false;
    event(0, OPENRAC_UI_CHANGE, 0, 1);
    // Every UI string must fit the SDK's byte limit, in every PAL language.
    for (int language : {0, 2, 3, 4, 5}) {
        std::memcpy(memory.data() + 0x0015EE88u, &language, sizeof language);
        event(0, OPENRAC_UI_FRAME);
        CHECK(pages.size() == 1 && pages[0].labels.size() == 4);
    }
    event(0, OPENRAC_UI_CHANGE, 1, 7);
    event(0, OPENRAC_UI_CHANGE, 2, 1);
    event(0, OPENRAC_UI_CHANGE, 0, 0);
    std::ifstream saved(temp / "controls.ini");
    std::string saved_text((std::istreambuf_iterator<char>(saved)), {});
    CHECK(saved_text.find("sensitivity=200") != std::string::npos);
    CHECK(saved_text.find("enabled=0") != std::string::npos);
    frame(frame_user);
    CHECK(capture == 0);
    event(0, OPENRAC_UI_CHOOSE, 3);  // Reset re-enables the pack.
    ++input.tick;
    frame(frame_user);
    CHECK(capture == 1);
    // A full Options page must refuse clearly, rather than put a fallback row in the main menu.
    event(0, OPENRAC_UI_CHANGE, 1, 2);  // Persist 75%, then reload the library.
    pages.clear();
    api.version = "ntsc";
    const int german = 3;
    std::memcpy(memory.data() + 0x0015ED88u, &german, sizeof german);
    reject_pause = true;
    failure.clear();
    CHECK(load(&api, handle) != 0 && !failure.empty());
    reject_pause = false;
    pages.clear();
    failure.clear();
    CHECK(load(&api, handle) == 0 && failure.empty());
    CHECK(pages[0].values[1] == 2);
    CHECK(pages[0].labels[1] == "Empfindlichkeit");
    auto unload = reinterpret_cast<void (*)()>(symbol(lib, "openrac_mod_unload"));
    CHECK(unload != nullptr);
    unload();
    CHECK(capture == 0);
    // The installed OpenRAC 0.10 uses API 1.10: do not access 1.11's data_dir callback.
    pages.clear();
    api.api_minor = 10;
    api.size = offsetof(openrac_api, data_dir);
    api.data_dir = nullptr;
    reject_pause = false;
    CHECK(load(&api, handle) == 0);
    CHECK(pages[0].values[1] == 3);  // Manifest default, no 1.11 preferences read.
    ++input.tick;
    input.mouse_dx = 8;
    phase = OPENRAC_PLAY_PLAYING;
    input.focused = 1;
    input.overlay = 0;
    r = report();
    pad(pad_user, 0, r.data(), r.size());
    CHECK(r[2] > 128);
    fonts = generated_fonts("ntsc");
    phase = OPENRAC_PLAY_MENU;  // Native icons also paint on menus, with gyro input released.
    frame(frame_user);
    check_deck_letters(fonts);
    unload();
    api.version = "unsupported";
    CHECK(load(&api, handle) != 0 && !failure.empty());
    std::filesystem::remove_all(temp);
    std::puts("Steam Deck native load, UI and input lifecycle: OK");
}
