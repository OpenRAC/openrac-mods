/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.hpp): the host's PLAY and its save, LEVEL, WANT and the tick.
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

std::string save_description(const std::string& planet, int gold_bolts, bool new_game) {
    if (new_game) {
        return "New game";
    }

    std::string text = planet.empty() ? "A save" : planet;

    // The gold bolts say how far a save has come, where the game has no percentage.
    if (gold_bolts > 0) {
        text += ", " + std::to_string(gold_bolts) + (gold_bolts == 1 ? " gold bolt" : " gold bolts");
    }

    return text;
}

Coop::Coop(Session& session, Game& game, std::function<std::uint64_t()> clock)
    : session_(session), game_(game), clock_(std::move(clock)) {
    // Without a clock of the caller's, the monotonic one.
    if (!clock_) {
        clock_ = now_ms;
    }
}

void Coop::choose(Bytes save, std::string description) {
    chosen_ = std::move(save);
    description_ = std::move(description);
}

std::string Coop::take_notice() {
    std::string out;
    out.swap(notice_);
    return out;
}

int Coop::figures() const {
    return static_cast<int>(std::count_if(remotes_.begin(), remotes_.end(), [](const auto& r) {
        return r.second.figure >= 0;
    }));
}

bool Coop::start() {
    // The host starts, once, from the title, when every member is ready.
    if (!session_.is_host() || play_ != 0 || game_.status().phase != kPhaseFrontEnd
        || !session_.all_ready()) {
        return false;
    }

    // A game's id: any but 0 (none), new each time.
    play_ = static_cast<std::int32_t>((clock_() * 2654435761u) | 1u);
    const int level = chosen_.empty() ? 0 : -1;
    session_.start();
    send_play(kBroadcast, chosen_, level, false);
    told_.clear();

    for (const Member& m : session_.members()) {
        if (m.id != session_.id()) {
            told_.push_back(m.id);
        }
    }

    log("starting " + (chosen_.empty() ? std::string("a new game") : "a save of "
                       + std::to_string(chosen_.size()) + " bytes") + " for the party");

    // The host plays it too; a refusal (a save the game will not take) is said.
    if (!game_.start(level, chosen_)) {
        notice_ = "The game would not start";
        return false;
    }

    start_ms_ = clock_();

    return true;
}

bool Coop::start_blocked(int phase) const {
    return (phase == kPhaseScene || phase == kPhaseLoading) && play_ != 0 && session_.in_party()
           && !session_.is_host() && !joining_;
}

void Coop::own_skip(int phase, std::uint64_t now) {
    // Only a scene or a movie (a load: the title leaving, the flight between planets), in a game
    // of the party's, once in a while.
    if ((phase != kPhaseScene && phase != kPhaseLoading) || play_ == 0 || !session_.in_party()
        || (own_skip_ms_ != 0 && now - own_skip_ms_ < kSkipMs) || joining_) {
        return;
    }

    own_skip_ms_ = now;

    // Only the host skips: a member's Start is taken out of its pad (start_blocked), and said so.
    if (!session_.is_host()) {
        skip_refused_until_ = now + kSkipRefusedMs;
        log("skip pressed here: only the host can skip, it plays on");
        return;
    }

    ++skip_sequence_;

    // What it was pressed in: the host's scene (by its sequence), a scene of this game's own, or a
    // movie; the others skip that one only.
    const GameStatus status = game_.status();
    note_phase(status);
    const std::uint16_t shared = phase == kPhaseScene ? shared_here(now, status) : 0;
    const std::uint8_t what = phase != kPhaseScene ? kSkipMovie : shared != 0 ? kSkipShared : kSkipOther;

    if (shared != 0) {
        party_skip_ = shared;
    }

    log("skip pressed here: the party skips it"
        + (shared != 0 ? std::string(" (the host's scene ") + std::to_string(shared) + ")"
                       : std::string()));
    const Bytes bytes = Writer()
                            .u8(static_cast<std::uint8_t>(GameKind::kSkip))
                            .i32(session_.id())
                            .u8(static_cast<std::uint8_t>(skip_sequence_ >> 8))
                            .u8(static_cast<std::uint8_t>(skip_sequence_))
                            .i32(play_)
                            .u8(what)
                            .u8(static_cast<std::uint8_t>(shared >> 8))
                            .u8(static_cast<std::uint8_t>(shared))
                            .bytes();
    session_.send_data(session_.is_host() ? kBroadcast : session_.host(), bytes);
}

