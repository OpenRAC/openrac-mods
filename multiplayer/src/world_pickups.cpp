/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The shared world (world.hpp): pickups taken, claimed and flown to their player; crates broken again.
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

bool World::quiet_drop(int slot) {
    // A member in the host's level: the host's game drops the bolts (its copy of the crate breaks
    // too), so this game's would be the same bolts twice.
    const bool quiet = session_.in_party() && !session_.is_host() && coop_.playing() && level_ >= 0
                       && level_ == coop_.host_level();

    if (quiet) {
        trace_line("bolts", "suppressed local drop of " + moby_name(slot) + " (the host's game drops it)");
    }

    return quiet;
}

bool World::collect(int slot) {
    const auto moby = mobys_.find(slot);
    const int oclass = moby != mobys_.end() ? moby->second.oclass : 0;

    // A bolt another player took, arrived at that player (or at this game's Ratchet, for one taken
    // for him there): given already, nothing added and nothing claimed; the bolt update deletes it.
    if (const auto f = flights_.find(slot); f != flights_.end()) {
        trace_line("bolts", moby_name(slot) + " id " + std::to_string(f->second.id) + " reaches player "
                                + std::to_string(f->second.player) + (f->second.figure >= 0 ? "'s figure" : " here")
                                + " (taken there: nothing added here)");
        flights_.erase(f);

        for (auto it = net_slot_.begin(); it != net_slot_.end(); ++it) {
            if (it->second == slot) {
                loose_.erase(it->first);
                net_slot_.erase(it);
                break;
            }
        }

        driven_.erase(slot);
        return false;
    }

    // The host: its own Ratchet's, or the member's whose figure stood in for him (the bolt flew to
    // that figure: the nearest player), the bolt gone from every game (DESPAWN or KILLED at the
    // next tick); nobody's claim takes it after this.
    if (session_.is_host()) {
        const std::uint16_t id = hosting_ ? id_of(slot) : kNoId;

        if (id != kNoId && standing_ != 0) {
            claimed_.insert(id);
            send_taken(id, standing_);
            members_[standing_].bolts += bolt_value(oclass);
            trace_line("bolts", "player " + std::to_string(standing_) + "'s figure collects " + moby_name(slot) + " id "
                                    + std::to_string(id) + " +" + std::to_string(bolt_value(oclass))
                                    + " (theirs: BOLTS; nothing added here)");
            return false;
        }

        if (id != kNoId) {
            claimed_.insert(id);
            send_taken(id, session_.id());
            trace_line("bolts", "host collects " + moby_name(slot) + " id " + std::to_string(id) + " +"
                                    + std::to_string(bolt_value(oclass)) + " (its own credit)");
        }

        return true;
    }

    if (!session_.in_party() || !coop_.playing() || level_ < 0 || level_ != coop_.host_level()) {
        return true;
    }

    // The host's bolt made here, or a placed bolt (the same in every game): the host decides.
    std::uint16_t id = kNoId;

    for (auto it = net_slot_.begin(); it != net_slot_.end(); ++it) {
        if (it->second == slot) {
            id = static_cast<std::uint16_t>(kSpawnedId | it->first);
            loose_.erase(it->first);
            poses_.erase(id);
            hidden_.erase(id);
            driven_.erase(slot);
            net_slot_.erase(it);
            break;
        }
    }

    if (id == kNoId && moby != mobys_.end() && moby->second.key >= 0 && bolt_class(oclass)) {
        id = static_cast<std::uint16_t>(moby->second.key);
    }

    // One of this game's own (none the host knows of): its own.
    if (id == kNoId) {
        trace_line("bolts", "member collects its own " + moby_name(slot) + " (not the host's)");
        return true;
    }

    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kClaim)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
    u16(w, id);
    session_.send_data(session_.host(), w.bytes());
    trace_line("bolts", "claim sent for " + moby_name(slot) + " id " + std::to_string(id)
                            + ": suppressed local add of " + std::to_string(bolt_value(oclass)));
    return false;
}

