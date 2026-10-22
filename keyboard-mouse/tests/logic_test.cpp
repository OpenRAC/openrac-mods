// SPDX-License-Identifier: GPL-3.0-or-later
// The keyboard and mouse mod's logic (src/kbm.hpp) alone: binds read from settings, conflicts,
// the keys' stick, the mouse's stick, the Quick Select's direction, the report the game reads and
// the weapon wheel; and the manifest's keys and defaults against the action table.
//
// Run with the manifest's path: logic_test <openrac-mod.json>.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "kbm.hpp"

namespace {

int g_failures = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        if (!(c)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

bool near(float a, float b, float within = 0.02f) {
    return std::fabs(a - b) <= within;
}

using kbm::Action;

void parsing() {
    kbm::Binds b;
    CHECK(kbm::parse_binds("KeyW", b) && b[0] == 26 && b[1] == 0);
    CHECK(kbm::parse_binds("KeyW,ArrowUp", b) && b[0] == 26 && b[1] == 82);
    CHECK(kbm::parse_binds(" KeyW , ArrowUp ", b) && b[0] == 26 && b[1] == 82);
    CHECK(kbm::parse_binds("", b) && b[0] == 0 && b[1] == 0);
    CHECK(kbm::parse_binds("MouseRight,WheelDown", b) && b[0] == OPENRAC_BIND_MOUSE_RIGHT && b[1] == OPENRAC_BIND_WHEEL_DOWN);
    CHECK(!kbm::parse_binds("KeyW,KeyW", b) && b[0] == 0);
    CHECK(!kbm::parse_binds("KeyW,KeyA,KeyS", b));
    CHECK(!kbm::parse_binds("Spacebar", b));
    CHECK(!kbm::parse_binds("keyw", b));  // names are as KeyboardEvent.code has them

    // A setting the mod cannot read is its default; the rest as written.
    std::map<std::string, std::string> settings = {{"jump", "KeyJ"}, {"fire", "NotAKey"}, {"walk", ""}};
    const kbm::Bindings bindings = kbm::read_bindings([&](const char* key) {
        const auto it = settings.find(key);
        return it == settings.end() ? std::string("?") : it->second;
    });
    CHECK(bindings.of(Action::Jump)[0] == 13);                          // J
    CHECK(bindings.of(Action::Fire)[0] == OPENRAC_BIND_MOUSE_LEFT);     // its default
    CHECK(bindings.of(Action::Walk)[0] == 0);                           // unbound on purpose
    CHECK(bindings.of(Action::MoveForward)[0] == 26);                   // "?": the default, W
}

void conflicts() {
    // The defaults conflict nowhere.
    const kbm::Bindings defaults = kbm::read_bindings([](const char*) { return std::string("?"); });
    CHECK(kbm::conflicts(defaults).empty());
    // The same key on two play actions is a conflict; on a play and a menu action it is not.
    kbm::Bindings b = defaults;
    b.binds[static_cast<std::size_t>(Action::CameraReset)] = {9, 0};  // F, the wrench's too
    const auto found = kbm::conflicts(b);
    CHECK(found.size() == 1 && found[0].a == Action::Wrench && found[0].b == Action::CameraReset && found[0].bind == 9);
    b = defaults;
    b.binds[static_cast<std::size_t>(Action::MenuBack)] = {41, 0};  // Escape, also Pause (play)
    CHECK(kbm::conflicts(b).empty());
    // Every default's name is one the table knows.
    for (const kbm::ActionInfo& a : kbm::kActions) {
        kbm::Binds parsed;
        CHECK(kbm::parse_binds(a.fallback, parsed) && parsed[0] != 0);
    }
}

void key_stick() {
    kbm::MoveTuning t;
    t.rise_ticks = 4.0f;
    kbm::KeyStick s;
    // W held: up (y < 0), a quarter more each tick, full after four.
    s.step(true, false, false, false, false, t);
    CHECK(near(s.y, -0.25f) && s.x == 0.0f);
    for (int i = 0; i < 5; ++i) {
        s.step(true, false, false, false, false, t);
    }
    CHECK(near(s.y, -1.0f));
    // W and D: the diagonal on the circle.
    for (int i = 0; i < 8; ++i) {
        s.step(true, false, false, true, false, t);
    }
    CHECK(near(s.x, 0.7071f) && near(s.y, -0.7071f));
    CHECK(near(std::sqrt(s.x * s.x + s.y * s.y), 1.0f));
    // Walking: half way.
    for (int i = 0; i < 8; ++i) {
        s.step(true, false, false, false, true, t);
    }
    CHECK(near(s.y, -0.5f) && near(s.x, 0.0f));
    // Let go: back twice as fast as it rose (from 0.5, at 0.5 a tick: at once).
    s.step(false, false, false, false, false, t);
    CHECK(s.x == 0.0f && s.y == 0.0f);
    // No ramp: full at once. Opposite keys cancel.
    t.rise_ticks = 0.0f;
    s.step(false, true, false, false, false, t);
    CHECK(s.y == 1.0f);
    s.step(true, true, false, false, false, t);
    CHECK(s.y == 0.0f);
}

void mouse_stick() {
    kbm::LookTuning t;
    kbm::MouseLook m;
    // A tick's motion at sensitivity 1: eased over two ticks, then proportional.
    m.step(12.0f, 0.0f, false, t);
    CHECK(near(m.x, 0.25f));
    m.step(12.0f, 0.0f, false, t);
    CHECK(near(m.x, 0.5f));
    m.step(100.0f, 0.0f, false, t);
    CHECK(m.x == 1.0f);  // clamped
    m.step(0.0f, 0.0f, false, t);
    m.step(0.0f, 0.0f, false, t);
    CHECK(m.x == 0.0f);  // the mouse at rest: the camera stops
    m.step(-24.0f, 0.0f, false, t);
    m.step(-24.0f, 0.0f, false, t);
    CHECK(near(m.x, -1.0f));
    // Sensitivity doubles it.
    t.sensitivity = 2.0f;
    m.reset();
    m.step(6.0f, 0.0f, false, t);
    m.step(6.0f, 0.0f, false, t);
    CHECK(near(m.x, 0.5f));
    // Acceleration: a fast motion goes further than twice a slow one's.
    t.sensitivity = 1.0f;
    t.acceleration = 1.0f;
    kbm::MouseLook slow, fast;
    slow.step(2.0f, 0.0f, false, t);
    slow.step(2.0f, 0.0f, false, t);
    fast.step(4.0f, 0.0f, false, t);
    fast.step(4.0f, 0.0f, false, t);
    CHECK(fast.x > 2.0f * slow.x);
    t.acceleration = 0.0f;
    // The tilt holds where the mouse left it; up (dy < 0) is a stick pushed up.
    m.reset();
    m.step(0.0f, -80.0f, false, t);
    CHECK(near(m.y, -0.5f));
    m.step(0.0f, 0.0f, false, t);
    CHECK(near(m.y, -0.5f));
    m.step(0.0f, -400.0f, false, t);
    CHECK(m.y == -1.0f);
    // Inverted.
    t.invert_y = true;
    m.reset();
    m.step(0.0f, -80.0f, false, t);
    CHECK(near(m.y, 0.5f));
    t.invert_y = false;
    // Levelling out when it rests.
    t.pitch_holds = false;
    m.reset();
    m.step(0.0f, 80.0f, false, t);
    for (int i = 0; i < 60; ++i) {
        m.step(0.0f, 0.0f, false, t);
    }
    CHECK(m.y == 0.0f);
    // First person: the tilt is a rate too.
    m.reset();
    m.step(0.0f, 12.0f, true, t);
    CHECK(near(m.y, 0.5f));
    m.step(0.0f, 0.0f, true, t);
    CHECK(m.y == 0.0f);
}

void quick_select() {
    // The game's ring: slot 0 at the top, clockwise.
    CHECK(kbm::ring_slot(0.0f, -1.0f) == 0);
    CHECK(kbm::ring_slot(0.7071f, -0.7071f) == 1);
    CHECK(kbm::ring_slot(1.0f, 0.0f) == 2);
    CHECK(kbm::ring_slot(0.7071f, 0.7071f) == 3);
    CHECK(kbm::ring_slot(0.0f, 1.0f) == 4);
    CHECK(kbm::ring_slot(-1.0f, 0.0f) == 6);
    CHECK(kbm::ring_slot(-0.7071f, -0.7071f) == 7);
    CHECK(kbm::ring_slot(0.0f, 0.0f) == -1);
    // The mouse's motion points the stick at the slot the way it moves; within the dead zone, none.
    const float dead = 30.0f;
    struct Case {
        float dx, dy;
        int slot;
    };
    const Case cases[] = {{0, -50, 0}, {40, -40, 1}, {50, 0, 2}, {35, 35, 3}, {0, 60, 4}, {-30, 30, 5}, {-45, 0, 6},
                          {-40, -40, 7}, {10, -10, -1}, {45, -10, 2}, {-5, 45, 4}};
    for (const Case& c : cases) {
        kbm::RingPointer p;
        p.add(c.dx, c.dy, dead);
        const auto [x, y] = p.stick(dead);
        CHECK(kbm::ring_slot(x, y) == c.slot);
        if (c.slot >= 0) {
            CHECK(near(std::sqrt(x * x + y * y), 1.0f, 1e-4f));  // past the game's 0.9
        }
    }
    // Small motions add up; the pointer is held within twice the dead zone, so turning back
    // answers soon: a long way right, then a little left, points left.
    kbm::RingPointer p;
    for (int i = 0; i < 8; ++i) {
        p.add(5.0f, 0.0f, dead);
    }
    CHECK(kbm::ring_slot(p.stick(dead).first, p.stick(dead).second) == 2);
    for (int i = 0; i < 100; ++i) {
        p.add(10.0f, 0.0f, dead);
    }
    CHECK(near(p.x, 60.0f));
    p.add(-100.0f, 0.0f, dead);
    CHECK(kbm::ring_slot(p.stick(dead).first, p.stick(dead).second) == 6);
}

void report() {
    // The game reads our stick bytes back as what we meant, dead zone and all.
    CHECK(kbm::stick_byte(0.0f) == 0x80);
    CHECK(kbm::game_axis(kbm::stick_byte(1.0f)) == 1.0f);
    CHECK(kbm::game_axis(kbm::stick_byte(-1.0f)) == -1.0f);
    for (float v = 0.05f; v <= 1.0f; v += 0.05f) {
        CHECK(near(kbm::game_axis(kbm::stick_byte(v)), v, 0.02f));
        CHECK(near(kbm::game_axis(kbm::stick_byte(-v)), -v, 0.02f));
    }
    // Buttons held on top of the pad's; a pad's stick kept where ours rests.
    std::uint8_t r[18];
    for (std::uint8_t& b : r) {
        b = 0;
    }
    r[0] = 0xFF;
    r[1] = 0xFF & ~(kbm::kSquare >> 8);  // the gamepad holds Square
    r[2] = r[3] = 0x80;
    r[4] = 0x10;  // the gamepad's left stick, pushed left
    r[5] = 0x80;
    kbm::Virtual v;
    v.held = kbm::kCross | kbm::kStart;
    v.right_x = 0.5f;
    kbm::merge(v, r, 18);
    const std::uint16_t buttons = static_cast<std::uint16_t>(r[0] | (r[1] << 8));
    CHECK((buttons & kbm::kCross) == 0 && (buttons & kbm::kStart) == 0 && (buttons & kbm::kSquare) == 0);
    CHECK((buttons & kbm::kCircle) != 0);
    CHECK(r[12] == 0xFF);  // Cross's pressure
    CHECK(r[13] == 0x00);  // Square's: the pad's own report says what it says
    CHECK(near(kbm::game_axis(r[2]), 0.5f) && r[3] == 0x80);
    CHECK(r[4] == 0x10 && r[5] == 0x80);  // ours at rest: the gamepad's
    v = kbm::Virtual{};
    v.left_y = -1.0f;
    kbm::merge(v, r, 18);
    CHECK(r[4] == 0x10 && kbm::game_axis(r[5]) == -1.0f);
}

void weapon_wheel() {
    const std::array<std::int32_t, 8> slots = {0x0F, 0, 0x0A, 0x10, 0, 0, 0, 0x17};
    CHECK(kbm::cycle_item(slots, 0x0F, 1) == 0x0A);   // skips the empty slot
    CHECK(kbm::cycle_item(slots, 0x17, 1) == 0x0F);   // wraps
    CHECK(kbm::cycle_item(slots, 0x0F, -1) == 0x17);
    CHECK(kbm::cycle_item(slots, 0x08, 1) == 0x0F);   // the wrench, not in the ring: the first
    CHECK(kbm::cycle_item(slots, 0x08, -1) == 0x17);  // or the last
    const std::array<std::int32_t, 8> one = {0, 0, 0x0F, 0, 0, 0, 0, 0};
    CHECK(kbm::cycle_item(one, 0x0F, 1) == 0x0F);
    const std::array<std::int32_t, 8> none = {};
    CHECK(kbm::cycle_item(none, 0x08, 1) == 0x08);
}

// Each action's setting in the manifest, with the table's default and group.
void manifest(const char* path) {
    std::ifstream f(path);
    CHECK(f.good());
    std::stringstream s;
    s << f.rdbuf();
    const std::string text = s.str();
    for (const kbm::ActionInfo& a : kbm::kActions) {
        const std::size_t at = text.find(std::string("\"key\": \"") + a.key + "\"");
        CHECK(at != std::string::npos);
        if (at == std::string::npos) {
            continue;
        }
        const std::size_t end = text.find('}', at);
        const std::string object = text.substr(at, end - at);
        CHECK(object.find("\"type\": \"keybind\"") != std::string::npos);
        CHECK(object.find(std::string("\"default\": \"") + a.fallback + "\"") != std::string::npos);
        CHECK(object.find(a.group == kbm::Group::Play ? "\"group\": \"play\"" : "\"group\": \"menu\"")
              != std::string::npos);
    }
}

}  // namespace

int main(int argc, char** argv) {
    parsing();
    conflicts();
    key_stick();
    mouse_stick();
    quick_select();
    report();
    weapon_wheel();
    if (argc > 1) {
        manifest(argv[1]);
    }
    if (g_failures == 0) {
        std::puts("keyboard-mouse logic: all passed");
    }
    return g_failures == 0 ? 0 : 1;
}
