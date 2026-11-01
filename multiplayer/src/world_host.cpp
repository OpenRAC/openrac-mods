/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The shared world (world.hpp): the host's side: the world sent to the members, spawns, deaths and the tick.
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

void World::send_spawn(std::int32_t target, int level, std::uint16_t net, const MobyState& moby) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kSpawn)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level)));
    u16(w, net);
    u16(w, static_cast<std::uint16_t>(static_cast<std::int16_t>(moby.oclass)));
    vec(w, moby.pose.position);
    u16(w, static_cast<std::uint16_t>(angle_bits(moby.pose.rotation[2])));
    session_.send_data(target, w.bytes());
    trace_line("msg", "sent SPAWN to " + std::to_string(target) + " net " + std::to_string(net) + " class "
                          + std::to_string(moby.oclass) + " slot " + std::to_string(moby.slot) + " at " + xyz(moby.pose.position));
}

void World::send_dead(std::int32_t member, int level) {
    const int records = party_dead_.empty() ? 0 : *party_dead_.rbegin() + 1;
    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kDead)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level)));
    u16(w, static_cast<std::uint16_t>(records));
    std::vector<std::uint8_t> bits(static_cast<std::size_t>((records + 7) / 8), 0);

    for (const int key : party_dead_) {
        bits[static_cast<std::size_t>(key / 8)] |= static_cast<std::uint8_t>(1u << (key % 8));
    }

    w.raw(bits);
    session_.send_data(member, w.bytes());
    trace_line("msg", "sent DEAD to " + std::to_string(member) + " level " + std::to_string(level) + " records "
                          + std::to_string(records) + " dead " + std::to_string(party_dead_.size()));
}

void World::give_world(std::int32_t id, Member& m) {
    m.want = false;
    m.synced = true;
    m.sent.clear();
    send_dead(id, level_);

    for (const auto& [slot, net] : shared_) {
        send_spawn(id, level_, net.id, mobys_[slot]);
    }

    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kSynced)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
    w.i32(static_cast<std::int32_t>(m.load));
    session_.send_data(id, w.bytes());
    log("player " + std::to_string(id) + " is given the world of level " + std::to_string(level_) + ": "
        + std::to_string(party_dead_.size()) + " dead, " + std::to_string(shared_.size()) + " spawned");
}

void World::take_broke() {
    for (const auto& [key, sender] : broke_) {
        const int slot = game_.moby_slot(key);
        const auto it = mobys_.find(slot);
        trace_line("msg", "BROKE of key " + std::to_string(key) + " from " + std::to_string(sender) + " slot "
                              + std::to_string(slot));

        // A crate breaks here as it broke there; a pickup taken there is gone here.
        if (slot >= 0 && it != mobys_.end() && crate_class(it->second.oclass)) {
            note(slot, "BROKE, a crate broken as there", sender);
            replay_break(slot, clock_());
        } else if (slot >= 0 && it != mobys_.end() && pickup_class(it->second.oclass)) {
            note(slot, "BROKE, a pickup taken there", sender);
            kill(slot, "BROKE from " + std::to_string(sender));
        }
    }

    broke_.clear();
}

void World::remade() {
    int killed = 0;

    // Each game's own interactables follow that game's own story; the rest stay as the party left
    // them.
    for (const int key : party_dead_) {
        const int slot = alive_keys_.count(key) != 0 ? game_.moby_slot(key) : -1;
        const auto moby = mobys_.find(slot);

        if (moby != mobys_.end() && !interactable_class(moby->second.oclass)) {
            note(slot, "dead for the party, made again by the level", 0);
            kill(slot, "dead for the party");
            ++killed;
        }
    }

    log("level " + std::to_string(level_) + " made again here: " + std::to_string(killed)
        + " mobys dead for the party deleted, the members told");
    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kReset)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
    session_.send_data(kBroadcast, w.bytes());
}

void World::drop_spawned() {
    for (const auto& [net, slot] : net_slot_) {
        if (flights_.count(slot) == 0) {
            note(slot, "RESET, the host's level made again", session_.host());
            kill(slot, "RESET from the host");
        }

        driven_.erase(slot);
        poses_.erase(static_cast<std::uint16_t>(kSpawnedId | net));
        hidden_.erase(static_cast<std::uint16_t>(kSpawnedId | net));
    }

    net_slot_.clear();
    loose_.clear();
    loot_.clear();
}