void World::claim(std::int32_t member, int level, std::uint16_t id) {
    int slot = -1;

    if (hosting_ && level == level_ && (id & kSpawnedId) != 0) {
        slot = slot_of(id);
    } else if (hosting_ && level == level_ && alive_keys_.count(id) != 0) {
        slot = game_.moby_slot(id);
    }

    const auto moby = mobys_.find(slot);
    const int value = moby != mobys_.end() ? bolt_value(moby->second.oclass) : 0;

    // The first claim of a bolt still here, that nobody took: the claimant's, gone from every game.
    if (slot < 0 || value <= 0 || claimed_.count(id) != 0) {
        trace_line("bolts", "claim refused from " + std::to_string(member) + " id " + std::to_string(id) + " level "
                                + std::to_string(level) + (claimed_.count(id) != 0 ? " (already taken)" : " (not here)"));
        return;
    }

    claimed_.insert(id);
    send_taken(id, member);

    // Here it flies to the claimant's figure as the game's pickup does, then goes (DESPAWN or
    // KILLED for everyone once its update deletes it); with no figure to fly to, at once.
    if (!fly(slot, id, member, clock_())) {
        note(slot, "CLAIM, a bolt taken there", member);
        kill(slot, "CLAIM from " + std::to_string(member));
    }

    session_.send_data(member, Writer().u8(static_cast<std::uint8_t>(WorldKind::kBolts)).i32(value).bytes());
    counts_.bolts += value;
    trace_line("bolts", "claim granted to " + std::to_string(member) + " id " + std::to_string(id) + " "
                            + moby_name(slot) + ": sent BOLTS +" + std::to_string(value));
}

void World::send_taken(std::uint16_t id, std::int32_t player) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kTaken)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
    u16(w, id);
    w.i32(player);
    session_.send_data(kBroadcast, w.bytes());
    trace_line("msg", "sent TAKEN to all id " + std::to_string(id) + " by " + std::to_string(player));
}

bool World::fly(int slot, std::uint16_t id, std::int32_t player, std::uint64_t now) {
    const auto moby = mobys_.find(slot);

    if (moby == mobys_.end() || !bolt_class(moby->second.oclass)) {
        return false;
    }

    const bool here = player == session_.id();
    const int figure = here ? -1 : coop_.figure_of(player);

    if (!here && figure < 0) {
        return false;
    }

    flights_[slot] = {player, figure, id, now + kFlightMs};
    trace_line("bolts", moby_name(slot) + " id " + std::to_string(id) + " flies to player " + std::to_string(player)
                            + (here ? " (this game's Ratchet)" : "'s figure"));
    return true;
}

void World::stand_flight(int slot) {
    const Flight& f = flights_.at(slot);
    const MobyState& bolt = mobys_.at(slot);
    float to[3] = {0, 0, 0};
    bool found = false;

    // Where the player is shown here: the figure (or this game's Ratchet).
    if (f.figure >= 0) {
        const auto shown = mobys_.find(game_.figure_slot(f.figure));
        Pose p;

        if (shown != mobys_.end()) {
            std::copy(shown->second.pose.position, shown->second.pose.position + 3, to);
            found = true;
        } else if (coop_.latest(f.player, p)) {
            std::copy(p.position, p.position + 3, to);
            found = true;
        }
    } else {
        Pose p;

        if (game_.hero(p)) {
            std::copy(p.position, p.position + 3, to);
            found = true;
        }
    }

    if (!found) {
        return;
    }

    // Not yet set off (it hovers, lands or falls): the player stands within its reach, on the way
    // to the player and at its height, so its own update sets it off toward them; then where they
    // are, which it flies to.
    if (bolt.state != kBoltFlying) {
        const float dx = to[0] - bolt.pose.position[0];
        const float dy = to[1] - bolt.pose.position[1];
        const float d = std::sqrt(dx * dx + dy * dy);
        const float k = d > kBoltReach ? kBoltReach / d : 1.0f;
        to[0] = bolt.pose.position[0] + dx * k;
        to[1] = bolt.pose.position[1] + dy * k;
        to[2] = bolt.pose.position[2];
    }

    game_.stand_in(to, f.figure);
}

void World::add_flight_eyes(std::vector<float>& eyes) const {
    for (const auto& [slot, flight] : flights_) {
        const auto bolt = mobys_.find(slot);

        if (bolt != mobys_.end() && eyes.size() < 3 * 8) {
            eyes.insert(eyes.end(), bolt->second.pose.position, bolt->second.pose.position + 3);
        }
    }
}

void World::tick_flights(std::uint64_t now) {
    for (auto it = flights_.begin(); it != flights_.end();) {
        const auto moby = mobys_.find(it->first);

        if (moby == mobys_.end() || !bolt_class(moby->second.oclass)) {
            it = flights_.erase(it);
        } else if (now >= it->second.until) {
            note(it->first, "a taken bolt's flight timed out", it->second.player);
            kill(it->first, "a taken bolt's flight timed out");
            it = flights_.erase(it);
        } else {
            ++it;
        }
    }
}