bool Coop::movie_press(std::uint64_t now) {
    // Only for the movie the skip came in (the same load): never the next one, nor a scene.
    const GameStatus status = game_.status();
    note_phase(status);

    // On its way into a game that is on: whatever plays on the way, press after press.
    if (joining_ && status.phase == kPhaseLoading) {
        joining_press_ = !joining_press_;
        return joining_press_;
    }

    if (movie_presses_ <= 0 || now >= skip_until_ || status.phase != kPhaseLoading
        || movie_load_ != load_instance_) {
        movie_presses_ = 0;
        return false;
    }

    // Pressed every other report: each press is a new one.
    --movie_presses_;
    return (movie_presses_ & 1) != 0;
}

void Coop::handle(std::int32_t sender, const Bytes& bytes) {
    if (bytes.empty() || !session_.in_party()) {
        return;
    }

    const auto kind = static_cast<GameKind>(bytes[0]);

    if (kind == GameKind::kPose) {
        on_pose(sender, bytes);
        return;
    }

    if (kind == GameKind::kShot) {
        on_shot(sender, bytes);
        return;
    }

    Reader r(bytes.data() + 1, bytes.size() - 1);
    const bool from_host = sender == session_.host() && !session_.is_host();

    if (kind == GameKind::kPoses) {
        if (from_host) {
            on_poses(sender, r);
        }

        return;
    }

    switch (kind) {
        case GameKind::kPlay: {
            const std::int32_t play = r.i32();
            const bool save = r.u8() != 0;
            const std::int32_t level = r.i32();
            const std::int32_t size = r.i32();
            const std::string build = r.left() > 0 ? r.str() : std::string();
            const bool joining = r.left() > 0 && r.u8() != 0;

            // The host's word only, whole, and a save of a size a game has.
            if (!from_host || !r.ok() || play == 0 || size < 0
                || static_cast<std::size_t>(size) > kMaxSaveBytes || (save && size == 0)) {
                return;
            }

            // Another build plays another game: refused, and said why (the lobby's page has it
            // in full: party_mods.hpp).
            if (build != build_) {
                notice_ = "You can't join: your game differs from the host's";
                log("refused the host's game: its build is " + build + ", this one " + build_);
                play_ = play;
                applied_ = play;  // not asked for again
                return;
            }

            play_ = play;
            pending_ = true;
            pending_play_ = play;
            pending_level_ = level;
            pending_save_ = save;
            pending_joining_ = joining;
            expected_ = save ? static_cast<std::size_t>(size) : 0;
            received_ = 0;
            save_.assign(expected_, 0);
            notice_ = "Game starting";
            log("the host's game: " + (save ? std::to_string(size) + " bytes of save" : std::string("a new game"))
                + ", level " + std::to_string(level));
            break;
        }
        case GameKind::kChunk: {
            const std::int32_t play = r.i32();
            const std::int32_t at = r.i32();
            const Bytes piece = r.rest();

            // A piece of the save awaited, inside it.
            if (!from_host || !r.ok() || !pending_ || play != pending_play_ || at < 0
                || static_cast<std::size_t>(at) + piece.size() > expected_) {
                return;
            }

            std::copy(piece.begin(), piece.end(), save_.begin() + at);
            received_ += piece.size();
            break;
        }
        case GameKind::kLevel: {
            const std::int32_t play = r.i32();
            const std::int32_t level = r.i32();
            const bool scene = r.left() > 0 && r.u8() != 0;
            // The host's travels (a host without them: none).
            const bool travels = r.left() >= 7;
            const std::uint8_t travel_high = travels ? r.u8() : 0;
            const auto travel = static_cast<std::uint16_t>(travel_high << 8 | (travels ? r.u8() : 0));
            const std::int32_t dest = travels ? r.i32() : -1;
            const bool arriving = travels && r.u8() != 0;

            if (!from_host || !r.ok() || play == 0) {
                break;
            }

            play_ = play;
            host_level_ = level;
            host_scene_ = scene;
            host_arriving_ = arriving;

            if (!travels) {
                break;
            }

            // The first word heard is where the host is now (this game starts there); a newer
            // travel is a departure to follow.
            if (!heard_travel_) {
                heard_travel_ = true;
                host_travel_ = travel;
            } else if (travel != host_travel_) {
                host_travel_ = travel;

                if (dest >= 0 && applied_ == play_) {
                    const auto now = clock_();
                    leave_dest_ = travel_dest_ = dest;
                    leave_ms_ = 0;
                    arriving_ = true;
                    depart_ms_ = now;
                    log("the host's ship leaves for level " + std::to_string(dest) + ": leaving with it");
                }
            }

            break;
        }
        case GameKind::kSkip:
            on_skip(sender, from_host, bytes, r);
            break;
        case GameKind::kWant: {
            const std::int32_t play = r.i32();

            // The host answers a member that asks for the game that is on.
            if (session_.is_host() && r.ok() && play_ != 0 && play == play_
                && member_of(session_, sender) && may_play(sender)) {
                const GameStatus status = game_.status();
                const Bytes live = in_level(status.phase) ? game_.capture() : Bytes{};

                log("player " + std::to_string(sender) + " asked for the game");

                if (!live.empty()) {
                    send_play(sender, live, status.level, true);
                } else {
                    send_play(sender, chosen_, chosen_.empty() ? 0 : -1, false);
                }
            }

            break;
        }
        case GameKind::kScene:
            on_scene(from_host, r);
            break;
        case GameKind::kSceneState:
            on_scene_state(sender, r);
            break;
        default:
            break;
    }
}

