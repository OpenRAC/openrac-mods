/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The shared world (world.hpp): the messages received.
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

void World::land(std::uint16_t net, int slot) {
    const auto id = static_cast<std::uint16_t>(kSpawnedId | net);
    game_.moby_drive(slot, false);
    driven_.erase(slot);
    loose_.insert(net);
    poses_.erase(id);
    hidden_.erase(id);
    trace_line("bolts", "host's bolt net " + std::to_string(net) + " landed: runs here, " + moby_name(slot));
}

void World::hear_world(std::uint16_t sequence) {
    if (!world_heard_) {
        world_heard_ = true;
        world_newest_ = sequence;
        world_bits_ = 0;
    } else if (sequence_newer(sequence, world_newest_)) {
        const int step = static_cast<std::uint16_t>(sequence - world_newest_);
        // The one that was newest is now bit step - 1, and the older ones move up with it.
        const std::uint32_t moved = step >= 32 ? 0 : world_bits_ << step;
        world_bits_ = step > 32 ? 0 : moved | 1u << (step - 1);
        world_newest_ = sequence;
        const double missed = std::min(step - 1, 4);
        world_loss_ += 0.05 * (missed / (missed + 1.0) - world_loss_);
    } else if (const int back = static_cast<std::uint16_t>(world_newest_ - sequence); back >= 1 && back <= 32) {
        world_bits_ |= 1u << (back - 1);
    }

    coop_.world_ack(world_newest_, world_bits_);
}

