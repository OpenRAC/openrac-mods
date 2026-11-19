// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together (coop.hpp): the shots sent and shown.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "coop.hpp"
#include "coop_util.hpp"

namespace mp {

using namespace coop_util;

void Coop::queue_shot(const ShotEntry& shot) {
    // A queue held back by the rates keeps only where a track is now, not everywhere it has been.
    for (auto it = shots_out_.rbegin(); it != shots_out_.rend(); ++it) {
        // The newer move of the same track takes the older one's place.
        if (shot_replaces(shot, it->entry)) {
            it->entry = shot;
            return;
        }
    }

    shots_out_.push_back({shot, clock_()});
}

void Coop::send_queued_shots(std::uint64_t now) {
    // A shot held longer than kShotHoldMs is dropped: a late flash reads as a bug.
    while (!shots_out_.empty() && now - shots_out_.front().queued_ms > kShotHoldMs) {
        shots_out_.pop_front();
    }

    const std::uint64_t every = session_.role().host() ? kShotSendHostMs : kShotSendMemberMs;

    // Nothing goes while the queue is empty or the last send is too recent.
    if (shots_out_.empty() || now - shots_sent_ms_ < every) {
        return;
    }

    std::vector<ShotEntry> entries;

    // One SHOTX holds at most kShotXMax shots; the rest wait for the next.
    for (std::size_t i = 0; i < shots_out_.size() && entries.size() < kShotXMax; ++i) {
        entries.push_back(shots_out_[i].entry);
    }

    // A state the rates refuse is not sent: these go in the next one, or expire.
    if (session_.send_state(
            session_.role().host() ? kBroadcast : session_.host(), encode_shotx(entries)
        )) {
        shots_out_.erase(
            shots_out_.begin(), shots_out_.begin() + static_cast<std::ptrdiff_t>(entries.size())
        );
        shots_sent_ms_ = now;
    }
}

void Coop::send_shots(const GameStatus& status) {
    // Taken each tick, so none is left over from before a level or a party.
    const std::vector<Shot> shots = game_.hero_shots();

    // Out of a level or a party's game, no shot is kept.
    if (!in_level(status.phase) || play_ == 0) {
        shots_out_.clear();
        return;
    }

    const std::uint64_t now = clock_();
    const auto stamp = static_cast<std::uint32_t>(
        session_.has_clock() ? session_.server_ms() : static_cast<std::int64_t>(now)
    );

    // Each shot the hero fired is queued with the level and stamp it was fired at.
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

    // Only a whole SHOT of another member of the party is shown here.
    if (!decode_shot(bytes, m) || m.owner == session_.id() || !member_of(session_, m.owner)) {
        return;
    }

    // The host passes a member's own shot on to everyone; a member takes only the host's.
    if (session_.role().host()) {
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
    e.stamp = static_cast<std::uint32_t>(
        session_.has_clock() ? session_.server_ms() : static_cast<std::int64_t>(clock_())
    );
    e.shot = m.shot;
    take_shot(e);
}

void Coop::on_shotx(std::int32_t sender, const Bytes& bytes) {
    std::vector<ShotEntry> entries;

    // Bytes that are no whole SHOTX are dropped.
    if (!decode_shotx(bytes, entries)) {
        return;
    }

    // Each shot of a SHOTX is checked on its own.
    for (const ShotEntry& e : entries) {
        // A shot of this player's own, or of someone outside the party, is not shown.
        if (e.owner == session_.id() || !member_of(session_, e.owner)) {
            continue;
        }

        // The host queues a member's own shot for everyone; a member takes only the host's.
        if (session_.role().host()) {
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

    // A remote player's shots wait at most kShotsWaiting; the oldest goes first.
    if (r.shots.size() >= kShotsWaiting) {
        r.shots.pop_front();
    }

    // Fired when its figure is where the shooter was: a stamp in this game's clock (the stamp is
    // the low bits of the server's, or of the sender's clock while none is known), as far behind
    // as its poses are shown.
    const auto now = static_cast<double>(clock_());
    double time = now;

    // With the server's clock known the stamp gives the shot's own time; without it, it is now.
    if (session_.has_clock()) {
        const std::int64_t server = session_.server_ms();
        const auto ago = static_cast<std::int32_t>(static_cast<std::uint32_t>(server) - e.stamp);
        time = static_cast<double>(server - ago) - session_.clock_offset_ms();
    }

    r.shots.push_back(
        {std::max(now, time + r.poses.delay_ms()), e.level, e.shot, time + r.poses.delay_ms()}
    );
}

void Coop::fire_shots(Remote& r, std::uint64_t now, const GameStatus& status) {
    const auto t = static_cast<double>(now);

    // Shots whose time has come are fired in the order they were queued.
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

        // A shot is counted as fired only when the figure took it.
        if (game_.figure_shot(r.figure, shot.shot)) {
            ++shots_fired_;
        }
    }
}

}  // namespace mp