void Coop::tick(std::uint64_t now) {
    // A party playing: the world runs on under the game's menus; otherwise the game as it is.
    const bool overlay = session_.in_party() && play_ != 0;

    if (overlay != overlay_) {
        overlay_ = overlay;
        game_.overlay_pause(overlay);
    }

    if (!session_.in_party()) {
        // The party went: so does what was shown of it, and a game playing the party's (the
        // host's as a member's: alone, its world is no game) goes back to the title, then to the
        // Multiplayer page. One in the title's pages stays.
        if (play_ != 0 || !remotes_.empty() || pending_) {
            const int phase = game_.status().phase;
            const bool starting = start_ms_ != 0 && now - start_ms_ < kStartingMs;

            if (party_game_ && phase != kPhaseNone && (phase != kPhaseFrontEnd || starting)) {
                to_title_ = true;
                title_since_ = now;
                left_title_ = phase != kPhaseFrontEnd;
                title_ms_ = 0;
                title_taken_ = false;
                home_ = false;
                log("out of the party: back to the title");
            }

            reset();
        }

        tick_leaving(now);
        return;
    }

    to_title_ = home_ = false;

    party_game_ = play_ != 0 && (session_.is_host() || applied_ == play_);

    // The clock asked again now and then, so a long session's stamps do not drift.
    if (clock_ms_ == 0) {
        clock_ms_ = now;
    } else if (now - clock_ms_ >= kClockMs) {
        clock_ms_ = now;
        session_.sync_clock();
    }

    const GameStatus status = game_.status();
    note_phase(status);

    // Each change of the game's phase, level or load in the log, with the clock (the same on every
    // game of a machine): the party's timeline.
    if (status.phase != logged_.phase || status.level != logged_.level || status.loads != logged_.loads) {
        logged_ = status;
        log("game at " + std::to_string(now) + " ms: phase " + std::to_string(status.phase) + ", level "
            + std::to_string(status.level) + ", load " + std::to_string(status.loads)
            + (arriving_ ? ", arriving" : ""));
    }

    if (session_.is_host()) {
        tick_host(now, status);
    } else {
        tick_member(now, status);
    }

    send_pose(now, status);
    send_shots(status);

    // The host out of a level has no pose of its own to go with the members': theirs go alone.
    if (session_.is_host() && !relay_.empty() && now - relay_ms_ >= 2 * kPoseMs) {
        relay_ms_ = now;

        if (session_.members().size() > 2) {
            send_poses(nullptr);
        } else {
            relay_.clear();
        }
    }

    if (tracing() && in_level(status.phase)) {
        Pose hero;

        if (game_.hero(hero)) {
            mp::trace("H", static_cast<double>(now), 0.0, hero);
        }
    }

    show_remotes(now, status);
    update_barrier(now, status);
}

