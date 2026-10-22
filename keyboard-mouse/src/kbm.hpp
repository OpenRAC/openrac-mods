// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The keyboard and mouse mod's own logic, apart from the game and the port so it is tested alone:
// the actions and their binds, conflicts between them, the keys to the left stick, the mouse to
// the right stick, the mouse to the Quick Select ring's direction, and the DualShock report the
// game reads. keyboard_mouse.cpp joins it to the game.
#ifndef KBM_HPP
#define KBM_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "openrac/keys.h"

namespace kbm {

// ---- Actions ----

// What a bind does. The pad buttons are Ratchet & Clank's: Cross jumps, Square swings the wrench,
// Circle fires, Triangle holds the Quick Select open, R1 crouches, L1 looks in first person, L2
// strafes (with the lock-strafe mod), Select opens the map, Start pauses.
enum class Action : int {
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    Walk,
    Jump,
    Fire,
    QuickSelect,
    Wrench,
    Crouch,
    Strafe,
    FirstPerson,
    Map,
    CameraReset,
    Pause,
    Weapon1,
    Weapon2,
    Weapon3,
    Weapon4,
    Weapon5,
    Weapon6,
    Weapon7,
    Weapon8,
    WeaponNext,
    WeaponPrev,
    MenuConfirm,
    MenuBack,
    MenuUp,
    MenuDown,
    MenuLeft,
    MenuRight,
    Count,
};
inline constexpr int kActionCount = static_cast<int>(Action::Count);

// When an action's binds count: "play" ones always (the keyboard's; the mouse's while the cursor
// is captured), "menu" ones only out of play (a menu, the pause screen, a cutscene). Two actions
// of the same group may not share an input; of different groups they may.
enum class Group : int { Play, Menu };

struct ActionInfo {
    const char* key;       // the setting's key in openrac-mod.json
    const char* fallback;  // its default, as the manifest's
    Group group;
};

// In Action's order; the manifest lists the same keys and defaults (a test checks they agree).
inline constexpr std::array<ActionInfo, kActionCount> kActions = {{
    {"move_forward", "KeyW", Group::Play},
    {"move_back", "KeyS", Group::Play},
    {"move_left", "KeyA", Group::Play},
    {"move_right", "KeyD", Group::Play},
    {"walk", "AltLeft", Group::Play},
    {"jump", "Space", Group::Play},
    {"fire", "MouseLeft", Group::Play},
    {"quick_select", "MouseRight", Group::Play},
    {"wrench", "KeyF,MouseMiddle", Group::Play},
    {"crouch", "ControlLeft,KeyC", Group::Play},
    {"strafe", "ShiftLeft", Group::Play},
    {"first_person", "KeyV,MouseBack", Group::Play},
    {"map", "Tab,KeyM", Group::Play},
    {"camera_reset", "KeyQ", Group::Play},
    {"pause", "Escape,KeyP", Group::Play},
    {"weapon_1", "Digit1", Group::Play},
    {"weapon_2", "Digit2", Group::Play},
    {"weapon_3", "Digit3", Group::Play},
    {"weapon_4", "Digit4", Group::Play},
    {"weapon_5", "Digit5", Group::Play},
    {"weapon_6", "Digit6", Group::Play},
    {"weapon_7", "Digit7", Group::Play},
    {"weapon_8", "Digit8", Group::Play},
    {"weapon_next", "WheelDown", Group::Play},
    {"weapon_prev", "WheelUp", Group::Play},
    {"menu_confirm", "Enter,NumpadEnter", Group::Menu},
    {"menu_back", "Backspace", Group::Menu},
    {"menu_up", "ArrowUp", Group::Menu},
    {"menu_down", "ArrowDown", Group::Menu},
    {"menu_left", "ArrowLeft", Group::Menu},
    {"menu_right", "ArrowRight", Group::Menu},
}};

inline constexpr int kMaxBinds = 2;

// One action's binds (OPENRAC_BIND_*, a key's HID usage); 0 for an empty slot.
using Binds = std::array<int, kMaxBinds>;

// A keybind setting's text ("KeyW,ArrowUp") as binds: names of openrac/keys.h, split at commas or
// spaces. False, and no binds, when a name is unknown, one is there twice, or there are more than
// two: the caller then takes the default (as the port does with a value it would not take).
inline bool parse_binds(std::string_view text, Binds& out) {
    out = {};
    int count = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ',' || text[i] == ' ') {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && text[end] != ',' && text[end] != ' ') {
            ++end;
        }
        const int bind = openrac_bind_from_name_n(text.data() + i, end - i);
        if (bind == OPENRAC_BIND_NONE || count == kMaxBinds || std::find(out.begin(), out.end(), bind) != out.end()) {
            out = {};
            return false;
        }
        out[static_cast<std::size_t>(count++)] = bind;
        i = end;
    }
    return true;
}

