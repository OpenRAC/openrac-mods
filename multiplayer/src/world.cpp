/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The shared world (world.hpp): the host's snapshots, the ids of the mobys and the tick.
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

Bytes encode_world(int level, std::int64_t stamp, const std::vector<WorldEntry>& entries, std::uint16_t sequence) {
    const std::size_t count = std::min(entries.size(), kWorldEntries);
    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kWorld))
        .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level)))
        .i64(stamp);
    u16(w, sequence);
    w.u8(static_cast<std::uint8_t>(count));

    for (std::size_t i = 0; i < count; ++i) {
        const WorldEntry& e = entries[i];
        u16(w, e.id);
        vec(w, e.pose.position);

        for (const float r : e.pose.rotation) {
            u16(w, static_cast<std::uint16_t>(angle_bits(r)));
        }

        w.u8(static_cast<std::uint8_t>(std::min<std::uint32_t>(e.pose.seq, 255)))
            .u8(static_cast<std::uint8_t>(std::min<std::uint32_t>(e.pose.frame, 255)))
            .u8(static_cast<std::uint8_t>(std::clamp(std::lround(e.pose.anim_time * 255.0f), 0L, 255L)))
            .u8(static_cast<std::uint8_t>(std::clamp(std::lround(e.pose.anim_speed * 32.0f), 0L, 255L)))
            .u8(static_cast<std::uint8_t>((e.hidden ? 1 : 0) | (e.loose ? 2 : 0)));
    }

    return w.bytes();
}

bool decode_world(const Bytes& bytes, int& level, std::int64_t& stamp, std::vector<WorldEntry>& out, std::uint16_t* sequence) {
    if (bytes.empty() || bytes[0] != static_cast<std::uint8_t>(WorldKind::kWorld)) {
        return false;
    }

    Reader r(bytes.data() + 1, bytes.size() - 1);
    level = static_cast<std::int8_t>(r.u8());
    stamp = r.i64();
    const std::uint16_t heard = u16(r);

    if (sequence != nullptr) {
        *sequence = heard;
    }

    const std::uint8_t count = r.u8();
    out.clear();

    for (std::uint8_t i = 0; i < count && r.ok(); ++i) {
        WorldEntry e;
        e.id = u16(r);
        vec(r, e.pose.position);

        for (float& a : e.pose.rotation) {
            a = static_cast<float>(static_cast<std::int16_t>(u16(r))) / 10000.0f;
        }

        e.pose.seq = r.u8();
        e.pose.frame = r.u8();
        e.pose.next_frame = e.pose.frame + 1;
        e.pose.anim_time = static_cast<float>(r.u8()) / 255.0f;
        e.pose.anim_speed = static_cast<float>(r.u8()) / 32.0f;
        const std::uint8_t flags = r.u8();
        e.hidden = (flags & 1) != 0;
        e.loose = (flags & 2) != 0;
        e.pose.level = level;
        out.push_back(e);
    }

    return r.ok();
}

float world_weight(const WorldSight& s) {
    // What is near: what it can see there, what can reach the player.
    const float reach = s.distance > kWorldClose ? kWorldClose / s.distance : 1.0f;
    float weight = reach * reach;
    weight *= s.creature ? 1.5f : 1.0f;
    weight *= s.ahead ? 1.5f : 1.0f;
    weight *= s.hidden ? 0.5f : 1.0f;
    weight = std::max(weight, kWorldBaseMs / kWorldFloorMs);
    // At any distance, what it watches: what it just shot, a bolt it may go for.
    weight *= s.hit ? 3.0f : 1.0f;
    weight *= s.flying ? 2.0f : 1.0f;
    return weight;
}

World::World(Session& session, Game& game, Coop& coop, std::function<std::uint64_t()> clock)
    : session_(session), game_(game), coop_(coop), clock_(std::move(clock)) {
    // Without a clock of the caller's, the monotonic one.
    if (!clock_) {
        clock_ = now_ms;
    }
}

void World::reset() {
    release_holds();
    mobys_.clear();
    alive_keys_.clear();
    dead_keys_.clear();
    listed_ = false;
    shared_.clear();
    members_.clear();
    standing_ = 0;
    driven_.clear();
    poses_.clear();
    posed_at_.clear();
    net_slot_.clear();
    loose_.clear();
    landed_early_.clear();
    world_heard_ = false;
    world_loss_ = 0.0;
    coop_.no_world_ack();
    claimed_.clear();
    flights_.clear();
    flight_eyes_ = false;
    host_killed_.clear();
    placed_class_.clear();
    reported_.clear();
    replays_.clear();
    loot_.clear();
    hidden_.clear();
    host_ms_.clear();
    stamps_.clear();
    party_dead_.clear();
    broke_.clear();
    broke_back_.clear();
    kept_bolts_.clear();
    spared_.clear();
    dug_.clear();
    downs_.clear();
    aims_.clear();
    kills_.clear();
    crate_states_.clear();
    soft_replays_.clear();
    restored_.clear();
    digs_.clear();
    dig_asked_.clear();
    remade_ = false;
    level_own_.clear();
    synced_ = false;
    hello_ms_ = 0;
    placed_driven_ = false;
    despawn_.reset();
    traced_.in_range.clear();
    traced_.driven.clear();
    traced_.sent.clear();
    traced_.received.clear();
    hosting_ = false;
    level_ = -1;
}

