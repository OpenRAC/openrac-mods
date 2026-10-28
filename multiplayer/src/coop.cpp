/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.hpp): the host's PLAY and its save, LEVEL, WANT, the players' poses and
 * their figures.
 */
#include "coop.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mp {
namespace {

double step(const Pose& a, const Pose& b) {
    double sum = 0.0;

    for (int i = 0; i < 3; ++i) {
        const double d = a.position[i] - b.position[i];
        sum += d * d;
    }

    return std::sqrt(sum);
}

bool member_of(const Session& session, std::int32_t id) {
    const std::vector<Member>& members = session.members();
    return std::any_of(members.begin(), members.end(), [&](const Member& m) { return m.id == id; });
}

}  // namespace

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
    // The host starts, once, from the title.
    if (!session_.is_host() || play_ != 0 || game_.status().phase != kPhaseFrontEnd) {
        return false;
    }

    // A game's id: any but 0 (none), new each time.
    play_ = static_cast<std::int32_t>((clock_() * 2654435761u) | 1u);
    const int level = chosen_.empty() ? 0 : -1;
    session_.start();
    send_play(kBroadcast, chosen_, level);
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

    return true;
}

bool Coop::rejoin() {
    if (session_.is_host() || play_ == 0 || pending_) {
        return false;
    }

    // As one that never played it: WANT goes at the next tick.
    applied_ = 0;
    want_ms_ = 0;
    return true;
}

