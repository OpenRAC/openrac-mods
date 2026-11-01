/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.hpp): the players' poses and their figures.
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

void Coop::on_pose(std::int32_t sender, const Bytes& bytes) {
    PoseMessage m;

    if (!decode_pose(bytes, m) || m.owner == session_.id() || !member_of(session_, m.owner)) {
        return;
    }

    if (session_.role().host()) {
        // A member speaks for itself only; the host passes it on with the next poses that go.
        if (sender != m.owner) {
            return;
        }

        // Its word on WORLD is for this game alone.
        relay_[m.owner] = m.world_ack ? Bytes(bytes.begin(), bytes.end() - kPoseAckBytes) : bytes;

        if (m.world_ack) {
            world_acks_[m.owner] = {m.world_newest, m.world_bits};
        }
    } else if (sender != session_.host()) {
        return;
    }

    // The stamp in this game's clock: the server's clock less this game's offset to it.
    const auto now = static_cast<double>(clock_());
    const double time =
        session_.has_clock() ? static_cast<double>(m.stamp_ms) - session_.clock_offset_ms() : now;
    Remote& r = remotes_[m.owner];
    if (tracing()) {
        std::fprintf(stderr, "ATRACE seq %u %d\n", m.sequence, m.teleport ? 1 : 0);
        mp::trace("R", now, time, m.pose);
    }

    r.poses.add(m.pose, m.sequence, time, now, m.teleport);
    r.scene = m.scene;

    if (trace_ != nullptr && trace_->on()) {
        char text[320];
        std::snprintf(text, sizeof text,
                      "recv player %d seq %u stamp %lld local %.0f arrival %.0f transit %.0f at %.3f %.3f %.3f "
                      "vel %.3f %.3f %.3f state %d%s",
                      m.owner, m.sequence, static_cast<long long>(m.stamp_ms), time, now, now - time,
                      m.pose.position[0], m.pose.position[1], m.pose.position[2], m.pose.velocity[0],
                      m.pose.velocity[1], m.pose.velocity[2], m.pose.state, m.teleport ? " teleport" : "");
        trace_->line("pose", text);
    }
}

bool Coop::take_world_ack(std::int32_t player, std::uint16_t& newest, std::uint32_t& bits) {
    const auto it = world_acks_.find(player);

    if (it == world_acks_.end()) {
        return false;
    }

    newest = it->second.newest;
    bits = it->second.bits;
    world_acks_.erase(it);
    return true;
}

void Coop::on_poses(std::int32_t sender, Reader& r) {
    const std::uint8_t count = r.u8();

    for (std::uint8_t i = 0; i < count && r.ok(); ++i) {
        const std::uint8_t high = r.u8();
        const std::size_t size = static_cast<std::size_t>(high) << 8 | r.u8();

        if (size == 0 || size > r.left()) {
            return;
        }

        Bytes one(size);

        for (std::uint8_t& b : one) {
            b = r.u8();
        }

        on_pose(sender, one);
    }
}

void Coop::send_poses(const Bytes* own) {
    std::vector<const Bytes*> poses;

    if (own != nullptr) {
        poses.push_back(own);
    }

    for (const auto& [id, bytes] : relay_) {
        poses.push_back(&bytes);
    }

    // Nothing to pass on: the host's own, as a POSE. (With one member it hears only the host.)
    if (poses.size() == 1 && own != nullptr) {
        session_.send_state(kBroadcast, *own);
        relay_.clear();
        return;
    }

    const std::size_t room = kMaxDatagramBytes - kClientStateHeader;

    for (std::size_t at = 0; at < poses.size();) {
        Writer w;
        std::size_t size = 2;
        std::size_t end = at;

        while (end < poses.size() && size + 2 + poses[end]->size() <= room) {
            size += 2 + poses[end]->size();
            ++end;
        }

        w.u8(static_cast<std::uint8_t>(GameKind::kPoses)).u8(static_cast<std::uint8_t>(end - at));

        for (std::size_t i = at; i < end; ++i) {
            const auto length = poses[i]->size();
            w.u8(static_cast<std::uint8_t>(length >> 8)).u8(static_cast<std::uint8_t>(length)).raw(*poses[i]);
        }

        session_.send_state(kBroadcast, w.bytes());
        at = std::max(end, at + 1);
    }

    relay_.clear();
}