void Coop::tick_member(std::uint64_t now, const GameStatus& status) {
    // The host's game, once its save is all here.
    if (pending_ && (!pending_save_ || received_ >= expected_)) {
        if (status.phase == kPhaseFrontEnd) {
            pending_ = false;

            if (game_.start(pending_level_, pending_save_ ? save_ : Bytes{})) {
                applied_ = pending_play_;
                start_ms_ = now;
                joining_ = pending_joining_;
                joining_since_ = now;
                log(joining_ ? "joining the host's game" : "started the host's game");
            } else {
                notice_ = "The host's game would not start here";
            }

            save_.clear();
        } else if (in_level(status.phase)) {
            // Playing already (the host started again): the level follows, the save cannot.
            pending_ = false;
            applied_ = pending_play_;

            if (pending_level_ >= 0) {
                host_level_ = pending_level_;
            }
        }
    }

    tick_joining(now, status);

    // A scene of this game's that the host is out of (skipped there, or played out): skipped here,
    // through the game's own skip, so the party goes on together.
    const bool together = play_ != 0 && applied_ == play_ && host_level_ == status.level;

    if (host_scene_ || host_out_since_ == 0) {
        host_out_since_ = host_scene_ ? 0 : now;
    }

    // A scene of the host's that started late here plays out as long as it was late (coop.hpp).
    tick_member_scene(now, status);
    const bool host_out =
        !host_scene_ && host_out_since_ != 0 && now - host_out_since_ >= kHostOutMs + scene_lag_ms();

    if (together && status.phase == kPhaseScene && host_out
        && (skip_ms_ == 0 || now - skip_ms_ >= kSkipMs)) {
        if (skip_ms_ == 0) {
            log("skip requested: the host is out of the scene");
        }

        skip_ms_ = now;
        skip_asked_ = true;
        game_.skip();
    } else if (status.phase != kPhaseScene) {
        skip_ms_ = 0;
    }

    // The host's ship left: this game leaves too, from wherever it is in its level (playing, a
    // scene, the pause menu, a vendor), asked again until a level load says it went; then the
    // party arrives together (the host says when everyone is in).
    if (leave_dest_ >= 0) {
        if ((leave_ms_ != 0 && status.loads != leave_loads_)
            || (in_level(status.phase) && status.level == leave_dest_)) {
            log("left for level " + std::to_string(leave_dest_));
            leave_dest_ = -1;
        } else if (in_level(status.phase) && status.level != leave_dest_
                   && (leave_ms_ == 0 || now - leave_ms_ >= kLeaveMs)) {
            if (leave_ms_ == 0) {
                log("leaving level " + std::to_string(status.level) + " for " + std::to_string(leave_dest_)
                    + " (phase " + std::to_string(status.phase) + ")");
            }

            leave_ms_ = now;
            leave_loads_ = status.loads;
            game_.travel(leave_dest_);
        }
    }

    if (arriving_) {
        if (!host_arriving_ && in_level(status.phase) && host_level_ == status.level) {
            arriving_ = false;
            log("everyone is in level " + std::to_string(status.level) + ": going on");
        } else if (now - depart_ms_ >= kArriveMs) {
            arriving_ = false;
            log("the party's arrival took too long: going on");
        }
    }

    // In the host's game: in the host's level (not while the party is on its way to it).
    if (play_ != 0 && applied_ == play_ && status.phase == kPhasePlaying && host_level_ >= 0 && !arriving_
        && status.level != host_level_ && (travel_ms_ == 0 || now - travel_ms_ >= kTravelMs)) {
        travel_ms_ = now;
        log("travelling to the host's level " + std::to_string(host_level_));
        game_.travel(host_level_);
    }

    // Back in the title with the host's game on: asked for again.
    if (play_ != 0 && applied_ != play_ && !pending_ && status.phase == kPhaseFrontEnd
        && (want_ms_ == 0 || now - want_ms_ >= kWantMs)) {
        want_ms_ = now;
        session_.send_data(
            session_.host(),
            Writer().u8(static_cast<std::uint8_t>(GameKind::kWant)).i32(play_).bytes()
        );
    }
}

void Coop::reset() {
    for (auto& entry : remotes_) {
        drop_figure(entry.first, entry.second, "the party's game reset");
    }

    remotes_.clear();
    relay_.clear();
    joining_ = pending_joining_ = world_ready_ = false;
    play_ = 0;
    told_.clear();
    pending_ = false;
    save_.clear();
    expected_ = received_ = 0;
    applied_ = 0;
    party_game_ = false;
    start_ms_ = 0;
    host_scene_ = sent_scene_ = false;
    hold_ = false;
    skip_until_ = 0;
    movie_presses_ = 0;
    skip_scene_ = movie_load_ = 0;
    hold_since_ = skip_ms_ = host_out_since_ = 0;
    in_scene_ = skip_asked_ = false;
    host_level_ = -1;
    sent_level_ = -2;
    pose_sent_ = false;
    travel_ = 0;
    travel_dest_ = -1;
    arriving_ = sent_arriving_ = false;
    waiting_for_.clear();
    heard_travel_ = false;
    host_travel_ = 0;
    leave_dest_ = -1;
    leave_ms_ = 0;
    host_arriving_ = false;
    refused_until_ = 0;
    skip_refused_until_ = 0;
    reset_scenes();
}

}  // namespace mp
