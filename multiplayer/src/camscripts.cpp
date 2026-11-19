// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Camera scripts shown to the players near (camscripts.hpp).

#include "camscripts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "world_util.hpp"

namespace mp {

using namespace wire;

namespace {

// The sticks' dead zone, as the game reads them (camera_latch.c in the port), about the middle.
constexpr int kStickMiddle = 0x7F;
// How far a stick may be from the middle before it counts as moved.
constexpr int kStickDead = 0x30;

// Pi, to turn an angle the short way round.
constexpr float kPi = 3.14159265f;

// Returns the distance between the points `a` and `b`, each three floats.
float distance(const float* a, const float* b) {
    const float x = a[0] - b[0];
    const float y = a[1] - b[1];
    const float z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}

// An angle on the way from `a` to `b`, the short way round.
float angle_between(float a, float b, float t) {
    float step = std::fmod(b - a, 2.0f * kPi);

    // Past half a turn, the short way is round the other side.
    if (step > kPi) {
        step -= 2.0f * kPi;
    } else if (step < -kPi) {
        // The same the other way.
        step += 2.0f * kPi;
    }

    return a + step * t;
}

}  // namespace

CamShare camera_share(int oclass) {
    switch (oclass) {
        // The lifts: their shot goes with the ride.
        case 21:
        case 244:
        case 877:
        case 1270:
            return CamShare::kCamRiders;
        // The level showing something happen: the camera triggers and their controllers.
        case 654:
        case 701:
        case 737:
        case 1028:
        case 1083:
        case 1108:
        case 1118:
        case 1156:
        case 1157:
            return CamShare::kCamNear;
        // Any other class is the player's own.
        default:
            return CamShare::kCamOwn;
    }
}

bool pad_active(const std::uint8_t* report, int size) {
    // A report too short to hold two button bytes and four stick bytes has nothing pressed.
    if (size < 6) {
        return false;
    }

    // A button byte other than 0xFF has a button down (seen in game code: the port's pad report).
    if (report[0] != 0xFF || report[1] != 0xFF) {
        return true;
    }

    // Bytes 2 to 5 are the two sticks; one off the middle by the dead zone counts as moved.
    for (int i = 2; i < 6; ++i) {
        if (std::abs(static_cast<int>(report[i]) - kStickMiddle) >= kStickDead) {
            return true;
        }
    }

    return false;
}

Bytes encode_camscript(const CamScript& script) {
    Writer w;
    w.u8(kCamScriptKind);
    w.i32(script.play);
    w.i32(script.level);
    u16(w, script.sequence);
    u16(w, static_cast<std::uint16_t>(script.id));
    w.u8(script.started ? 1 : 0);

    // Where the moby is and the first view go only with a start.
    if (script.started) {
        vec(w, script.where);
        vec(w, script.eye);
        vec(w, script.rotation);
    }

    return w.bytes();
}

bool decode_camscript(const Bytes& bytes, CamScript& out) {
    Reader r(bytes.data(), bytes.size());

    // A message of another kind is not a CAMSCRIPT.
    if (r.u8() != kCamScriptKind) {
        return false;
    }

    out.play = r.i32();
    out.level = r.i32();
    out.sequence = u16(r);
    out.id = static_cast<std::int16_t>(u16(r));
    out.started = r.u8() != 0;

    // A start carries the moby's place and the first view.
    if (out.started) {
        vec(r, out.where);
        vec(r, out.eye);
        vec(r, out.rotation);
    }

    return r.ok() && r.left() == 0;
}

Bytes encode_camstate(const CamState& state) {
    Writer w;
    w.u8(kCamStateKind);
    u16(w, state.sequence);
    w.i64(state.stamp);
    vec(w, state.eye);
    vec(w, state.rotation);
    return w.bytes();
}

bool decode_camstate(const Bytes& bytes, CamState& out) {
    Reader r(bytes.data(), bytes.size());

    // A message of another kind is not a CAMSTATE.
    if (r.u8() != kCamStateKind) {
        return false;
    }

    out.sequence = u16(r);
    out.stamp = r.i64();
    vec(r, out.eye);
    vec(r, out.rotation);
    return r.ok() && r.left() == 0;
}

CamScripts::CamScripts(
    Session& session, Game& game, Coop& coop, std::function<std::uint64_t()> clock
)
    : session_(session),
      game_(game),
      coop_(coop),
      clock_(clock ? std::move(clock) : now_ms) {}

void CamScripts::reset() {
    // A script on show gives the camera back to the hero.
    if (showing_) {
        take_back(true, "the party is gone");
    }

    telling_ = false;
    telling_own_ = false;
    heard_ = false;
    views_.clear();
    health_ = -1;
    input_ms_ = hurt_ms_ = 0;
}

void CamScripts::input(std::uint64_t now) {
    input_ms_ = now;
}

void CamScripts::script(int id, bool started) {
    // Only a host in a party, with a game on, tells of scripts.
    if (!session_.in_party() || !session_.is_host() || !coop_.playing()) {
        return;
    }

    const auto now = clock_();
    const GameStatus status = game_.status();
    CamScript word;
    word.play = coop_.play();
    word.level = status.level;
    word.id = id;
    word.started = started;

    // The end of a script.
    if (!started) {
        // Only the end of the script being told is sent.
        if (telling_ && id == telling_id_) {
            word.sequence = sequence_;
            session_.send_data(kBroadcast, encode_camscript(word));
            telling_ = false;

            // A member's own script is shown to this game's player, so its end reaches this game
            // too.
            if (telling_own_) {
                heard_end(now);
            }

            telling_own_ = false;
        }

        return;
    }

    // The player's own scripts (seats, rides, scenes) are never shared.
    if (camera_share(id) == CamShare::kCamOwn) {
        return;
    }

    CameraView view;

    // The game has no camera view to send.
    if (!game_.camera_view(view)) {
        return;
    }

    // A sequence number of 0 is never used, so a wrap goes on to 1.
    if (++sequence_ == 0) {
        ++sequence_;
    }

    word.sequence = sequence_;
    std::copy_n(view.where, 3, word.where);
    std::copy_n(view.eye, 3, word.eye);
    std::copy_n(view.rotation, 3, word.rotation);
    session_.send_data(kBroadcast, encode_camscript(word));
    ++sent_;
    telling_ = true;
    telling_id_ = id;
    telling_own_ = view.stand_in;
    view_ms_ = now;
    log("camera script of class " + std::to_string(id) + (view.stand_in ? ", a member's," : "")
        + " told to the party");

    // A member's: this game's player is one of those it may be shown to.
    if (telling_own_) {
        heard_start(word, now);
    }
}

void CamScripts::send_view(std::uint64_t now) {
    // A view goes out only while a script is told, every kCamStateMs.
    if (!telling_ || now - view_ms_ < kCamStateMs) {
        return;
    }

    CameraView view;

    // The game has no camera view to send.
    if (!game_.camera_view(view)) {
        return;
    }

    view_ms_ = now;
    CamState state;
    state.sequence = sequence_;
    state.stamp = session_.server_ms();
    std::copy_n(view.eye, 3, state.eye);
    std::copy_n(view.rotation, 3, state.rotation);
    session_.send_state(kBroadcast, encode_camstate(state));

    // A member's script is shown to this game's player from the same views.
    if (telling_own_) {
        View kept;
        kept.stamp = state.stamp;
        std::copy_n(view.eye, 3, kept.eye);
        std::copy_n(view.rotation, 3, kept.rotation);
        heard_view(kept, now);
    }
}

void CamScripts::handle(std::int32_t sender, const Bytes& bytes) {
    // Only a member listens, and only to its host.
    if (bytes.empty() || !session_.in_party() || session_.is_host() || sender != session_.host()) {
        return;
    }

    const auto now = clock_();

    // A CAMSCRIPT starts or ends a script.
    if (bytes[0] == kCamScriptKind) {
        CamScript word;

        // A message that does not decode, or is of another game, is dropped.
        if (!decode_camscript(bytes, word) || word.play == 0 || word.play != coop_.play()) {
            return;
        }

        // A start shows the script; an end ends the one heard.
        if (word.started) {
            heard_start(word, now);
        } else if (heard_ && word.sequence == heard_sequence_) {
            heard_end(now);
        }

        return;
    }

    CamState state;

    // A CAMSTATE counts only for the script heard.
    if (bytes[0] == kCamStateKind && decode_camstate(bytes, state) && heard_
        && state.sequence == heard_sequence_) {
        View view;
        view.stamp = state.stamp;
        std::copy_n(state.eye, 3, view.eye);
        std::copy_n(state.rotation, 3, view.rotation);
        heard_view(view, now);
    }
}

bool CamScripts::may_start(const CamScript& script, std::uint64_t now) {
    const GameStatus status = game_.status();
    Pose hero;
    const CamShare share = camera_share(script.id);

    // A script starts only for a player in control, in its level, with the host's world and in no
    // scene.
    if (share == CamShare::kCamOwn || status.phase != kPhasePlaying || status.scene >= 0
        || status.level != script.level || coop_.joining() || coop_.hold_input()
        || !game_.hero(hero) || hero.health <= 0) {
        return false;
    }

    const float reach = share == CamShare::kCamRiders ? kCamRiderReach : kCamNear;

    // Out of reach of the script's moby.
    if (distance(hero.position, script.where) > reach) {
        return false;
    }

    return (input_ms_ == 0 || now - input_ms_ >= kCamQuietMs)
           && (hurt_ms_ == 0 || now - hurt_ms_ >= kCamHurtMs);
}

void CamScripts::heard_start(const CamScript& script, std::uint64_t now) {
    // A script on show gives way to the new one.
    if (showing_) {
        take_back(true, "another script starts");
    }

    heard_ = true;
    heard_sequence_ = script.sequence;
    heard_play_ = script.play;
    heard_level_ = script.level;
    heard_id_ = script.id;
    heard_ms_ = now;
    views_.clear();
    View first;
    first.stamp = session_.server_ms();
    std::copy_n(script.eye, 3, first.eye);
    std::copy_n(script.rotation, 3, first.rotation);
    views_.push_back(first);

    // It is shown only when the player may see it and the game takes it.
    if (may_start(script, now) && game_.camera_play(script.id)) {
        showing_ = true;
        show_ms_ = now;
        ++shown_;
        log("showing the camera script of class " + std::to_string(script.id));
    }
}

void CamScripts::heard_view(const View& view, std::uint64_t now) {
    heard_ms_ = now;

    // A view not newer than the last arrived out of order.
    if (!views_.empty() && view.stamp <= views_.back().stamp) {
        return;
    }

    views_.push_back(view);

    // Only the latest 16 views are kept.
    while (views_.size() > 16) {
        views_.pop_front();
    }
}

void CamScripts::heard_end(std::uint64_t /*now*/) {
    heard_ = false;

    // Only a script that was shown gives the camera back.
    if (showing_) {
        take_back(false, "the script is over");
    }

    views_.clear();
}

CamScripts::View CamScripts::view_at(std::int64_t when) const {
    // With no views there is nothing to show.
    if (views_.empty()) {
        return {};
    }

    // Before the first view, the first is shown.
    if (when <= views_.front().stamp) {
        return views_.front();
    }

    // Find the two views the time lies between.
    for (std::size_t i = 1; i < views_.size(); ++i) {
        const View& b = views_[i];

        // Views before `when` are passed.
        if (when > b.stamp) {
            continue;
        }

        const View& a = views_[i - 1];

        // A cut in the script stays a cut.
        if (distance(a.eye, b.eye) > kCamCut) {
            return a;
        }

        const float t = static_cast<float>(when - a.stamp) / static_cast<float>(b.stamp - a.stamp);
        View out;
        out.stamp = when;

        // Eye and rotation blend on each of the three axes.
        for (int c = 0; c < 3; ++c) {
            out.eye[c] = a.eye[c] + (b.eye[c] - a.eye[c]) * t;
            out.rotation[c] = angle_between(a.rotation[c], b.rotation[c], t);
        }

        return out;
    }

    return views_.back();
}

void CamScripts::show(std::uint64_t now) {
    const GameStatus status = game_.status();

    // Anything that takes the player elsewhere or into a scene ends the script.
    if (status.phase != kPhasePlaying || status.scene >= 0 || status.level != heard_level_
        || coop_.play() != heard_play_ || coop_.hold_input()) {
        take_back(true, "the player is elsewhere");
    } else if (input_ms_ > show_ms_) {
        take_back(true, "the player moved");
    } else if (hurt_ms_ > show_ms_) {
        take_back(true, "the player was hurt");
    } else if (now - heard_ms_ >= kCamStaleMs) {
        take_back(true, "nothing heard of it");
        heard_ = false;
    } else {
        // Otherwise the view of kCamDelayMs ago is played.
        const View view = view_at(session_.server_ms() - static_cast<std::int64_t>(kCamDelayMs));

        // The game took the camera away, so the script is no longer shown.
        if (!game_.camera_show(view.eye, view.rotation)) {
            showing_ = false;
            log("the game took the camera from the script");
        }
    }
}

void CamScripts::take_back(bool cut, const std::string& why) {
    showing_ = false;
    game_.camera_play(cut ? kCameraCut : kCameraEnd);
    log("camera back with the hero: " + why);
}

void CamScripts::tick(std::uint64_t now) {
    // Outside a party or a game nothing is kept.
    if (!session_.in_party() || !coop_.playing()) {
        // Only when something was kept.
        if (telling_ || heard_ || showing_) {
            reset();
        }

        return;
    }

    // The hero hurt: a health less than the last.
    Pose hero;

    // A hero that is not there has no health to watch.
    if (game_.hero(hero)) {
        // A fall in health is a hit; the first reading only sets the start.
        if (health_ >= 0 && hero.health < health_) {
            hurt_ms_ = now;
        }

        health_ = hero.health;
    }

    // The host sends the views.
    if (session_.is_host()) {
        send_view(now);
    }

    // A script on show is checked and played each tick.
    if (showing_) {
        show(now);
    }

    // Nothing shown here and nothing heard for a while: what was kept is no use.
    if (heard_ && !showing_ && now - heard_ms_ >= kCamStaleMs) {
        heard_ = false;
        views_.clear();
    }
}

}  // namespace mp