struct Bindings {
    std::array<Binds, kActionCount> binds{};
    const Binds& of(Action a) const { return binds[static_cast<std::size_t>(a)]; }
};

// The bindings from each action's setting text (`text(key)`), the default where a text is not one.
template <class Text>
Bindings read_bindings(Text&& text) {
    Bindings b;
    for (int a = 0; a < kActionCount; ++a) {
        const ActionInfo& info = kActions[static_cast<std::size_t>(a)];
        if (!parse_binds(text(info.key), b.binds[static_cast<std::size_t>(a)])) {
            parse_binds(info.fallback, b.binds[static_cast<std::size_t>(a)]);
        }
    }
    return b;
}

struct Conflict {
    Action a;
    Action b;
    int bind;
};

// Inputs bound to two actions of the same group: each pair once, a before b.
inline std::vector<Conflict> conflicts(const Bindings& b) {
    std::vector<Conflict> out;
    for (int x = 0; x < kActionCount; ++x) {
        for (int y = x + 1; y < kActionCount; ++y) {
            if (kActions[static_cast<std::size_t>(x)].group != kActions[static_cast<std::size_t>(y)].group) {
                continue;
            }
            for (int bx : b.binds[static_cast<std::size_t>(x)]) {
                for (int by : b.binds[static_cast<std::size_t>(y)]) {
                    if (bx != 0 && bx == by) {
                        out.push_back({static_cast<Action>(x), static_cast<Action>(y), bx});
                    }
                }
            }
        }
    }
    return out;
}

// The mouse's buttons and its wheel: they count only while the cursor is captured.
inline bool is_mouse(int bind) {
    return bind >= OPENRAC_BIND_MOUSE_LEFT;
}

// ---- The left stick from keys ----

struct MoveTuning {
    float walk = 0.5f;        // how far the stick goes while walking (0..1)
    float rise_ticks = 4.0f;  // ticks from rest to full (0: at once)
};

// Digital keys to an analog stick: the four directions as a vector (diagonals on the circle, not
// the square), scaled for walking, eased toward that target each tick so a tap is a nudge and a
// hold runs, and let go twice as fast as it rises so a stop is crisp. x > 0 right, y > 0 back
// (down on the stick, as the console's bytes).
struct KeyStick {
    float x = 0.0f;
    float y = 0.0f;

    void step(bool forward, bool back, bool left, bool right, bool walk, const MoveTuning& t) {
        float tx = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
        float ty = (back ? 1.0f : 0.0f) - (forward ? 1.0f : 0.0f);
        const float length = std::sqrt(tx * tx + ty * ty);
        if (length > 1.0f) {
            tx /= length;
            ty /= length;
        }
        if (walk) {
            tx *= t.walk;
            ty *= t.walk;
        }
        const float rise = t.rise_ticks <= 0.0f ? 2.0f : 1.0f / t.rise_ticks;
        x = approach(x, tx, rise);
        y = approach(y, ty, rise);
    }

    static float approach(float now, float target, float rise) {
        // Toward the centre (a key let go, or the other way pressed): twice as fast.
        const bool slowing = std::fabs(target) < std::fabs(now) || target * now < 0.0f;
        const float step = slowing ? rise * 2.0f : rise;
        if (now < target) {
            return std::min(now + step, target);
        }
        return std::max(now - step, target);
    }
};

// ---- The right stick from the mouse ----

struct LookTuning {
    float sensitivity = 1.0f;   // the mouse's turn, 1 the default
    float acceleration = 0.0f;  // 0: none; more turns a fast flick further than a slow drag
    bool invert_y = false;
    bool pitch_holds = true;  // the camera's tilt stays where the mouse leaves it
};

