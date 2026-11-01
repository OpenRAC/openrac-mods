/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The shared world (world.hpp): the member's side: the driven mobys and the despawn.
 */
#include "world.hpp"
#include "world_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace mp {

using namespace wire;

void World::tick_member(std::uint64_t now, const GameStatus& status) {
    coop_.world_ready(synced_);
    tick_held(now);

    // Left down with no word from the host: the game's own way.
    Pose hero;

    if (death_held_ && game_.hero(hero) && hero.health <= 0) {
        dead_ms_ = dead_ms_ != 0 ? dead_ms_ : now;

        if (now - dead_ms_ >= kDownedMs + kDownedGraceMs) {
            dead_ms_ = 0;
            game_.respawn();
        }
    } else {
        dead_ms_ = 0;
    }

    // A full wipe: once back and given the world, beside the host at its checkpoint.
    if (wipe_until_ != 0 && now >= wipe_until_) {
        wipe_until_ = 0;
    } else if (wipe_until_ != 0 && synced_ && game_.hero(hero) && hero.health > 0 && coop_.can_go()) {
        wipe_until_ = 0;
        coop_.go_to_host(now);
    }

    // Only in the host's game and level: the world is the host's there.
    if (!coop_.playing() || !in_level(status.phase) || status.level != coop_.host_level()) {
        return;
    }

    // Each placed moby that runs an update of its own is driven, once a level load.
    if (!placed_driven_) {
        placed_driven_ = true;

        for (const auto& [slot, moby] : mobys_) {
            if (moby.key >= 0 && (moby.flags & kMobyUpdates) != 0 && moby.oclass != 0
                && !local_class(moby.oclass) && game_.moby_drive(slot, true)) {
                driven_.insert(slot);
            }
        }

        log("driving " + std::to_string(driven_.size()) + " mobys of level " + std::to_string(level_)
            + " from the host");
    }

    // These mobys have nothing of the host's world on them until it answers.
    if (!synced_ && (hello_ms_ == 0 || now - hello_ms_ >= kHelloMs)) {
        hello_ms_ = now;
        Writer w;
        w.u8(static_cast<std::uint8_t>(WorldKind::kHello)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
        w.i32(static_cast<std::int32_t>(loads_));
        w.i32(static_cast<std::int32_t>(game_.sim_digest(level_)));
        session_.send_data(session_.host(), w.bytes());
        trace_line("msg", "sent HELLO level " + std::to_string(level_) + " load " + std::to_string(loads_));
    }

    // Each driven moby where the host's snapshots put it, a delay behind.
    const auto t = static_cast<double>(now);
    const bool trace_poses = tracing();

    for (auto& [id, poses] : poses_) {
        const int slot = slot_of(id);
        Pose pose;

        if (slot >= 0 && poses.at(t, pose)) {
            // Moved by something else since it was put there (its own update or the game's)?
            if (trace_poses) {
                const auto was = posed_at_.find(slot);
                const auto moby = mobys_.find(slot);

                if (was != posed_at_.end() && moby != mobys_.end()
                    && distance2(was->second.data(), moby->second.pose.position) > 1e-4f) {
                    ++moved_since_;
                }

                posed_at_[slot] = {pose.position[0], pose.position[1], pose.position[2]};
            }

            game_.moby_pose(slot, pose);
        }
    }

    // Once a second, how they are shown: what all the frames since showed, the buffers of three
    // that moved, and how many were moved by something else between two ticks.
    if (trace_poses && now - poses_traced_ms_ >= kWorldPoseTraceMs) {
        poses_traced_ms_ = now;
        int by[6] = {0, 0, 0, 0, 0, 0};
        std::string some;
        int described = 0;

        for (auto& [id, poses] : poses_) {
            const Interpolator::Counts counts = poses.take_counts();
            int moving = 0;

            for (int i = 0; i < 6; ++i) {
                by[i] += counts.by[i];
                moving += i == static_cast<int>(Shown::kBetween) || i == static_cast<int>(Shown::kAhead) ? counts.by[i] : 0;
            }

            // A few that moved: their buffers.
            if (moving > 0 && described < 3) {
                ++described;
                some += " | id " + std::to_string(id) + " " + shown_name(poses.shown()) + " " + poses.describe();
            }
        }

        trace_line("pose", "mobys " + std::to_string(poses_.size()) + " frames " + counts_text(by) + " moved by the game "
                               + std::to_string(moved_since_) + some);
        moved_since_ = 0;
    }

    // Each driven moby hidden or shown as the host's is (its own update, which would, does not
    // run here).
    for (const auto& [id, hidden] : hidden_) {
        const int slot = slot_of(id);
        const auto moby = mobys_.find(slot);

        if (moby == mobys_.end() || driven_.count(slot) == 0
            || ((moby->second.flags & kMobyHidden) != 0) == hidden) {
            continue;
        }

        if (game_.moby_hide(slot, hidden)) {
            trace_line("vis", std::string(hidden ? "hide" : "show") + " by sync (the host's is) " + moby_name(slot));
            note(slot, hidden ? "hidden as the host's is" : "shown as the host's is", session_.host());
        }
    }
}

void World::tick_despawn(std::uint64_t now, const std::vector<MobyState>& listing) {
    DespawnLog::View view;
    Pose hero;

    if (game_.hero(hero)) {
        view.hero = true;
        std::copy(hero.position, hero.position + 3, view.hero_at);
    }

    view.camera = game_.camera(view.camera_at);

    if (!despawn_.out) {
        despawn_.out = [this](const std::string& line) { trace_line("despawn", line); };
        despawn_.deletion = [this](int slot, MobyDeletion& d) {
            return game_.moby_deletion(slot, d.by, d.by_class, d.by_mod);
        };
        despawn_.role = [this](int slot, const MobyState& m) -> std::string {
            if (hosting_) {
                const bool placed_runs = m.key >= 0 && (m.flags & kMobyUpdates) != 0 && m.oclass != 0
                                         && !local_class(m.oclass);
                return placed_runs || shared_.count(slot) != 0 ? "shared" : "local";
            }

            return driven_.count(slot) != 0 ? "driven" : "local";
        };
        despawn_.host_seen = [this](int slot, const MobyState&) -> std::string {
            if (hosting_ || session_.is_host()) {
                return "";
            }

            const std::uint16_t id = id_of(slot);
            const auto seen = id != kNoId ? host_ms_.find(id) : host_ms_.end();

            if (seen == host_ms_.end()) {
                return "host never sent it";
            }

            const std::uint64_t ago = clock_() - seen->second;
            return "host sent it " + std::to_string(ago / 1000) + "." + std::to_string(ago % 1000 / 100)
                   + " s ago" + (hidden_.count(id) != 0 && hidden_.at(id) ? " hidden" : "");
        };
    }

    despawn_.tick(now, listing, view);
}

}  // namespace mp
