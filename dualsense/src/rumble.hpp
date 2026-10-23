// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The vibration the controller plays: a list of steps, each with a strength for the heavy
// motor and one for the light one, and a length in milliseconds.
//
// Ratchet & Clank 1 asks for no vibration at all: the retail program links no motor command
// (the linker dead-stripped sceVibSetActParam, games/rac1/src/core/00125218.c), and the
// waveform mixer that would have driven it has no caller. So the vibration here is the mod's
// own: it makes one out of what the game does.

#pragma once

#include <cstdint>
#include <vector>

namespace dualsense {

// One step of a vibration.
struct RumbleStep {
    std::uint16_t heavy = 0;  // the body motor, 0..65535
    std::uint16_t light = 0;  // the light motor, 0..65535
    std::uint32_t ms = 0;     // how long it lasts
};

// The strengths the motors take in SDL's terms: 0..65535, where 65535 is full.
inline constexpr std::uint16_t kFull = 0xFFFF;

// A vibration playing: the steps left and the step now.
class Rumble {
public:
    // Starts a vibration: the steps play in order, each for its length. A vibration already
    // playing is replaced.
    void play(std::vector<RumbleStep> steps);

    // Goes on to the next step when its time is up. `now` is in milliseconds; it only has to
    // grow. Returns true while something is playing.
    bool update(std::uint32_t now);

    // The strengths now: what the motors should be at.
    std::uint16_t heavy() const { return heavy_; }

    std::uint16_t light() const { return light_; }

    // True while a vibration is playing.
    bool playing() const { return index_ < steps_.size(); }

    // Stops the vibration where it is.
    void stop();

private:
    std::vector<RumbleStep> steps_;
    std::size_t index_ = 0;
    std::uint32_t until_ = 0;    // when the current step ends
    std::uint32_t started_ = 0;  // when the current step began
    std::uint16_t heavy_ = 0;
    std::uint16_t light_ = 0;
};

// The vibration for an event, with `strength` a percentage of the full strength.
//
// `kind` is one of:
//   "hit"    the player hit something: a short thump
//   "damage" the player was hit: a sharp crack and a heavier thump under it
//   "break"  something broke: a soft thump
//   "pickup" something was picked up: a short blip
//   "gold"   a gold bolt: two blips, the second stronger
//   "land"   the player landed: a thump that grows with the fall
// `fall_ms` is how long the fall was, for "land"; it is ignored otherwise.
std::vector<RumbleStep> rumble_for(const char* kind, int strength, std::uint32_t fall_ms = 0);

}  // namespace dualsense