// The mouse's motion over a tick, at sensitivity 1, that pushes the stick all the way.
inline constexpr float kPixelsForFullTurn = 24.0f;
// The tilt's full travel, in pixels at sensitivity 1.
inline constexpr float kPixelsForFullTilt = 160.0f;

// The mouse to the right stick. The follow camera turns at a rate the stick's x sets, so x is the
// tick's motion (eased over two ticks, so a mouse polled unevenly against the game's ticks does
// not stutter); it tilts toward an elevation the stick's y sets, so y keeps where the mouse put it
// (pitch_holds) or eases back to level. In first person both axes are rates, so y is the tick's
// motion there.
struct MouseLook {
    float x = 0.0f;
    float y = 0.0f;
    float tilt = 0.0f;  // the held elevation, -1 (up) .. 1 (down)
    float last_dx = 0.0f;

    // The gain for a tick's motion of `speed` pixels: 1 without acceleration.
    static float gain(float speed, const LookTuning& t) {
        if (t.acceleration <= 0.0f) {
            return 1.0f;
        }
        return 1.0f + t.acceleration * std::min(speed / kPixelsForFullTurn, 2.0f);
    }

    void step(float dx, float dy, bool first_person, const LookTuning& t) {
        const float sens = std::max(t.sensitivity, 0.0f);
        const float ydir = t.invert_y ? -1.0f : 1.0f;
        const float smooth_dx = 0.5f * (dx + last_dx);
        last_dx = dx;
        const float speed = std::sqrt(smooth_dx * smooth_dx + dy * dy);
        x = clamp1(smooth_dx * sens * gain(speed, t) / kPixelsForFullTurn);
        if (first_person) {
            y = clamp1(dy * ydir * sens * gain(speed, t) / kPixelsForFullTurn);
            return;
        }
        tilt = clamp1(tilt + dy * ydir * sens / kPixelsForFullTilt);
        if (!t.pitch_holds && dy == 0.0f) {
            tilt *= 0.9f;
            if (std::fabs(tilt) < 0.01f) {
                tilt = 0.0f;
            }
        }
        y = tilt;
    }

    void reset() { *this = MouseLook{}; }

    static float clamp1(float v) { return std::max(-1.0f, std::min(1.0f, v)); }
};

// ---- The Quick Select ring from the mouse ----

// While the Quick Select is held open, the mouse's motion adds up to a pointer from the ring's
// centre; past the dead zone it points the left stick all the way along it (the game picks a slot
// only for a stick pushed past 0.9, and keeps a slot until the stick turns well away from it), and
// within it the stick rests. The pointer is held within twice the dead zone, so turning back the
// other way answers at once.
struct RingPointer {
    float x = 0.0f;
    float y = 0.0f;

    void reset() { x = y = 0.0f; }

    void add(float dx, float dy, float dead_zone) {
        x += dx;
        y += dy;
        const float limit = 2.0f * std::max(dead_zone, 1.0f);
        const float length = std::sqrt(x * x + y * y);
        if (length > limit) {
            x *= limit / length;
            y *= limit / length;
        }
    }

    // The stick (x right, y down), unit length or (0, 0).
    std::pair<float, float> stick(float dead_zone) const {
        const float length = std::sqrt(x * x + y * y);
        if (length <= dead_zone || length == 0.0f) {
            return {0.0f, 0.0f};
        }
        return {x / length, y / length};
    }
};

// The slot the game's ring gives a stick pointing (x, y), y down: its slots are 45 degrees apart,
// slot 0 at the top and going clockwise (slot i's icon is at angle i * pi / 4 - pi / 2), each
// taking the 45 degrees around it. -1 for a stick at rest. What the tests check the pointer against.
inline int ring_slot(float x, float y) {
    if (x == 0.0f && y == 0.0f) {
        return -1;
    }
    const float pi = 3.14159265f;
    float a = std::atan2(y, x) + pi / 2.0f;  // 0 at the top, clockwise
    while (a < 0.0f) {
        a += 2.0f * pi;
    }
    return static_cast<int>(std::floor(a / (pi / 4.0f) + 0.5f)) % 8;
}

// ---- The report the game reads ----