void World::tick_crates(std::uint64_t now) {
    (void)now;

    for (const auto& [slot, moby] : mobys_) {
        if (moby.key < 0 || !crate_class(moby.oclass)) {
            continue;
        }

        const auto [was, first] = crate_states_.try_emplace(moby.key, moby.state);
        const bool broke = !first && moby.state == kCrateWaiting && was->second != kCrateWaiting && was->second != 0;
        was->second = moby.state;

        if (!broke) {
            continue;
        }

        const bool replayed = replays_.count(slot) != 0;
        Writer w;
        w.u8(static_cast<std::uint8_t>(WorldKind::kBroke)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
        u16(w, static_cast<std::uint16_t>(moby.key));

        // The host's copy broke (its drop goes out as any): the others' break with it. A member's
        // tells the host, unless it broke because the host said so.
        if (hosting_ && !coop_.arriving()) {
            session_.send_data(kBroadcast, w.bytes());
        } else if (!session_.is_host() && !replayed && coop_.playing() && level_ == coop_.host_level() && synced_) {
            w.u8(1);
            session_.send_data(session_.host(), w.bytes());
            trace_line("msg", "sent BROKE to host key " + std::to_string(moby.key) + " (comes back)");
        }
    }
}

bool World::metal_detect(int key) {
    const std::uint64_t now = clock_();

    if (hosting_) {
        return dug_.insert(key).second || game_.moby_slot(key) >= 0;
    }

    if (!session_.in_party() || session_.is_host() || !coop_.playing() || level_ < 0 || level_ != coop_.host_level()) {
        return true;
    }

    if (digs_.count(key) != 0) {
        return true;
    }

    const auto asked = dig_asked_.find(key);

    if (asked == dig_asked_.end() || now - asked->second >= kDigAskMs) {
        dig_asked_[key] = now;
        Writer w;
        w.u8(static_cast<std::uint8_t>(WorldKind::kClaim)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
        u16(w, static_cast<std::uint16_t>(key));
        w.u8(1);
        session_.send_data(session_.host(), w.bytes());
        trace_line("bolts", "dig asked for key " + std::to_string(key));
    }

    return false;
}

void World::claim_dig(std::int32_t member, int level, int key) {
    if (!hosting_ || level != level_) {
        return;
    }

    const int slot = alive_keys_.count(key) != 0 ? game_.moby_slot(key) : -1;
    const auto id = static_cast<std::uint16_t>(key);

    // Gone here, or dug: the asker's copy goes too.
    if (slot < 0 || dug_.count(key) != 0) {
        Writer w;
        w.u8(static_cast<std::uint8_t>(WorldKind::kKilled)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
        u16(w, 1);
        u16(w, id);
        session_.send_data(member, w.bytes());
        trace_line("bolts", "dig refused to " + std::to_string(member) + " key " + std::to_string(key));
        return;
    }

    dug_.insert(key);
    send_taken(id, member);
    note(slot, "a pile of buried bolts dug there", member);
    kill(slot, "dig by " + std::to_string(member));
    trace_line("bolts", "dig granted to " + std::to_string(member) + " key " + std::to_string(key));
}

void World::tick_digs(std::uint64_t now) {
    for (auto it = digs_.begin(); it != digs_.end();) {
        if (now < it->second) {
            ++it;
            continue;
        }

        const int slot = game_.moby_slot(it->first);

        if (slot >= 0 && mobys_.count(slot) != 0) {
            kill(slot, "a dig the host granted, not dug in time");
        }

        it = digs_.erase(it);
    }
}

void World::replay_break(int slot, std::uint64_t now, bool respawning) {
    const auto it = mobys_.find(slot);

    if (it == mobys_.end() || replays_.count(slot) != 0 || it->second.state == kCrateBroken
        || it->second.state == kCrateWaiting) {
        return;
    }

    if (respawning) {
        soft_replays_.insert(slot);
    }

    Hit h;
    h.victim = slot;
    h.attacker = game_.figure_slot(-1);
    // A metal crate takes only an explosion's hit, and an oom-oom box goes off at once on one (on
    // a wrench's, a few ticks later); the others a wrench's.
    h.mask = it->second.oclass == 502 || it->second.oclass == 505 ? 0x20000u : 0x10000u;
    h.damage = 1.0f;
    std::copy(it->second.pose.position, it->second.pose.position + 3, h.position);
    game_.hit(h);
    replays_[slot] = now + kReplayMs;
}

}  // namespace mp
