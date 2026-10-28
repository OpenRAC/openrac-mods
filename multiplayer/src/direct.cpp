/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Direct connect's page logic (direct.hpp): ring rows, their acceleration, the address, the
 * hosting lines.
 */
#include "direct.hpp"

namespace mp {

int ring_wrap(long long value, const Ring& ring) {
    const long long size = static_cast<long long>(ring.hi) - ring.lo + 1;
    long long offset = (value - ring.lo) % size;

    // The remainder keeps the sign of the value: below lo comes in from hi.
    if (offset < 0) {
        offset += size;
    }

    return static_cast<int>(ring.lo + offset);
}

std::vector<std::string> ring_choices(int value, const Ring& ring) {
    std::vector<std::string> choices;
    choices.reserve(kRingWindow);

    for (int i = 0; i < kRingWindow; ++i) {
        const long long at =
            static_cast<long long>(value) + static_cast<long long>(i - kRingCentre) * ring.step;
        choices.push_back(std::to_string(ring_wrap(at, ring)));
    }

    return choices;
}

int ring_step(int value, int choice, const Ring& ring, int times) {
    const long long delta = static_cast<long long>(choice - kRingCentre) * ring.step * times;
    return ring_wrap(static_cast<long long>(value) + delta, ring);
}

int Acceleration::times(int item, std::uint64_t now_ms) {
    // Another row, or a pause since the last change: a new press.
    if (item != item_ || now_ms - last_ms_ > kHeldGapMs) {
        run_ = 0;
    } else {
        ++run_;
    }

    item_ = item;
    last_ms_ = now_ms;
    return run_ >= kHeldRepeats ? kFast : 1;
}

std::string DirectAddress::text() const {
    std::string text;

    for (std::size_t i = 0; i < octets.size(); ++i) {
        text += (i != 0 ? "." : "") + std::to_string(octets[i]);
    }

    return text + ":" + std::to_string(port);
}

std::vector<std::string> hosting_lines(
    const std::vector<std::string>& addresses, int port, std::size_t most
) {
    const std::string suffix = ":" + std::to_string(port);
    std::vector<std::string> lines;

    for (const std::string& address : addresses) {
        // The page has room for so many.
        if (lines.size() >= most) {
            break;
        }

        lines.push_back("Hosting on " + address + suffix);
    }

    // No address it can tell (or no room): the port alone.
    if (lines.empty()) {
        lines.push_back("Hosting on port " + std::to_string(port));
    }

    return lines;
}

}  // namespace mp
