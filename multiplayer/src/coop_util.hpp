/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The co-op's private helpers (coop*.cpp): the pose step, the animation trace and the member test.
 */
#pragma once

#include "coop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace mp {
namespace coop_util {

inline double step(const Pose& a, const Pose& b) {
    double sum = 0.0;

    for (int i = 0; i < 3; ++i) {
        const double d = a.position[i] - b.position[i];
        sum += d * d;
    }

    return std::sqrt(sum);
}

/* OPENRAC_ANIM_TRACE set: the poses sent, received and shown, a line each on stderr (ATRACE). */
inline bool tracing() {
    static const bool on = std::getenv("OPENRAC_ANIM_TRACE") != nullptr;
    return on;
}

inline void trace(const char* what, double now, double extra, const Pose& p) {
    std::fprintf(stderr,
                 "ATRACE %s %.1f %.1f %d %u %.3f %.3f %.3f %.4f %.3f %.3f %.3f %d %u %u %u %.4f %.4f\n",
                 what, now, extra, p.level, p.flags, p.position[0], p.position[1], p.position[2],
                 p.rotation[2], p.velocity[0], p.velocity[1], p.velocity[2], p.state, p.seq, p.frame,
                 p.next_frame, p.anim_time, p.anim_speed);
}

inline bool member_of(const Session& session, std::int32_t id) {
    const std::vector<Member>& members = session.members();
    return std::any_of(members.begin(), members.end(), [&](const Member& m) { return m.id == id; });
}

}  // namespace coop_util
}  // namespace mp
