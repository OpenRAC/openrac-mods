/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The level's scenes played together (coop.hpp: SCENE). A scene of the level's own (a cutscene
 * the level's mobys start: a person spoken to, a place reached, Veldin's opening) starts in the
 * host's game only, since the members' copies of the mobys that start them are driven from the
 * host's. So the host tells each one it starts, by the level's number of it, and a member in that
 * level starts the same one, from its start, through the game's own start (the SDK's play_scene),
 * once its player is in control. The scene's actors are then its own game's (made by the scene,
 * the world shares none of them: world.cpp).
 *
 * The barrier (coop.cpp) waits for everyone as before. A member's scene that started late is
 * skipped after the host's has ended only once it has had as long as it was late (at most
 * kSceneLagMs), so it plays out in full; a skip in any game skips it at once (SKIP).
 */
#include <algorithm>
#include <string>

#include "coop.hpp"

namespace mp {

void Coop::send_scene(std::uint64_t now) {
    scene_sent_ms_ = now;
    const std::uint64_t elapsed = scene_ >= 0 ? now - scene_start_ms_ : 0;
    session_.send_data(
        kBroadcast,
        Writer()
            .u8(static_cast<std::uint8_t>(GameKind::kScene))
            .i32(play_)
            .i32(scene_level_)
            .i32(scene_)
            .u8(static_cast<std::uint8_t>(scene_sequence_ >> 8))
            .u8(static_cast<std::uint8_t>(scene_sequence_))
            .i32(static_cast<std::int32_t>(std::min<std::uint64_t>(elapsed, 0x7FFFFFFF)))
            .bytes()
    );
}

void Coop::tick_host_scene(std::uint64_t now, const GameStatus& status) {
    const int scene = in_level(status.phase) ? status.scene : -1;

    // A scene starts (a new sequence: the same scene again is played again) or ends: told at once;
    // one playing, once a second for a member that arrives in the level meanwhile.
    if (scene != scene_) {
        if (scene >= 0) {
            ++scene_sequence_;
            scene_start_ms_ = now;
            scene_level_ = status.level;
            log("scene " + std::to_string(scene) + " of level " + std::to_string(status.level)
                + " started here: the party plays it too");
        }

        scene_ = scene;
        send_scene(now);
    } else if (scene >= 0 && now - scene_sent_ms_ >= kLevelMs) {
        send_scene(now);
    }
}

void Coop::on_scene(bool from_host, Reader& r) {
    const std::int32_t play = r.i32();
    const std::int32_t level = r.i32();
    const std::int32_t scene = r.i32();
    const std::uint8_t high = r.u8();
    const auto sequence = static_cast<std::uint16_t>(high << 8 | r.u8());
    const std::int32_t elapsed = r.i32();

    if (!from_host || !r.ok() || play == 0 || play != play_) {
        return;
    }

    if (scene < 0) {
        host_scene_id_ = -1;
        return;
    }

    // A scene not heard of before: not played here yet, its start by this game's clock.
    if (scene != host_scene_id_ || sequence != host_scene_sequence_ || level != host_scene_level_) {
        const auto now = clock_();
        const auto played = static_cast<std::uint64_t>(std::max<std::int32_t>(elapsed, 0));
        host_scene_id_ = scene;
        host_scene_sequence_ = sequence;
        host_scene_level_ = level;
        host_scene_start_ms_ = now - std::min(played, now);
        scene_done_ = false;
        log("the host plays scene " + std::to_string(scene) + " of level " + std::to_string(level)
            + (played > 0 ? " (" + std::to_string(played) + " ms in)" : ""));
    }
}

void Coop::tick_member_scene(std::uint64_t now, const GameStatus& status) {
    // The host out of its scenes for a while (its word of the end lost): its scene is over.
    if (!host_scene_ && host_out_since_ != 0 && now - host_out_since_ >= kHostOutMs) {
        host_scene_id_ = -1;
    }

    const bool here = play_ != 0 && applied_ == play_ && in_level(status.phase)
                      && status.level == host_level_ && status.level == host_scene_level_;

    // The host's scene playing here (started from its word, or by this game itself): how late.
    if (status.scene >= 0 && here && status.scene == host_scene_id_ && !scene_done_) {
        scene_done_ = true;
        scene_asked_ms_ = 0;
        shared_scene_ = status.scene;
        const std::uint64_t late = now > host_scene_start_ms_ ? now - host_scene_start_ms_ : 0;
        shared_lag_ms_ = std::min(late, kSceneLagMs);
        log("playing the host's scene " + std::to_string(status.scene) + " here, " + std::to_string(late)
            + " ms after the host");
    } else if (status.scene < 0 && shared_scene_ >= 0) {
        shared_scene_ = -1;
        shared_lag_ms_ = 0;
    }

    const bool wanted = here && host_scene_id_ >= 0 && !scene_done_;

    if (!wanted) {
        // A request the game has not acted on, for a scene that is over or elsewhere: dropped.
        if (scene_asked_ms_ != 0) {
            scene_asked_ms_ = 0;
            game_.play_scene(-1);
            log("the host's scene is over: not starting it here");
        }

        return;
    }

    // Started once the player is in control (the game waits for it too); asked again now and
    // then, in case a load dropped it.
    if (status.phase == kPhasePlaying && (scene_asked_ms_ == 0 || now - scene_asked_ms_ >= kSceneAskMs)) {
        if (game_.play_scene(host_scene_id_)) {
            if (scene_asked_ms_ == 0) {
                log("starting the host's scene " + std::to_string(host_scene_id_) + " here");
            }

            scene_asked_ms_ = now;
        }
    }
}

void Coop::reset_scenes() {
    if (scene_asked_ms_ != 0) {
        game_.play_scene(-1);
    }

    scene_ = scene_level_ = -1;
    scene_sequence_ = 0;
    scene_start_ms_ = scene_sent_ms_ = 0;
    host_scene_id_ = host_scene_level_ = -1;
    host_scene_sequence_ = 0;
    host_scene_start_ms_ = 0;
    scene_done_ = false;
    scene_asked_ms_ = 0;
    shared_scene_ = -1;
    shared_lag_ms_ = 0;
}

}  // namespace mp
