/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.hpp): the level followed, the barrier and the ship's travel.
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

void Coop::update_barrier(std::uint64_t now, const GameStatus& status) {
    // The scene's start and end in the log, and whether a skip ended it.
    const bool scene = status.phase == kPhaseScene;

    if (scene && !in_scene_) {
        log(std::string("scene started (") + (session_.role().host() ? "host" : "member")
            + (host_scene_ && !session_.role().host() ? ", the host's too" : "") + ")");
    } else if (!scene && in_scene_) {
        log(skip_asked_ ? "scene ended after the party's skip" : "scene ended");
        skip_asked_ = false;
    }

    in_scene_ = scene;
    bool waiting = false;

    if (session_.role().host() && play_ != 0) {
        // The host out of a scene while a member in its level still plays one.
        for (const auto& [id, r] : remotes_) {
            Pose p;
            waiting = waiting
                      || (r.scene && r.poses.newest(p) && p.level == status.level
                          && r.poses.silence_ms(static_cast<double>(now)) < 2000.0);
        }

        // Or still to play (or playing) the host's newest scene, as its SCENE_STATE says.
        waiting = waiting || members_in_scene(now, status);
    } else if (play_ != 0 && applied_ == play_ && host_level_ == status.level) {
        // A member out of its scene while the host's goes on, or with the host's scene still to
        // play here (until it played it, or the party skipped it).
        waiting = host_scene_ || scene_pending_;
    }

    // Let go by the players waiting (coop_wait.cpp), until what they waited for is over.
    if (!waiting) {
        barrier_open_ = false;
    }

    waiting = waiting && !barrier_open_;

    // The party on its way to a planet: nobody goes on until everyone is in.
    const bool arrival = arriving_ && play_ != 0 && status.level == travel_dest_;
    waiting = waiting || arrival;

    waiting = waiting && status.phase == kPhasePlaying && !joining_;

    if (!waiting) {
        // (Into a scene of its own meanwhile: nobody is out yet, the hold just ends.)
        if (hold_since_ != 0 && status.phase == kPhasePlaying) {
            log("everyone is out of the scene: going on");
        }

        hold_since_ = 0;
        hold_ = false;
    } else {
        if (hold_since_ == 0) {
            hold_since_ = now;
            log(arrival ? (session_.role().host() ? "waiting for " + std::to_string(waiting_for_.size())
                                                    + " players to arrive in level " + std::to_string(status.level)
                                              : "waiting for the party to arrive in level " + std::to_string(status.level))
                : session_.role().host() ? "waiting for the members to leave their scene"
                                     : "waiting for the host's scene to end");
        }

        // The arrival's own limit is kArriveMs (it ends the wait itself).
        const std::uint64_t most = arrival ? kArriveMs : session_.role().host() ? kBarrierMs : kHostSceneMs;
        hold_ = now - hold_since_ < most;
    }

    // A party's skip still to act on the scene of this game's it came in (not one started since).
    if (skip_scene_ != 0 && skip_scene_ == scene_instance_ && now < skip_until_
        && status.phase == kPhaseScene && now - skip_ms_ >= kSkipMs) {
        skip_ms_ = now;
        skip_asked_ = true;
        game_.skip();
    }

    tick_wait(now, status);
}

void Coop::tick_host(std::uint64_t now, const GameStatus& status) {
    if (play_ == 0) {
        return;
    }

    // Those who left are forgotten (back, they are told again).
    told_.erase(
        std::remove_if(
            told_.begin(), told_.end(), [&](std::int32_t id) { return !member_of(session_, id); }
        ),
        told_.end()
    );

    // A member who came after the start: the host's game as it is now, once the host plays.
    for (const Member& m : session_.members()) {
        const bool told = std::find(told_.begin(), told_.end(), m.id) != told_.end();

        if (m.id == session_.id() || told || !in_level(status.phase) || !may_play(m.id)) {
            continue;
        }

        const Bytes live = game_.capture();

        if (!live.empty()) {
            log("player " + std::to_string(m.id) + " joins the game in level " + std::to_string(status.level));
            send_play(m.id, live, status.level, true);
            told_.push_back(m.id);
        }
    }

    tick_arrival(now, status);
    tick_safe(now, status);
    host_played_ = host_played_ || in_level(status.phase);

    // The level played, at each change and once a second.
    const int level = in_level(status.phase) ? status.level : -1;

    const bool scene = status.phase == kPhaseScene;
    host_scene_ = scene;

    if (level != sent_level_ || scene != sent_scene_ || arriving_ != sent_arriving_
        || now - level_ms_ >= kLevelMs) {
        send_level(now, level, scene);
    }

    tick_host_scene(now, status);
}