void World::send_world(std::int32_t member, Member& m, std::uint64_t now, const Pose& at) {
    struct Pick {
        float rank;
        int slot;
        std::uint16_t id;
        bool moved;
    };

    std::vector<Pick> picks;

    for (const auto& [slot, moby] : mobys_) {
        const bool placed_runs = moby.key >= 0 && (moby.flags & kMobyUpdates) != 0 && moby.oclass != 0
                                 && !local_class(moby.oclass);

        // The host's loot is every game's own once made: not posed from here.
        if (!placed_runs && (shared_.count(slot) == 0 || loot_class(moby.oclass))) {
            continue;
        }

        const std::uint16_t id = id_of(slot);
        const float d2 = distance2(moby.pose.position, at.position);
        const auto sent = m.sent.find(id);
        const bool hidden = (moby.flags & kMobyHidden) != 0;
        const std::uint64_t age = sent == m.sent.end() ? kWorldFarMs : now - sent->second.ms;
        // A bolt landed: the member's copy runs as its own once told, so it is told again only now
        // and then (a lost datagram), however it spins.
        const bool loose = bolt_class(moby.oclass) && moby.state == kBoltHovering;
        const bool moved = sent == m.sent.end() || sent->second.hidden != hidden || sent->second.loose != loose
                           || (!loose && changed(sent->second.pose, moby.pose));
        const bool again = sent != m.sent.end() && sent->second.repeats > 0;
        const bool stale = age >= (d2 <= kWorldNear * kWorldNear ? kWorldRefreshMs : kWorldFarMs);

        if (moved || again || stale) {
            // The nearest first, and what waited long comes up past them.
            picks.push_back({std::sqrt(d2) / (1.0f + static_cast<float>(age) / 200.0f), slot, id, moved});
        }
    }

    std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.rank < b.rank; });
    std::vector<WorldEntry> entries;
    std::vector<const Pick*> taken;

    for (const Pick& p : picks) {
        if (entries.size() >= kWorldEntries) {
            break;
        }

        const MobyState& moby = mobys_[p.slot];
        const bool hidden = (moby.flags & kMobyHidden) != 0;
        const bool loose = bolt_class(moby.oclass) && moby.state == kBoltHovering;
        entries.push_back({p.id, moby.pose, hidden, loose});
        taken.push_back(&p);
    }

    m.world_ms = now;

    if (entries.empty()) {
        return;
    }

    const std::int64_t stamp =
        session_.has_clock() ? session_.server_ms() : static_cast<std::int64_t>(now);

    // Refused by the bucket: nothing is taken as sent.
    if (!session_.send_state(member, encode_world(level_, stamp, entries))) {
        return;
    }

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto sent = m.sent.find(entries[i].id);
        const int repeats = taken[i]->moved ? kWorldRepeats : sent != m.sent.end() ? std::max(sent->second.repeats - 1, 0) : 0;
        m.sent[entries[i].id] = {entries[i].pose, entries[i].hidden, entries[i].loose, now, repeats};
    }

    if (tracing()) {
        std::set<std::uint16_t> ids;

        for (const WorldEntry& e : entries) {
            ids.insert(e.id);
        }

        trace_snapshot("sent to " + std::to_string(member), traced_.sent[member], ids);
    }

    ++counts_.snapshots;
}

