// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The co-op's private helpers (coop*.cpp): the pose step, the animation trace and the member test.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "coop.hpp"

namespace mp {
namespace coop_util {

// Returns the straight-line distance between the positions of `a` and `b`, in the level's units.
// Rotation is not part of it.
inline double step(const Pose& a, const Pose& b) {
    double sum = 0.0;

    // The distance is over x, y and z.
    for (int i = 0; i < 3; ++i) {
        const double d = a.position[i] - b.position[i];
        sum += d * d;
    }

    return std::sqrt(sum);
}

// OPENRAC_ANIM_TRACE set: the poses sent, received and shown, a line each on stderr (ATRACE).
inline bool tracing() {
    static const bool on = std::getenv("OPENRAC_ANIM_TRACE") != nullptr;
    return on;
}

// Writes one ATRACE line to stderr for the pose `p`. `what` names the place it was seen at ("R"
// received, "O" shown, "H" the hero's own), `now` is the game's clock in ms, and `extra` is a
// second number for the line: the pose's time on this game's clock for "R", the delay of the poses'
// buffer for "O", 0 for "H". Called on the game's tick thread, and only when tracing().
inline void trace(const char* what, double now, double extra, const Pose& p) {
    std::fprintf(
        stderr,
        "ATRACE %s %.1f %.1f %d %u %.3f %.3f %.3f %.4f %.3f %.3f %.3f %d %u %u %u %.4f %.4f\n",
        what,
        now,
        extra,
        p.level,
        p.flags,
        p.position[0],
        p.position[1],
        p.position[2],
        p.rotation[2],
        p.velocity[0],
        p.velocity[1],
        p.velocity[2],
        p.state,
        p.seq,
        p.frame,
        p.next_frame,
        p.anim_time,
        p.anim_speed
    );
}

// Returns whether `id` is one of the members the session holds now.
inline bool member_of(const Session& session, std::int32_t id) {
    const std::vector<Member>& members = session.members();
    return std::any_of(members.begin(), members.end(), [&](const Member& m) { return m.id == id; });
}

}  // namespace coop_util
}  // namespace mp
