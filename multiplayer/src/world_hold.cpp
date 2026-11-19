// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The shared world (world.hpp): an enemy's grab, on the host and in the grabbed player's game.
//
// The enemy runs in the host's game, with its victim standing in for the hero. What its update does
// to that hero stays off the host's (the SDK's hero_stand_in_read says what it was), so the host
// knows the grab began when the update puts the hero who stood in into the grab's state
// (grab_class). From then on the moby and the player are locked to each other: pick_target gives it
// no other target, however long the grab lasts; no second grab takes the player; the update goes on
// seeing the hero in the grab's state, not what a pose says he is doing.
//
// The victim's game plays it (HOLD): its own hero in the same state, with his controls gone, going
// where the host says the update holds him, a word at a time; RELEASE ends it with where he ends
// up. A HOLD lost changes nothing, the next says the same, and the first one starts it. The host
// checks what it asked: a player whose pose does not show the state in kHoldAckMs (he was in a
// scene, or on a ride) is let go. His game does not rely on the host either: no word for
// kHoldLeaseMs, and it lets him go by itself, as for a host who dropped.
//
// Every end is explicit: the update lets go, the moby is deleted, the player dies or leaves the
// level or the party, a level is made again. A fatal grab ends when he is back from the dead.
//
// The host's own hero in a grab has no message to go by: the game does it all. The lock is all the
// host needs, read from his pose after the update.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "world.hpp"
#include "world_util.hpp"

namespace mp {

using namespace wire;

bool World::stand_for(const MobyState& moby, std::int32_t target, const Pose& at) {
    const int figure = coop_.figure_of(target);
    const GrabClass* grab = grab_class(moby.oclass);

    // A class that does not grab, or a game that cannot hold, stands the player in as a plain
    // target.
    if (grab == nullptr || !game_.can_hold()) {
        Pose hero;
        stood_state_ = game_.hero(hero) ? hero.state : 0;
        stood_health_ = hero.health;
        return game_.stand_in(at.position, figure);
    }

    // What the update is to see of him: its own grab while there is one, else his pose.
    const auto held = holds_.find(moby.slot);
    StandIn as;
    std::copy(at.position, at.position + 3, as.position);
    as.state = held != holds_.end() ? held->second.hero_state : at.state;
    as.health = held != holds_.end() && (held->second.flags & kGrabFatal) != 0 ? 0 : at.health;
    as.flags = (at.flags & kPoseAirborne) != 0 ? kStandInAirborne : 0;
    stood_state_ = as.state;
    stood_health_ = as.health;
    return game_.stand_in_as(as, figure);
}

void World::after_stand_in(int slot, std::int32_t player) {
    StandIn did;
    const auto it = mobys_.find(slot);

    // Without the moby or the stand-in's read-back there is nothing to compare.
    if (it == mobys_.end() || !game_.stand_in_read(did)) {
        return;
    }

    const GrabClass* grab = grab_class(it->second.oclass);
    const auto held = holds_.find(slot);

    // A grab already on follows where the update holds him.
    if (held != holds_.end()) {
        Hold& hold = held->second;
        const bool fatal = (hold.flags & kGrabFatal) != 0;

        // A fatal grab carries him in its mouth; a hold says where it pulls him.
        std::copy_n(fatal ? it->second.pose.position : did.position, 3, hold.position);
        hold.yaw = did.yaw;

        // A hold ends when the update puts the hero in another state; a fatal grab ends otherwise.
        if (!fatal && did.state != hold.hero_state) {
            release_hold(held);
        }

        return;
    }

    // The update put the stand-in into the grab's state, and he was not in it before: the grab
    // begins.
    if (grab != nullptr && did.state == grab->hero_state && stood_state_ != grab->hero_state) {
        Hold hold;
        hold.player = player;
        hold.id = id_of(slot);
        hold.hero_state = grab->hero_state;
        hold.animation = grab->animation;
        hold.flags = grab->flags;
        std::copy_n(
            (grab->flags & kGrabFatal) != 0 ? it->second.pose.position : did.position,
            3,
            hold.position
        );
        hold.yaw = did.yaw;
        hold.since = tick_ms_;
        Hold& held_now = holds_[slot] = hold;
        tell_hold(held_now, tick_ms_);
        return;
    }

    // Anything else that ended the hero who stood in (a kill volume, a fatal fall's scene): he dies
    // in his own game, by the only way it has, and not in this one.
    const bool ended = (did.flags & kStandInKilled) != 0 || (did.health <= 0 && stood_health_ > 0);
    const auto last = kills_.find(player);

    // The word that he died goes at most once each kKillAgainMs; REVIVE 1 sends him to his
    // checkpoint.
    if (ended && (last == kills_.end() || tick_ms_ - last->second >= kKillAgainMs)) {
        kills_[player] = tick_ms_;
        session_.send_data(
            player, Writer().u8(static_cast<std::uint8_t>(WorldKind::kRevive)).u8(1).bytes()
        );
    }
}

void World::after_update(int slot) {
    const auto it = mobys_.find(slot);

    // A moby that is gone has no grab to read.
    if (it == mobys_.end()) {
        return;
    }

    const GrabClass* grab = grab_class(it->second.oclass);
    const auto aim = aims_.find(slot);

    // Only a grab class aimed at the host's own hero is read from his pose.
    if (grab == nullptr || aim == aims_.end() || aim->second.player != 0) {
        return;
    }

    Pose hero;

    // Without a hero pose there is nothing to read.
    if (!game_.hero(hero)) {
        return;
    }

    const auto held = holds_.find(slot);

    // The host's hero in the grab's state locks the grab; out of it, the lock ends.
    if (held == holds_.end() && hero.state == grab->hero_state) {
        Hold hold;
        hold.hero_state = grab->hero_state;
        hold.since = tick_ms_;
        holds_[slot] = hold;
    } else if (held != holds_.end() && held->second.player == 0 && hero.state != grab->hero_state) {
        holds_.erase(held);
    }
}

bool World::held_elsewhere(std::int32_t player, int slot) const {
    return std::any_of(holds_.begin(), holds_.end(), [&](const auto& hold) {
        return hold.first != slot && hold.second.player == player;
    });
}

void World::tell_hold(Hold& hold, std::uint64_t now) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kHold));
    u16(w, hold.id);
    w.u8(static_cast<std::uint8_t>(hold.flags));
    w.u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(hold.animation)));
    vec(w, hold.position);
    f32(w, hold.yaw);

    // The first says it for sure; after it, only the place, which the next says better.
    if (hold.told == 0) {
        session_.send_data(hold.player, w.bytes());
        ++counts_.holds;
        trace_line(
            "hold", "player " + std::to_string(hold.player) + " moby " + std::to_string(hold.id)
        );
    } else {
        session_.send_state(hold.player, w.bytes());
    }

    hold.told = now;
}

