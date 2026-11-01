/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The shared world's private helpers (world*.cpp): the wire's numbers and the pose test.
 */
#pragma once

#include "protocol.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace mp {
namespace wire {

inline void u16(Writer& w, std::uint16_t v) {
    w.u8(static_cast<std::uint8_t>(v >> 8)).u8(static_cast<std::uint8_t>(v));
}

inline std::uint16_t u16(Reader& r) {
    const auto high = static_cast<std::uint16_t>(r.u8() << 8);
    return static_cast<std::uint16_t>(high | r.u8());
}

inline void f32(Writer& w, float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    w.i32(static_cast<std::int32_t>(u));
}

inline float f32(Reader& r) {
    const auto u = static_cast<std::uint32_t>(r.i32());
    float f = 0;
    std::memcpy(&f, &u, sizeof f);
    return std::isfinite(f) ? f : 0.0f;
}

inline void vec(Writer& w, const float* v) {
    for (int i = 0; i < 3; ++i) {
        f32(w, v[i]);
    }
}

inline void vec(Reader& r, float* v) {
    for (int i = 0; i < 3; ++i) {
        v[i] = f32(r);
    }
}

inline std::int16_t angle_bits(float radians) {
    return static_cast<std::int16_t>(std::clamp(std::lround(radians * 10000.0f), -32768L, 32767L));
}

inline float distance2(const float* a, const float* b) {
    float sum = 0;
    for (int i = 0; i < 3; ++i) {
        sum += (a[i] - b[i]) * (a[i] - b[i]);
    }
    return sum;
}

inline bool changed(const Pose& a, const Pose& b) {
    return distance2(a.position, b.position) > 0.0004f
           || std::fabs(a.rotation[2] - b.rotation[2]) > 0.01f
           || std::fabs(a.rotation[0] - b.rotation[0]) > 0.01f
           || std::fabs(a.rotation[1] - b.rotation[1]) > 0.01f || a.seq != b.seq || a.frame != b.frame;
}


}  // namespace wire
}  // namespace mp
