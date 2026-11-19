// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The shared world (world.hpp): pickups taken, claimed and flown to their player; crates broken
// again.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "world.hpp"
#include "world_util.hpp"

namespace mp {

using namespace wire;

bool World::quiet_drop(int slot) {
    // A member in the host's level: the host's game drops the bolts (its copy of the crate breaks
    // too), so this game's would be the same bolts twice.
    const bool quiet = session_.in_party() && !session_.role().host() && coop_.playing()
                       && level_ >= 0 && level_ == coop_.host_level();

    // The verbose log records each drop this game leaves out.
    if (quiet) {
        trace_line(
            "bolts", "suppressed local drop of " + moby_name(slot) + " (the host's game drops it)"
        );
    }

    return quiet;
}

bool World::collect(int slot) {
    const auto moby = mobys_.find(slot);
    const int oclass = moby != mobys_.end() ? moby->second.oclass : 0;

    // A bolt another player took, arrived at that player (or at this game's Ratchet, for one taken
    // for him there): given already, nothing added and nothing claimed; the bolt update deletes it.
    if (const auto f = flights_.find(slot); f != flights_.end()) {
        trace_line(
            "bolts",
            moby_name(slot) + " id " + std::to_string(f->second.id) + " reaches player "
                + std::to_string(f->second.player) + (f->second.figure >= 0 ? "'s figure" : " here")
                + " (taken there: nothing added here)"
        );
        flights_.erase(f);

        // The bolt is forgotten as a copy of the host's one, so nothing else drives it.
        for (auto it = net_slot_.begin(); it != net_slot_.end(); ++it) {
            // Find the net id this slot was made for.
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
    if (session_.role().host()) {
        const std::uint16_t id = hosting_ ? id_of(slot) : kNoId;

        // The bolt flew to a member's figure, so that member is the taker.
        if (id != kNoId && standing_ != 0) {
            claimed_.insert(id);
            send_taken(id, standing_);
            members_[standing_].bolts += bolt_value(oclass);
            trace_line(
                "bolts",
                "player " + std::to_string(standing_) + "'s figure collects " + moby_name(slot)
                    + " id " + std::to_string(id) + " +" + std::to_string(bolt_value(oclass))
                    + " (theirs: BOLTS; nothing added here)"
            );
            return false;
        }

        // The host's own Ratchet took it: its credit.
        if (id != kNoId) {
            claimed_.insert(id);
            send_taken(id, session_.id());
            trace_line(
                "bolts",
                "host collects " + moby_name(slot) + " id " + std::to_string(id) + " +"
                    + std::to_string(bolt_value(oclass)) + " (its own credit)"
            );
        }

        return true;
    }

    // Outside the host's level there is no host bolt to claim, so the bolt is this game's own.
    if (!session_.in_party() || !coop_.playing() || level_ < 0 || level_ != coop_.host_level()) {
        return true;
    }

    // The host's bolt made here, or a placed bolt (the same in every game): the host decides.
    std::uint16_t id = kNoId;

    // The slot's net id names the bolt on the wire; the copy is forgotten as it is claimed.
    for (auto it = net_slot_.begin(); it != net_slot_.end(); ++it) {
        // Find the net id of this slot.
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

    // A placed bolt is named by its key, the same in every game.
    if (id == kNoId && moby != mobys_.end() && moby->second.key >= 0 && bolt_class(oclass)) {
        id = static_cast<std::uint16_t>(moby->second.key);
    }

    // One of this game's own (none the host knows of): its own.
    if (id == kNoId) {
        trace_line("bolts", "member collects its own " + moby_name(slot) + " (not the host's)");
        return true;
    }

    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kClaim))
        .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
    u16(w, id);
    session_.send_data(session_.host(), w.bytes());
    trace_line(
        "bolts",
        "claim sent for " + moby_name(slot) + " id " + std::to_string(id)
            + ": suppressed local add of " + std::to_string(bolt_value(oclass))
    );
    return false;
}

void World::claim(std::int32_t member, int level, std::uint16_t id) {
    int slot = -1;

    // Only the host answers a claim, and only for the level it is in.
    if (hosting_ && level == level_ && (id & kSpawnedId) != 0) {
        slot = slot_of(id);
    } else if (hosting_ && level == level_ && alive_keys_.count(id) != 0) {
        slot = game_.moby_slot(id);
    }

    const auto moby = mobys_.find(slot);
    const int value = moby != mobys_.end() ? bolt_value(moby->second.oclass) : 0;

    // The first claim of a bolt still here, that nobody took: the claimant's, gone from every game.
    if (slot < 0 || value <= 0 || claimed_.count(id) != 0) {
        trace_line(
            "bolts",
            "claim refused from " + std::to_string(member) + " id " + std::to_string(id) + " level "
                + std::to_string(level)
                + (claimed_.count(id) != 0 ? " (already taken)" : " (not here)")
        );
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

    session_.send_data(
        member, Writer().u8(static_cast<std::uint8_t>(WorldKind::kBolts)).i32(value).bytes()
    );
    counts_.bolts += value;
    trace_line(
        "bolts",
        "claim granted to " + std::to_string(member) + " id " + std::to_string(id) + " "
            + moby_name(slot) + ": sent BOLTS +" + std::to_string(value)
    );
}

void World::send_taken(std::uint16_t id, std::int32_t player) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kTaken))
        .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
    u16(w, id);
    w.i32(player);
    session_.send_data(kBroadcast, w.bytes());
    trace_line(
        "msg", "sent TAKEN to all id " + std::to_string(id) + " by " + std::to_string(player)
    );
}

