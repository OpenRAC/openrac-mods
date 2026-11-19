// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The shared world's private helpers (world*.cpp): the wire's numbers and the pose test.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "protocol.hpp"
#include "world.hpp"

namespace mp {
namespace wire {

// Writes `v` as 16 bits, high byte first.
inline void u16(Writer& w, std::uint16_t v) {
    w.u8(static_cast<std::uint8_t>(v >> 8)).u8(static_cast<std::uint8_t>(v));
}

// Reads 16 bits, high byte first, as the writer wrote them.
inline std::uint16_t u16(Reader& r) {
    const auto high = static_cast<std::uint16_t>(r.u8() << 8);
    return static_cast<std::uint16_t>(high | r.u8());
}

// Writes `f` as the 32 bits of its IEEE form.
inline void f32(Writer& w, float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    w.i32(static_cast<std::int32_t>(u));
}

// Reads a float as the writer wrote it; a value that is not finite reads as 0.
inline float f32(Reader& r) {
    const auto u = static_cast<std::uint32_t>(r.i32());
    float f = 0;
    std::memcpy(&f, &u, sizeof f);
    return std::isfinite(f) ? f : 0.0f;
}

// Writes three floats, one after the other.
inline void vec(Writer& w, const float* v) {
    for (int i = 0; i < 3; ++i) {
        f32(w, v[i]);
    }
}

// Reads three floats, one after the other.
inline void vec(Reader& r, float* v) {
    for (int i = 0; i < 3; ++i) {
        v[i] = f32(r);
    }
}

// Returns `radians` as a signed 16-bit count of 1/10,000 radian, clamped to what 16 bits hold.
inline std::int16_t angle_bits(float radians) {
    return static_cast<std::int16_t>(std::clamp(std::lround(radians * 10000.0f), -32768L, 32767L));
}

// Returns the squared distance between two points of three floats.
inline float distance2(const float* a, const float* b) {
    float sum = 0;

    for (int i = 0; i < 3; ++i) {
        sum += (a[i] - b[i]) * (a[i] - b[i]);
    }

    return sum;
}

// Returns whether pose `b` differs from `a` enough to be sent again. That is a move of 0.02 or more
// (the distance is squared, 0.0004), a turn of 0.01 radian, or another sequence or frame.
inline bool changed(const Pose& a, const Pose& b) {
    return distance2(a.position, b.position) > 0.0004f
           || std::fabs(a.rotation[2] - b.rotation[2]) > 0.01f
           || std::fabs(a.rotation[0] - b.rotation[0]) > 0.01f
           || std::fabs(a.rotation[1] - b.rotation[1]) > 0.01f || a.seq != b.seq
           || a.frame != b.frame;
}

}  // namespace wire
}  // namespace mp
