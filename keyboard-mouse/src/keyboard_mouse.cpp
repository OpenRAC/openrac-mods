// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Keyboard and mouse for Ratchet & Clank (README.md): the keys and the mouse, as the player bound
// them in the launcher, pressed into the first pad's report each tick, on top of a gamepad's. The
// mouse turns the camera as the right stick does, and while the Quick Select is held open picks its
// slot by the way it moves; the number keys and the wheel take a Quick Select slot's item at once.
// Takes the keyboard from the port (its own WASD table stops) and the cursor while in play.
#include <array>
#include <cstdint>
#include <string>

#include "discs.h"
#include "kbm.hpp"
#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"

namespace {

using openrac::gaddr;
namespace rac1 = openrac::rac1;
using kbm::Action;

openrac::Mod* s_mod = nullptr;
const kbm_disc* s_disc = nullptr;

// The player's settings.
kbm::Bindings s_binds;
kbm::MoveTuning s_move;
kbm::LookTuning s_look_tuning;
float s_ring_dead_zone = 30.0f;
bool s_capture = true;

// What the keys and the mouse hold now.
kbm::Virtual s_pad;
kbm::KeyStick s_keys;
kbm::MouseLook s_look;
kbm::RingPointer s_ring;
bool s_ring_open = false;
std::uint32_t s_tick = 0xFFFFFFFFu;
int s_asked_capture = -1;
int s_reset_ticks = 0;  // the camera's swing behind Ratchet, while it lasts

float (*s_fast_cos)(float) = nullptr;
float (*s_fast_sin)(float) = nullptr;
void (*s_look_along)(gaddr, float, float) = nullptr;

template <class T>
T& ref(gaddr address) {
    return s_mod->memory().ref<T>(address);
}

gaddr hero() {
    return s_disc->hero;
}

// The game mode, 0 in play; -1 before a level's program is there to say.
int game_mode() {
    if (s_mod->loaded_level() < 0) {
        return -1;
    }
    return ref<std::int32_t>(s_mod->relocate_level(0, s_disc->level_mode));
}

// ---- The tick's input ----

struct Tick {
    const openrac_input* in = nullptr;
    bool play = false;   // the game is in play: play binds and the mouse count
    bool mouse = false;  // the mouse's binds and motion count: the cursor is captured, in play
};

// Whether a bind of an action is held (or went down since the last tick): a mouse bind only while
// the mouse counts, a key while the port's overlay leaves the keyboard to the game.
bool bound(const Tick& t, Action a, bool pressed) {
    if (kbm::kActions[static_cast<std::size_t>(a)].group == kbm::Group::Menu && t.play) {
        return false;
    }
    for (int b : s_binds.of(a)) {
        if (b == 0 || (kbm::is_mouse(b) ? !t.mouse : t.in->overlay != 0)) {
            continue;
        }
        if (pressed ? openrac_bind_pressed(t.in, b) : openrac_bind_held(t.in, b)) {
            return true;
        }
    }
    return false;
}
bool held(const Tick& t, Action a) {
    return bound(t, a, false);
}
bool pressed(const Tick& t, Action a) {
    return bound(t, a, true);
}

// The item a Quick Select slot holds, put in hand as the Quick Select does (the hero takes it on
// his next update); nothing for an empty slot or the item already in hand.
void take_item(std::int32_t item) {
    if (item != 0 && item != ref<std::int32_t>(hero() + KBM_HERO_ITEM)) {
        ref<std::int32_t>(hero() + KBM_HERO_PENDING) = item;
    }
}

std::array<std::int32_t, 8> quick_select_slots() {
    std::array<std::int32_t, 8> slots{};
    for (std::size_t i = 0; i < slots.size(); ++i) {
        slots[i] = ref<std::int32_t>(s_disc->quick_select + static_cast<gaddr>(4 * i));
    }
    return slots;
}

void weapons(const Tick& t) {
    if (!t.play || ref<std::uint8_t>(hero() + KBM_HERO_VEHICLE) != 0 || s_ring_open) {
        return;
    }
    const auto slots = quick_select_slots();
    for (int n = 0; n < 8; ++n) {
        if (pressed(t, static_cast<Action>(static_cast<int>(Action::Weapon1) + n))) {
            take_item(slots[static_cast<std::size_t>(n)]);
            return;
        }
    }
    const int step = pressed(t, Action::WeaponNext) ? 1 : pressed(t, Action::WeaponPrev) ? -1 : 0;
    if (step != 0) {
        // From the item asked for and not yet in hand, so turning the wheel twice goes two on.
        const std::int32_t pending = ref<std::int32_t>(hero() + KBM_HERO_PENDING);
        const std::int32_t current = pending != 0 ? pending : ref<std::int32_t>(hero() + KBM_HERO_ITEM);
        take_item(kbm::cycle_item(slots, current, step));
    }
}

// The tick's pad from the keyboard and mouse: once a tick, when the port has read them anew.
void step(const openrac_input* in, std::uint16_t pad_buttons) {
    Tick t;
    t.in = in;
    const int mode = game_mode();
    t.play = mode == 0;
    t.mouse = t.play && in->captured != 0 && in->overlay == 0;

    // The cursor: captured in play, let go in a menu, on the pause screen, and (the port does it
    // too) while the window is not focused or the overlay is open.
    const int want = s_capture && t.play && in->focused != 0 && in->overlay == 0 ? 1 : 0;
    if (want != s_asked_capture) {
        s_mod->capture_mouse(want != 0);
        s_asked_capture = want;
    }

    kbm::Virtual v;
    const auto press = [&](Action a, std::uint16_t button) {
        if (held(t, a)) {
            v.held = static_cast<std::uint16_t>(v.held | button);
        }
    };
    press(Action::Jump, kbm::kCross);
    press(Action::Fire, kbm::kCircle);
    press(Action::Wrench, kbm::kSquare);
    press(Action::Crouch, kbm::kR1);
    press(Action::Strafe, kbm::kL2);
    press(Action::FirstPerson, kbm::kL1);
    press(Action::Map, kbm::kSelect);
    press(Action::Pause, kbm::kStart);
    press(Action::MenuConfirm, kbm::kCross);
    press(Action::MenuBack, kbm::kTriangle);
    press(Action::MenuUp, kbm::kUp);
    press(Action::MenuDown, kbm::kDown);
    press(Action::MenuLeft, kbm::kLeft);
    press(Action::MenuRight, kbm::kRight);

    // The Quick Select: Triangle held while its bind is, the mouse pointing the left stick at a slot.
    const bool ring = t.play && held(t, Action::QuickSelect);
    if (ring && !s_ring_open) {
        s_ring.reset();
    }
    s_ring_open = ring;
    if (ring) {
        v.held = static_cast<std::uint16_t>(v.held | kbm::kTriangle);
        if (t.mouse) {
            s_ring.add(in->mouse_dx, in->mouse_dy, s_ring_dead_zone);
        }
        const auto [x, y] = s_ring.stick(s_ring_dead_zone);
        v.left_x = x;
        v.left_y = y;
        s_keys = kbm::KeyStick{};
    } else {
        s_keys.step(held(t, Action::MoveForward), held(t, Action::MoveBack), held(t, Action::MoveLeft),
                    held(t, Action::MoveRight), held(t, Action::Walk), s_move);
        v.left_x = s_keys.x;
        v.left_y = s_keys.y;
    }

    // The camera: the mouse as the right stick, in play and out of the ring.
    if (t.mouse && !ring) {
        const bool first_person = ((v.held | pad_buttons) & kbm::kL1) != 0;
        s_look.step(in->mouse_dx, in->mouse_dy, first_person, s_look_tuning);
        v.right_x = s_look.x;
        v.right_y = s_look.y;
        if (s_reset_ticks > 0 && in->mouse_dx != 0.0f) {
            s_reset_ticks = 0;  // the player turns it: the swing stops
        }
    } else if (!t.play) {
        s_look.x = s_look.y = 0.0f;
    }
    if (t.play && pressed(t, Action::CameraReset)) {
        s_look.reset();
        s_reset_ticks = 30;
    }

    weapons(t);
    s_pad = v;
}

// The first pad's report, before the game reads it: the keys and the mouse on top of the pad's.
void on_pad(int port, std::uint8_t* report, int size) {
    if (port != 0 || size < 6) {
        return;
    }
    const openrac_input* in = s_mod->api().input();
    if (in->tick != s_tick) {
        s_tick = in->tick;
        const std::uint16_t pad_held = static_cast<std::uint16_t>(~(report[0] | (report[1] << 8)));
        step(in, pad_held);
    }
    kbm::merge(s_pad, report, size);
}

// The follow camera's per-tick requests: after the game's (and any mod's before this one), while
// the camera's reset lasts, a request to look along the way Ratchet faces, as the game's own
// camera volumes ask for a heading.
template <class Original>
void camera_overrides(Original original, gaddr camera) {
    original(camera);
    if (s_reset_ticks <= 0 || game_mode() != 0) {
        s_reset_ticks = 0;
        return;
    }
    --s_reset_ticks;
    const float facing = s_mod->memory().read<float>(hero() + KBM_HERO_FACING);
    openrac::StackFrame frame(*s_mod, 16);
    const gaddr dir = frame.address();
    s_mod->memory().write<float>(dir, s_fast_cos(facing));
    s_mod->memory().write<float>(dir + 4u, s_fast_sin(facing));
    s_mod->memory().write<float>(dir + 8u, 0.0f);
    s_mod->memory().write<float>(dir + 12u, 0.0f);
    s_look_along(dir, 0.20943952f, 0.0f);
}

template <class Fn>
Fn game_function(const char* name) {
    return s_mod->required_function<Fn>(name);
}

void load(openrac::Mod& mod) {
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
    }
    if (!mod.has_input()) {
        throw openrac::Error("needs OpenRAC with mod interface 1.2 (the keyboard and mouse)");
    }
    s_mod = &mod;
    s_disc = nullptr;
    for (const kbm_disc& d : k_kbm_discs) {
        if (mod.version() == d.version) {
            s_disc = &d;
        }
    }
    if (s_disc == nullptr) {
        throw openrac::Error("no addresses for the " + std::string(mod.version()) + " disc");
    }
    s_binds = kbm::read_bindings([&](const char* key) { return mod.setting<std::string>(key); });
    for (const kbm::Conflict& c : kbm::conflicts(s_binds)) {
        mod.warn(std::string(openrac_bind_to_name(c.bind)) + " is bound to both "
                 + kbm::kActions[static_cast<std::size_t>(c.a)].key + " and "
                 + kbm::kActions[static_cast<std::size_t>(c.b)].key + ": it does both");
    }
    s_move.walk = static_cast<float>(mod.setting<double>("walk_speed"));
    s_move.rise_ticks = static_cast<float>(mod.setting<double>("stick_ramp_ms")) * 60.0f / 1000.0f;
    s_look_tuning.sensitivity = static_cast<float>(mod.setting<double>("mouse_sensitivity"));
    s_look_tuning.acceleration = static_cast<float>(mod.setting<double>("mouse_acceleration"));
    s_look_tuning.invert_y = mod.setting<bool>("invert_y");
    s_look_tuning.pitch_holds = mod.setting<std::string>("camera_tilt") != "return";
    s_ring_dead_zone = static_cast<float>(mod.setting<double>("quick_select_dead_zone"));
    s_capture = mod.setting<bool>("capture_cursor");

    s_fast_cos = game_function<float (*)(float)>(s_disc->fast_cos);
    s_fast_sin = game_function<float (*)(float)>(s_disc->fast_sin);
    s_look_along = game_function<void (*)(gaddr, float, float)>(s_disc->camera_look_along);

    s_pad = kbm::Virtual{};
    s_keys = kbm::KeyStick{};
    s_look.reset();
    s_ring.reset();
    s_ring_open = false;
    s_tick = 0xFFFFFFFFu;
    s_asked_capture = -1;
    s_reset_ticks = 0;

    mod.take_input(OPENRAC_INPUT_KEYBOARD | OPENRAC_INPUT_MOUSE);
    mod.on_pad([](int port, std::uint8_t* report, int size) { on_pad(port, report, size); });
    mod.hook<rac1::FollowCamera_ApplyOverrides>([](auto original, std::uint32_t camera) {
        camera_overrides(original, camera);
    });
    mod.info("keyboard and mouse on the " + std::string(mod.version()) + " disc");
}

}  // namespace

OPENRAC_MOD_CPP("keyboard-mouse", "1.0.0", load)