bool World::fly(int slot, std::uint16_t id, std::int32_t player, std::uint64_t now) {
    const auto moby = mobys_.find(slot);

    // Only a bolt can fly to a player.
    if (moby == mobys_.end() || !bolt_class(moby->second.oclass)) {
        return false;
    }

    const bool here = player == session_.id();
    const int figure = here ? -1 : coop_.figure_of(player);

    // A player whose figure is not shown here has nothing to fly to.
    if (!here && figure < 0) {
        return false;
    }

    flights_[slot] = {player, figure, id, now + kFlightMs};
    trace_line(
        "bolts",
        moby_name(slot) + " id " + std::to_string(id) + " flies to player " + std::to_string(player)
            + (here ? " (this game's Ratchet)" : "'s figure")
    );
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

        // The figure's moby gives the place; without it the player's last pose does.
        if (shown != mobys_.end()) {
            std::copy(shown->second.pose.position, shown->second.pose.position + 3, to);
            found = true;
        } else if (coop_.latest(f.player, p)) {
            std::copy(p.position, p.position + 3, to);
            found = true;
        }
    } else {
        Pose p;

        // This game's Ratchet gives the place when the game says where he is.
        if (game_.hero(p)) {
            std::copy(p.position, p.position + 3, to);
            found = true;
        }
    }

    // With no place to fly to, no stand-in is set for this update.
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
    // Each flying bolt gets an eye, so it runs wherever the camera is.
    for (const auto& [slot, flight] : flights_) {
        const auto bolt = mobys_.find(slot);

        // At most 8 eyes go to the game, three floats each.
        if (bolt != mobys_.end() && eyes.size() < 3 * 8) {
            eyes.insert(eyes.end(), bolt->second.pose.position, bolt->second.pose.position + 3);
        }
    }
}

