// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Keyboard and mouse for Ratchet & Clank (README.md): the keys and the mouse, as the player bound
// them in the launcher, pressed into the first pad's report each tick, on top of a gamepad's. What
// a key presses follows where the game is (kbm::Context): in play the console's layout; in the
// title, the menus and the dialogs confirm and back; in a scene, a movie or the ship, a skip. In
// play the mouse turns the camera as the right stick does, and while the Quick Select is held open
// picks its slot by the way it moves; in the menus the cursor is free, points at the game's own
// menu items, clicks, and its wheel scrolls. Takes the keyboard from the port (its own WASD table
// stops) and the cursor while in play.
#include <array>
#include <cmath>
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
using kbm::Context;

openrac::Mod* s_mod = nullptr;
const kbm_disc* s_disc = nullptr;

// The player's settings.
kbm::Bindings s_binds;
kbm::MoveTuning s_move;
kbm::LookTuning s_look_tuning;
float s_ring_dead_zone = 30.0f;
bool s_capture = true;
bool s_menu_mouse = true;

// What the keys and the mouse hold now.
kbm::Virtual s_pad;
kbm::KeyStick s_keys;
kbm::MouseLook s_look;
kbm::RingPointer s_ring;
kbm::Latch s_latch;
kbm::Pulser s_pulses;
kbm::Stepper s_stepper;
std::array<kbm::Repeat, 4> s_repeat;
bool s_ring_open = false;
std::uint32_t s_tick = 0xFFFFFFFFu;
int s_asked_capture = -1;
int s_reset_ticks = 0;  // the camera's swing behind Ratchet, while it lasts
int s_click_wait = 0;   // a click on an item not yet focused: Cross once the focus has moved
std::uint16_t s_click_button = 0;

float (*s_fast_cos)(float) = nullptr;
float (*s_fast_sin)(float) = nullptr;
void (*s_look_along)(gaddr, float, float) = nullptr;

template <class T>
T& ref(gaddr address) {
    return s_mod->memory().ref<T>(address);
}

template <class T>
T read(gaddr address) {
    return s_mod->memory().read<T>(address);
}

gaddr hero() {
    return s_disc->hero;
}

// A game address worth reading: in the console's 32 MB, aligned.
bool plausible(gaddr address) {
    return address >= 0x00100000u && address < 0x02000000u && (address & 3u) == 0;
}

// Where the game is: a level's mode word (its program's copy) with a level loaded, else the boot
// program's (the title and the main menu).
Context context_now(int* dialog_kind) {
    const int level = s_mod->loaded_level();
    const int level_mode = level >= 0 ? read<std::int32_t>(s_mod->relocate_level(0, s_disc->level_mode)) : -1;
    const int boot_mode = level < 0 ? read<std::int32_t>(s_disc->boot_mode) : -1;
    const Context c = kbm::context_of(level, level_mode, boot_mode);
    *dialog_kind = c == Context::Dialog ? read<std::int32_t>(s_mod->relocate(s_disc->dialog)) : -1;
    return c;
}

// ---- The tick's input ----

struct Tick {
    const openrac_input* in = nullptr;
    Context where = Context::Play;
    bool play = false;     // play binds of the mouse count (in play, the cursor captured)
    bool pointer = false;  // menu binds of the mouse count (out of play, the cursor over the game)
};

// Whether one of an action's binds is held, or went down since the last tick (a tap shorter than a
// tick still presses for one): a mouse bind only where the mouse counts, any while the port's
// overlay leaves the input to the game.
bool active(const Tick& t, Action a) {
    const bool menu = kbm::kActions[static_cast<std::size_t>(a)].group == kbm::Group::Menu;
    if (menu && t.where == Context::Play) {
        return false;
    }
    if (t.in->overlay != 0) {
        return false;
    }
    for (int b : s_binds.of(a)) {
        if (b == 0) {
            continue;
        }
        if (kbm::is_mouse(b) && !(menu ? t.pointer : t.play)) {
            continue;
        }
        if (openrac_bind_held(t.in, b) || openrac_bind_pressed(t.in, b)) {
            return true;
        }
    }
    return false;
}