std::uint16_t World::id_of(int slot) const {
    const auto m = mobys_.find(slot);

    if (m != mobys_.end() && m->second.key >= 0) {
        return static_cast<std::uint16_t>(m->second.key);
    }

    // A spawned one shared by the host, or made here from the host's.
    if (const auto s = shared_.find(slot); s != shared_.end()) {
        return static_cast<std::uint16_t>(kSpawnedId | s->second.id);
    }

    for (const auto& [net, at] : net_slot_) {
        if (at == slot) {
            return static_cast<std::uint16_t>(kSpawnedId | net);
        }
    }

    for (const auto& [key, at] : restored_) {
        if (at == slot) {
            return static_cast<std::uint16_t>(key);
        }
    }

    return kNoId;
}

int World::slot_of(std::uint16_t id) const {
    if (id == kNoId) {
        return -1;
    }

    if ((id & kSpawnedId) == 0) {
        const auto restored = restored_.find(id);
        return restored != restored_.end() ? restored->second : game_.moby_slot(id);
    }

    const auto net = static_cast<std::uint16_t>(id & ~kSpawnedId);

    if (hosting_) {
        for (const auto& [slot, n] : shared_) {
            if (n.id == net) {
                return slot;
            }
        }

        return -1;
    }

    const auto it = net_slot_.find(net);
    return it != net_slot_.end() ? it->second : -1;
}

void World::follow_load(const GameStatus& status) {
    // New mobys: nothing of the last ones stands (the game's drives end with them too). The same
    // level made again keeps what is dead for the party.
    if (status.loads != loads_ || status.level != level_) {
        const bool again = status.level == level_ && level_ >= 0 && listed_;
        std::set<int> kept;
        std::map<int, std::int32_t> broke;
        std::set<int> broke_back;
        std::vector<KeptBolt> bolts;
        std::set<int> spared;
        std::set<int> dug;

        if (again) {
            kept.swap(party_dead_);
            broke.swap(broke_);
            broke_back.swap(broke_back_);
            spared.swap(spared_);
            dug.swap(dug_);

            for (const auto& [slot, net] : shared_) {
                const auto moby = mobys_.find(slot);
                const auto id = static_cast<std::uint16_t>(kSpawnedId | net.id);

                if (hosting_ && moby != mobys_.end() && bolt_class(net.oclass) && claimed_.count(id) == 0
                    && flights_.count(slot) == 0 && bolts.size() < kKeptBolts) {
                    bolts.push_back({net.oclass, moby->second.pose});
                }
            }
        }

        reset();
        party_dead_ = std::move(kept);
        broke_ = std::move(broke);
        broke_back_ = std::move(broke_back);
        kept_bolts_ = std::move(bolts);
        spared_ = std::move(spared);
        dug_ = std::move(dug);
        remade_ = again;
        loads_ = status.loads;
        level_ = status.level;
    }
}

void World::tick(std::uint64_t now) {
    tick_world(now);

    if (tracing()) {
        trace_tick(now);
    }
}