void World::tick_host(std::uint64_t now, const GameStatus& status) {
    // The spawned mobys that count: creatures and bolts, never a figure (class 0), nor a scene's
    // actor (each game playing the scene makes its own: coop_scenes.cpp).
    std::set<int> seen;

    for (const auto& [slot, moby] : mobys_) {
        const bool counts = moby.key < 0 && moby.oclass != 0 && (moby.flags & kMobyScene) == 0
                            && ((moby.flags & kMobyCreature) != 0 || bolt_class(moby.oclass)
                                || loot_class(moby.oclass));

        if (!counts) {
            continue;
        }

        // Loot there when the level is first listed is the level's own (a nanotech crate makes
        // its pickup at its first update): every game's load makes its own.
        if (loot_class(moby.oclass) && !listed_) {
            level_own_.insert(slot);
        }

        if (level_own_.count(slot) != 0) {
            continue;
        }

        seen.insert(slot);
        const auto it = shared_.find(slot);

        // A slot made again for another class is a new moby.
        if (it != shared_.end() && it->second.oclass == moby.oclass) {
            continue;
        }

        const std::uint16_t net = next_net_;
        next_net_ = static_cast<std::uint16_t>(next_net_ % 0x7FFE + 1);
        shared_[slot] = {net, moby.oclass};

        send_spawn(kBroadcast, level_, net, moby);
        ++counts_.spawns;
    }

    for (auto it = level_own_.begin(); it != level_own_.end();) {
        it = mobys_.count(*it) == 0 ? level_own_.erase(it) : std::next(it);
    }

    std::vector<std::uint16_t> gone;

    for (auto it = shared_.begin(); it != shared_.end();) {
        if (seen.count(it->first) == 0) {
            // Its net id may come again (they wrap): a new bolt's then, not taken.
            gone.push_back(it->second.id);
            claimed_.erase(static_cast<std::uint16_t>(kSpawnedId | it->second.id));
            it = shared_.erase(it);
        } else {
            ++it;
        }
    }

    if (!gone.empty()) {
        Writer w;
        w.u8(static_cast<std::uint8_t>(WorldKind::kDespawn)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
        u16(w, static_cast<std::uint16_t>(gone.size()));

        std::string nets;

        for (const std::uint16_t net : gone) {
            u16(w, net);
            nets += (nets.empty() ? "" : " ") + std::to_string(net);
        }

        session_.send_data(kBroadcast, w.bytes());
        trace_line("msg", "sent DESPAWN to all n=" + std::to_string(gone.size()) + " nets [" + nets + "]");
        counts_.despawns += static_cast<int>(gone.size());
    }

    // The members: where each is, what it is told, the eyes on them.
    std::vector<float> eyes;
    std::string eye_players;

    for (auto it = members_.begin(); it != members_.end();) {
        it = std::any_of(session_.members().begin(), session_.members().end(),
                         [&](const mp::Member& x) { return x.id == it->first; })
                 ? std::next(it)
                 : members_.erase(it);
    }

    for (const mp::Member& who : session_.members()) {
        if (who.id == session_.id()) {
            continue;
        }

        Member& m = members_[who.id];
        Pose at;

        // The world to one that asked; it asks again when it has new mobys.
        if (m.want) {
            give_world(who.id, m);
        }

        // Elsewhere, or nothing heard yet: no snapshots to place. Its world stands: a game that
        // really left says HELLO when it is back, and its HELLO may well beat its first pose here.
        if (!coop_.latest(who.id, at) || at.level != status.level) {
            m.level = at.level;
            continue;
        }

        m.level = at.level;

        if (eyes.size() < 3 * 8) {
            eyes.insert(eyes.end(), at.position, at.position + 3);
            eye_players += " " + std::to_string(who.id) + " at " + xyz(at.position);
        }

        // Each gets its share of what the state's budget has left for the world.
        const std::uint64_t share = 1000 * (session_.members().size() - 1) / kWorldBudget;

        if (m.synced && now - m.world_ms >= std::max(kWorldMs, share)) {
            send_world(who.id, m, now, at);
        }

        if (m.bolts > 0) {
            session_.send_data(who.id, Writer().u8(static_cast<std::uint8_t>(WorldKind::kBolts)).i32(m.bolts).bytes());
            counts_.bolts += m.bolts;
            m.bolts = 0;
        }
    }

    // A bolt flying to whoever took it runs wherever the camera is (an eye on it).
    add_flight_eyes(eyes);
    game_.eyes(eyes);

    // Each change of their number, each tick a menu is up, else once a second.
    if (tracing()
        && (eyes.size() != traced_.eyes || status.phase == kPhaseMenu || now - traced_.eyes_ms >= 1000)) {
        traced_.eyes = eyes.size();
        traced_.eyes_ms = now;
        trace_line("eyes", "set n=" + std::to_string(eyes.size() / 3) + (eye_players.empty() ? " (none)" : eye_players));
    }
}

}  // namespace mp
