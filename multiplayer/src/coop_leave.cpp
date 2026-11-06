/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.hpp): a player who leaves.
 */
#include "coop.hpp"
#include "coop_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace mp {

using namespace coop_util;

void Coop::tick_leaving(std::uint64_t now) {
    const int phase = game_.status().phase;

    if (to_title_) {
        if (in_level(phase)) {
            // In the level (playing, a scene, a menu over it): the game's way back to the title,
            // asked again while it has not gone (a travel still to happen, or refused).
            left_title_ = true;
            const std::uint64_t wait = title_taken_ ? kTravelMs : kLeaveMs;

            if (title_ms_ == 0 || now - title_ms_ >= wait) {
                title_ms_ = now;
                title_taken_ = game_.travel(-1);

                if (title_taken_) {
                    log("left the level for the title");
                }
            }
        } else if (phase == kPhaseLoading) {
            left_title_ = true;
        } else if (phase == kPhaseNone
                   || (phase == kPhaseFrontEnd
                       && (left_title_ || now - title_since_ >= kStartingMs))) {
            // In the title (or the game's start never left it): the Multiplayer page next.
            to_title_ = false;
            home_ = left_title_ && phase == kPhaseFrontEnd;
            home_since_ = now;
            home_ms_ = 0;
        }
    }

    // Back in the title: the Multiplayer page asked for until it shows (the main menu may not be
    // up yet), for a while, and not once the player left the title again.
    if (home_) {
        if (phase != kPhaseFrontEnd || now - home_since_ >= kHomeMs) {
            home_ = false;
        } else if (home_ms_ == 0 || now - home_ms_ >= kHomeRetryMs) {
            home_ms_ = now;
            game_.open_home();
        }
    }
}

}  // namespace mp
