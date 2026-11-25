// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The line NG++ shows at the top of the game's picture (the port's play_notice). The port keeps a
// line up until the mod replaces or clears it, so every line NG++ shows goes through here: each
// has its time, and the line clears itself when it is up.

#pragma once

#include <functional>
#include <string>
#include <utility>

namespace ng_plus_plus {

// The time a line stays up, in sixtieths of a second (seconds * 60, as discs.h's times are).
constexpr int kNoticeSixtieths = 150;

// One line at a time: a new line replaces the one up. Only the game's thread uses it.
class Notices {
public:
    // Shows (a non-empty text) or clears (an empty one) the line on the port.
    using Sink = std::function<void(const std::string&)>;

    explicit Notices(Sink sink) : m_sink(std::move(sink)) {}

    // Shows `text` for `ticks` game ticks, in place of the line up.
    void show(const std::string& text, int ticks) {
        m_left = ticks > 0 ? ticks : 1;
        m_sink(text);
    }

    // Counts one game tick; clears the line when its time is up.
    void tick() {
        if (m_left == 0) {
            return;
        }

        // The line's time is up: cleared.
        if (--m_left == 0) {
            m_sink("");
        }
    }

    // Returns whether a line is up.
    bool up() const { return m_left > 0; }

private:
    Sink m_sink;
    int m_left = 0;  // Ticks the line has left; 0 none.
};

}  // namespace ng_plus_plus