void Coop::send_level(std::uint64_t now, int level, bool scene) {
    sent_level_ = level;
    sent_scene_ = scene;
    sent_arriving_ = arriving_;
    level_ms_ = now;
    session_.send_data(
        kBroadcast,
        Writer()
            .u8(static_cast<std::uint8_t>(GameKind::kLevel))
            .i32(play_)
            .i32(level)
            .u8(scene ? 1 : 0)
            .u8(static_cast<std::uint8_t>(travel_ >> 8))
            .u8(static_cast<std::uint8_t>(travel_))
            .i32(travel_dest_)
            .u8(arriving_ ? 1 : 0)
            .bytes()
    );
}

bool Coop::ship_travel(int dest, int here) {
    // A landing again where the ship is, or no party's game: as the game does.
    if (dest == here || !session_.in_party() || play_ == 0) {
        return true;
    }

    if (!session_.role().host()) {
        if (applied_ != play_) {
            return true;
        }

        notice_ = "Only the host flies the ship to another planet";
        refused_until_ = clock_() + kRefusedMs;
        log("the ship's travel to level " + std::to_string(dest) + " refused: only the host flies it");
        return false;
    }

    // The host leaves: everyone who plays its game leaves with it, now, and is waited for there.
    const auto now = clock_();
    ++travel_;
    travel_dest_ = dest;
    arriving_ = true;
    depart_ms_ = now;
    depart_loads_ = game_.status().loads;
    waiting_for_.clear();

    for (const auto& [id, r] : remotes_) {
        if (member_of(session_, id) && r.poses.silence_ms(static_cast<double>(now)) < kGoneMs) {
            waiting_for_.push_back(id);
        }
    }

    log("leaving for level " + std::to_string(dest) + " by ship (travel " + std::to_string(travel_)
        + "): " + std::to_string(waiting_for_.size()) + " players leave too");
    send_level(now, here, host_scene_);
    return true;
}

std::string Coop::banner(std::uint64_t now) const {
    if (now < refused_until_) {
        return "Only the host flies the ship to another planet";
    }

    if (now < skip_refused_until_) {
        return "Only the host can skip";
    }

    if (leave_dest_ >= 0 && now < leave_at_) {
        const std::string planet = game_.level_name(leave_dest_);
        return planet.empty() ? "The host is leaving" : "The host is leaving for " + planet;
    }

    if (hold_ && wait_.kind != kWaitNone) {
        const std::string what = arriving_ ? "Waiting for the other players to arrive"
                                           : "Waiting for the scene to end";

        if (session_.role().host()) {
            return what + ": Start goes on";
        }

        return what + ": Start to skip (" + std::to_string(wait_.votes) + "/"
               + std::to_string(wait_.voters) + ")";
    }

    if (arriving_ && hold_) {
        return "Waiting for the other players to arrive";
    }

    if (placing_ == PlaceFor::kJoin) {
        return host_scene_ ? "Waiting for the host's scene to end: Start goes in now"
                           : "Joining the game";
    }

    if (placing_ == PlaceFor::kHost) {
        return "Going to the host";
    }

    if (joining_) {
        return "Joining the game";
    }

    if (now < no_spot_until_) {
        return "No safe spot near the host";
    }

    return {};
}

void Coop::tick_arrival(std::uint64_t now, const GameStatus& status) {
    if (!arriving_) {
        return;
    }

    // Each member waited for is in once its game plays the new level (its newest pose there).
    waiting_for_.erase(
        std::remove_if(
            waiting_for_.begin(),
            waiting_for_.end(),
            [&](std::int32_t id) {
                const auto it = remotes_.find(id);
                Pose p;
                return !member_of(session_, id)
                       || (it != remotes_.end() && it->second.poses.newest(p) && p.level == travel_dest_
                           && it->second.poses.silence_ms(static_cast<double>(now)) < 2000.0);
            }
        ),
        waiting_for_.end()
    );

    const bool here = in_level(status.phase) && status.level == travel_dest_ && status.loads != depart_loads_;

    if (here && waiting_for_.empty()) {
        arriving_ = false;
        log("everyone is in level " + std::to_string(travel_dest_) + ": going on ("
            + std::to_string(now - depart_ms_) + " ms after leaving)");
    } else if (now - depart_ms_ >= kArriveMs) {
        arriving_ = false;
        waiting_for_.clear();
        log("going on in level " + std::to_string(status.level) + " without the players still on their way");
    }
}

}  // namespace mp
