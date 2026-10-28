/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The verbose log (trace.hpp): its lines, at most kTraceLinesPerSecond a second.
 */
#include "trace.hpp"

#include <cstdio>

namespace mp {

std::string xyz(const float* v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.1f %.1f %.1f", v[0], v[1], v[2]);
    return b;
}

void Trace::flush() {
    // A new second: what the last one left out, in one line.
    if (now_ - second_ms_ < 1000) {
        return;
    }

    if (dropped_ > 0 && sink_) {
        sink_(
            "[mp-trace] f=" + std::to_string(frame_) + " t=" + std::to_string(now_) + " " + side_
            + " trace dropped " + std::to_string(dropped_) + " lines in the last second (at most "
            + std::to_string(kTraceLinesPerSecond) + " a second)"
        );
    }

    second_ms_ = now_;
    lines_ = 0;
    dropped_ = 0;
}

void Trace::tick(std::uint64_t now, const std::string& side) {
    ++frame_;
    now_ = now;
    side_ = side;
    flush();
}

void Trace::line(const std::string& what, const std::string& text) {
    if (!sink_) {
        return;
    }

    if (lines_ >= kTraceLinesPerSecond) {
        ++dropped_;
        ++dropped_total_;
        return;
    }

    ++lines_;
    ++written_;
    sink_(
        "[mp-trace] f=" + std::to_string(frame_) + " t=" + std::to_string(now_) + " " + side_ + " "
        + what + " " + text
    );
}

}  // namespace mp
