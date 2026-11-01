/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.hpp): the shots sent and shown.
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

void Coop::send_shots(const GameStatus& status) {
    // Taken each tick, so none is left over from before a level or a party.
    const std::vector<Shot> shots = game_.hero_shots();

    if (!in_level(status.phase) || play_ == 0) {
        return;
    }

    for (const Shot& shot : shots) {
        ShotMessage m;
        m.owner = session_.id();
        m.level = status.level;
        m.shot = shot;
        session_.send_data(session_.is_host() ? kBroadcast : session_.host(), encode_shot(m));
        ++shots_sent_;
    }
}

void Coop::on_shot(std::int32_t sender, const Bytes& bytes) {
    ShotMessage m;

    if (!decode_shot(bytes, m) || m.owner == session_.id() || !member_of(session_, m.owner)) {
        return;
    }

    if (session_.is_host()) {
        // As a pose: a member's own, passed on to everyone.
        if (sender != m.owner) {
            return;
        }

        session_.send_data(kBroadcast, bytes);
    } else if (sender != session_.host()) {
        return;
    }

    // A scene playing here: the others' figures are hidden, their shots dropped.
    if (game_.status().phase == kPhaseScene) {
        return;
    }

    Remote& r = remotes_[m.owner];

    if (r.shots.size() >= kShotsWaiting) {
        r.shots.pop_front();
    }

    // Fired when its figure is where the shooter was: as long after it came as its poses are
    // shown after they come (the delay less the transit).
    const auto now = static_cast<double>(clock_());
    r.shots.push_back({now + r.poses.behind_ms(), m.level, m.shot});
}

void Coop::fire_shots(Remote& r, std::uint64_t now, const GameStatus& status) {
    const auto t = static_cast<double>(now);

    while (!r.shots.empty() && r.shots.front().due_ms <= t) {
        const Remote::PendingShot shot = r.shots.front();
        r.shots.pop_front();

        // Another level's, one whose figure is not shown in time, or one in a scene here: dropped.
        if (shot.level != status.level || r.figure < 0 || r.hidden || status.phase == kPhaseScene
            || t - shot.due_ms > kShotLateMs) {
            continue;
        }

        if (game_.figure_shot(r.figure, shot.shot)) {
            ++shots_fired_;
        }
    }
}

}  // namespace mp
