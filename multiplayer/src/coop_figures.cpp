// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together (coop.hpp): the players' poses and their figures.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "coop.hpp"
#include "coop_util.hpp"

namespace mp {

using namespace coop_util;

void Coop::on_pose(std::int32_t sender, const Bytes& bytes) {
    PoseMessage m;

    // Only a whole POSE of another member of the party is shown here.
    if (!decode_pose(bytes, m) || m.owner == session_.id() || !member_of(session_, m.owner)) {
        return;
    }

    // The host takes a member's own pose to pass on; a member takes only the host's.
    if (session_.role().host()) {
        // A member speaks for itself only; the host passes it on with the next poses that go.
        if (sender != m.owner) {
            return;
        }

        // Its word on WORLD is for this game alone.
        relay_[m.owner] = m.world_ack ? Bytes(bytes.begin(), bytes.end() - kPoseAckBytes) : bytes;

        // Only a pose that carries a word on WORLD gives the host one.
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

    // With OPENRAC_ANIM_TRACE set, the pose received and its time go to stderr.
    if (tracing()) {
        std::fprintf(stderr, "ATRACE seq %u %d\n", m.sequence, m.teleport ? 1 : 0);
        mp::trace("R", now, time, m.pose);
    }

    r.poses.add(m.pose, m.sequence, time, now, m.teleport);
    r.scene = m.scene;
    r.menu = m.menu;

    // With the verbose log on, each pose received gets a line.
    if (trace_ != nullptr && trace_->on()) {
        char text[320];
        std::snprintf(
            text,
            sizeof text,
            "recv player %d seq %u stamp %lld local %.0f arrival %.0f transit %.0f at %.3f %.3f "
            "%.3f "
            "vel %.3f %.3f %.3f state %d%s",
            m.owner,
            m.sequence,
            static_cast<long long>(m.stamp_ms),
            time,
            now,
            now - time,
            m.pose.position[0],
            m.pose.position[1],
            m.pose.position[2],
            m.pose.velocity[0],
            m.pose.velocity[1],
            m.pose.velocity[2],
            m.pose.state,
            m.teleport ? " teleport" : ""
        );
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

    // Each pose of the datagram is read as a POSE of its own.
    for (std::uint8_t i = 0; i < count && r.ok(); ++i) {
        const std::uint8_t high = r.u8();
        const std::size_t size = static_cast<std::size_t>(high) << 8 | r.u8();

        // A length of 0, or past what is left, means the datagram is cut: the rest is dropped.
        if (size == 0 || size > r.left()) {
            return;
        }

        Bytes one(size);

        // The bytes are copied out of the Reader so that on_pose reads them as a POSE.
        for (std::uint8_t& b : one) {
            b = r.u8();
        }

        on_pose(sender, one);
    }
}

void Coop::send_poses(const Bytes* own) {
    std::vector<const Bytes*> poses;

    // The host's own pose goes first, when it has one.
    if (own != nullptr) {
        poses.push_back(own);
    }

    // Then each member's newest pose not passed on yet.
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

    // As many poses go in a datagram as fit; the rest start the next one.
    for (std::size_t at = 0; at < poses.size();) {
        Writer w;
        // A POSES datagram starts with its kind byte and its count.
        std::size_t size = 2;
        std::size_t end = at;

        // A pose is added while its length (2 bytes) and its bytes still fit.
        while (end < poses.size() && size + 2 + poses[end]->size() <= room) {
            size += 2 + poses[end]->size();
            ++end;
        }

        w.u8(static_cast<std::uint8_t>(GameKind::kPoses)).u8(static_cast<std::uint8_t>(end - at));

        // Each pose is written as its length and its bytes.
        for (std::size_t i = at; i < end; ++i) {
            const auto length = poses[i]->size();
            w.u8(static_cast<std::uint8_t>(length >> 8))
                .u8(static_cast<std::uint8_t>(length))
                .raw(*poses[i]);
        }

        session_.send_state(kBroadcast, w.bytes());
        at = std::max(end, at + 1);
    }

    relay_.clear();
}

void Coop::trace_hero(std::uint64_t now, const GameStatus& status) {
    Pose hero;

    // Only with the verbose log on, in a level, and with a hero to read.
    if (trace_ == nullptr || !trace_->on() || !in_level(status.phase) || !game_.hero(hero)) {
        return;
    }

    // Each tick: the hero's z, how far it went and how often it changed (the ground's snap).
    HeroZ& h = hero_z_;
    const float z = hero.position[2];

    // The first tick of a period starts the range; later ticks widen it.
    if (h.ticks == 0) {
        h.low = h.high = z;
    } else {
        h.low = std::min(h.low, z);
        h.high = std::max(h.high, z);
        // A z that moved under 0.0001 does not count as a change.
        h.changes += std::fabs(z - h.last) > 1e-4f ? 1 : 0;
    }

    h.last = z;
    ++h.ticks;

    // The window is said and cleared twice a second.
    if (now - h.since_ms >= kPoseTraceMs) {
        char text[192];
        std::snprintf(
            text,
            sizeof text,
            "hero ticks %d z %.3f..%.3f changes %d now %s vel %.3f %.3f %.3f",
            h.ticks,
            h.low,
            h.high,
            h.changes,
            xyz(hero.position).c_str(),
            hero.velocity[0],
            hero.velocity[1],
            hero.velocity[2]
        );
        trace_->line("pose", text);
        h = HeroZ{};
        h.since_ms = now;
    }
}

void Coop::send_pose(std::uint64_t now, const GameStatus& status) {
    trace_hero(now, status);

    // Poses go at most every kPoseMs, and only from a level.
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
    m.menu = status.phase == kPhaseMenu;
    m.sequence = ++sequence_;
    m.stamp_ms = session_.has_clock() ? session_.server_ms() : static_cast<std::int64_t>(now);
    pose_ms_ = now;
    pose_loads_ = status.loads;
    pose_sent_ = true;
    last_pose_ = m.pose;

    // A member's pose goes to the host alone, with its word on WORLD; the host sends its own on.
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
    // Only a figure that exists is removed from the game.
    if (r.figure >= 0) {
        game_.figure_remove(r.figure);
        r.figure = -1;
        log("player " + std::to_string(player) + " no longer shown: " + why);
    }

    r.hidden = false;
}

bool Coop::hide_figure(std::int32_t player, Remote& r, bool hidden) {
    // A figure that does not exist, or is in the state asked, needs nothing.
    if (r.figure < 0 || r.hidden == hidden) {
        return true;
    }

    const int slot = game_.figure_slot(r.figure);

    // A figure the game cannot find or hide is reported as not hidden, so the caller drops it.
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

        // Every figure of the old load is gone with it; new ones are made as the poses call for
        // them.
        for (auto& entry : remotes_) {
            entry.second.figure = -1;
            entry.second.hidden = false;
        }
    }

    // A scene playing in this game: the others' figures hidden (made only after it), shown again
    // once it is over.
    const bool scene = status.phase == kPhaseScene;

    // Each remote is shown, hidden or dropped by what its poses and this game say.
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
        } else if (r.poses.silence_ms(t) >= kGoneMs && !session_.away(it->first)
                   && !session_.dropped()) {
            // (A connection that dropped: stood where it was, until the slot is given up.)
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

        // While a scene plays here no figure is made, and its shots are dropped.
        if (scene && r.figure < 0) {
            r.shots.clear();
            ++it;
            continue;
        }

        // A figure is made again only after kFigureRetryMs, once the level refused one.
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

        // Traced: the pose as shown, with the delay of the poses' buffer.
        if (tracing() && r.figure >= 0) {
            mp::trace("O", t, r.poses.delay_ms(), pose);
        }

        // Twice a second, how its poses are shown: the buffer, what the frames since showed, and
        // this game's clock to the server's.
        if (trace_ != nullptr && trace_->on() && now - r.traced_ms >= kPoseTraceMs) {
            r.traced_ms = now;
            const Interpolator::Counts counts = r.poses.take_counts();
            trace_->line(
                "pose",
                "shown player " + std::to_string(it->first) + " " + shown_name(r.poses.shown())
                    + " at " + xyz(pose.position) + " cursor "
                    + std::to_string(std::llround(r.poses.cursor_ms())) + " " + r.poses.describe()
                    + " frames " + counts_text(counts.by) + " clock rtt "
                    + std::to_string(session_.rtt_ms()) + " offset "
                    + std::to_string(std::llround(session_.clock_offset_ms()))
            );
        }

        // Hidden: its pieces too (they follow the figure, but are drawn by their own mobys).
        if (r.hidden) {
            // Each piece the pose holds is flagged hidden.
            for (std::uint32_t i = 0; i < pose.held_count && i < kHeldMax; ++i) {
                pose.held[i].flags |= kHeldHidden;
            }
        }

        // The game says false for a figure that is gone; a new one is made later.
        if (r.figure >= 0 && !game_.figure_pose(r.figure, pose)) {
            r.figure = -1;
            r.hidden = false;
        }

        std::copy(std::begin(pose.position), std::end(pose.position), std::begin(r.shown_at));
        r.late =
            r.poses.silence_ms(t) >= kTagLateMs || session_.link(it->first).ping_ms >= kTagPingMs;

        fire_shots(r, now, status);

        ++it;
    }

    update_tag_sight(static_cast<double>(now));
}

void Coop::update_tag_sight(double now) {
    float camera[3];
    const bool have_camera = game_.camera(camera);
    bool asked = false;

    // Each figure shown has its sight advanced once a tick.
    for (auto& entry : remotes_) {
        Remote& r = entry.second;

        // A figure not shown has no tag to fade.
        if (r.figure < 0 || r.hidden) {
            continue;
        }

        // One figure's lines a tick. No camera or no answer leaves the tag as a tag without the
        // check: seen. A figure past where a tag ends costs nothing.
        const float dx = r.shown_at[0] - camera[0];
        const float dy = r.shown_at[1] - camera[1];
        const float dz = r.shown_at[2] - camera[2];
        const bool in_range = !have_camera || dx * dx + dy * dy + dz * dz < kTagGone * kTagGone;

        // Only one figure is asked per tick, and only when it is due and in range of a tag.
        if (!asked && in_range && r.sight.due(now)) {
            asked = true;
            r.sight.asked(!have_camera || sees_figure(camera, r.shown_at), now);
        }

        r.sight.advance(now);
    }
}

bool Coop::sees_figure(const float* camera, const float* feet) {
    // Either the head or the middle in view is enough.
    for (const float above : {kTagSeeHead, kTagSeeMiddle}) {
        const float point[3] = {feet[0], feet[1], feet[2] + above};
        bool blocked = false;

        // A line the game cannot test, or one the level does not block, counts as seen.
        if (!game_.level_line(camera, point, blocked) || !blocked) {
            return true;
        }
    }

    return false;
}

std::vector<NameTag> Coop::name_tags() const {
    std::vector<NameTag> tags;
    int slot = 0;

    // The party's order is every game's, so a player has one colour everywhere.
    for (const Member& m : session_.members()) {
        const auto it = remotes_.find(m.id);

        // Only players whose figures are shown have a tag.
        if (it != remotes_.end() && it->second.figure >= 0 && !it->second.hidden) {
            NameTag tag;
            tag.name = m.name;
            tag.slot = slot;
            std::copy(
                std::begin(it->second.shown_at),
                std::end(it->second.shown_at),
                std::begin(tag.position)
            );
            tag.late = it->second.late;
            tag.sight = it->second.sight.strength();
            tags.push_back(std::move(tag));
        }

        ++slot;
    }

    return tags;
}

bool Coop::latest(std::int32_t player, Pose& out) const {
    const auto it = remotes_.find(player);

    // One whose connection dropped stands in no enemy's way, in nobody's count.
    return it != remotes_.end() && !session_.away(player) && it->second.poses.newest(out);
}

int Coop::figure_of(std::int32_t player) const {
    const auto it = remotes_.find(player);
    return it != remotes_.end() ? it->second.figure : -1;
}

}  // namespace mp
