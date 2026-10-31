/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.hpp): the host's PLAY and its save, LEVEL, WANT, the players' poses and
 * their figures.
 */
#include "coop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

/* OPENRAC_ANIM_TRACE set: the poses sent, received and shown, a line each on stderr (ATRACE). */
bool tracing() {
    static const bool on = std::getenv("OPENRAC_ANIM_TRACE") != nullptr;
    return on;
}

void trace(const char* what, double now, double extra, const Pose& p) {
    std::fprintf(stderr,
                 "ATRACE %s %.1f %.1f %d %u %.3f %.3f %.3f %.4f %.3f %.3f %.3f %d %u %u %u %.4f %.4f\n",
                 what, now, extra, p.level, p.flags, p.position[0], p.position[1], p.position[2],
                 p.rotation[2], p.velocity[0], p.velocity[1], p.velocity[2], p.state, p.seq, p.frame,
                 p.next_frame, p.anim_time, p.anim_speed);
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
                && member_of(session_, sender)) {
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

    if (tracing() && in_level(status.phase)) {
        Pose hero;

        if (game_.hero(hero)) {
            mp::trace("H", static_cast<double>(now), 0.0, hero);
        }
    }

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

        // Or still to play (or playing) the host's newest scene, as its SCENE_STATE says.
        waiting = waiting || members_in_scene(now, status);
    } else if (play_ != 0 && applied_ == play_ && host_level_ == status.level) {
        // A member out of its scene while the host's goes on, or with the host's scene still to
        // play here (until it played it, or the party skipped it).
        waiting = host_scene_ || scene_pending_;
    }

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
            log(arrival ? (session_.is_host() ? "waiting for " + std::to_string(waiting_for_.size())
                                                    + " players to arrive in level " + std::to_string(status.level)
                                              : "waiting for the party to arrive in level " + std::to_string(status.level))
                : session_.is_host() ? "waiting for the members to leave their scene"
                                     : "waiting for the host's scene to end");
        }

        // The arrival's own limit is kArriveMs (it ends the wait itself).
        const std::uint64_t most = arrival ? kArriveMs : session_.is_host() ? kBarrierMs : kHostSceneMs;
        hold_ = now - hold_since_ < most;
    }

    // A party's skip still to act on the scene of this game's it came in (not one started since).
    if (skip_scene_ != 0 && skip_scene_ == scene_instance_ && now < skip_until_
        && status.phase == kPhaseScene && now - skip_ms_ >= kSkipMs) {
        skip_ms_ = now;
        skip_asked_ = true;
        game_.skip();
    }
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
            send_play(m.id, live, status.level, true);
            told_.push_back(m.id);
        }
    }

    tick_arrival(now, status);

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

    if (!session_.is_host()) {
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

    if (arriving_ && hold_) {
        return "Waiting for the other players to arrive";
    }

    if (joining_) {
        return "Joining the game";
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
    session_.send_state(session_.is_host() ? kBroadcast : session_.host(), encode_pose(m));
}

void Coop::send_shots(const GameStatus& status) {
    // Taken each tick, so none is left over from before a level or a party.
    const std::vector<Shot> shots = game_.hero_shots();

    if (!in_level(status.phase) || play_ == 0) {
        return;
    }

    for (const Shot& shot : shots) {
        ShotMessage m;
        m.owner = session_.id();
        m.level = status.level;
        m.shot = shot;
        session_.send_data(session_.is_host() ? kBroadcast : session_.host(), encode_shot(m));
        ++shots_sent_;
    }
}

void Coop::on_shot(std::int32_t sender, const Bytes& bytes) {
    ShotMessage m;

    if (!decode_shot(bytes, m) || m.owner == session_.id() || !member_of(session_, m.owner)) {
        return;
    }

    if (session_.is_host()) {
        // As a pose: a member's own, passed on to everyone.
        if (sender != m.owner) {
            return;
        }

        session_.send_data(kBroadcast, bytes);
    } else if (sender != session_.host()) {
        return;
    }

    // A scene playing here: the others' figures are hidden, their shots dropped.
    if (game_.status().phase == kPhaseScene) {
        return;
    }

    Remote& r = remotes_[m.owner];

    if (r.shots.size() >= kShotsWaiting) {
        r.shots.pop_front();
    }

    // Fired when its figure is where the shooter was: as long after it came as its poses are
    // shown after they come (the delay less the transit).
    const auto now = static_cast<double>(clock_());
    r.shots.push_back({now + r.poses.behind_ms(), m.level, m.shot});
}

void Coop::fire_shots(Remote& r, std::uint64_t now, const GameStatus& status) {
    const auto t = static_cast<double>(now);

    while (!r.shots.empty() && r.shots.front().due_ms <= t) {
        const Remote::PendingShot shot = r.shots.front();
        r.shots.pop_front();

        // Another level's, one whose figure is not shown in time, or one in a scene here: dropped.
        if (shot.level != status.level || r.figure < 0 || r.hidden || status.phase == kPhaseScene
            || t - shot.due_ms > kShotLateMs) {
            continue;
        }

        if (game_.figure_shot(r.figure, shot.shot)) {
            ++shots_fired_;
        }
    }
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
        // A player who died is not shown until it stands again (its health back).
        // Nor while the party arrives somewhere (the figures come once all are in).
        const char* hidden = nullptr;

        if (arriving_) {
            hidden = "the party arrives";
        } else if (!in_level(status.phase)) {
            hidden = "this game is out of a level";
        } else if (!r.poses.at(t, pose)) {
            hidden = "no pose";
        } else if (r.poses.silence_ms(t) >= kGoneMs) {
            hidden = "silent";
        } else if (pose.level != status.level) {
            hidden = "in another level";
        } else if (pose.health <= 0) {
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

        fire_shots(r, now, status);

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

void Coop::tick_leaving(std::uint64_t now) {
    const int phase = game_.status().phase;

    if (to_title_) {
        if (in_level(phase)) {
            // In the level (playing, a scene, a menu over it): the game's way back to the title,
            // asked again while it has not gone (a travel still to happen, or refused).
            left_title_ = true;
            const std::uint64_t wait = title_taken_ ? kTravelMs : kLeaveMs;

            if (title_ms_ == 0 || now - title_ms_ >= wait) {
                title_ms_ = now;
                title_taken_ = game_.travel(-1);

                if (title_taken_) {
                    log("left the level for the title");
                }
            }
        } else if (phase == kPhaseLoading) {
            left_title_ = true;
        } else if (phase == kPhaseNone
                   || (phase == kPhaseFrontEnd
                       && (left_title_ || now - title_since_ >= kStartingMs))) {
            // In the title (or the game's start never left it): the Multiplayer page next.
            to_title_ = false;
            home_ = left_title_ && phase == kPhaseFrontEnd;
            home_since_ = now;
            home_ms_ = 0;
        }
    }

    // Back in the title: the Multiplayer page asked for until it shows (the main menu may not be
    // up yet), for a while, and not once the player left the title again.
    if (home_) {
        if (phase != kPhaseFrontEnd || now - home_since_ >= kHomeMs) {
            home_ = false;
        } else if (home_ms_ == 0 || now - home_ms_ >= kHomeRetryMs) {
            home_ms_ = now;
            game_.open_home();
        }
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