bool pressed_now(const Tick& t, Action a) {
    if (t.in->overlay != 0) {
        return false;
    }
    const bool menu = kbm::kActions[static_cast<std::size_t>(a)].group == kbm::Group::Menu;
    for (int b : s_binds.of(a)) {
        if (b != 0 && !(kbm::is_mouse(b) && !(menu ? t.pointer : t.play)) && openrac_bind_pressed(t.in, b)) {
            return true;
        }
    }
    return false;
}

// An action's wheel binds that turned this tick (a notch is one press, sent as a pulse), and
// whether one of its other binds is down.
bool wheel_notch(const Tick& t, Action a) {
    for (int b : s_binds.of(a)) {
        if (b >= OPENRAC_BIND_WHEEL_UP && t.pointer && t.in->overlay == 0 && openrac_bind_pressed(t.in, b)) {
            return true;
        }
    }
    return false;
}
bool held_not_wheel(const Tick& t, Action a) {
    if (t.in->overlay != 0) {
        return false;
    }
    for (int b : s_binds.of(a)) {
        if (b == 0 || b >= OPENRAC_BIND_WHEEL_UP || (kbm::is_mouse(b) && !t.pointer)) {
            continue;
        }
        if (openrac_bind_held(t.in, b) || openrac_bind_pressed(t.in, b)) {
            return true;
        }
    }
    return false;
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
    if (t.where != Context::Play || ref<std::uint8_t>(hero() + KBM_HERO_VEHICLE) != 0 || s_ring_open) {
        return;
    }
    const auto slots = quick_select_slots();
    for (int n = 0; n < 8; ++n) {
        if (pressed_now(t, static_cast<Action>(static_cast<int>(Action::Weapon1) + n))) {
            take_item(slots[static_cast<std::size_t>(n)]);
            return;
        }
    }
    const int step = pressed_now(t, Action::WeaponNext) ? 1 : pressed_now(t, Action::WeaponPrev) ? -1 : 0;
    if (step != 0) {
        // From the item asked for and not yet in hand, so turning the wheel twice goes two on.
        const std::int32_t pending = ref<std::int32_t>(hero() + KBM_HERO_PENDING);
        const std::int32_t current = pending != 0 ? pending : ref<std::int32_t>(hero() + KBM_HERO_ITEM);
        take_item(kbm::cycle_item(slots, current, step));
    }
}

// ---- The mouse over the menus ----

// The menus' page shown now, 0 for none (a page changing, or no menu).
gaddr menu_page() {
    const gaddr page = read<std::uint32_t>(s_mod->relocate(s_disc->page_menu) + 4u);
    return plausible(page) ? page : 0;
}

// A text list's rows: how many, and the selected one; false for a widget that is no text list.
bool text_list(gaddr widget, int* count, int* selected) {
    const gaddr items = read<std::uint32_t>(widget + KBM_LIST_ITEMS);
    if (!plausible(items)) {
        return false;
    }
    int n = 0;
    while (n < 32 && read<std::int16_t>(items + KBM_LIST_ITEM_SIZE * static_cast<gaddr>(n)) != 0) {
        ++n;
    }
    const std::int32_t sel = read<std::int32_t>(widget + KBM_LIST_SELECTED);
    if (n == 0 || n == 32 || sel < 0 || sel >= n) {
        return false;
    }
    *count = n;
    *selected = sel;
    return true;
}

kbm::Rect rect_of(gaddr widget) {
    kbm::Rect r;
    r.x = read<std::int32_t>(widget + KBM_WIDGET_RECT);
    r.y = read<std::int32_t>(widget + KBM_WIDGET_RECT + 4u);
    r.w = read<std::int32_t>(widget + KBM_WIDGET_RECT + 8u);
    r.h = read<std::int32_t>(widget + KBM_WIDGET_RECT + 12u);
    return r;
}

