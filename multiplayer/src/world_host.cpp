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
    // Its mobys are new: nothing it was told stands, and a word on it from before says nothing.
    m.tracked.clear();
    m.window.clear();
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

namespace {

// What a member is told of a moby, and whether it differs from what the moby is now.
void tell(WorldTold& told, const Pose& pose, bool hidden, bool loose) {
    std::copy(pose.position, pose.position + 3, told.position);
    std::copy(pose.rotation, pose.rotation + 3, told.rotation);
    told.seq = pose.seq;
    told.frame = pose.frame;
    told.hidden = hidden;
    told.loose = loose;
}

bool differs(const WorldTold& told, const Pose& pose, bool hidden, bool loose) {
    // A bolt landed: the member's copy runs as its own once told, however it spins.
    if (told.hidden != hidden || told.loose != loose) {
        return true;
    }

    Pose was;
    std::copy(told.position, told.position + 3, was.position);
    std::copy(told.rotation, told.rotation + 3, was.rotation);
    was.seq = told.seq;
    was.frame = told.frame;
    return !loose && changed(was, pose);
}

}  // namespace

void World::take_ack(Member& m, std::uint16_t newest, std::uint32_t bits, std::uint64_t now) {
    for (auto it = m.window.begin(); it != m.window.end();) {
        const auto behind = static_cast<std::uint16_t>(newest - it->sequence);

        // Newer than this word speaks of.
        if (behind >= 0x8000) {
            ++it;
            continue;
        }

        const bool heard = behind == 0 || (behind <= 32 && ((bits >> (behind - 1)) & 1) != 0);

        // Not heard, but it may still be on its way behind the newer ones.
        if (!heard && behind < kWorldLostAfter) {
            ++it;
            continue;
        }

        if (behind == 0) {
            m.rtt_ms += 0.1 * (static_cast<double>(now - it->ms) - m.rtt_ms);
        }

        for (const auto& [id, told] : it->told) {
            const auto t = m.tracked.find(id);

            if (t == m.tracked.end()) {
                continue;
            }

            if (heard && (!t->second.delivered || sequence_newer(it->sequence, t->second.known_seq))) {
                t->second.known = told;
                t->second.known_seq = it->sequence;
                t->second.delivered = true;
            }

            t->second.flying = t->second.flying && t->second.sent_seq != it->sequence;
        }

        it = m.window.erase(it);
    }
}

void World::expire_window(Member& m, std::uint64_t now) {
    const auto wait = std::clamp(static_cast<std::uint64_t>(2.0 * m.rtt_ms), kWorldMinWaitMs, kWorldMaxWaitMs);

    while (!m.window.empty() && (now - m.window.front().ms >= wait || m.window.size() > kWorldWindow)) {
        for (const auto& [id, told] : m.window.front().told) {
            const auto t = m.tracked.find(id);

            if (t != m.tracked.end() && t->second.sent_seq == m.window.front().sequence) {
                t->second.flying = false;
            }
        }

        m.window.pop_front();
    }
}