void Coop::trace_hero(std::uint64_t now, const GameStatus& status) {
    Pose hero;

    if (trace_ == nullptr || !trace_->on() || !in_level(status.phase) || !game_.hero(hero)) {
        return;
    }

    // Each tick: the hero's z, how far it went and how often it changed (the ground's snap).
    HeroZ& h = hero_z_;
    const float z = hero.position[2];

    if (h.ticks == 0) {
        h.low = h.high = z;
    } else {
        h.low = std::min(h.low, z);
        h.high = std::max(h.high, z);
        h.changes += std::fabs(z - h.last) > 1e-4f ? 1 : 0;
    }

    h.last = z;
    ++h.ticks;

    if (now - h.since_ms >= kPoseTraceMs) {
        char text[192];
        std::snprintf(text, sizeof text, "hero ticks %d z %.3f..%.3f changes %d now %s vel %.3f %.3f %.3f",
                      h.ticks, h.low, h.high, h.changes, xyz(hero.position).c_str(), hero.velocity[0],
                      hero.velocity[1], hero.velocity[2]);
        trace_->line("pose", text);
        h = HeroZ{};
        h.since_ms = now;
    }
}

void Coop::send_pose(std::uint64_t now, const GameStatus& status) {
    trace_hero(now, status);

    if (!in_level(status.phase) || now - pose_ms_ < kPoseMs) {
        return;
    }

    PoseMessage m;

    if (!game_.hero(m.pose)) {
        return;
    }

    // A new level, the level again, or a step no run makes: the others put the figure there.
    m.teleport = !pose_sent_ || status.loads != pose_loads_ || m.pose.level != last_pose_.level
                 || step(m.pose, last_pose_) > kSnapDistance;
    m.owner = session_.id();
    m.scene = status.phase == kPhaseScene && !joining_;
    m.sequence = ++sequence_;
    m.stamp_ms = session_.has_clock() ? session_.server_ms() : static_cast<std::int64_t>(now);
    pose_ms_ = now;
    pose_loads_ = status.loads;
    pose_sent_ = true;
    last_pose_ = m.pose;

    if (!session_.role().host()) {
        m.world_ack = world_ack_;
        m.world_newest = world_newest_;
        m.world_bits = world_bits_;
        session_.send_state(session_.host(), encode_pose(m));
        return;
    }

    // With one member there is no one to pass its poses on to.
    if (session_.members().size() <= 2) {
        relay_.clear();
    }

    const Bytes own = encode_pose(m);
    relay_ms_ = now;
    send_poses(&own);
}

void Coop::drop_figure(std::int32_t player, Remote& r, const char* why) {
    if (r.figure >= 0) {
        game_.figure_remove(r.figure);
        r.figure = -1;
        log("player " + std::to_string(player) + " no longer shown: " + why);
    }

    r.hidden = false;
}

bool Coop::hide_figure(std::int32_t player, Remote& r, bool hidden) {
    if (r.figure < 0 || r.hidden == hidden) {
        return true;
    }

    const int slot = game_.figure_slot(r.figure);

    if (slot < 0 || !game_.moby_hide(slot, hidden)) {
        return false;
    }

    r.hidden = hidden;
    log("player " + std::to_string(player) + (hidden ? " hidden for the scene" : " shown again"));
    return true;
}