// The mouse in a menu: over a page's item, it is the one the game focuses (as the d-pad moves the
// focus between a page's items: the next tick's focus change plays the game's own sound); over a
// row of the focused list, the d-pad steps the selection to it. Elsewhere (the vendor, the dialogs,
// the item grids) its motion steps the d-pad. A click on an item not yet focused focuses it first.
// Returns false when the click must wait for the focus to move.
bool menu_mouse(const Tick& t, bool click) {
    const openrac_input* in = t.in;
    const bool moved = in->mouse_dx != 0.0f || in->mouse_dy != 0.0f;
    if (!t.pointer || !s_menu_mouse) {
        return true;
    }
    const float px = in->pointer_x * kbm::kScreenWidth;
    const float py = in->pointer_y * kbm::kScreenHeight;
    const gaddr page = (t.where == Context::Menu) ? menu_page() : 0;
    if (page != 0) {
        std::array<kbm::Rect, KBM_PAGE_WIDGET_COUNT> rects{};
        std::array<gaddr, KBM_PAGE_WIDGET_COUNT> widgets{};
        int count = 0;
        for (int i = 0; i < KBM_PAGE_WIDGET_COUNT; ++i) {
            const gaddr w = read<std::uint32_t>(page + KBM_PAGE_WIDGETS + 4u * static_cast<gaddr>(i));
            if (plausible(w)) {
                widgets[static_cast<std::size_t>(count)] = w;
                rects[static_cast<std::size_t>(count)] = rect_of(w);
                ++count;
            }
        }
        const int hit = kbm::hit_widget(rects.data(), count, px, py);
        if (hit >= 0) {
            const gaddr widget = widgets[static_cast<std::size_t>(hit)];
            const gaddr focused = read<std::uint32_t>(page + KBM_PAGE_FOCUS);
            if (widget != focused) {
                if ((moved || click) && read<std::uint32_t>(page + KBM_PAGE_PENDING) == 0) {
                    ref<std::uint32_t>(page + KBM_PAGE_PENDING) = widget;
                }
                return !click;
            }
            int rows = 0;
            int selected = 0;
            if (moved && text_list(widget, &rows, &selected) && rows > 1 && s_pulses.idle()) {
                const int row = kbm::list_row(rects[static_cast<std::size_t>(hit)], rows,
                                              read<std::uint32_t>(widget + KBM_LIST_FLAGS), py);
                if (row >= 0 && row != selected) {
                    s_pulses.push(row < selected ? kbm::kUp : kbm::kDown);
                }
                s_stepper.reset();
                return true;
            }
            if (!moved || text_list(widget, &rows, &selected)) {
                return true;
            }
            // A grid's cells: stepped below.
        }
    }
    if (moved && t.where != Context::Title) {
        const std::uint16_t dir = s_stepper.step(in->mouse_dx, in->mouse_dy);
        if (dir != 0) {
            s_pulses.push(dir);
        }
    }
    return true;
}

// ---- The tick ----

