/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The shared world (world.hpp): the gate of a moby's own game logic and the hits.
 */
#include "world.hpp"
#include "world_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace mp {

using namespace wire;

bool World::gate(int slot, int phase) {
    // The level being made again this very frame: every slot known is another moby's now.
    if (game_.status().loads != loads_) {
        standing_ = 0;
        return false;
    }

    // A bolt another player took flies to that player (in either game).
    if (phase == kMobyBefore && flights_.count(slot) != 0 && mobys_.count(slot) != 0) {
        stand_flight(slot);
        return false;
    }

    if (!hosting_ || members_.empty()) {
        return false;
    }

    if (phase != 0) {
        // The bolts a member's stand-in collected are that member's.
        if (standing_ != 0) {
            const int delta = game_.bolts(0) - bolts_before_;

            if (delta > 0) {
                game_.bolts(-delta);
                members_[standing_].bolts += delta;
            }

            standing_ = 0;
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

    Pose at;
    const std::int32_t target = pick_target(slot, it->second, at);

    if (target > 0 && game_.stand_in(at.position, coop_.figure_of(target))) {
        standing_ = target;
        bolts_before_ = game_.bolts(0);
    }

    return false;
}

std::int32_t World::pick_target(int slot, const MobyState& moby, Pose& at) {
    // The nearest player to it who can be fought: the host, or a member in this level.
    Pose hero;
    float best = 1e30f;
    std::int32_t nearest = -1;

    if (game_.hero(hero) && hero.health > 0) {
        best = distance2(hero.position, moby.pose.position);
        nearest = 0;
    }

    const auto known = aims_.find(slot);
    const std::int32_t current = known != aims_.end() ? known->second.player : -1;
    float kept = current == 0 && nearest == 0 ? best : -1.0f;
    Pose kept_at;

    for (const auto& [id, member] : members_) {
        Pose p;

        if (member.level == level_ && coop_.latest(id, p) && p.health > 0) {
            const float d2 = distance2(p.position, moby.pose.position);

            if (id == current) {
                kept = d2;
                kept_at = p;
            }

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

    if (aim.player < 0 || moby.state != aim.state) {
        aim.state = moby.state;
        aim.state_ms = tick_ms_;
    }

    // The target it has, unless it has gone or died: an attack goes to its end, and only a player
    // clearly nearer takes it away (a moby between two players would flip each tick).
    if (kept >= 0.0f
        && (tick_ms_ - aim.state_ms < kAttackStickMs || best >= kept * kTargetBand * kTargetBand)) {
        at = kept_at;
        return current;
    }

    aim.player = nearest;
    return nearest;
}

bool World::hit(const Hit& h) {
    if (hosting_) {
        // A hit on a member's figure (an enemy that stood the hero in there): that member's.
        for (const auto& [id, member] : members_) {
            const int figure = coop_.figure_of(id);

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

    if (driven_.count(h.attacker) != 0) {
        return true;
    }

    const std::uint16_t id = id_of(h.victim);

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