void World::tick_world(std::uint64_t now) {
    if (!session_.in_party()) {
        revive_rule_ = session_.role().host() && revive_rule_;
        tick_death_hold();

        if (level_ != -1 || !driven_.empty() || !members_.empty()) {
            reset();
        }

        return;
    }

    tick_ms_ = now;
    tick_death_hold();
    const GameStatus status = game_.status();

    follow_load(status);
    hosting_ = session_.role().host() && !coop_.between_hosts() && coop_.playing() && in_level(status.phase);

    // This tick's mobys. A tick the game lists none of (a level loading, a menu that has its own)
    // changes nothing: what is known holds, and nothing is ever taken for dead by its absence.
    std::map<int, MobyState> listed;
    std::set<int> alive;
    std::set<int> dead;
    std::vector<MobyState> all;

    if (in_level(status.phase)) {
        all = game_.mobys();

        if (despawn_on_) {
            tick_despawn(now, all);
        }

        for (MobyState& m : all) {
            m.pose.level = status.level;

            if (m.key >= 0 && (m.flags & kMobyDead) != 0) {
                dead.insert(m.key);
                continue;
            }

            if (m.key >= 0) {
                alive.insert(m.key);
            }

            listed[m.slot] = m;
        }
    }

    if (listed.empty()) {
        log_counts(now);
        return;
    }

    // The placed mobys the game itself says died since the last listing (the only evidence).
    std::vector<std::uint16_t> newly_dead;

    for (const int key : dead) {
        if (dead_keys_.count(key) == 0) {
            newly_dead.push_back(static_cast<std::uint16_t>(key));
        }
    }

    mobys_ = std::move(listed);
    alive_keys_ = std::move(alive);
    dead_keys_ = std::move(dead);

    if (hosting_) {
        party_dead_.insert(dead_keys_.begin(), dead_keys_.end());

        for (const int key : dead_keys_) {
            spared_.erase(key);
        }

        if (remade_) {
            remade();
        }

        take_broke();
    }

    remade_ = false;

    // The party arriving after a ship travel: nothing of the host's world goes out until all are
    // in (coop.hpp), what it learns meanwhile (the level's own dead) goes with the DEAD each member
    // is given then.
    if (hosting_ && coop_.arriving()) {
        log_counts(now);
    } else if (hosting_) {
        tick_host(now, status);

        // Dead for everyone.
        for (std::size_t at = 0; at < newly_dead.size(); at += 500) {
            Writer w;
            w.u8(static_cast<std::uint8_t>(WorldKind::kKilled))
                .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
            const std::size_t n = std::min<std::size_t>(500, newly_dead.size() - at);
            u16(w, static_cast<std::uint16_t>(n));

            std::string keys;

            for (std::size_t i = 0; i < n; ++i) {
                u16(w, newly_dead[at + i]);
                keys += (i != 0 ? " " : "") + std::to_string(newly_dead[at + i]);
            }

            session_.send_data(kBroadcast, w.bytes());
            trace_line("msg", "sent KILLED to all level " + std::to_string(level_) + " n=" + std::to_string(n) + " keys [" + keys + "]");
            counts_.kills += static_cast<int>(n);
        }
    } else if (!session_.role().host() && !coop_.between_hosts()) {
        tick_member(now, status);

        // A crate broken here (the game says so) that the host did not kill: gone in every game. A
        // placed bolt taken here went to the host as a claim (collect) instead. Told once the host
        // has answered for these mobys: what broke before that is told then.
        if (coop_.playing() && status.level == coop_.host_level() && synced_) {
            for (const int key : dead_keys_) {
                const auto cls = placed_class_.find(key);

                if (host_killed_.count(key) == 0 && reported_.count(key) == 0 && cls != placed_class_.end()
                    && !bolt_class(cls->second) && (crate_class(cls->second) || pickup_class(cls->second))) {
                    reported_.insert(key);
                    Writer w;
                    w.u8(static_cast<std::uint8_t>(WorldKind::kBroke))
                        .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
                    u16(w, static_cast<std::uint16_t>(key));
                    session_.send_data(session_.host(), w.bytes());
                    trace_line("msg", "sent BROKE to host key " + std::to_string(key) + " class " + std::to_string(cls->second));
                    ++counts_.kills;
                }
            }
        }

        for (const auto& [slot, moby] : mobys_) {
            if (moby.key >= 0) {
                placed_class_[moby.key] = moby.oclass;
            }
        }
    }

    tick_crates(now);
    tick_digs(now);
    tick_flights(now);

    // A member's game: an eye on each bolt flying to whoever took it, so it runs (flies) wherever
    // the camera is; none once they are done.
    if (!hosting_ && (!flights_.empty() || flight_eyes_)) {
        std::vector<float> eyes;
        add_flight_eyes(eyes);
        game_.eyes(eyes);
        flight_eyes_ = !eyes.empty();
    }

    // A replayed crate that has not broken by its deadline: deleted.
    for (auto it = replays_.begin(); it != replays_.end();) {
        const auto moby = mobys_.find(it->first);

        if (moby == mobys_.end()) {
            it = replays_.erase(it);
        } else if (moby->second.state == kCrateWaiting || (now >= it->second && soft_replays_.count(it->first) != 0)) {
            // One that comes back goes on as its own game has it; a late hit breaks it yet.
            soft_replays_.erase(it->first);
            it = replays_.erase(it);
        } else if (now >= it->second) {
            note(it->first, "a replayed crate's break timed out", 0);
            kill(it->first, "a replayed crate's break timed out");
            it = replays_.erase(it);
        } else {
            ++it;
        }
    }

    listed_ = true;

    log_counts(now);
}

void World::log_counts(std::uint64_t now) {
    // A line in the log now and then of what went on.
    if (now - counts_ms_ >= kWorldLogMs) {
        const Counts& c = counts_;
        if (!mobys_.empty() || c.snapshots + c.hits + c.kills + c.spawns + c.despawns + c.damage + c.bolts > 0) {
            log("world: " + std::to_string(mobys_.size()) + " mobys (" + std::to_string(driven_.size())
                + " driven), " + std::to_string(c.snapshots) + " snapshots, " + std::to_string(c.hits)
                + " hits, " + std::to_string(c.kills) + " kills, " + std::to_string(c.spawns)
                + " spawns, " + std::to_string(c.despawns) + " despawns, " + std::to_string(c.damage) + " damage, " + std::to_string(c.bolts)
                + " bolts");
        }
        counts_ = Counts{};
        counts_ms_ = now;
    }
}

}  // namespace mp