void World::release_hold(std::map<int, Hold>::iterator it) {
    const Hold& hold = it->second;

    // The host's own hero has no one to tell, and nothing goes outside a party.
    if (hold.player != 0 && session_.in_party()) {
        Writer w;
        w.u8(static_cast<std::uint8_t>(WorldKind::kRelease));
        u16(w, hold.id);
        vec(w, hold.position);
        f32(w, hold.yaw);
        session_.send_data(hold.player, w.bytes());
        ++counts_.releases;
        trace_line("hold", "released player " + std::to_string(hold.player));
    }

    holds_.erase(it);
}

void World::release_holds() {
    // Every hold ends and tells its player.
    while (!holds_.empty()) {
        release_hold(holds_.begin());
    }

    // A member's own hero held by the host is let go too.
    if (held_) {
        held_ = false;
        game_.let_go();
    }
}

void World::tick_holds(std::uint64_t now) {
    // Each hold is checked against what its player's pose shows.
    for (auto it = holds_.begin(); it != holds_.end();) {
        Hold& hold = it->second;
        const auto next = std::next(it);

        // The moby deleted ends any grab.
        if (mobys_.count(it->first) == 0) {
            release_hold(it);
            it = next;
            continue;
        }

        // The host's: read after the update.
        if (hold.player == 0) {
            it = next;
            continue;
        }

        const auto member = members_.find(hold.player);
        Pose p;
        const bool here = member != members_.end() && member->second.level == level_
                          && coop_.latest(hold.player, p);
        const bool fatal = (hold.flags & kGrabFatal) != 0;

        // His pose shows the grab's state: the grab has taken.
        if (here && p.state == hold.hero_state) {
            hold.shown = true;
        }

        // A fatal grab needs to know he died.
        if (here && p.health <= 0) {
            hold.dead = true;
        }

        const bool over =
            !here
            || (fatal ? (hold.dead && p.health > 0) || now - hold.since >= kFatalHoldMs
                      : p.health <= 0 || (hold.shown && p.state != hold.hero_state)
                            || (!hold.shown && now - hold.since >= kHoldAckMs));

        // An ended grab goes; a live one repeats where the victim is held, each kHoldSendMs.
        if (over) {
            release_hold(it);
        } else if (now - hold.told >= kHoldSendMs) {
            tell_hold(hold, now);
        }

        it = next;
    }
}

void World::hold_heard(Reader& r) {
    r.u8();
    r.u8();
    const std::uint32_t flags = r.u8();
    const int animation = static_cast<std::int8_t>(r.u8());
    float position[3];
    vec(r, position);
    const float yaw = f32(r);

    // A message cut short, or a game that cannot hold, is ignored.
    if (!r.ok() || !game_.can_hold()) {
        return;
    }

    // The first HOLD starts the grab in this game.
    if (!held_) {
        held_ = game_.grab(animation, flags);
    }

    // Each HOLD renews the lease and moves him to where the host holds him.
    if (held_) {
        held_ms_ = clock_();
        game_.hold_at(position, yaw);
    }
}

void World::release_heard(Reader& r) {
    r.u8();
    r.u8();
    float position[3];
    vec(r, position);
    const float yaw = f32(r);

    // RELEASE puts him where the host says and lets go, if this game held him.
    if (r.ok() && held_) {
        game_.hold_at(position, yaw);
        game_.let_go();
        held_ = false;
    }
}

void World::tick_held(std::uint64_t now) {
    // With no word for kHoldLeaseMs the game lets go by itself.
    if (held_ && now - held_ms_ >= kHoldLeaseMs) {
        held_ = false;
        game_.let_go();
    }
}

}  // namespace mp