void World::send_world(std::int32_t member, Member& m, std::uint64_t now, const Pose& at) {
    struct Pick {
        float priority;
        std::uint16_t id;
        int slot;
    };

    const auto since = static_cast<float>(std::min<std::uint64_t>(now - m.world_ms, kWorldMaxWaitMs));
    expire_window(m, now);

    for (auto it = m.hit_ms.begin(); it != m.hit_ms.end();) {
        it = now - it->second >= kWorldHitMs ? m.hit_ms.erase(it) : std::next(it);
    }

    std::vector<Pick> picks;
    std::set<std::uint16_t> here;

    for (const auto& [slot, moby] : mobys_) {
        const bool placed_runs = moby.key >= 0 && (moby.flags & kMobyUpdates) != 0 && moby.oclass != 0
                                 && !local_class(moby.oclass);

        // The host's loot is every game's own once made: not posed from here.
        if (!placed_runs && (shared_.count(slot) == 0 || loot_class(moby.oclass))) {
            continue;
        }

        const std::uint16_t id = id_of(slot);
        here.insert(id);
        const bool hidden = (moby.flags & kMobyHidden) != 0;
        const bool loose = bolt_class(moby.oclass) && moby.state == kBoltHovering;
        const auto [track, first] = m.tracked.try_emplace(id);
        Tracked& t = track->second;
        // The member has it: the newest word that went came, and it says what the moby is. A word
        // older than that says nothing of what the member shows once the newest comes, and a word
        // given up for lost may still come, so even a moby back where it was known to be goes.
        const bool known = t.delivered && t.known_seq == t.sent_seq && !differs(t.known, moby.pose, hidden, loose);
        // Or the word that will give it is on its way.
        const bool waiting = t.flying && !differs(t.sent, moby.pose, hidden, loose);

        if (waiting || (known && now - t.ms < kWorldKeepMs)) {
            t.priority = 0;
            continue;
        }

        const float dx = moby.pose.position[0] - at.position[0];
        const float dy = moby.pose.position[1] - at.position[1];
        const float d = std::sqrt(distance2(moby.pose.position, at.position));
        const float yaw = at.rotation[2];
        WorldSight sight;
        sight.distance = d;
        sight.creature = (moby.flags & kMobyCreature) != 0;
        sight.ahead = d > 0.0f && (dx * std::cos(yaw) + dy * std::sin(yaw)) / d > 0.5f;
        sight.hit = m.hit_ms.count(id) != 0;
        sight.flying = bolt_class(moby.oclass) && !loose;
        sight.hidden = hidden;
        const float weight = world_weight(sight);
        t.priority += weight * since / kWorldBaseMs;

        // New to it: the nearest first. It stands and the last word did not come, or it hid or
        // showed: a change to see now, not a stream that has its next word coming anyway.
        const bool flipped = t.ms != 0 && (t.sent.hidden != hidden || t.sent.loose != loose);

        if (first || (!known && !differs(t.sent, moby.pose, hidden, loose)) || flipped) {
            t.priority = std::max(t.priority, 1.0f + weight);
        }

        if (t.priority >= 1.0f) {
            picks.push_back({t.priority, id, slot});
        }
    }

    // What left the level left the books.
    for (auto it = m.tracked.begin(); it != m.tracked.end();) {
        it = here.count(it->first) == 0 ? m.tracked.erase(it) : std::next(it);
    }

    std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.id < b.id;
    });
    std::vector<WorldEntry> entries;

    for (const Pick& p : picks) {
        if (entries.size() >= kWorldEntries) {
            break;
        }

        const MobyState& moby = mobys_[p.slot];
        const bool hidden = (moby.flags & kMobyHidden) != 0;
        const bool loose = bolt_class(moby.oclass) && moby.state == kBoltHovering;
        entries.push_back({p.id, moby.pose, hidden, loose});
    }

    m.world_ms = now;

    if (entries.empty()) {
        return;
    }

    const std::int64_t stamp =
        session_.has_clock() ? session_.server_ms() : static_cast<std::int64_t>(now);
    const auto sequence = static_cast<std::uint16_t>(m.sequence + 1);

    // Refused by the bucket: nothing is taken as sent, and the priorities stand.
    if (!session_.send_state(member, encode_world(level_, stamp, entries, sequence))) {
        return;
    }

    m.sequence = sequence;
    Datagram datagram;
    datagram.sequence = sequence;
    datagram.ms = now;
    datagram.told.reserve(entries.size());

    for (const WorldEntry& e : entries) {
        WorldTold told;
        tell(told, e.pose, e.hidden, e.loose);
        datagram.told.emplace_back(e.id, told);
        Tracked& t = m.tracked[e.id];
        t.sent = told;
        t.sent_seq = sequence;
        t.flying = true;
        t.ms = now;
        // What is left over counts for the next: a snapshot's steps do not round a period up.
        t.priority = t.priority < 2.0f ? t.priority - 1.0f : 0.0f;
    }

    m.window.push_back(std::move(datagram));

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

        std::uint16_t newest = 0;
        std::uint32_t bits = 0;

        if (coop_.take_world_ack(who.id, newest, bits)) {
            take_ack(m, newest, bits, now);
        }

        // Each gets its share of what the state's budget has left for the world, the shots' first.
        const std::uint64_t budget = coop_.firing(now) ? kWorldFiringBudget : kWorldBudget;
        const std::uint64_t share = 1000 * (session_.members().size() - 1) / budget;

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
