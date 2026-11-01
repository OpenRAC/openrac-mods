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
 * kSceneLagMs), so it plays out in full; the host's skip skips it at once (SKIP: only the host's).
 *
 * Each start of a host's scene is its own (SCENE's sequence, the party's count of them, the host's):
 * a skip says which one it was pressed in, and a member's SCENE_STATE which one it is in or out of,
 * so a skip or an "out" of one scene never ends or opens the next. A member that has not played the
 * host's scene yet (in a menu, a load) plays it once it can, even after the host's has ended, and
 * the barrier waits for it: a member's until it played it, the host's while the member says it
 * still plays it; the host's skip of it ends that wait. Every game counts its own scenes and loads
 * as they start (note_phase), and a skip asked of it acts on the one it came in only.
 */
#include <algorithm>
#include <string>

#include "coop.hpp"

namespace mp {

namespace {

bool in_party(const Session& session, std::int32_t id) {
    const std::vector<Member>& members = session.members();
    return std::any_of(members.begin(), members.end(), [&](const Member& m) { return m.id == id; });
}

}  // namespace

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
            // Never 0 (no scene), as the count wraps.
            if (++scene_sequence_ == 0) {
                ++scene_sequence_;
            }

            scene_start_ms_ = now;
            scene_level_ = status.level;
            barrier_open_ = false;
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

    // The host's scene over: the one this game knows of, if not played here yet, is still played
    // (when it can be soon: tick_member_scene).
    if (scene < 0) {
        if (host_scene_id_ >= 0 && sequence == host_scene_sequence_ && host_scene_end_ms_ == 0) {
            host_scene_end_ms_ = clock_();
        }

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
        host_scene_end_ms_ = 0;
        scene_first_ask_ms_ = 0;
        scene_done_ = false;
        barrier_open_ = false;
        log("the host plays scene " + std::to_string(scene) + " of level " + std::to_string(level)
            + (played > 0 ? " (" + std::to_string(played) + " ms in)" : ""));
    }
}