void Coop::send_play(std::int32_t target, const Bytes& save, int level) {
    session_.send_data(
        target,
        Writer()
            .u8(static_cast<std::uint8_t>(GameKind::kPlay))
            .i32(play_)
            .u8(save.empty() ? 0 : 1)
            .i32(level)
            .i32(static_cast<std::int32_t>(save.size()))
            .str(build_)
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

void Coop::handle(std::int32_t sender, const Bytes& bytes) {
    if (bytes.empty() || !session_.in_party()) {
        return;
    }

    const auto kind = static_cast<GameKind>(bytes[0]);

    if (kind == GameKind::kPose) {
        on_pose(sender, bytes);
        return;
    }

    Reader r(bytes.data() + 1, bytes.size() - 1);
    const bool from_host = sender == session_.host() && !session_.is_host();

    switch (kind) {
        case GameKind::kPlay: {
            const std::int32_t play = r.i32();
            const bool save = r.u8() != 0;
            const std::int32_t level = r.i32();
            const std::int32_t size = r.i32();
            const std::string build = r.left() > 0 ? r.str() : std::string();

            // The host's word only, whole, and a save of a size a game has.
            if (!from_host || !r.ok() || play == 0 || size < 0
                || static_cast<std::size_t>(size) > kMaxSaveBytes || (save && size == 0)) {
                return;
            }

            // Another build plays another game: refused, and said why.
            if (build != build_) {
                notice_ = "Incompatible version: the host has " + (build.empty() ? "another" : build);
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

            if (from_host && r.ok() && play != 0) {
                play_ = play;
                host_level_ = level;
                host_scene_ = scene;
            }

            break;
        }
        case GameKind::kWant: {
            const std::int32_t play = r.i32();

            // The host answers a member that asks for the game that is on.
            if (session_.is_host() && r.ok() && play_ != 0 && play == play_
                && member_of(session_, sender)) {
                const GameStatus status = game_.status();
                const Bytes live = in_level(status.phase) ? game_.capture() : Bytes{};

                log("player " + std::to_string(sender) + " asked for the game");

                if (!live.empty()) {
                    send_play(sender, live, status.level);
                } else {
                    send_play(sender, chosen_, chosen_.empty() ? 0 : -1);
                }
            }

            break;
        }
        default:
            break;
    }
}

void Coop::on_pose(std::int32_t sender, const Bytes& bytes) {
    PoseMessage m;

    if (!decode_pose(bytes, m) || m.owner == session_.id() || !member_of(session_, m.owner)) {
        return;
    }

    if (session_.is_host()) {
        // A member speaks for itself only; the host passes it on to everyone.
        if (sender != m.owner) {
            return;
        }

        session_.send_state(kBroadcast, bytes);
    } else if (sender != session_.host()) {
        return;
    }

    // The stamp in this game's clock: the server's clock less this game's offset to it.
    const auto now = static_cast<double>(clock_());
    const double time =
        session_.has_clock() ? static_cast<double>(m.stamp_ms) - session_.clock_offset_ms() : now;
    Remote& r = remotes_[m.owner];
    r.poses.add(m.pose, m.sequence, time, now, m.teleport);
    r.scene = m.scene;
}

void Coop::tick(std::uint64_t now) {
    // A party playing: the world runs on under the game's menus; otherwise the game as it is.
    const bool overlay = session_.in_party() && play_ != 0;

    if (overlay != overlay_) {
        overlay_ = overlay;
        game_.overlay_pause(overlay);
    }

    if (!session_.in_party()) {
        // The party went: so does what was shown of it, and a member in the host's game (whose
        // world was the host's) goes back to the title.
        if (play_ != 0 || !remotes_.empty() || pending_) {
            if (member_playing_ && game_.status().phase == kPhasePlaying && game_.travel(-1)) {
                notice_ = "The party is over: back to the title";
                log("the party is over: back to the title");
            }

            reset();
        }

        return;
    }

    member_playing_ = !session_.is_host() && play_ != 0 && applied_ == play_;

    // The clock asked again now and then, so a long session's stamps do not drift.
    if (clock_ms_ == 0) {
        clock_ms_ = now;
    } else if (now - clock_ms_ >= kClockMs) {
        clock_ms_ = now;
        session_.sync_clock();
    }

    const GameStatus status = game_.status();

    if (session_.is_host()) {
        tick_host(now, status);
    } else {
        tick_member(now, status);
    }

    send_pose(now, status);
    show_remotes(now, status);
    update_barrier(now, status);
}

void Coop::update_barrier(std::uint64_t now, const GameStatus& status) {
    // The scene's start and end in the log, and whether a skip ended it.
    const bool scene = status.phase == kPhaseScene;

    if (scene && !in_scene_) {
        log(std::string("scene started (") + (session_.is_host() ? "host" : "member")
            + (host_scene_ && !session_.is_host() ? ", the host's too" : "") + ")");
    } else if (!scene && in_scene_) {
        log(skip_asked_ ? "scene ended after the party's skip" : "scene ended");
        skip_asked_ = false;
    }

    in_scene_ = scene;
    bool waiting = false;

    if (session_.is_host() && play_ != 0) {
        // The host out of a scene while a member in its level still plays one.
        for (const auto& [id, r] : remotes_) {
            Pose p;
            waiting = waiting
                      || (r.scene && r.poses.newest(p) && p.level == status.level
                          && r.poses.silence_ms(static_cast<double>(now)) < 2000.0);
        }
    } else if (play_ != 0 && applied_ == play_ && host_level_ == status.level) {
        // A member out of its scene while the host's goes on.
        waiting = host_scene_;
    }

    waiting = waiting && status.phase == kPhasePlaying;

    if (!waiting) {
        if (hold_since_ != 0) {
            log("everyone is out of the scene: going on");
        }

        hold_since_ = 0;
        hold_ = false;
    } else {
        if (hold_since_ == 0) {
            hold_since_ = now;
            log(session_.is_host() ? "waiting for the members to leave their scene"
                                   : "waiting for the host's scene to end");
        }

        const std::uint64_t most = session_.is_host() ? kBarrierMs : kHostSceneMs;
        hold_ = now - hold_since_ < most;
    }

    const bool block = !session_.is_host() && play_ != 0 && status.phase == kPhaseScene && host_scene_;

    if (block && !block_start_) {
        log("Start in this scene is the host's to press");
    }

    block_start_ = block;
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

        if (m.id == session_.id() || told || !in_level(status.phase)) {
            continue;
        }

        const Bytes live = game_.capture();

        if (!live.empty()) {
            log("player " + std::to_string(m.id) + " joins the game in level " + std::to_string(status.level));
            send_play(m.id, live, status.level);
            told_.push_back(m.id);
        }
    }

    // The level played, at each change and once a second.
    const int level = in_level(status.phase) ? status.level : -1;

    const bool scene = status.phase == kPhaseScene;
    host_scene_ = scene;

    if (level != sent_level_ || scene != sent_scene_ || now - level_ms_ >= kLevelMs) {
        sent_level_ = level;
        sent_scene_ = scene;
        level_ms_ = now;
        session_.send_data(
            kBroadcast,
            Writer()
                .u8(static_cast<std::uint8_t>(GameKind::kLevel))
                .i32(play_)
                .i32(level)
                .u8(scene ? 1 : 0)
                .bytes()
        );
    }
}

void Coop::tick_member(std::uint64_t now, const GameStatus& status) {
    // The host's game, once its save is all here.
    if (pending_ && (!pending_save_ || received_ >= expected_)) {
        if (status.phase == kPhaseFrontEnd) {
            pending_ = false;

            if (game_.start(pending_level_, pending_save_ ? save_ : Bytes{})) {
                applied_ = pending_play_;
                log("started the host's game");
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

    // A scene of this game's that the host is out of (skipped there, or played out): skipped here,
    // through the game's own skip, so the party goes on together.
    const bool together = play_ != 0 && applied_ == play_ && host_level_ == status.level;

    if (host_scene_ || host_out_since_ == 0) {
        host_out_since_ = host_scene_ ? 0 : now;
    }

    const bool host_out = !host_scene_ && host_out_since_ != 0 && now - host_out_since_ >= kHostOutMs;

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

    // In the host's game: in the host's level.
    if (play_ != 0 && applied_ == play_ && status.phase == kPhasePlaying && host_level_ >= 0
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

void Coop::send_pose(std::uint64_t now, const GameStatus& status) {
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
    m.scene = status.phase == kPhaseScene;
    m.sequence = ++sequence_;
    m.stamp_ms = session_.has_clock() ? session_.server_ms() : static_cast<std::int64_t>(now);
    pose_ms_ = now;
    pose_loads_ = status.loads;
    pose_sent_ = true;
    last_pose_ = m.pose;
    session_.send_state(session_.is_host() ? kBroadcast : session_.host(), encode_pose(m));
}

void Coop::drop_figure(Remote& r) {
    if (r.figure >= 0) {
        game_.figure_remove(r.figure);
        r.figure = -1;
    }
}

void Coop::show_remotes(std::uint64_t now, const GameStatus& status) {
    // A new level load took every figure with it.
    if (status.loads != figure_loads_) {
        figure_loads_ = status.loads;

        for (auto& entry : remotes_) {
            entry.second.figure = -1;
        }
    }

    for (auto it = remotes_.begin(); it != remotes_.end();) {
        Remote& r = it->second;

        // Out of the party: gone.
        if (!member_of(session_, it->first)) {
            drop_figure(r);
            it = remotes_.erase(it);
            continue;
        }

        Pose pose;
        const auto t = static_cast<double>(now);
        // A player who died is not shown until it stands again (its health back).
        const bool shown = in_level(status.phase) && r.poses.at(t, pose)
                           && r.poses.silence_ms(t) < kGoneMs && pose.level == status.level
                           && pose.health > 0;

        // Elsewhere, silent, or this game not in a level: no figure.
        if (!shown) {
            drop_figure(r);
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

        if (r.figure >= 0 && !game_.figure_pose(r.figure, pose)) {
            r.figure = -1;
        }

        ++it;
    }
}

bool Coop::latest(std::int32_t player, Pose& out) const {
    const auto it = remotes_.find(player);
    return it != remotes_.end() && it->second.poses.newest(out);
}

int Coop::figure_of(std::int32_t player) const {
    const auto it = remotes_.find(player);
    return it != remotes_.end() ? it->second.figure : -1;
}

void Coop::reset() {
    for (auto& entry : remotes_) {
        drop_figure(entry.second);
    }

    remotes_.clear();
    play_ = 0;
    told_.clear();
    pending_ = false;
    save_.clear();
    expected_ = received_ = 0;
    applied_ = 0;
    member_playing_ = false;
    host_scene_ = sent_scene_ = false;
    hold_ = block_start_ = false;
    hold_since_ = skip_ms_ = host_out_since_ = 0;
    in_scene_ = skip_asked_ = false;
    host_level_ = -1;
    sent_level_ = -2;
    pose_sent_ = false;
}

}  // namespace mp
