// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The shared world (world.hpp): the gate of a moby's own game logic and the hits.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "world.hpp"
#include "world_util.hpp"

namespace mp {

using namespace wire;

bool World::gate(int slot, int phase) {
    // The level being made again this frame: every slot known is another moby's now.
    if (game_.status().loads != loads_) {
        standing_ = 0;
        return false;
    }

    // A bolt another player took flies to that player (in either game).
    if (phase == kMobyBefore && flights_.count(slot) != 0 && mobys_.count(slot) != 0) {
        stand_flight(slot);
        return false;
    }

    // A member's game, or a host with nobody to stand in for, runs every update as the game has it.
    if (!hosting_ || members_.empty()) {
        return false;
    }

    // After an update: settle what the stand-in did to the host's state.
    if (phase != 0) {
        // The bolts a member's stand-in collected are that member's.
        if (standing_ != 0) {
            after_stand_in(slot, standing_);

            const int delta = game_.bolts(0) - bolts_before_;

            // Bolts the stand-in's update added to the host's count belong to that member, so they
            // come off.
            if (delta > 0) {
                game_.bolts(-delta);
                members_[standing_].bolts += delta;
            }

            standing_ = 0;
        } else {
            after_update(slot);
        }

        return false;
    }

    const auto it = mobys_.find(slot);

    // The hero's own class (figures) and the crates, pickups and interactables (each game's)
    // keep the hero as he is; but a bolt flies to the player nearest it, as an enemy goes for one.
    if (it == mobys_.end() || it->second.oclass == 0
        || (local_class(it->second.oclass) && !bolt_class(it->second.oclass))) {
        return false;
    }

    // A zone trigger counts every player in it: the port asks that of it (rac1/host/world.c).
    if (it->second.oclass == kSpatialTriggerClass) {
        return false;
    }

    Pose at;
    const std::int32_t target = pick_target(slot, it->second, at);

    // Only a member is stood in for (the host is 0); the update then sees his figure as the hero.
    if (target > 0 && stand_for(it->second, target, at)) {
        standing_ = target;
        bolts_before_ = game_.bolts(0);
    }

    return false;
}

std::int32_t World::pick_target(int slot, const MobyState& moby, Pose& at) {
    // A grab holds its victim, however long and however near the others stand.
    if (const auto held = holds_.find(slot); held != holds_.end()) {
        // The host himself is held.
        if (held->second.player == 0) {
            return 0;
        }

        return coop_.latest(held->second.player, at) ? held->second.player : -1;
    }

    const GrabClass* grab = grab_class(moby.oclass);

    // The nearest player to it who can be fought: the host, or a member in this level. One in
    // another enemy's grip is no prey for a second.
    Pose hero;
    // Larger than any squared distance, so the first player in reach wins.
    float best = 1e30f;
    std::int32_t nearest = -1;

    // The host's hero is a candidate while alive and not in another grab.
    if (game_.hero(hero) && hero.health > 0 && (grab == nullptr || !held_elsewhere(0, slot))) {
        best = distance2(hero.position, moby.pose.position);
        nearest = 0;
    }

    const auto known = aims_.find(slot);
    const std::int32_t current = known != aims_.end() ? known->second.player : -1;
    float kept = current == 0 && nearest == 0 ? best : -1.0f;
    Pose kept_at;

    // Each member in this level is a candidate.
    for (const auto& [id, member] : members_) {
        Pose p;

        // Only members who are alive and not in another moby's grab count.
        if (member.level == level_ && coop_.latest(id, p) && p.health > 0
            && (grab == nullptr || !held_elsewhere(id, slot))) {
            const float d2 = distance2(p.position, moby.pose.position);

            // The distance of the current target is kept to weigh it against the nearest.
            if (id == current) {
                kept = d2;
                kept_at = p;
            }

            // The nearest player so far wins.
            if (d2 < best) {
                best = d2;
                nearest = id;
                at = p;
            }
        }
    }

    // Something that flies to whoever is nearest has no target to keep.
    if (nearest < 0 || bolt_class(moby.oclass) || spawned_bolt_class(moby.oclass)) {
        return nearest;
    }

    Aim& aim = aims_[slot];

    // A new state starts the attack's stick time again.
    if (aim.player < 0 || moby.state != aim.state) {
        aim.state = moby.state;
        aim.state_ms = tick_ms_;
    }

    // The target it has, unless it has gone or died: an attack goes to its end, and only a player
    // clearly nearer takes it away (a moby between two players would flip each tick). A leap at a
    // grab's victim and a moby that remembers who it fights are not to be turned at all. A grabber
    // has no stick: its chase of one it can't reach breaks off and starts again, each change of
    // state restarting a stick, and that would keep it from a player beside it for good.
    const bool lunging =
        grab != nullptr && moby.state >= grab->lunge_first && moby.state <= grab->lunge_last;
    const bool sticking = grab == nullptr && tick_ms_ - aim.state_ms < kAttackStickMs;

    // Keep the current target while one of those holds; otherwise the nearest takes it.
    if (kept >= 0.0f
        && (lunging || sticking || keeps_hero_class(moby.oclass)
            || best >= kept * kTargetBand * kTargetBand)) {
        at = kept_at;
        return current;
    }

    aim.player = nearest;
    return nearest;
}

bool World::member_hit_ok(std::int32_t sender, const Hit& h) const {
    // A message that makes no sense is refused.
    if (!hit_makes_sense(h)) {
        return false;
    }

    // Its word on where it is, when there is one: a hit from another level is late, and its slot
    // here is another moby; one from across the level is no shot.
    Pose at;
    const auto moby = mobys_.find(h.victim);

    // With no word of where the sender is, or no such moby here, the hit is taken.
    if (moby == mobys_.end() || !coop_.latest(sender, at)) {
        return true;
    }

    return at.level == level_
           && distance2(at.position, moby->second.pose.position) <= kHitReach * kHitReach;
}

bool World::hit(const Hit& h) {
    // The host takes only hits on a member's figure, which go to that member.
    if (hosting_) {
        // A hit on a member's figure (an enemy that stood the hero in there): that member's.
        for (const auto& [id, member] : members_) {
            const int figure = coop_.figure_of(id);

            // Only the figure of the member whose slot is the victim.
            if (figure < 0 || game_.figure_slot(figure) != h.victim) {
                continue;
            }

            Writer w;
            w.u8(static_cast<std::uint8_t>(WorldKind::kDamage));
            u16(w, h.attacker >= 0 ? id_of(h.attacker) : kNoId);
            w.i32(static_cast<std::int32_t>(h.mask));
            f32(w, h.damage);
            vec(w, h.position);
            vec(w, h.velocity);
            session_.send_data(id, w.bytes());
            ++counts_.damage;

            // The attacker hit that player a moment ago, so its attack stick starts again.
            if (const auto aim = aims_.find(h.attacker); aim != aims_.end()) {
                aim->second.state_ms = tick_ms_;
            }

            return true;
        }

        return false;
    }

    // A member: a hit on the host's world goes to the host (the host's world hits its own).
    if (driven_.count(h.victim) == 0) {
        return false;
    }

    // A hit between two driven mobys is the host's world acting on itself: it is not sent.
    if (driven_.count(h.attacker) != 0) {
        return true;
    }

    const std::uint16_t id = id_of(h.victim);

    // A victim with no name on the wire cannot be told to the host; the hit is still taken.
    if (id != kNoId) {
        Writer w;
        w.u8(static_cast<std::uint8_t>(WorldKind::kHit));
        u16(w, id);
        w.i32(static_cast<std::int32_t>(h.mask));
        f32(w, h.damage);
        vec(w, h.position);
        vec(w, h.velocity);
        session_.send_data(session_.host(), w.bytes());
        ++counts_.hits;
    }

    return true;
}

}  // namespace mp