void World::handle(std::int32_t sender, const Bytes& bytes) {
    if (bytes.empty() || !session_.in_party()) {
        return;
    }

    // Messages are read ahead of the tick: a respawn since the last one has taken every slot known
    // (one still held would be a dead moby's, past the array's end, and deleting it brings the
    // game down).
    follow_load(game_.status());

    const auto kind = static_cast<WorldKind>(bytes[0]);
    const bool from_host = sender == session_.host() && !session_.is_host();
    Reader r(bytes.data() + 1, bytes.size() - 1);

    // A member broke a crate: gone here, and so in every game (KILLED at the next tick).
    if (kind == WorldKind::kBroke) {
        const int level = static_cast<std::int8_t>(r.u8());
        const std::uint16_t key = u16(r);
        trace_line("msg", "received BROKE from " + std::to_string(sender) + " level " + std::to_string(level) + " key "
                              + std::to_string(key));

        // Held for the next listing: the reliable stream's word may come as this game's level is
        // being made again, with no mobys known, and is the only word there will be.
        if (r.ok() && session_.is_host() && level == level_) {
            broke_[key] = sender;
        }

        return;
    }

    // A member collected a bolt in its game: the host decides whose it is.
    if (kind == WorldKind::kClaim) {
        const int level = static_cast<std::int8_t>(r.u8());
        const std::uint16_t id = u16(r);
        trace_line("msg", "received CLAIM from " + std::to_string(sender) + " level " + std::to_string(level) + " id "
                              + std::to_string(id));

        if (r.ok() && session_.is_host()) {
            claim(sender, level, id);
        }

        return;
    }

    // A member with new mobys: the world as it stands, at the next tick that has it.
    if (kind == WorldKind::kHello) {
        const int level = static_cast<std::int8_t>(r.u8());
        const auto load = static_cast<std::uint32_t>(r.i32());
        const auto digest = static_cast<std::uint32_t>(r.left() >= 4 ? r.i32() : 0);
        trace_line("msg", "received HELLO from " + std::to_string(sender) + " level " + std::to_string(level) + " load "
                              + std::to_string(load));

        if (r.ok() && hosting_ && level == level_) {
            // A level that does not agree with the member's gets no world: told, and it leaves.
            const std::uint32_t own = game_.sim_digest(level_);

            if (own != 0 && digest != 0 && own != digest) {
                log("player " + std::to_string(sender) + "'s level " + std::to_string(level)
                    + " does not agree with this game's: refused");
                members_.erase(sender);
                Writer w;
                w.u8(static_cast<std::uint8_t>(WorldKind::kReset)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level)));
                w.u8(1);
                session_.send_data(sender, w.bytes());
                return;
            }

            Member& m = members_[sender];
            m.want = true;
            m.load = load;
            m.synced = false;
        }

        return;
    }

    // The member's word: a hit on the host's world.
    if (kind == WorldKind::kHit) {
        const std::uint16_t id = u16(r);
        Hit h;
        h.mask = static_cast<std::uint32_t>(r.i32());
        h.damage = f32(r);
        vec(r, h.position);
        vec(r, h.velocity);
        h.victim = slot_of(id);

        if (!hosting_ || !r.ok() || h.victim < 0) {
            return;
        }

        // From that player's figure, the hero's class (the enemies read the attacker's class).
        const int figure = coop_.figure_of(sender);
        h.attacker = figure >= 0 ? game_.figure_slot(figure) : game_.figure_slot(-1);
        game_.hit(h);
        ++counts_.hits;
        members_[sender].hit_ms[id] = clock_();
        return;
    }

    if (!from_host) {
        return;
    }

    switch (kind) {
        case WorldKind::kWorld: {
            int level = -1;
            std::int64_t stamp = 0;
            std::uint16_t heard = 0;
            std::vector<WorldEntry> entries;

            if (!decode_world(bytes, level, stamp, entries, &heard) || level != level_) {
                return;
            }

            // Said back with the next pose: only what was taken, so a datagram of another level
            // is one the host will send again.
            hear_world(heard);

            const auto now = static_cast<double>(clock_());
            const double time =
                session_.has_clock() ? static_cast<double>(stamp) - session_.clock_offset_ms() : now;
            const auto sequence = static_cast<std::uint16_t>(stamp >> 4);

            if (tracing()) {
                char text[160];
                std::snprintf(text, sizeof text, "received stamp %lld local %.0f arrival %.0f transit %.0f n=%zu",
                              static_cast<long long>(stamp), time, now, now - time, entries.size());
                trace_line("snap", text);
            }

            for (const WorldEntry& e : entries) {
                const auto net = static_cast<std::uint16_t>(e.id & ~kSpawnedId);
                const bool spawned = (e.id & kSpawnedId) != 0;

                // A datagram overtaken by a newer one has nothing to say of this moby.
                if (const auto last = stamps_.find(e.id); last != stamps_.end() && last->second > stamp) {
                    continue;
                }

                stamps_[e.id] = stamp;

                // A host's bolt run here already, and one landed there: no longer posed from there
                // (it hovers and flies to this game's Ratchet, who claims it).
                if (spawned && loose_.count(net) != 0) {
                    continue;
                }

                if (spawned && e.loose) {
                    const auto it = net_slot_.find(net);

                    // Its SPAWN is on the stream, and may still be on its way: this word stays
                    // heard (and the host knows it was) for when it comes.
                    if (it != net_slot_.end()) {
                        land(net, it->second);
                    } else {
                        landed_early_.insert(net);
                    }

                    continue;
                }

                // WORLD has no velocities: the buffer makes them (play.hpp).
                Interpolator& buffer = poses_.try_emplace(e.id, true).first->second;
                buffer.set_loss(world_loss_);
                buffer.add(e.pose, sequence, time, now, false);
                hidden_[e.id] = e.hidden;
                host_ms_[e.id] = clock_();
            }

            if (tracing()) {
                std::set<std::uint16_t> ids;
                std::string hidden;

                for (const WorldEntry& e : entries) {
                    ids.insert(e.id);
                    hidden += e.hidden ? " " + std::to_string(e.id) : "";
                }

                trace_snapshot("received from " + std::to_string(sender), traced_.received, ids);

                if (!hidden.empty()) {
                    trace_line("snap", "received hidden ids [" + hidden.substr(1) + "]");
                }
            }

            ++counts_.snapshots;
            break;
        }
        case WorldKind::kKilled: {
            const int level = static_cast<std::int8_t>(r.u8());
            const std::uint16_t count = u16(r);
            trace_line("msg", "received KILLED from " + std::to_string(sender) + " level " + std::to_string(level) + " n="
                                  + std::to_string(count));

            for (std::uint16_t i = 0; i < count && r.ok(); ++i) {
                const std::uint16_t key = u16(r);
                const int slot = level == level_ ? game_.moby_slot(key) : -1;
                trace_line("msg", "received KILLED key " + std::to_string(key) + " slot " + std::to_string(slot));
                const auto moby = mobys_.find(slot);

                // A crate breaks here as it broke there; anything else goes.
                if (slot >= 0 && moby != mobys_.end() && crate_class(moby->second.oclass)) {
                    note(slot, "KILLED, a crate broken as there", sender);
                    replay_break(slot, clock_());
                    ++counts_.kills;
                } else if (slot >= 0 && flights_.count(slot) != 0) {
                    // A placed bolt taken there, flying to whoever took it: it goes on arrival.
                    trace_line("bolts", "KILLED of " + moby_name(slot) + " left to its flight");
                    ++counts_.kills;
                } else if (slot >= 0) {
                    note(slot, "KILLED", sender);
                    kill(slot, "KILLED from " + std::to_string(sender));
                    driven_.erase(slot);
                    ++counts_.kills;
                }

                host_killed_.insert(key);

                poses_.erase(key);
            }

            break;
        }
        case WorldKind::kSpawn: {
            const int level = static_cast<std::int8_t>(r.u8());
            const std::uint16_t net = u16(r);
            const auto oclass = static_cast<std::int16_t>(u16(r));
            Pose pose;
            vec(r, pose.position);
            pose.rotation[2] = static_cast<float>(static_cast<std::int16_t>(u16(r))) / 10000.0f;

            if (!r.ok() || level != level_ || net_slot_.count(net) != 0) {
                return;
            }

            const int slot = game_.moby_spawn(oclass, pose);
            trace_line("msg", "received SPAWN from " + std::to_string(sender) + " net " + std::to_string(net) + " class "
                                  + std::to_string(oclass) + " at " + xyz(pose.position) + " made slot " + std::to_string(slot)
                                  + (loot_class(oclass) ? " (loot, this game's own)" : ""));

            // Loot (ammo, health) an enemy dropped there is this game's own to collect.
            if (slot >= 0 && loot_class(oclass)) {
                game_.moby_drive(slot, false);
                loot_.insert(net);
                ++counts_.spawns;
            } else if (slot >= 0) {
                net_slot_[net] = slot;
                driven_.insert(slot);
                ++counts_.spawns;

                if (landed_early_.erase(net) != 0) {
                    land(net, slot);
                }
            }

            break;
        }
        case WorldKind::kDespawn: {
            const int level = static_cast<std::int8_t>(r.u8());
            const std::uint16_t count = u16(r);

            for (std::uint16_t i = 0; i < count && r.ok(); ++i) {
                const std::uint16_t net = u16(r);
                const auto it = net_slot_.find(net);
                trace_line("msg", "received DESPAWN from " + std::to_string(sender) + " level " + std::to_string(level) + " net "
                                      + std::to_string(net) + " slot " + std::to_string(it != net_slot_.end() ? it->second : -1)
                                      + (loot_.count(net) != 0 ? " (loot: kept)" : ""));

                // Loot stays until this game's player collects it.
                if (loot_.erase(net) != 0) {
                    continue;
                }

                loose_.erase(net);
                landed_early_.erase(net);
                ++counts_.despawns;

                if (level == level_ && it != net_slot_.end() && flights_.count(it->second) != 0) {
                    // Taken there, flying to whoever took it: it goes on arrival.
                    trace_line("bolts", "DESPAWN of " + moby_name(it->second) + " left to its flight");
                    driven_.erase(it->second);
                    net_slot_.erase(it);
                } else if (level == level_ && it != net_slot_.end()) {
                    note(it->second, "DESPAWN", sender);
                    kill(it->second, "DESPAWN from " + std::to_string(sender));
                    driven_.erase(it->second);
                    net_slot_.erase(it);
                }

                poses_.erase(static_cast<std::uint16_t>(kSpawnedId | net));
            }

            break;
        }
        case WorldKind::kTaken: {
            const int level = static_cast<std::int8_t>(r.u8());
            const std::uint16_t id = u16(r);
            const std::int32_t player = r.i32();

            if (!r.ok() || level != level_) {
                return;
            }

            // A bolt that runs here by itself (one of the host's landed here, or a placed one):
            // it flies to whoever took it. One the host's poses still drive shows the host's way.
            int slot = -1;

            if ((id & kSpawnedId) != 0) {
                const auto net = static_cast<std::uint16_t>(id & ~kSpawnedId);
                const auto it = net_slot_.find(net);
                slot = it != net_slot_.end() && loose_.count(net) != 0 ? it->second : -1;
            } else {
                slot = game_.moby_slot(id);
            }

            trace_line("msg", "received TAKEN from " + std::to_string(sender) + " id " + std::to_string(id) + " by "
                                  + std::to_string(player) + " slot " + std::to_string(slot));

            if (slot >= 0 && driven_.count(slot) == 0 && flights_.count(slot) == 0) {
                fly(slot, id, player, clock_());
            }

            break;
        }
        case WorldKind::kDead: {
            const int level = static_cast<std::int8_t>(r.u8());
            const std::uint16_t records = u16(r);
            const Bytes bits = r.rest();

            trace_line("msg", "received DEAD from " + std::to_string(sender) + " level " + std::to_string(level) + " records "
                                  + std::to_string(records));

            if (level != level_ || bits.size() < static_cast<std::size_t>((records + 7) / 8)) {
                return;
            }

            // A placed moby dead in the host's game (killed there before this game came): dead
            // here too.
            int killed = 0;

            for (const auto& [slot, moby] : mobys_) {
                const bool dead = moby.key >= 0 && moby.key < records
                                  && (bits[static_cast<std::size_t>(moby.key / 8)] >> (moby.key % 8)) & 1;

                if (dead) {
                    note(slot, "DEAD", sender);
                }

                if (dead && kill(slot, "DEAD from " + std::to_string(sender))) {
                    driven_.erase(slot);
                    host_killed_.insert(moby.key);
                    ++killed;
                }
            }

            log("the host's world: " + std::to_string(killed) + " placed mobys dead here too");
            break;
        }
        case WorldKind::kSynced: {
            const int level = static_cast<std::int8_t>(r.u8());
            const auto load = static_cast<std::uint32_t>(r.i32());

            if (r.ok() && level == level_ && load == loads_) {
                synced_ = true;
                log("the host's world of level " + std::to_string(level) + " is here");
            }

            break;
        }
        case WorldKind::kReset: {
            const int level = static_cast<std::int8_t>(r.u8());
            const bool differs = r.left() > 0 && r.u8() != 0;

            if (r.ok() && level == level_ && differs) {
                log("the host's level " + std::to_string(level) + " does not agree with this game's: leaving");
                level_differs_ = true;
            } else if (r.ok() && level == level_) {
                log("the host's level was made again: asking for its world");
                drop_spawned();
                synced_ = false;
                hello_ms_ = 0;
            }

            break;
        }
        case WorldKind::kDamage: {
            Hit h;
            h.attacker = slot_of(u16(r));
            h.mask = static_cast<std::uint32_t>(r.i32());
            h.damage = f32(r);
            vec(r, h.position);
            vec(r, h.velocity);
            h.victim = game_.figure_slot(-1);

            // A joiner not in yet, or just put beside the host: it comes to no harm.
            if (r.ok() && h.victim >= 0 && !coop_.shielded()) {
                game_.hit(h);
                ++counts_.damage;
            }

            break;
        }
        case WorldKind::kBolts: {
            const std::int32_t n = r.i32();

            if (r.ok() && n > 0 && n < 1000000) {
                game_.bolts(n);
                counts_.bolts += n;
                trace_line("bolts", "BOLTS credit +" + std::to_string(n) + " from the host");
            }

            break;
        }
        default:
            break;
    }
}

}  // namespace mp
