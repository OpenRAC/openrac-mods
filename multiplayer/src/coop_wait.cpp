/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.hpp): waits for others, and going on without them.
 *
 * A wait is the host's to name. While players stand held for others (the party arriving on a planet
 * with some still on their way; a scene that others still play), the host gives the wait an id and
 * says who it holds (WAIT_VOTE): those not being waited for. Each of them may press Start. The host's
 * ends the wait at once. A member's is a vote; more than half of those held end it. A skip of a wait
 * is no skip of anything else: the players still on their way arrive when they arrive, and whoever
 * still plays a scene plays it to its end (only the host skips a scene, coop_scenes.cpp); those who
 * waited simply have their control back.
 */
#include "coop.hpp"
#include "coop_util.hpp"

#include <algorithm>
#include <string>

namespace mp {

using namespace coop_util;

void Coop::release_wait(std::uint64_t now, const char* why) {
    log(std::string("going on without the others: ") + why);

    if (wait_.kind == kWaitArrival) {
        arriving_ = false;
        waiting_for_.clear();
    } else {
        barrier_open_ = true;
        released_scene_ = scene_sequence_;
    }

    send_wait(now, true);
    wait_ = Wait{};
    votes_.clear();
}

void Coop::skip_wait(std::uint64_t now) {
    if (session_.is_host()) {
        if (wait_.kind != kWaitNone) {
            release_wait(now, "the host's Start");
        }

        return;
    }

    if (wait_.kind == kWaitNone || voted_ == wait_.id) {
        return;
    }

    voted_ = wait_.id;
    log("voted to go on without the others");
    session_.send_data(
        session_.host(),
        Writer()
            .u8(static_cast<std::uint8_t>(GameKind::kWaitVote))
            .i32(play_)
            .u8(static_cast<std::uint8_t>(wait_.id >> 8))
            .u8(static_cast<std::uint8_t>(wait_.id))
            .bytes()
    );
}

void Coop::send_wait(std::uint64_t now, bool passed) {
    wait_sent_ms_ = now;
    session_.send_data(
        kBroadcast,
        Writer()
            .u8(static_cast<std::uint8_t>(GameKind::kWaitVote))
            .i32(play_)
            .u8(static_cast<std::uint8_t>(wait_.id >> 8))
            .u8(static_cast<std::uint8_t>(wait_.id))
            .u8(wait_.kind)
            .u8(static_cast<std::uint8_t>(wait_.votes))
            .u8(static_cast<std::uint8_t>(wait_.voters))
            .u8(passed ? 1 : 0)
            .bytes()
    );
}

void Coop::tick_wait(std::uint64_t now, const GameStatus& status) {
    if (!session_.is_host() || play_ == 0) {
        return;
    }

    // What holds anyone now, and who it holds.
    std::uint8_t kind = kWaitNone;
    int voters = 0;
    const int players = static_cast<int>(session_.members().size());

    if (arriving_ && status.level == travel_dest_ && !waiting_for_.empty()) {
        kind = kWaitArrival;
        voters = players - static_cast<int>(waiting_for_.size());
    } else if (released_scene_ != scene_sequence_ && scene_sequence_ != 0
               && status.level == scene_level_ && party_skip_ != scene_sequence_) {
        const bool host_in = status.phase == kPhaseScene;
        int out = 0, in = 0;

        for (const auto& [id, s] : scene_states_) {
            if (member_of(session_, id) && s.sequence == scene_sequence_) {
                ++(s.state == kSceneOut ? out : in);
            }
        }

        // The host out with members still in it, or members out with the host still in it.
        if (host_in ? out > 0 : in > 0) {
            kind = kWaitScene;
            voters = out + (host_in ? 0 : 1);
        }
    }

    if (kind == kWaitNone) {
        if (wait_.kind != kWaitNone) {
            wait_ = Wait{};
            votes_.clear();
            send_wait(now, false);
        }

        return;
    }

    bool told = false;

    if (kind != wait_.kind) {
        if (++wait_count_ == 0) {
            ++wait_count_;
        }

        wait_.id = wait_count_;
        wait_.kind = kind;
        votes_.clear();
        told = true;
    }

    // A voter gone takes its vote along.
    votes_.erase(
        std::remove_if(
            votes_.begin(), votes_.end(), [&](std::int32_t id) { return !member_of(session_, id); }
        ),
        votes_.end()
    );

    const int votes = static_cast<int>(votes_.size());
    told = told || votes != wait_.votes || voters != wait_.voters;
    wait_.votes = votes;
    wait_.voters = voters;

    if (votes > 0 && votes * 2 > voters) {
        release_wait(now, "the players' votes");
    } else if (told || now - wait_sent_ms_ >= kLevelMs) {
        send_wait(now, false);
    }
}

void Coop::on_wait_vote(std::int32_t sender, bool from_host, Reader& r) {
    const std::int32_t play = r.i32();
    const std::uint8_t high = r.u8();
    const auto id = static_cast<std::uint16_t>(high << 8 | r.u8());

    if (!r.ok() || play == 0 || play != play_) {
        return;
    }

    if (session_.is_host()) {
        // Of the wait on now, once a member.
        if (id == wait_.id && wait_.kind != kWaitNone && member_of(session_, sender)
            && std::find(votes_.begin(), votes_.end(), sender) == votes_.end()) {
            votes_.push_back(sender);
            log("player " + std::to_string(sender) + " voted to go on without the others");
        }

        return;
    }

    const std::uint8_t kind = r.u8();
    const std::uint8_t votes = r.u8();
    const std::uint8_t voters = r.u8();
    const bool passed = r.u8() != 0;

    if (!from_host || !r.ok() || kind > kWaitScene) {
        return;
    }

    if (passed) {
        // Arrival: the host's LEVEL says so too. A scene: this game's hold lets go.
        barrier_open_ = kind == kWaitScene;
        wait_ = Wait{};
        return;
    }

    wait_ = {id, kind, votes, voters};
}

}  // namespace mp
