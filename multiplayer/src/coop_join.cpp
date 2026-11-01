/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Playing together (coop.hpp): the PLAY sent, the joining and the rejoin, being put beside the host
 * and starting again.
 *
 * The place (the host's word, and only its word: the member's game finds the spot itself, as a
 * spot means something only against a level as it stands, and asks the port to put the hero
 * there). The host keeps its last safe pose and answers what is asked; each asking has a ticket, so
 * an answer to an earlier one moves nobody. Ways out, for a joiner: its own Start, the host's ship
 * leaving, kPlaceMostMs; all leave it where the ship or the level's start put it, which is its
 * fallback too when no spot exists. For Go to host, the fallback is that landing place.
 */
#include "coop.hpp"
#include "coop_util.hpp"
#include "world_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace mp {

using namespace coop_util;
using wire::f32;

bool Coop::rejoin() {
    if (session_.role().host() || play_ == 0 || pending_) {
        return false;
    }

    // As one that never played it: WANT goes at the next tick.
    applied_ = 0;
    want_ms_ = 0;
    return true;
}

void Coop::rejoined(std::uint64_t now) {
    // A PLAY the drop cut in half is asked for whole.
    if (pending_) {
        pending_ = false;
        save_.clear();
        expected_ = received_ = 0;
        applied_ = 0;
    }

    want_ms_ = 0;
    // The host's next LEVEL says where it is now, whatever it did meanwhile.
    heard_travel_ = false;
    leave_dest_ = -1;
    arriving_ = false;
    host_scene_ = false;
    world_ready_ = false;

    // In the game as it left it: the rest is a late join's (the place, the quiet gifts).
    if (play_ != 0 && applied_ == play_) {
        placing_ = PlaceFor::kNone;
        joining_ = true;
        joining_since_ = now;
    }
}

void Coop::member_back(std::int32_t member) {
    scene_states_.erase(member);
    // It is told where the party is at once.
    level_ms_ = 0;
}