void World::tick_flights(std::uint64_t now) {
    // A flight ends when its bolt is gone or its time is up.
    for (auto it = flights_.begin(); it != flights_.end();) {
        const auto moby = mobys_.find(it->first);

        // A bolt gone from the listing has nothing left to fly.
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

    // Each placed crate's state is compared with the one of the last tick.
    for (const auto& [slot, moby] : mobys_) {
        // Only placed crates are watched, by their key.
        if (moby.key < 0 || !crate_class(moby.oclass)) {
            continue;
        }

        const auto [was, first] = crate_states_.try_emplace(moby.key, moby.state);
        const bool broke = !first && moby.state == kCrateWaiting && was->second != kCrateWaiting
                           && was->second != 0;
        was->second = moby.state;

        // Only a crate that went to its waiting state this tick broke.
        if (!broke) {
            continue;
        }

        const bool replayed = replays_.count(slot) != 0;
        Writer w;
        w.u8(static_cast<std::uint8_t>(WorldKind::kBroke))
            .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
        u16(w, static_cast<std::uint16_t>(moby.key));

        // The host's copy broke (its drop goes out as any): the others' break with it. A member's
        // tells the host, unless it broke because the host said so.
        if (hosting_ && !coop_.arriving()) {
            session_.send_data(kBroadcast, w.bytes());
        } else if (!session_.role().host() && !replayed && coop_.playing()
                   && level_ == coop_.host_level() && synced_) {
            w.u8(1);
            session_.send_data(session_.host(), w.bytes());
            trace_line(
                "msg", "sent BROKE to host key " + std::to_string(moby.key) + " (comes back)"
            );
        }
    }
}

bool World::metal_detect(int key) {
    const std::uint64_t now = clock_();

    // The host digs without asking: a pile is dug once, a repeat only while its moby is still
    // there.
    if (hosting_) {
        return dug_.insert(key).second || game_.moby_slot(key) >= 0;
    }

    // Outside the host's level the dig is this game's own.
    if (!session_.in_party() || session_.role().host() || !coop_.playing() || level_ < 0
        || level_ != coop_.host_level()) {
        return true;
    }

    // The host has already said yes for this pile.
    if (digs_.count(key) != 0) {
        return true;
    }

    const auto asked = dig_asked_.find(key);

    // The ask is repeated every kDigAskMs until the host answers.
    if (asked == dig_asked_.end() || now - asked->second >= kDigAskMs) {
        dig_asked_[key] = now;
        Writer w;
        w.u8(static_cast<std::uint8_t>(WorldKind::kClaim))
            .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
        u16(w, static_cast<std::uint16_t>(key));
        w.u8(1);
        session_.send_data(session_.host(), w.bytes());
        trace_line("bolts", "dig asked for key " + std::to_string(key));
    }

    return false;
}

void World::claim_dig(std::int32_t member, int level, int key) {
    // Only the host, in its own level, answers a dig claim.
    if (!hosting_ || level != level_) {
        return;
    }

    const int slot = alive_keys_.count(key) != 0 ? game_.moby_slot(key) : -1;
    const auto id = static_cast<std::uint16_t>(key);

    // Gone here, or dug: the asker's copy goes too.
    if (slot < 0 || dug_.count(key) != 0) {
        Writer w;
        w.u8(static_cast<std::uint8_t>(WorldKind::kKilled))
            .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
        u16(w, 1);
        u16(w, id);
        session_.send_data(member, w.bytes());
        trace_line(
            "bolts", "dig refused to " + std::to_string(member) + " key " + std::to_string(key)
        );
        return;
    }

    dug_.insert(key);
    send_taken(id, member);
    note(slot, "a pile of buried bolts dug there", member);
    kill(slot, "dig by " + std::to_string(member));
    trace_line("bolts", "dig granted to " + std::to_string(member) + " key " + std::to_string(key));
}

void World::tick_digs(std::uint64_t now) {
    // Each dig the host granted waits for the member to make it.
    for (auto it = digs_.begin(); it != digs_.end();) {
        // A granted dig stays the member's until its time is up.
        if (now < it->second) {
            ++it;
            continue;
        }

        const int slot = game_.moby_slot(it->first);

        // The pile is deleted if it is still listed; otherwise it is already gone.
        if (slot >= 0 && mobys_.count(slot) != 0) {
            kill(slot, "a dig the host granted, not dug in time");
        }

        it = digs_.erase(it);
    }
}

void World::replay_break(int slot, std::uint64_t now, bool respawning) {
    const auto it = mobys_.find(slot);

    // A crate already breaking, hidden or being replayed is not hit again.
    if (it == mobys_.end() || replays_.count(slot) != 0 || it->second.state == kCrateBroken
        || it->second.state == kCrateWaiting) {
        return;
    }

    // A crate that comes back is not deleted when its deadline passes.
    if (respawning) {
        soft_replays_.insert(slot);
    }

    Hit h;
    h.victim = slot;
    h.attacker = game_.figure_slot(-1);
    // A metal crate takes only an explosion's hit, and an oom-oom box goes off at once on one (on
    // a wrench's, a few ticks later); the others a wrench's.
    // Masks: 0x10000 is a wrench's hit, 0x20000 an explosion's (assumed).
    h.mask = it->second.oclass == 502 || it->second.oclass == 505 ? 0x20000u : 0x10000u;
    h.damage = 1.0f;
    std::copy(it->second.pose.position, it->second.pose.position + 3, h.position);
    game_.hit(h);
    replays_[slot] = now + kReplayMs;
}

}  // namespace mp
