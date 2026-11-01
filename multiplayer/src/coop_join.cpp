/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.hpp): the PLAY sent, the joining and the rejoin.
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

bool Coop::rejoin() {
    if (session_.is_host() || play_ == 0 || pending_) {
        return false;
    }

    // As one that never played it: WANT goes at the next tick.
    applied_ = 0;
    want_ms_ = 0;
    return true;
}

void Coop::send_play(std::int32_t target, const Bytes& save, int level, bool joining) {
    session_.send_data(
        target,
        Writer()
            .u8(static_cast<std::uint8_t>(GameKind::kPlay))
            .i32(play_)
            .u8(save.empty() ? 0 : 1)
            .i32(level)
            .i32(static_cast<std::int32_t>(save.size()))
            .str(build_)
            .u8(joining ? 1 : 0)
            .bytes()
    );

    // The save in pieces, in order (TCP keeps it).
    for (std::size_t at = 0; at < save.size(); at += kChunkBytes) {
        const std::size_t n = std::min(kChunkBytes, save.size() - at);
        Writer w;
        w.u8(static_cast<std::uint8_t>(GameKind::kChunk))
            .i32(play_)
            .i32(static_cast<std::int32_t>(at))
            .raw(Bytes(save.begin() + static_cast<std::ptrdiff_t>(at),
                       save.begin() + static_cast<std::ptrdiff_t>(at + n)));
        session_.send_data(target, w.bytes());
    }
}

void Coop::tick_joining(std::uint64_t now, const GameStatus& status) {
    if (!joining_) {
        return;
    }

    const bool there = status.phase == kPhasePlaying && status.level == host_level_ && world_ready_;

    if (there || now - joining_since_ >= kJoiningMs) {
        joining_ = false;
        log(there ? "joined the host's game: " + std::to_string(now - joining_since_) + " ms"
                  : "joining took too long: going on");
        return;
    }

    // Whatever scene its load started is no scene of the party's.
    if (status.phase == kPhaseScene && (skip_ms_ == 0 || now - skip_ms_ >= kSkipMs)) {
        skip_ms_ = now;
        skip_asked_ = true;
        game_.skip();
    }
}

}  // namespace mp