void Coop::send_play(std::int32_t target, const Bytes& save, int level, bool joining) {
    if (announce_) {
        announce_(target);
    }

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

    joining_hold_ = status.phase == kPhasePlaying;

    // Waiting for its place (tick_place ends this).
    if (placing_ == PlaceFor::kJoin) {
        return;
    }

    const bool there = status.phase == kPhasePlaying && status.level == host_level_ && world_ready_;

    if (there && game_.can_place() && !arriving_) {
        begin_place(PlaceFor::kJoin, now);
        return;
    }

    if (there || now - joining_since_ >= kJoiningMs) {
        joining_ = joining_hold_ = false;
        quiet_until_ = now + kQuietMs;
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

namespace {

bool near(const float* a, const float* b) {
    float sum = 0;

    for (int i = 0; i < 3; ++i) {
        sum += (a[i] - b[i]) * (a[i] - b[i]);
    }

    return sum < kPlaceNear * kPlaceNear;
}

}  // namespace

void Coop::tick_again(std::uint64_t now) {
    // A save that would not load is no reason to go; nor to stay in the old game.
    if (!again_ && pending_save_ && !game_.save_usable(save_)) {
        notice_ = kBadSaveMember;
        log("refused the host's save");
        applied_ = pending_play_;
        pending_ = false;
        save_.clear();
        game_.travel(-1);
        return;
    }

    // Out of the old game by the game's own way, asked again while it has not gone.
    if (!again_) {
        again_ = true;
        again_since_ = now;
        again_ms_ = 0;
        notice_ = "The host started again";
        log("the host started again: leaving for the title");
    }

    if (again_ms_ == 0 || now - again_ms_ >= kTravelMs) {
        again_ms_ = now;
        game_.travel(-1);
    }
}

void Coop::clear_game() {
    joining_ = joining_hold_ = false;
    placing_ = PlaceFor::kNone;
    place_moving_ = place_landing_ = place_last_ = false;
    shield_until_ = quiet_until_ = go_again_ms_ = no_spot_until_ = 0;
    has_landing_ = false;
    safe_ms_ = 0;
    host_played_ = false;
    again_ = false;
    wait_ = Wait{};
    votes_.clear();
    voted_ = 0;
    barrier_open_ = false;
    arriving_ = sent_arriving_ = false;
    waiting_for_.clear();
    travel_dest_ = -1;
    heard_travel_ = false;
    host_travel_ = 0;
    leave_dest_ = -1;
    host_arriving_ = false;
    reset_scenes();
}

bool Coop::shielded() const { return joining_ || clock_() < shield_until_; }

bool Coop::can_go() const {
    const GameStatus status = game_.status();
    return session_.in_party() && !session_.role().host() && play_ != 0 && applied_ == play_
           && in_level(status.phase) && status.level == host_level_ && !joining_ && !arriving_
           && placing_ == PlaceFor::kNone && game_.can_place();
}

bool Coop::go_to_host(std::uint64_t now) {
    if (!can_go() || now < go_again_ms_) {
        return false;
    }

    go_again_ms_ = now + kGoAgainMs;
    log("going to the host");
    begin_place(PlaceFor::kHost, now);
    return true;
}

void Coop::begin_place(PlaceFor why, std::uint64_t now) {
    placing_ = why;
    ++place_ticket_;
    place_since_ = place_began_ = now;
    place_ask_ms_ = 0;
    place_last_ = place_moving_ = place_landing_ = false;
}

void Coop::end_place(std::uint64_t now, const std::string& how) {
    const PlaceFor why = placing_;
    placing_ = PlaceFor::kNone;
    place_moving_ = false;
    log("place: " + how);

    if (why == PlaceFor::kJoin) {
        joining_ = joining_hold_ = false;
        quiet_until_ = now + kQuietMs;
        log("joined the host's game: " + std::to_string(now - joining_since_) + " ms");
    }
}

void Coop::place_at(const float* position, float yaw, bool beside, std::uint64_t now) {
    // Each member its own side of the host: behind it, then a quarter of the way around.
    float turn = 0;
    const std::vector<Member>& members = session_.members();

    for (std::size_t i = 0, seen = 0; beside && i < members.size(); ++i) {
        if (members[i].id == session_.id()) {
            turn = static_cast<float>(seen) * 1.5707964f;
        } else if (members[i].id != session_.host()) {
            ++seen;
        }
    }

    if (!game_.place(position, yaw + turn, beside)) {
        no_place(now);
        return;
    }

    std::copy(position, position + 3, place_to_);
    place_moving_ = true;
    place_moved_ms_ = now;
}

void Coop::no_place(std::uint64_t now) {
    // A joiner stands where the ship or the level's start put it, which is no bad place to be.
    if (placing_ == PlaceFor::kJoin) {
        shield_until_ = now + kShieldMs;
        end_place(now, "no spot beside the host: staying where the level put this game");
        return;
    }

    // Go to host: the landing place, once.
    if (has_landing_ && !place_landing_) {
        place_landing_ = true;
        place_at(landing_.position, landing_.rotation[2], false, now);
        return;
    }

    no_spot_until_ = now + kRefusedMs;
    end_place(now, "no spot");
}

void Coop::on_place(bool from_host, Reader& r) {
    const std::int32_t play = r.i32();
    const std::uint8_t high = r.u8();
    const auto ticket = static_cast<std::uint16_t>(high << 8 | r.u8());
    const std::uint8_t answer = r.u8();
    float at[3];

    for (float& v : at) {
        v = f32(r);
    }

    const float yaw = f32(r);

    if (!from_host || !r.ok() || play != play_ || placing_ == PlaceFor::kNone || place_moving_
        || ticket != place_ticket_) {
        return;
    }

    const auto now = clock_();

    if (answer == kPlaceNone) {
        no_place(now);
    } else if (answer == kPlaceHere || answer == kPlaceLast) {
        log(answer == kPlaceHere ? "place: beside the host" : "place: at the host's last safe spot");
        place_at(at, yaw, true, now);
    }
}

void Coop::tick_place(std::uint64_t now, const GameStatus& status) {
    Pose hero;

    // Where this level load put the hero: the first of it in control.
    if (status.phase == kPhasePlaying && (!has_landing_ || status.loads != landing_loads_)
        && game_.hero(hero)) {
        landing_ = hero;
        has_landing_ = true;
        landing_loads_ = status.loads;
    }

    if (placing_ == PlaceFor::kNone) {
        return;
    }

    // Out of the level, or the host out of it: over.
    if (!in_level(status.phase) || status.level != host_level_ || arriving_) {
        end_place(now, "the level changed");
        return;
    }

    if (place_moving_) {
        // The port moves him once the player is in control: the time counts from there.
        if (status.phase != kPhasePlaying) {
            place_moved_ms_ = now;
        } else if (game_.hero(hero) && near(hero.position, place_to_)) {
            shield_until_ = now + kShieldMs;
            end_place(now, "there");
        } else if (now - place_moved_ms_ >= kPlaceMoveMs) {
            // The port found no spot there.
            place_moving_ = false;
            no_place(now);
        }

        return;
    }

    if (now - place_began_ >= kPlaceMostMs) {
        no_place(now);
        return;
    }

    // A host's scene is waited out, and takes nothing from the wait for a safe spot.
    if (host_scene_) {
        place_since_ = now;
    }

    if (place_ask_ms_ != 0 && now - place_ask_ms_ < kPlaceAskMs) {
        return;
    }

    place_ask_ms_ = now;
    place_last_ = now - place_since_ >= kPlaceWaitMs;
    session_.send_data(
        session_.host(),
        Writer()
            .u8(static_cast<std::uint8_t>(GameKind::kPlaceAsk))
            .i32(play_)
            .u8(static_cast<std::uint8_t>(place_ticket_ >> 8))
            .u8(static_cast<std::uint8_t>(place_ticket_))
            .u8(place_last_ ? 1 : 0)
            .bytes()
    );
}

void Coop::tick_safe(std::uint64_t now, const GameStatus& status) {
    if (status.loads != safe_loads_) {
        safe_loads_ = status.loads;
        safe_ms_ = 0;
    }

    Pose hero;

    if (status.phase == kPhasePlaying && game_.hero(hero) && (hero.flags & kPoseSteady) != 0) {
        safe_ = hero;
        safe_ms_ = now;
    } else if (status.phase == kPhaseMenu && safe_ms_ != 0 && now - safe_ms_ < kSafeFreshMs) {
        // A menu moves nobody.
        safe_ms_ = now;
    }
}

void Coop::on_place_ask(std::int32_t sender, Reader& r) {
    const std::int32_t play = r.i32();
    const std::uint8_t high = r.u8();
    const auto ticket = static_cast<std::uint16_t>(high << 8 | r.u8());
    const bool last = r.u8() != 0;

    if (!session_.role().host() || !r.ok() || play == 0 || play != play_ || !member_of(session_, sender)
        || !may_play(sender)) {
        return;
    }

    const GameStatus status = game_.status();
    const auto now = clock_();
    const bool can = in_level(status.phase) && status.phase != kPhaseScene && !arriving_;
    const bool known = safe_ms_ != 0 && status.loads == safe_loads_;
    std::uint8_t answer = kPlaceWait;

    if (can && known && now - safe_ms_ < kSafeFreshMs) {
        answer = kPlaceHere;
    } else if (can && last) {
        answer = known ? kPlaceLast : kPlaceNone;
    }

    Writer w;
    w.u8(static_cast<std::uint8_t>(GameKind::kPlace))
        .i32(play_)
        .u8(static_cast<std::uint8_t>(ticket >> 8))
        .u8(static_cast<std::uint8_t>(ticket))
        .u8(answer);

    for (const float v : safe_.position) {
        f32(w, v);
    }

    f32(w, safe_.rotation[2]);
    session_.send_data(sender, w.bytes());
}

}  // namespace mp