void Coop::tick_member_scene(std::uint64_t now, const GameStatus& status) {
    // The host out of its scenes for a while since its scene started (its word of the end lost):
    // its scene is over.
    if (host_scene_id_ >= 0 && host_scene_end_ms_ == 0 && !host_scene_ && host_out_since_ != 0
        && host_out_since_ >= host_scene_start_ms_ && now - host_out_since_ >= kHostOutMs) {
        host_scene_end_ms_ = host_out_since_;
    }

    const bool here = play_ != 0 && applied_ == play_ && in_level(status.phase)
                      && status.level == host_level_ && status.level == host_scene_level_;

    // The host's scene playing here (started from its word, or by this game itself): how late.
    if (status.scene >= 0 && here && status.scene == host_scene_id_ && !scene_done_) {
        scene_done_ = true;
        scene_asked_ms_ = 0;
        shared_scene_ = status.scene;
        shared_sequence_ = host_scene_sequence_;
        const std::uint64_t late = now > host_scene_start_ms_ ? now - host_scene_start_ms_ : 0;
        shared_lag_ms_ = std::min(late, kSceneLagMs);
        log("playing the host's scene " + std::to_string(status.scene) + " here, "
            + std::to_string(late) + " ms after the host");

        // Skipped by the party before it started here: skipped at once.
        if (party_skip_ != 0 && party_skip_ == host_scene_sequence_
            && skip_scene_ != scene_instance_) {
            log("the party skipped the host's scene " + std::to_string(status.scene)
                + ": skipping it here too");
            skip_here(now);
        }
    } else if (status.scene < 0 && shared_scene_ >= 0) {
        shared_scene_ = -1;
        shared_lag_ms_ = 0;
    }

    // Still to play here: not played, not skipped by the party, and, the host's over, soon (asked
    // for kSceneAskMs in vain, or kSceneLagMs after the host's end: given up).
    const bool skipped = party_skip_ != 0 && party_skip_ == host_scene_sequence_;
    const bool given_up =
        host_scene_end_ms_ != 0
        && (now - host_scene_end_ms_ >= kSceneLagMs
            || (scene_first_ask_ms_ != 0
                && now - std::max(scene_first_ask_ms_, host_scene_end_ms_) >= kSceneAskMs));

    // A scene under way when this member joined the game is not its to play: it stands by.
    if (joining_) {
        scene_done_ = true;
    }

    const bool wanted = here && host_scene_id_ >= 0 && !scene_done_ && !skipped && !given_up;
    scene_pending_ = wanted;
    send_scene_state(now);

    if (!wanted) {
        // A request the game has not acted on, for a scene that is over, skipped or elsewhere:
        // dropped.
        if (scene_asked_ms_ != 0) {
            scene_asked_ms_ = 0;
            game_.play_scene(-1);
            log(skipped ? "the party skipped the host's scene " + std::to_string(host_scene_id_)
                              + ": not starting it here"
                        : "the host's scene is over: not starting it here");
        }

        return;
    }

    // Started once the player is in control (the game waits for it too); asked again now and
    // then, in case a load dropped it.
    if (status.phase == kPhasePlaying
        && (scene_asked_ms_ == 0 || now - scene_asked_ms_ >= kSceneAskMs)) {
        if (game_.play_scene(host_scene_id_)) {
            if (scene_asked_ms_ == 0) {
                log("starting the host's scene " + std::to_string(host_scene_id_) + " here"
                    + (host_scene_end_ms_ != 0 ? " (the host's has ended)" : ""));
            }

            if (scene_first_ask_ms_ == 0) {
                scene_first_ask_ms_ = now;
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
    host_scene_end_ms_ = scene_first_ask_ms_ = 0;
    scene_pending_ = false;
    shared_sequence_ = state_sequence_ = 0;
    state_sent_ = 0xFF;
    state_ms_ = 0;
    scene_states_.clear();
    released_scene_ = 0;
    skip_scene_ = movie_load_ = 0;
    party_skip_ = 0;
    noted_phase_ = noted_scene_ = -1;
}

void Coop::note_phase(const GameStatus& status) {
    // A menu over a scene (or over the level) keeps what was before it.
    if (status.phase == kPhaseMenu) {
        return;
    }

    // A scene starts: out of something else into one, or another scene of the level's in its
    // place. A load starts (a movie plays in one).
    if (status.phase == kPhaseScene
        && (noted_phase_ != kPhaseScene || status.scene != noted_scene_)) {
        ++scene_instance_;
    } else if (status.phase == kPhaseLoading && noted_phase_ != kPhaseLoading) {
        ++load_instance_;
    }

    noted_phase_ = status.phase;
    noted_scene_ = status.phase == kPhaseScene ? status.scene : -1;
}

std::uint16_t Coop::shared_here(std::uint64_t now, const GameStatus& status) {
    if (status.phase != kPhaseScene || status.scene < 0) {
        return 0;
    }

    // The host's own: its newest scene, told now if this tick has not yet.
    if (session_.is_host()) {
        if (play_ != 0 && status.scene != scene_) {
            tick_host_scene(now, status);
        }

        return scene_ >= 0 && scene_ == status.scene ? scene_sequence_ : 0;
    }

    return status.scene == host_scene_id_ && status.level == host_scene_level_
               ? host_scene_sequence_
               : 0;
}

void Coop::skip_here(std::uint64_t now) {
    // The scene playing now, asked again a while (but no later one: skip_scene_).
    skip_scene_ = scene_instance_;
    skip_until_ = now + 1500;
    skip_ms_ = now;
    skip_asked_ = true;
    game_.skip();
}

void Coop::on_skip(std::int32_t sender, bool from_host, const Bytes& /*bytes*/, Reader& r) {
    const std::int32_t origin = r.i32();
    r.u8();
    r.u8();
    const bool ids = r.left() >= 7;
    const std::int32_t play = ids ? r.i32() : 0;
    const std::uint8_t what = ids ? r.u8() : 0;
    const std::uint8_t high = ids ? r.u8() : 0;
    const auto scene = static_cast<std::uint16_t>(high << 8 | (ids ? r.u8() : 0));

    // The origin's own game is skipping already; a member's word reaches the host only.
    if (!r.ok() || origin == session_.id() || play_ == 0
        || (!from_host && !(session_.is_host() && in_party(session_, sender)))) {
        return;
    }

    // Only the host skips: a member's (an older build's, or passed on by an older host) is ignored.
    if (session_.is_host() || origin != session_.host()) {
        log("skip requested by player " + std::to_string(origin)
            + ": only the host skips, ignored");
        return;
    }

    // Without what it was pressed in (an older build), or for another game: nothing to skip.
    if (!ids || play != play_ || what > kSkipMovie || (what == kSkipShared && scene == 0)) {
        log("skip requested by player " + std::to_string(origin)
            + " for no scene of this game's: ignored");
        return;
    }

    const GameStatus status = game_.status();
    const auto now = clock_();
    note_phase(status);
    const std::string by = "skip requested by player " + std::to_string(origin);

    if (what == kSkipMovie) {
        // The movie playing now, by Start pressed for it while it plays (movie_press).
        if (status.phase == kPhaseLoading) {
            skip_until_ = now + 1500;
            movie_presses_ = 24;
            movie_load_ = load_instance_;
            log(by + ": skipping the movie here");
        } else {
            log(by + ": nothing plays here");
        }

        return;
    }

    const std::uint16_t here = shared_here(now, status);

    if (what == kSkipShared) {
        // The host's scene `scene`: the newest known here (or one to come: its SCENE on its way);
        // an older one's skip is stale.
        const std::uint16_t newest = session_.is_host() ? scene_sequence_ : host_scene_sequence_;

        if (newest != 0 && older_scene(scene, newest)) {
            log(by + " of an earlier scene: ignored");
            return;
        }

        party_skip_ = scene;

        if (here == scene) {
            log(by + ": skipping the scene here");
            skip_here(now);
        } else if (scene_pending_ && scene == host_scene_sequence_) {
            log(by + ": the host's scene " + std::to_string(host_scene_id_)
                + " is not played here");
        } else {
            log(by
                + (status.phase == kPhaseScene ? ": another scene plays here, played on"
                                               : ": nothing plays here"));
        }

        return;
    }

    // A scene of the origin's own game: this game's own scene playing (none of the host's).
    if (status.phase == kPhaseScene && here == 0) {
        log(by + ": skipping the scene here");
        skip_here(now);
    } else {
        log(by
            + (status.phase == kPhaseScene ? ": the host's scene plays here, played on"
                                           : ": nothing plays here"));
    }
}

void Coop::send_scene_state(std::uint64_t now) {
    if (session_.is_host() || play_ == 0 || applied_ != play_ || host_scene_sequence_ == 0) {
        return;
    }

    const std::uint8_t state = shared_scene_ >= 0 && shared_sequence_ == host_scene_sequence_
                                   ? kScenePlaying
                               : scene_pending_ ? kScenePending
                                                : kSceneOut;

    if (state == state_sent_ && host_scene_sequence_ == state_sequence_
        && now - state_ms_ < kLevelMs) {
        return;
    }

    state_sent_ = state;
    state_sequence_ = host_scene_sequence_;
    state_ms_ = now;
    session_.send_data(
        session_.host(),
        Writer()
            .u8(static_cast<std::uint8_t>(GameKind::kSceneState))
            .i32(play_)
            .u8(static_cast<std::uint8_t>(host_scene_sequence_ >> 8))
            .u8(static_cast<std::uint8_t>(host_scene_sequence_))
            .u8(state)
            .bytes()
    );
}

void Coop::on_scene_state(std::int32_t sender, Reader& r) {
    const std::int32_t play = r.i32();
    const std::uint8_t high = r.u8();
    const auto sequence = static_cast<std::uint16_t>(high << 8 | r.u8());
    const std::uint8_t state = r.u8();

    if (!session_.is_host() || !r.ok() || play == 0 || play != play_ || state > kSceneOut
        || !in_party(session_, sender)) {
        return;
    }

    // A word of an older scene than the last heard is late: no word of the newer one.
    const auto known = scene_states_.find(sender);

    if (known != scene_states_.end() && older_scene(sequence, known->second.sequence)) {
        return;
    }

    scene_states_[sender] = {sequence, state, clock_()};
}

bool Coop::members_in_scene(std::uint64_t now, const GameStatus& status) const {
    // The host's newest scene, in this level, not skipped: a member that says it is still to play
    // it or plays it (lately). A word of another scene is none of this one.
    if (scene_sequence_ == 0 || scene_level_ != status.level || party_skip_ == scene_sequence_) {
        return false;
    }

    return std::any_of(scene_states_.begin(), scene_states_.end(), [&](const auto& entry) {
        const SceneState& s = entry.second;
        return s.sequence == scene_sequence_ && s.state != kSceneOut && now - s.ms < 2 * kLevelMs
               && in_party(session_, entry.first);
    });
}

}  // namespace mp