// The pad report's buttons (bytes 0 and 1, little-endian, a bit clear while held) in the console's
// order.
enum Button : std::uint16_t {
    kSelect = 1u << 0,
    kL3 = 1u << 1,
    kR3 = 1u << 2,
    kStart = 1u << 3,
    kUp = 1u << 4,
    kRight = 1u << 5,
    kDown = 1u << 6,
    kLeft = 1u << 7,
    kL2 = 1u << 8,
    kR2 = 1u << 9,
    kL1 = 1u << 10,
    kR1 = 1u << 11,
    kTriangle = 1u << 12,
    kCircle = 1u << 13,
    kCross = 1u << 14,
    kSquare = 1u << 15,
};

// The button of each pressure byte, bytes 6..17: right, left, up, down, triangle, circle, cross,
// square, L1, R1, L2, R2.
inline constexpr std::array<int, 12> kPressureBits = {5, 7, 4, 6, 12, 13, 14, 15, 10, 11, 8, 9};

// The game's stick reading: an axis's byte b counts from 0x7F, nothing within 0x30 of it, then
// (|b - 0x7F| - 0x30) / 0x4C up to 1. A value v (-1..1) as the byte the game reads back as about
// v, so a walk at 0.5 is a walk at 0.5 and not lost in the dead zone; 0 is the centre, 0x80.
inline std::uint8_t stick_byte(float v) {
    if (v == 0.0f) {
        return 0x80;
    }
    const float magnitude = std::min(std::fabs(v), 1.0f);
    const float distance = 0x30 + 1 + magnitude * (0x4C - 1);
    const float b = 0x7F + (v > 0.0f ? distance : -distance);
    return static_cast<std::uint8_t>(std::max(0.0f, std::min(255.0f, std::round(b))));
}

// What the game makes of a byte (its own reading, for the tests).
inline float game_axis(std::uint8_t b) {
    const int d = static_cast<int>(b) - 0x7F;
    const int m = d < 0 ? -d : d;
    if (m < 0x30) {
        return 0.0f;
    }
    const float v = std::min(1.0f, static_cast<float>(m - 0x30) / 0x4C);
    return d < 0 ? -v : v;
}

// The pad as the keyboard and mouse hold it this tick.
struct Virtual {
    std::uint16_t held = 0;  // Button bits held (set while held)
    float left_x = 0.0f, left_y = 0.0f;
    float right_x = 0.0f, right_y = 0.0f;
};

// Into the game's report (18 bytes: buttons, right stick, left stick, twelve pressures), on top of
// the gamepad's: our buttons are held as well as the pad's, and each stick axis of ours that is
// off centre takes that axis's place.
inline void merge(const Virtual& v, std::uint8_t* report, int size) {
    if (size < 6) {
        return;
    }
    std::uint16_t buttons = static_cast<std::uint16_t>(report[0] | (report[1] << 8));
    buttons = static_cast<std::uint16_t>(buttons & ~v.held);
    report[0] = static_cast<std::uint8_t>(buttons);
    report[1] = static_cast<std::uint8_t>(buttons >> 8);
    const float axes[4] = {v.right_x, v.right_y, v.left_x, v.left_y};
    for (int i = 0; i < 4; ++i) {
        if (axes[i] != 0.0f) {
            report[2 + i] = stick_byte(axes[i]);
        }
    }
    for (int i = 0; i < 12 && 6 + i < size; ++i) {
        if ((v.held >> kPressureBits[static_cast<std::size_t>(i)]) & 1u) {
            report[6 + i] = 0xFF;
        }
    }
}

// The next item in the Quick Select's eight slots from `current` (`step` +1 or -1), skipping
// empty slots (0); `current` itself when no other slot has one. An item not in the ring starts
// from before the first slot (or after the last, going back).
inline int cycle_item(const std::array<std::int32_t, 8>& slots, std::int32_t current, int step) {
    int at = -1;
    for (int i = 0; i < 8; ++i) {
        if (slots[static_cast<std::size_t>(i)] != 0 && slots[static_cast<std::size_t>(i)] == current) {
            at = i;
            break;
        }
    }
    if (at < 0) {
        at = step > 0 ? -1 : 8;
    }
    for (int n = 1; n <= 8; ++n) {
        const int i = ((at + step * n) % 8 + 8) % 8;
        const std::int32_t item = slots[static_cast<std::size_t>(i)];
        if (item != 0 && item != current) {
            return item;
        }
    }
    return current;
}

}  // namespace kbm

#endif  // KBM_HPP