// The tick's pad from the keyboard and mouse: once a tick, when the port has read them anew.
void step(const openrac_input* in, std::uint16_t pad_buttons) {
    Tick t;
    t.in = in;
    int dialog_kind = -1;
    t.where = context_now(&dialog_kind);
    const bool play = t.where == Context::Play;
    t.play = play && in->captured != 0 && in->overlay == 0;
    t.pointer = !play && in->pointer_inside != 0 && in->focused != 0 && in->overlay == 0
                && in->size >= offsetof(openrac_input, reserved2);

    // The cursor: captured in play, free everywhere else, and (the port does it too) while the
    // window is not focused or the overlay is open.
    const int want = s_capture && play && in->focused != 0 && in->overlay == 0 ? 1 : 0;
    if (want != s_asked_capture) {
        s_mod->capture_mouse(want != 0);
        s_asked_capture = want;
    }

    // The actions held this tick, less those held since before the game changed where it is.
    kbm::ActionSet held = 0;
    for (int a = 0; a < kbm::kActionCount; ++a) {
        if (active(t, static_cast<Action>(a))) {
            held |= kbm::ActionSet{1} << a;
        }
    }
    const kbm::Context before = s_latch.last;
    held = s_latch.filter(t.where, held);
    if (before != t.where) {
        s_pulses.clear();
        s_stepper.reset();
        s_click_wait = 0;
    }

    kbm::Virtual v;
    // A click on a menu item not yet focused waits for the focus; then it confirms.
    const bool click = t.pointer && (held & kbm::bit(Action::MenuConfirm)) != 0
                       && pressed_now(t, Action::MenuConfirm);
    if (!play) {
        if (!menu_mouse(t, click)) {
            held &= ~kbm::bit(Action::MenuConfirm);
            s_click_wait = 4;
            s_click_button = kbm::buttons_for(t.where, kbm::bit(Action::MenuConfirm), dialog_kind);
        }
    }
    v.held = kbm::buttons_for(t.where, held, dialog_kind);
    if (s_click_wait > 0 && --s_click_wait == 0) {
        s_pulses.push(s_click_button);
    }

    // The menus' directions: the d-pad, a held key repeating; a wheel notch is one step.
    if (!play) {
        const Action dirs[4] = {Action::MenuUp, Action::MenuDown, Action::MenuLeft, Action::MenuRight};
        const std::uint16_t pads[4] = {kbm::kUp, kbm::kDown, kbm::kLeft, kbm::kRight};
        for (int i = 0; i < 4; ++i) {
            const bool on = (held & kbm::bit(dirs[i])) != 0 && held_not_wheel(t, dirs[i]);
            const bool notch = (held & kbm::bit(dirs[i])) != 0 && !on && wheel_notch(t, dirs[i]);
            if (notch) {
                s_pulses.push(pads[i]);
            } else if (s_repeat[static_cast<std::size_t>(i)].step(on)) {
                v.held = static_cast<std::uint16_t>(v.held | pads[i]);
            }
        }
        v.held = static_cast<std::uint16_t>(v.held | s_pulses.step());
    } else {
        s_pulses.clear();
    }

    // The Quick Select: Triangle held while its bind is, the mouse pointing the left stick at a slot.
    const bool ring = play && (held & kbm::bit(Action::QuickSelect)) != 0;
    if (ring && !s_ring_open) {
        s_ring.reset();
    }
    s_ring_open = ring;
    const auto key = [&](Action a) { return (held & kbm::bit(a)) != 0; };
    if (ring) {
        v.held = static_cast<std::uint16_t>(v.held | kbm::kTriangle);
        if (t.play) {
            s_ring.add(in->mouse_dx, in->mouse_dy, s_ring_dead_zone);
        }
        const auto [x, y] = s_ring.stick(s_ring_dead_zone);
        v.left_x = x;
        v.left_y = y;
        s_keys = kbm::KeyStick{};
    } else {
        // The movement keys are the left stick everywhere: in a menu the game reads it as the d-pad.
        s_keys.step(key(Action::MoveForward), key(Action::MoveBack), key(Action::MoveLeft), key(Action::MoveRight),
                    key(Action::Walk), s_move);
        v.left_x = s_keys.x;
        v.left_y = s_keys.y;
    }

    // The camera: the mouse as the right stick, in play and out of the ring.
    if (t.play && !ring) {
        const bool first_person = ((v.held | pad_buttons) & kbm::kL1) != 0;
        s_look.step(in->mouse_dx, in->mouse_dy, first_person, s_look_tuning);
        v.right_x = s_look.x;
        v.right_y = s_look.y;
        if (s_reset_ticks > 0 && in->mouse_dx != 0.0f) {
            s_reset_ticks = 0;  // the player turns it: the swing stops
        }
    } else if (!play) {
        s_look.x = s_look.y = 0.0f;
    }
    if (play && pressed_now(t, Action::CameraReset)) {
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
    int kind = 0;
    if (s_reset_ticks <= 0 || context_now(&kind) != Context::Play) {
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
        throw openrac::Error("needs OpenRAC with mod interface 1.3 (the keyboard and mouse)");
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
    s_menu_mouse = mod.setting<bool>("menu_mouse");

    s_fast_cos = game_function<float (*)(float)>(s_disc->fast_cos);
    s_fast_sin = game_function<float (*)(float)>(s_disc->fast_sin);
    s_look_along = game_function<void (*)(gaddr, float, float)>(s_disc->camera_look_along);

    s_pad = kbm::Virtual{};
    s_keys = kbm::KeyStick{};
    s_look.reset();
    s_ring.reset();
    s_latch = kbm::Latch{};
    s_pulses.clear();
    s_stepper.reset();
    s_repeat = {};
    s_ring_open = false;
    s_tick = 0xFFFFFFFFu;
    s_asked_capture = -1;
    s_reset_ticks = 0;
    s_click_wait = 0;

    mod.take_input(OPENRAC_INPUT_KEYBOARD | OPENRAC_INPUT_MOUSE);
    mod.on_pad([](int port, std::uint8_t* report, int size) { on_pad(port, report, size); });
    mod.hook<rac1::FollowCamera_ApplyOverrides>([](auto original, std::uint32_t camera) {
        camera_overrides(original, camera);
    });
    mod.info("keyboard and mouse on the " + std::string(mod.version()) + " disc");
}

}  // namespace

OPENRAC_MOD_CPP("keyboard-mouse", "1.1.0", load)
