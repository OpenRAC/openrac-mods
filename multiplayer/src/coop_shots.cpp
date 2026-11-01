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

void Coop::queue_shot(const ShotEntry& shot) {
    for (auto it = shots_out_.rbegin(); it != shots_out_.rend(); ++it) {
        if (shot_replaces(shot, it->entry)) {
            it->entry = shot;
            return;
        }
    }

    shots_out_.push_back({shot, clock_()});
}

void Coop::send_queued_shots(std::uint64_t now) {
    while (!shots_out_.empty() && now - shots_out_.front().queued_ms > kShotHoldMs) {
        shots_out_.pop_front();
    }

    const std::uint64_t every = session_.is_host() ? kShotSendHostMs : kShotSendMemberMs;

    if (shots_out_.empty() || now - shots_sent_ms_ < every) {
        return;
    }

    std::vector<ShotEntry> entries;

    for (std::size_t i = 0; i < shots_out_.size() && entries.size() < kShotXMax; ++i) {
        entries.push_back(shots_out_[i].entry);
    }

    // A state the rates refuse is not sent: these go in the next one, or expire.
    if (session_.send_state(session_.is_host() ? kBroadcast : session_.host(), encode_shotx(entries))) {
        shots_out_.erase(shots_out_.begin(), shots_out_.begin() + static_cast<std::ptrdiff_t>(entries.size()));
        shots_sent_ms_ = now;
    }
}

void Coop::send_shots(const GameStatus& status) {
    // Taken each tick, so none is left over from before a level or a party.
    const std::vector<Shot> shots = game_.hero_shots();

    if (!in_level(status.phase) || play_ == 0) {
        shots_out_.clear();
        return;
    }

    const std::uint64_t now = clock_();
    const auto stamp =
        static_cast<std::uint32_t>(session_.has_clock() ? session_.server_ms() : static_cast<std::int64_t>(now));

    for (const Shot& shot : shots) {
        ShotEntry e;
        e.owner = session_.id();
        e.level = status.level;
        e.stamp = stamp;
        e.shot = shot;
        queue_shot(e);
        ++shots_sent_;
    }

    send_queued_shots(now);
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

    ShotEntry e;
    e.owner = m.owner;
    e.level = m.level;
    e.stamp = static_cast<std::uint32_t>(session_.has_clock() ? session_.server_ms() : static_cast<std::int64_t>(clock_()));
    e.shot = m.shot;
    take_shot(e);
}

void Coop::on_shotx(std::int32_t sender, const Bytes& bytes) {
    std::vector<ShotEntry> entries;

    if (!decode_shotx(bytes, entries)) {
        return;
    }

    for (const ShotEntry& e : entries) {
        if (e.owner == session_.id() || !member_of(session_, e.owner)) {
            continue;
        }

        if (session_.is_host()) {
            // As a pose: a member's own, passed on to everyone with its own stamp.
            if (sender != e.owner) {
                continue;
            }

            queue_shot(e);
        } else if (sender != session_.host()) {
            continue;
        }

        take_shot(e);
    }
}

void Coop::take_shot(const ShotEntry& e) {
    // A scene playing here: the others' figures are hidden, their shots dropped.
    if (game_.status().phase == kPhaseScene) {
        return;
    }

    Remote& r = remotes_[e.owner];

    if (r.shots.size() >= kShotsWaiting) {
        r.shots.pop_front();
    }

    // Fired when its figure is where the shooter was: a stamp in this game's clock (the stamp is
    // the low bits of the server's, or of the sender's clock while none is known), as far behind
    // as its poses are shown.
    const auto now = static_cast<double>(clock_());
    double time = now;

    if (session_.has_clock()) {
        const std::int64_t server = session_.server_ms();
        const auto ago = static_cast<std::int32_t>(static_cast<std::uint32_t>(server) - e.stamp);
        time = static_cast<double>(server - ago) - session_.clock_offset_ms();
    }

    r.shots.push_back({std::max(now, time + r.poses.delay_ms()), e.level, e.shot, time + r.poses.delay_ms()});
}

void Coop::fire_shots(Remote& r, std::uint64_t now, const GameStatus& status) {
    const auto t = static_cast<double>(now);

    while (!r.shots.empty() && r.shots.front().due_ms <= t) {
        const Remote::PendingShot shot = r.shots.front();
        r.shots.pop_front();

        // Too late to look like what it was: dropped. An end still counts: a track left on would
        // last until it goes silent.
        if (t - shot.shown_ms > kShotLateMs && (shot.shot.flags & kShotStop) == 0) {
            ++shots_late_;
            continue;
        }

        // Another level's, one whose figure is not shown in time, or one in a scene here: dropped.
        if (shot.level != status.level || r.figure < 0 || r.hidden || status.phase == kPhaseScene) {
            continue;
        }

        if (game_.figure_shot(r.figure, shot.shot)) {
            ++shots_fired_;
        }
    }
}

}  // namespace mp
