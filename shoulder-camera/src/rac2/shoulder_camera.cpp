// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Going Commando's part of the shoulder camera (README.md): the aim over the shoulder and the
// camera's distance. The aim, held or toggled on foot in play, holds L2 in pad 0's report (the
// game's strafe: Ratchet faces where the camera looks and the stick moves him sideways), and the
// follow camera's eye slides in along the line it looks along (rac2.camera.follow.read_stick: the
// tick's goals are set by then, and the camera eases to what is written after the game's read).
// Over the picture, the screen's edges darken and a crosshair shows where the shots go. Out of the
// aim, the camera's distance goal is the setting's share of the game's. view.hpp holds the maths.

#include "rac2/shoulder_camera.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "openrac/games/rac2.hpp"
#include "rac2/discs.hpp"
#include "rac2/view.hpp"

namespace shoulder_camera::rac2 {

namespace {

// The game's memory: 32 MiB, and the lowest address a camera's data may be at.
constexpr std::uint32_t kRamBytes = 32u << 20;
constexpr std::uint32_t kLowestData = 0x00100000u;
// How many ticks after the follow camera last ran the aim may still begin: it runs every tick on
// foot, and a menu or a scene stops it.
constexpr int kFollowSeenTicks = 2;

// The pad report (as the port's on_pad gives it): the buttons in bytes 0 and 1, a bit clear while
// held; L2 is bit 8 (the console's order), and its pressure byte is the report's byte 16.
constexpr std::uint16_t kL2 = 1u << 8;
constexpr int kL2Pressure = 16;

// The overlay's picture (openrac_overlay), and the aim's look on it: the vignette's bands, how far
// in from the edges they reach and how dark the outermost is (of 255).
constexpr float kPictureWidth = 512.0f;
constexpr float kPictureHeight = 448.0f;
constexpr int kVignetteBands = 12;
constexpr float kVignetteReach = 0.2f;
constexpr float kVignetteAlpha = 150.0f;
// The crosshair: its arms, the gap between them and their thickness, its opacity (of 255); how far
// ahead of the gun it shows where the shots go; the gun's height above where Ratchet stands when his
// hand holds none.
constexpr float kCrossArm = 6.0f;
constexpr float kCrossGap = 3.0f;
constexpr float kCrossThickness = 1.5f;
constexpr float kCrossAlpha = 230.0f;
constexpr float kAimAhead = 30.0f;
constexpr float kChestHeight = 1.0f;

// The settings' bounds, as the manifest's.
constexpr float kShareLeast = 0.4f;
constexpr float kShareMost = 0.9f;
constexpr float kDistanceLeast = 0.6f;
constexpr float kDistanceMost = 1.15f;

// The mod and its disc, set at load.
openrac::Mod* s_mod = nullptr;
const Disc* s_disc = nullptr;

// The settings, read at load and again when they change.
struct Settings {
    int binds[2] = {};      // the aim's keys or mouse buttons (setting aim), 0 for none
    bool pad_l2 = false;    // the pad's L2 aims too (setting pad_l2)
    bool toggles = false;   // a press aims and another stops (setting aim_toggle)
    float share = 0.6f;     // the eye's distance while aiming (setting shoulder_distance)
    float distance = 1.0f;  // the camera's distance out of the aim (setting camera_distance)
    bool crosshair = true;  // setting crosshair
    bool vignette = true;   // setting vignette
};

Settings s_set;
Aim s_aim;
// The ticks since the follow camera last ran; kFollowSeenTicks + 1 and more: not lately.
int s_since_follow = kFollowSeenTicks + 1;
// The pad's L2 at the last tick, for its presses.
bool s_pad_l2_was = false;
// The eye as the aim began (a distance of 0: none taken yet), and the view's pitch then.
Eye s_from{0.0f, 0.0f};
float s_pitch = 0.0f;

// A setting's share kept within its bounds; `fallback` for none.
float clamp_share(double v, float least, float most, float fallback) {
    const auto f = static_cast<float>(v);
    return std::isfinite(f) && f > 0.0f ? std::fmin(std::fmax(f, least), most) : fallback;
}

// The settings as the launcher (or the game) has them now.
void read_settings(const openrac::Mod& mod) {
    Settings s;
    const std::string binds = mod.setting<std::string>("aim");
    std::size_t at = 0;

    // Up to two binds, by their names with a comma between.
    for (int& bind : s.binds) {
        // No name left.
        if (at > binds.size()) {
            break;
        }

        std::size_t end = binds.find(',', at);
        end = end == std::string::npos ? binds.size() : end;
        bind = openrac::bind_from_name(std::string_view(binds).substr(at, end - at));
        at = end + 1;
    }

    s.pad_l2 = mod.setting<bool>("pad_l2");
    s.toggles = mod.setting<bool>("aim_toggle");
    s.share = clamp_share(mod.setting<double>("shoulder_distance"), kShareLeast, kShareMost, 0.6f);
    s.distance =
        clamp_share(mod.setting<double>("camera_distance"), kDistanceLeast, kDistanceMost, 1.0f);
    s.crosshair = mod.setting<bool>("crosshair");
    s.vignette = mod.setting<bool>("vignette");
    s_set = s;
}

// Whether the aim may be on: in play (not paused, no dialog, no scene), the follow camera running,
// Ratchet on foot (not swimming, grinding, as Giant Clank or dying).
bool aim_allowed() {
    const openrac::Memory m = s_mod->memory();

    // Not in play, the follow camera not running, or no hero on his feet.
    if (s_since_follow > kFollowSeenTicks
        || m.read<std::int32_t>(s_mod->relocate(s_disc->game_state)) != 0
        || m.read<std::uint32_t>(s_disc->hero_moby) == 0
        || m.read<std::int32_t>(s_disc->health) <= 0
        || m.read<std::uint8_t>(s_disc->hero_state + kHeroVehicle) == kGiantClank) {
        return false;
    }

    return aims_in_state(m.read<std::int32_t>(s_disc->hero_state));
}

// Pad 0's report, once a tick before the game reads it: the aim's key or the pad's L2, and while
// the aim is on, L2 held for the game's strafe.
void on_pad(int port, std::uint8_t* report, int size) {
    // Only the first pad's, which the keyboard drives too.
    if (port != 0 || report == nullptr || size < 2) {
        return;
    }

    // A tick more since the follow camera last ran (read_stick counts from 0 again).
    if (s_since_follow <= kFollowSeenTicks) {
        ++s_since_follow;
    }

    const auto buttons = static_cast<std::uint16_t>(report[0] | (report[1] << 8));
    const bool pad_l2 = s_set.pad_l2 && (buttons & kL2) == 0;
    const openrac::Input in = s_mod->input();
    bool held = pad_l2;
    bool pressed = pad_l2 && !s_pad_l2_was;
    s_pad_l2_was = pad_l2;

    // The keys count while the game's window has them and the port's own menu does not.
    if (in.focused() && !in.overlay()) {
        // Either of the aim's binds.
        for (int bind : s_set.binds) {
            held = held || in.held(bind);
            pressed = pressed || in.pressed(bind);
        }
    }

    s_aim.step(held, pressed, s_set.toggles, aim_allowed());

    // Not aiming: the report stays as it is.
    if (!s_aim.on) {
        return;
    }

    const auto with_l2 = static_cast<std::uint16_t>(buttons & ~kL2);
    report[0] = static_cast<std::uint8_t>(with_l2);
    report[1] = static_cast<std::uint8_t>(with_l2 >> 8);

    // L2's pressure too, where the report has it.
    if (size > kL2Pressure) {
        report[kL2Pressure] = 0xFF;
    }
}

// The follow camera's stick read (rac2.camera.follow.read_stick): the game's read, then, while the
// aim's look is in, the eye slid in along its view line (its distance and height and their goals,
// so the camera does not ease back), and out of it the distance goal at the setting's share. Once
// the look is gone the game's own goals take the eye back, as the game eases them.
void read_stick(std::uint32_t camera) {
    s_since_follow = 0;
    openrac::Memory m = s_mod->memory();
    const auto data = m.read<std::uint32_t>(camera + kCameraData);

    // A camera with no data of its own.
    if (data < kLowestData || data >= kRamBytes - 0x200u) {
        return;
    }

    // Out of the aim: the camera's distance.
    if (!(s_aim.blend > 0.0f)) {
        s_from.distance = 0.0f;
        const float goal = m.read<float>(data + kDistanceGoal);

        // At 1 the game's own goal stands.
        if (s_set.distance != 1.0f) {
            m.write<float>(data + kDistanceGoal, distance_goal(goal, s_set.distance));
        }

        return;
    }

    // The aim begins: where the eye is and where the view looks.
    if (!(s_from.distance > 0.1f)) {
        s_from = {m.read<float>(data + kDistance), m.read<float>(data + kHeight)};
        s_pitch = pitch_of(m.read<float>(s_mod->relocate(s_disc->camera_matrix) + kForwardZ));

        // No eye to start from: the aim leaves the camera alone.
        if (!(s_from.distance > 0.1f) || !std::isfinite(s_from.height)) {
            s_from.distance = 0.0f;
            return;
        }
    }

    const Eye eye = shoulder_eye(s_from, s_pitch, s_set.share, s_aim.blend);
    m.write<float>(data + kDistance, eye.distance);
    m.write<float>(data + kDistanceGoal, eye.distance);
    m.write<float>(data + kHeight, eye.height);
    m.write<float>(data + kHeightGoal, eye.height);
}

// The crosshair where the shots go: ahead of the gun (or of his chest when his hand holds none),
// along the way Ratchet faces; `alpha` its opacity (of 255).
void draw_crosshair(const openrac_overlay* o, std::uint32_t alpha) {
    const openrac::Memory m = s_mod->memory();
    const std::uint32_t at = s_disc->hero + kHeroPosition;
    float gun[3] =
        {m.read<float>(at), m.read<float>(at + 4u), m.read<float>(at + 8u) + kChestHeight};
    openrac_held held{};

    // The gun in his hand, where the port says.
    if (s_mod->hero_held(held) && held.count > 0) {
        // Its place, a coordinate at a time.
        for (int i = 0; i < 3; ++i) {
            gun[i] = held.pieces[0].position[i];
        }
    }

    const float yaw = m.read<float>(s_disc->hero + kHeroYaw);
    float ahead[3] = {};
    float x = 0.0f;
    float y = 0.0f;
    aim_point(gun, yaw, kAimAhead, ahead);

    // No facing, or the point is not on the picture.
    if (!std::isfinite(yaw) || o->project(o->context, ahead, &x, &y) == 0) {
        return;
    }

    // The four arms and a dot, white over a shadow a little down and right.
    const float t = kCrossThickness;

    // The shadow first, then the white.
    for (int pass = 0; pass < 2; ++pass) {
        const float s = pass == 0 ? t * 0.5f : 0.0f;
        const std::uint32_t rgba = pass == 0 ? (alpha * 3u / 5u) : (0xFFFFFF00u | alpha);
        o->rect(o->context, x - kCrossGap - kCrossArm + s, y - t * 0.5f + s, kCrossArm, t, rgba);
        o->rect(o->context, x + kCrossGap + s, y - t * 0.5f + s, kCrossArm, t, rgba);
        o->rect(o->context, x - t * 0.5f + s, y - kCrossGap - kCrossArm + s, t, kCrossArm, rgba);
        o->rect(o->context, x - t * 0.5f + s, y + kCrossGap + s, t, kCrossArm, rgba);
        o->rect(o->context, x - t * 0.5f + s, y - t * 0.5f + s, t, t, rgba);
    }
}

// The aim on the picture, once a frame while it is on or its look fades: the edges darkened in
// bands, deeper as the look comes in, and while it is on the crosshair.
void draw_aim(const openrac_overlay* o) {
    const float b = s_aim.blend;

    // No look on the picture.
    if (o == nullptr || !(b > 0.0f)) {
        return;
    }

    const float in = eased(b);
    const float sx = kPictureWidth * kVignetteReach / static_cast<float>(kVignetteBands);
    const float sy = kPictureHeight * kVignetteReach / static_cast<float>(kVignetteBands);

    // The bands from the edges in, lighter each.
    for (int i = 0; i < kVignetteBands && s_set.vignette; ++i) {
        const float out = 1.0f - static_cast<float>(i) / static_cast<float>(kVignetteBands);
        const auto alpha = static_cast<std::uint32_t>(kVignetteAlpha * in * out * out);
        const float dx = sx * static_cast<float>(i);
        const float dy = sy * static_cast<float>(i);
        const float w = kPictureWidth - 2.0f * dx;
        const float h = kPictureHeight - 2.0f * dy - 2.0f * sy;
        o->rect(o->context, dx, dy, w, sy, alpha);
        o->rect(o->context, dx, kPictureHeight - dy - sy, w, sy, alpha);
        o->rect(o->context, dx, dy + sy, sx, h, alpha);
        o->rect(o->context, kPictureWidth - dx - sx, dy + sy, sx, h, alpha);
    }

    // The crosshair while aiming, not as the look fades.
    if (s_aim.on && s_set.crosshair) {
        draw_crosshair(o, static_cast<std::uint32_t>(kCrossAlpha * in));
    }
}

}  // namespace

void load(openrac::Mod& mod) {
    s_disc = disc_of(mod.version());

    // A disc the mod has no addresses for.
    if (s_disc == nullptr) {
        throw openrac::Error(
            "shoulder-camera does not know Going Commando's " + std::string(mod.version()) + " disc"
        );
    }

    s_mod = &mod;
    s_aim = {};
    s_since_follow = kFollowSeenTicks + 1;
    s_pad_l2_was = false;
    s_from = {0.0f, 0.0f};
    read_settings(mod);
    mod.hook<openrac::rac2::FollowCamera_ReadStick>(
        [](auto original, std::uint32_t camera, std::uint32_t turn, std::uint32_t tilt) {
            original(camera, turn, tilt);
            read_stick(camera);
        }
    );
    mod.on_pad([](int port, std::uint8_t* report, int size) { on_pad(port, report, size); });
    mod.on_overlay([](void*, const openrac_overlay* o) { draw_aim(o); });
    mod.on_settings([] { read_settings(*s_mod); });
    mod.info("the shoulder camera on the " + std::string(mod.version()) + " disc");
}

}  // namespace shoulder_camera::rac2