void Coop::show_remotes(std::uint64_t now, const GameStatus& status) {
    // A new level load took every figure with it.
    if (status.loads != figure_loads_) {
        figure_loads_ = status.loads;

        for (auto& entry : remotes_) {
            entry.second.figure = -1;
            entry.second.hidden = false;
        }
    }

    // A scene playing in this game: the others' figures hidden (made only after it), shown again
    // once it is over.
    const bool scene = status.phase == kPhaseScene;

    for (auto it = remotes_.begin(); it != remotes_.end();) {
        Remote& r = it->second;

        // Out of the party: gone.
        if (!member_of(session_, it->first)) {
            drop_figure(it->first, r, "out of the party");
            it = remotes_.erase(it);
            continue;
        }

        Pose pose;
        const auto t = static_cast<double>(now);
        // A player who died is not shown until it stands again (its health back), unless they lie
        // there to be revived.
        // Nor while the party arrives somewhere (the figures come once all are in).
        const char* hidden = nullptr;

        // (A member warned of the ship's leaving still stands in the level with them.)
        if (arriving_ && leave_dest_ < 0) {
            hidden = "the party arrives";
        } else if (arriving_ && now >= leave_at_) {
            hidden = "the party arrives";
        } else if (!in_level(status.phase)) {
            hidden = "this game is out of a level";
        } else if (!r.poses.at(t, pose)) {
            hidden = "no pose";
        } else if (r.poses.silence_ms(t) >= kGoneMs) {
            hidden = "silent";
        } else if (pose.level != status.level) {
            hidden = "in another level";
        } else if (pose.health <= 0 && !show_fallen_) {
            hidden = "dead";
        }

        // Elsewhere, silent, or this game not in a level: no figure.
        if (hidden != nullptr) {
            drop_figure(it->first, r, hidden);
            ++it;
            continue;
        }

        // A game that cannot hide a figure has it taken out for the scene instead (and made again
        // after it).
        if (!hide_figure(it->first, r, scene)) {
            drop_figure(it->first, r, scene ? "a scene plays here" : "it could not be shown again");
        }

        if (scene && r.figure < 0) {
            r.shots.clear();
            ++it;
            continue;
        }

        if (r.figure < 0 && now >= r.retry_ms) {
            r.figure = game_.figure_create();

            // The level has no room now: later.
            if (r.figure < 0) {
                r.retry_ms = now + kFigureRetryMs;
            } else {
                log("player " + std::to_string(it->first) + " shown in level "
                    + std::to_string(status.level) + " (figure " + std::to_string(r.figure) + ")");
            }
        }

        if (tracing() && r.figure >= 0) {
            mp::trace("O", t, r.poses.delay_ms(), pose);
        }

        // Twice a second, how its poses are shown: the buffer, what the frames since showed, and
        // this game's clock to the server's.
        if (trace_ != nullptr && trace_->on() && now - r.traced_ms >= kPoseTraceMs) {
            r.traced_ms = now;
            const Interpolator::Counts counts = r.poses.take_counts();
            trace_->line(
                "pose", "shown player " + std::to_string(it->first) + " " + shown_name(r.poses.shown()) + " at "
                            + xyz(pose.position) + " cursor " + std::to_string(std::llround(r.poses.cursor_ms()))
                            + " " + r.poses.describe() + " frames " + counts_text(counts.by) + " clock rtt "
                            + std::to_string(session_.rtt_ms()) + " offset "
                            + std::to_string(std::llround(session_.clock_offset_ms()))
            );
        }

        // Hidden: its pieces too (they follow the figure, but are drawn by their own mobys).
        if (r.hidden) {
            for (std::uint32_t i = 0; i < pose.held_count && i < kHeldMax; ++i) {
                pose.held[i].flags |= kHeldHidden;
            }
        }

        if (r.figure >= 0 && !game_.figure_pose(r.figure, pose)) {
            r.figure = -1;
            r.hidden = false;
        }

        std::copy(std::begin(pose.position), std::end(pose.position), std::begin(r.shown_at));
        r.late = r.poses.silence_ms(t) >= kTagLateMs || session_.link(it->first).ping_ms >= kTagPingMs;

        fire_shots(r, now, status);

        ++it;
    }
}

std::vector<NameTag> Coop::name_tags() const {
    std::vector<NameTag> tags;
    int slot = 0;

    // The party's order is every game's, so a player has one colour everywhere.
    for (const Member& m : session_.members()) {
        const auto it = remotes_.find(m.id);

        if (it != remotes_.end() && it->second.figure >= 0 && !it->second.hidden) {
            NameTag tag;
            tag.name = m.name;
            tag.slot = slot;
            std::copy(std::begin(it->second.shown_at), std::end(it->second.shown_at), std::begin(tag.position));
            tag.late = it->second.late;
            tags.push_back(std::move(tag));
        }

        ++slot;
    }

    return tags;
}

bool Coop::latest(std::int32_t player, Pose& out) const {
    const auto it = remotes_.find(player);
    return it != remotes_.end() && it->second.poses.newest(out);
}

int Coop::figure_of(std::int32_t player) const {
    const auto it = remotes_.find(player);
    return it != remotes_.end() ? it->second.figure : -1;
}

}  // namespace mp
