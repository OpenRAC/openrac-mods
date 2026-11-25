// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Going Commando's aim over the shoulder, without the game: whether the aim is on (held or
// toggled), how far its look has come in, where the follow camera's eye goes for it, and where the
// shots go. No game memory here: shoulder_camera.cpp reads and writes the game, and the tests check
// this alone.

#pragma once

#include <algorithm>
#include <cmath>

namespace shoulder_camera::rac2 {

// How many ticks the aim's look takes to come in, and to go back out.
inline constexpr float kBlendTicks = 8.0f;

// The aim from one tick to the next: on while its key is held (or, with toggles, from one press to
// the next), only where it may be (on foot in play); its look comes in over kBlendTicks and goes
// back out as it ends.
struct Aim {
    bool toggled = false;  // with toggles: a press turned it on and none since turned it off
    bool on = false;       // aiming this tick
    float blend = 0.0f;    // the look: 0 none, 1 all of it

    // One tick: `held` the key (or the pad's button) is down, `pressed` it went down this tick,
    // `toggles` the setting, `allowed` the aim may be on here. Leaving where it may be ends it.
    void step(bool held, bool pressed, bool toggles, bool allowed) {
        // Held, or out of where it may be: no toggle kept; toggled, a press turns it over.
        if (!toggles || !allowed) {
            toggled = false;
        } else if (pressed) {
            toggled = !toggled;
        }

        on = allowed && (toggles ? toggled : held);
        blend = std::clamp(blend + (on ? 1.0f : -1.0f) / kBlendTicks, 0.0f, 1.0f);
    }
};

// The look's share eased in and out (smoothstep): slow at both ends.
inline float eased(float blend) {
    return blend * blend * (3.0f - 2.0f * blend);
}

// The follow camera's eye: its distance from Ratchet and its height above him (the camera looks at
// a point above him).
struct Eye {
    float distance;
    float height;
};

// The eye over the shoulder: from where it was as the aim began (`from`) and the view's pitch then
// (radians, > 0 looks up), it slides in along the line the view looks along to `share` of its
// distance as the look comes in (`blend`), so that the view keeps looking where it did: the
// distance shorter, the height lower by the pitch times the distance it came in.
inline Eye shoulder_eye(Eye from, float pitch, float share, float blend) {
    const float distance = from.distance * (1.0f + (share - 1.0f) * eased(blend));
    return {distance, from.height + std::tan(pitch) * (from.distance - distance)};
}

// The pitch of a view (radians, > 0 looks up) from its forward row's up part; 0 for none.
inline float pitch_of(float forward_z) {
    return std::isfinite(forward_z) ? std::asin(std::clamp(forward_z, -1.0f, 1.0f)) : 0.0f;
}

// The eye's distance goal at `share` of the game's: only a goal that is a distance (the game sets 0
// and less for its own reasons, which stay).
inline float distance_goal(float goal, float share) {
    return goal > 0.1f && std::isfinite(goal) ? goal * share : goal;
}

// Where the shots go, a point on the picture's line of fire: Going Commando's weapons fire level
// along the way Ratchet faces (`yaw`, radians), from his gun, so the point is `ahead` units in front
// of it at its height.
inline void aim_point(const float gun[3], float yaw, float ahead, float out[3]) {
    out[0] = gun[0] + ahead * std::cos(yaw);
    out[1] = gun[1] + ahead * std::sin(yaw);
    out[2] = gun[2];
}

// The hero's states the aim leaves alone (seen in game code, as OpenRAC's touch pad reads them):
// swimming, and grinding on a rail.
inline bool aims_in_state(int state) {
    const bool swimming = state >= 0x33 && state <= 0x37;
    const bool grinding = (state >= 0x28 && state <= 0x2B) || state == 0x42;
    return !swimming && !grinding;
}

}  // namespace shoulder_camera::rac2
