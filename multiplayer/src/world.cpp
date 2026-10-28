/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The shared world (world.hpp): the host's snapshots, kills, spawns, stand-ins, damage and bolts;
 * the members' driven mobys and their hits.
 */
#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace mp {
namespace {

void u16(Writer& w, std::uint16_t v) {
    w.u8(static_cast<std::uint8_t>(v >> 8)).u8(static_cast<std::uint8_t>(v));
}

std::uint16_t u16(Reader& r) {
    const auto high = static_cast<std::uint16_t>(r.u8() << 8);
    return static_cast<std::uint16_t>(high | r.u8());
}

void f32(Writer& w, float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    w.i32(static_cast<std::int32_t>(u));
}

float f32(Reader& r) {
    const auto u = static_cast<std::uint32_t>(r.i32());
    float f = 0;
    std::memcpy(&f, &u, sizeof f);
    return std::isfinite(f) ? f : 0.0f;
}

void vec(Writer& w, const float* v) {
    for (int i = 0; i < 3; ++i) {
        f32(w, v[i]);
    }
}

void vec(Reader& r, float* v) {
    for (int i = 0; i < 3; ++i) {
        v[i] = f32(r);
    }
}

std::int16_t angle_bits(float radians) {
    return static_cast<std::int16_t>(std::clamp(std::lround(radians * 10000.0f), -32768L, 32767L));
}

float distance2(const float* a, const float* b) {
    float sum = 0;
    for (int i = 0; i < 3; ++i) {
        sum += (a[i] - b[i]) * (a[i] - b[i]);
    }
    return sum;
}

bool changed(const Pose& a, const Pose& b) {
    return distance2(a.position, b.position) > 0.0004f
           || std::fabs(a.rotation[2] - b.rotation[2]) > 0.01f
           || std::fabs(a.rotation[0] - b.rotation[0]) > 0.01f
           || std::fabs(a.rotation[1] - b.rotation[1]) > 0.01f || a.seq != b.seq || a.frame != b.frame;
}

}  // namespace

Bytes encode_world(int level, std::int64_t stamp, const std::vector<WorldEntry>& entries) {
    const std::size_t count = std::min(entries.size(), kWorldEntries);
    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kWorld))
        .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level)))
        .i64(stamp)
        .u8(static_cast<std::uint8_t>(count));

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
            .u8(e.hidden ? 1 : 0);
    }

    return w.bytes();
}

bool decode_world(const Bytes& bytes, int& level, std::int64_t& stamp, std::vector<WorldEntry>& out) {
    if (bytes.empty() || bytes[0] != static_cast<std::uint8_t>(WorldKind::kWorld)) {
        return false;
    }

    Reader r(bytes.data() + 1, bytes.size() - 1);
    level = static_cast<std::int8_t>(r.u8());
    stamp = r.i64();
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
        e.hidden = (r.u8() & 1) != 0;
        e.pose.level = level;
        out.push_back(e);
    }

    return r.ok();
}

World::World(Session& session, Game& game, Coop& coop, std::function<std::uint64_t()> clock)
    : session_(session), game_(game), coop_(coop), clock_(std::move(clock)) {
    // Without a clock of the caller's, the monotonic one.
    if (!clock_) {
        clock_ = now_ms;
    }
}

void World::reset() {
    mobys_.clear();
    alive_keys_.clear();
    dead_keys_.clear();
    listed_ = false;
    shared_.clear();
    members_.clear();
    standing_ = 0;
    driven_.clear();
    poses_.clear();
    net_slot_.clear();
    host_killed_.clear();
    placed_class_.clear();
    replays_.clear();
    loot_.clear();
    hidden_.clear();
    host_ms_.clear();
    despawn_.reset();
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

    return kNoId;
}

int World::slot_of(std::uint16_t id) const {
    if (id == kNoId) {
        return -1;
    }

    if ((id & kSpawnedId) == 0) {
        return game_.moby_slot(id);
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

void World::tick(std::uint64_t now) {
    if (!session_.in_party()) {
        if (level_ != -1 || !driven_.empty() || !members_.empty()) {
            reset();
        }

        return;
    }

    const GameStatus status = game_.status();

    // A new level load: nothing of the last one stands (the game's drives end with it too).
    if (status.loads != loads_ || status.level != level_) {
        reset();
        loads_ = status.loads;
        level_ = status.level;
    }

    hosting_ = session_.is_host() && coop_.playing() && in_level(status.phase);

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

            for (std::size_t i = 0; i < n; ++i) {
                u16(w, newly_dead[at + i]);
            }

            session_.send_data(kBroadcast, w.bytes());
            counts_.kills += static_cast<int>(n);
        }
    } else if (!session_.is_host()) {
        tick_member(now, status);

        // A crate broken here (the game says so) that the host did not kill: gone in every game.
        if (coop_.playing() && status.level == coop_.host_level()) {
            for (const std::uint16_t key : newly_dead) {
                const auto cls = placed_class_.find(key);

                if (host_killed_.count(key) == 0 && cls != placed_class_.end()
                    && (crate_class(cls->second) || pickup_class(cls->second))) {
                    Writer w;
                    w.u8(static_cast<std::uint8_t>(WorldKind::kBroke))
                        .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
                    u16(w, key);
                    session_.send_data(session_.host(), w.bytes());
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

    // A replayed crate that has not broken by its deadline: deleted.
    for (auto it = replays_.begin(); it != replays_.end();) {
        const auto moby = mobys_.find(it->first);

        if (moby == mobys_.end()) {
            it = replays_.erase(it);
        } else if (now >= it->second) {
            note(it->first, "a replayed crate's break timed out", 0);
            game_.moby_kill(it->first);
            it = replays_.erase(it);
        } else {
            ++it;
        }
    }

    listed_ = true;

    log_counts(now);
}

bool World::quiet_death(int slot) {
    return replays_.count(slot) != 0;
}

void World::replay_break(int slot, std::uint64_t now) {
    const auto it = mobys_.find(slot);

    if (it == mobys_.end() || replays_.count(slot) != 0) {
        return;
    }

    Hit h;
    h.victim = slot;
    h.attacker = game_.figure_slot(-1);
    // A metal crate takes only an explosion's hit; the others a wrench's.
    h.mask = it->second.oclass == 502 ? 0x20000u : 0x10000u;
    h.damage = 1.0f;
    std::copy(it->second.pose.position, it->second.pose.position + 3, h.position);
    game_.hit(h);
    replays_[slot] = now + kReplayMs;
}

void World::log_counts(std::uint64_t now) {
    // A line in the log now and then of what went on.
    if (now - counts_ms_ >= kWorldLogMs) {
        const Counts& c = counts_;
        if (!mobys_.empty() || c.snapshots + c.hits + c.kills + c.spawns + c.damage + c.bolts > 0) {
            log("world: " + std::to_string(mobys_.size()) + " mobys (" + std::to_string(driven_.size())
                + " driven), " + std::to_string(c.snapshots) + " snapshots, " + std::to_string(c.hits)
                + " hits, " + std::to_string(c.kills) + " kills, " + std::to_string(c.spawns)
                + " spawns, " + std::to_string(c.damage) + " damage, " + std::to_string(c.bolts)
                + " bolts");
        }
        counts_ = Counts{};
        counts_ms_ = now;
    }
}

void World::send_spawn(std::int32_t target, int level, std::uint16_t net, const MobyState& moby) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kSpawn)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level)));
    u16(w, net);
    u16(w, static_cast<std::uint16_t>(static_cast<std::int16_t>(moby.oclass)));
    vec(w, moby.pose.position);
    u16(w, static_cast<std::uint16_t>(angle_bits(moby.pose.rotation[2])));
    session_.send_data(target, w.bytes());
}

void World::send_dead(std::int32_t member, int level) {
    const int records = dead_keys_.empty() ? 0 : *dead_keys_.rbegin() + 1;
    Writer w;
    w.u8(static_cast<std::uint8_t>(WorldKind::kDead)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level)));
    u16(w, static_cast<std::uint16_t>(records));
    std::vector<std::uint8_t> bits(static_cast<std::size_t>((records + 7) / 8), 0);

    for (const int key : dead_keys_) {
        bits[static_cast<std::size_t>(key / 8)] |= static_cast<std::uint8_t>(1u << (key % 8));
    }

    w.raw(bits);
    session_.send_data(member, w.bytes());
}

void World::send_world(std::int32_t member, Member& m, std::uint64_t now, const Pose& at) {
    struct Pick {
        float d2;
        int slot;
        std::uint16_t id;
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
        const bool fresh = sent == m.sent.end() || changed(sent->second.pose, moby.pose)
                           || sent->second.hidden != hidden
                           || (now - sent->second.ms >= kWorldRefreshMs && d2 <= kWorldNear * kWorldNear);

        if (fresh) {
            picks.push_back({d2, slot, id});
        }
    }

    std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.d2 < b.d2; });
    std::vector<WorldEntry> entries;

    for (const Pick& p : picks) {
        if (entries.size() >= kWorldEntries) {
            break;
        }

        const MobyState& moby = mobys_[p.slot];
        const bool hidden = (moby.flags & kMobyHidden) != 0;
        entries.push_back({p.id, moby.pose, hidden});
        m.sent[p.id] = {moby.pose, hidden, now};
    }

    m.world_ms = now;

    if (!entries.empty()) {
        const std::int64_t stamp =
            session_.has_clock() ? session_.server_ms() : static_cast<std::int64_t>(now);
        session_.send_state(member, encode_world(level_, stamp, entries));
        ++counts_.snapshots;
    }
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

        seen.insert(slot);
        const auto it = shared_.find(slot);

        // A slot made again for another class is a new moby.
        if (it != shared_.end() && it->second.oclass == moby.oclass) {
            continue;
        }

        const std::uint16_t net = next_net_;
        next_net_ = static_cast<std::uint16_t>(next_net_ % 0x7FFE + 1);
        shared_[slot] = {net, moby.oclass};

        for (auto& [id, member] : members_) {
            if (member.level == level_) {
                send_spawn(id, level_, net, moby);
            }
        }

        ++counts_.spawns;
    }

    std::vector<std::uint16_t> gone;

    for (auto it = shared_.begin(); it != shared_.end();) {
        if (seen.count(it->first) == 0) {
            gone.push_back(it->second.id);
            it = shared_.erase(it);
        } else {
            ++it;
        }
    }

    if (!gone.empty()) {
        Writer w;
        w.u8(static_cast<std::uint8_t>(WorldKind::kDespawn)).u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(level_)));
        u16(w, static_cast<std::uint16_t>(gone.size()));

        for (const std::uint16_t net : gone) {
            u16(w, net);
        }

        session_.send_data(kBroadcast, w.bytes());
    }

    // The members: where each is, what it is told, the eyes on them.
    std::vector<float> eyes;

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

        // Elsewhere (or nothing heard): told again when it comes.
        if (!coop_.latest(who.id, at) || at.level != status.level) {
            m.level = at.level;
            m.told_load = 0;
            continue;
        }

        m.level = at.level;

        if (m.told_load != loads_) {
            m.told_load = loads_;
            m.sent.clear();
            send_dead(who.id, level_);

            for (const auto& [slot, net] : shared_) {
                send_spawn(who.id, level_, net.id, mobys_[slot]);
            }

            log("player " + std::to_string(who.id) + " is given the world of level " + std::to_string(level_));
        }

        if (eyes.size() < 3 * 8) {
            eyes.insert(eyes.end(), at.position, at.position + 3);
        }

        if (now - m.world_ms >= kWorldMs) {
            send_world(who.id, m, now, at);
        }

        if (m.bolts > 0) {
            session_.send_data(who.id, Writer().u8(static_cast<std::uint8_t>(WorldKind::kBolts)).i32(m.bolts).bytes());
            counts_.bolts += m.bolts;
            m.bolts = 0;
        }
    }

    game_.eyes(eyes);
}

void World::tick_member(std::uint64_t now, const GameStatus& status) {
    // Only in the host's game and level: the world is the host's there.
    if (!coop_.playing() || !in_level(status.phase) || status.level != coop_.host_level()) {
        return;
    }

    // Each placed moby that runs an update of its own is driven, once a level load.
    if (driven_.empty()) {
        for (const auto& [slot, moby] : mobys_) {
            if (moby.key >= 0 && (moby.flags & kMobyUpdates) != 0 && moby.oclass != 0
                && !local_class(moby.oclass) && game_.moby_drive(slot, true)) {
                driven_.insert(slot);
            }
        }

        log("driving " + std::to_string(driven_.size()) + " mobys of level " + std::to_string(level_)
            + " from the host");
    }

    // Each driven moby where the host's snapshots put it, a delay behind.
    const auto t = static_cast<double>(now);

    for (auto& [id, poses] : poses_) {
        const int slot = slot_of(id);
        Pose pose;

        if (slot >= 0 && poses.at(t, pose)) {
            game_.moby_pose(slot, pose);
        }
    }

    // Each driven moby hidden or shown as the host's is (its own update, which would, does not
    // run here).
    for (const auto& [id, hidden] : hidden_) {
        const int slot = slot_of(id);
        const auto moby = mobys_.find(slot);

        if (moby == mobys_.end() || driven_.count(slot) == 0
            || ((moby->second.flags & kMobyHidden) != 0) == hidden) {
            continue;
        }

        if (game_.moby_hide(slot, hidden)) {
            note(slot, hidden ? "hidden as the host's is" : "shown as the host's is", session_.host());
        }
    }
}

void World::tick_despawn(std::uint64_t now, const std::vector<MobyState>& listing) {
    DespawnLog::View view;
    Pose hero;

    if (game_.hero(hero)) {
        view.hero = true;
        std::copy(hero.position, hero.position + 3, view.hero_at);
    }

    view.camera = game_.camera(view.camera_at);

    if (!despawn_.out) {
        despawn_.out = [this](const std::string& line) { log(line); };
        despawn_.deletion = [this](int slot, MobyDeletion& d) {
            return game_.moby_deletion(slot, d.by, d.by_class, d.by_mod);
        };
        despawn_.role = [this](int slot, const MobyState& m) -> std::string {
            if (hosting_) {
                const bool placed_runs = m.key >= 0 && (m.flags & kMobyUpdates) != 0 && m.oclass != 0
                                         && !local_class(m.oclass);
                return placed_runs || shared_.count(slot) != 0 ? "shared" : "local";
            }

            return driven_.count(slot) != 0 ? "driven" : "local";
        };
        despawn_.host_seen = [this](int slot, const MobyState&) -> std::string {
            if (hosting_ || session_.is_host()) {
                return "";
            }

            const std::uint16_t id = id_of(slot);
            const auto seen = id != kNoId ? host_ms_.find(id) : host_ms_.end();

            if (seen == host_ms_.end()) {
                return "host never sent it";
            }

            const std::uint64_t ago = clock_() - seen->second;
            return "host sent it " + std::to_string(ago / 1000) + "." + std::to_string(ago % 1000 / 100)
                   + " s ago" + (hidden_.count(id) != 0 && hidden_.at(id) ? " hidden" : "");
        };
    }

    despawn_.tick(now, listing, view);
}

void World::handle(std::int32_t sender, const Bytes& bytes) {
    if (bytes.empty() || !session_.in_party()) {
        return;
    }

    const auto kind = static_cast<WorldKind>(bytes[0]);
    const bool from_host = sender == session_.host() && !session_.is_host();
    Reader r(bytes.data() + 1, bytes.size() - 1);

    // A member broke a crate: gone here, and so in every game (KILLED at the next tick).
    if (kind == WorldKind::kBroke) {
        const int level = static_cast<std::int8_t>(r.u8());
        const std::uint16_t key = u16(r);
        const int slot = r.ok() && hosting_ && level == level_ ? game_.moby_slot(key) : -1;
        const auto it = mobys_.find(slot);

        // A crate breaks here as it broke there; a pickup taken there is gone here.
        if (slot >= 0 && it != mobys_.end() && crate_class(it->second.oclass)) {
            note(slot, "BROKE, a crate broken as there", sender);
            replay_break(slot, clock_());
        } else if (slot >= 0 && it != mobys_.end() && pickup_class(it->second.oclass)) {
            note(slot, "BROKE, a pickup taken there", sender);
            game_.moby_kill(slot);
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
        return;
    }

    if (!from_host) {
        return;
    }

    switch (kind) {
        case WorldKind::kWorld: {
            int level = -1;
            std::int64_t stamp = 0;
            std::vector<WorldEntry> entries;

            if (!decode_world(bytes, level, stamp, entries) || level != level_) {
                return;
            }

            const auto now = static_cast<double>(clock_());
            const double time =
                session_.has_clock() ? static_cast<double>(stamp) - session_.clock_offset_ms() : now;
            const auto sequence = static_cast<std::uint16_t>(stamp >> 4);

            for (const WorldEntry& e : entries) {
                poses_[e.id].add(e.pose, sequence, time, now, false);
                hidden_[e.id] = e.hidden;
                host_ms_[e.id] = clock_();
            }

            ++counts_.snapshots;
            break;
        }
        case WorldKind::kKilled: {
            const int level = static_cast<std::int8_t>(r.u8());
            const std::uint16_t count = u16(r);

            for (std::uint16_t i = 0; i < count && r.ok(); ++i) {
                const std::uint16_t key = u16(r);
                const int slot = level == level_ ? game_.moby_slot(key) : -1;
                const auto moby = mobys_.find(slot);

                // A crate breaks here as it broke there; anything else goes.
                if (slot >= 0 && moby != mobys_.end() && crate_class(moby->second.oclass)) {
                    note(slot, "KILLED, a crate broken as there", sender);
                    replay_break(slot, clock_());
                    ++counts_.kills;
                } else if (slot >= 0) {
                    note(slot, "KILLED", sender);
                    game_.moby_kill(slot);
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

            // Loot (ammo, health) an enemy dropped there is this game's own to collect.
            if (slot >= 0 && loot_class(oclass)) {
                game_.moby_drive(slot, false);
                loot_.insert(net);
                ++counts_.spawns;
            } else if (slot >= 0) {
                net_slot_[net] = slot;
                driven_.insert(slot);
                ++counts_.spawns;
            }

            break;
        }
        case WorldKind::kDespawn: {
            const int level = static_cast<std::int8_t>(r.u8());
            const std::uint16_t count = u16(r);

            for (std::uint16_t i = 0; i < count && r.ok(); ++i) {
                const std::uint16_t net = u16(r);
                const auto it = net_slot_.find(net);

                // Loot stays until this game's player collects it.
                if (loot_.erase(net) != 0) {
                    continue;
                }

                if (level == level_ && it != net_slot_.end()) {
                    note(it->second, "DESPAWN", sender);
                    game_.moby_kill(it->second);
                    driven_.erase(it->second);
                    net_slot_.erase(it);
                }

                poses_.erase(static_cast<std::uint16_t>(kSpawnedId | net));
            }

            break;
        }
        case WorldKind::kDead: {
            const int level = static_cast<std::int8_t>(r.u8());
            const std::uint16_t records = u16(r);
            const Bytes bits = r.rest();

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

                if (dead && game_.moby_kill(slot)) {
                    driven_.erase(slot);
                    host_killed_.insert(moby.key);
                    ++killed;
                }
            }

            log("the host's world: " + std::to_string(killed) + " placed mobys dead here too");
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

            if (r.ok() && h.victim >= 0) {
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
            }

            break;
        }
        default:
            break;
    }
}

bool World::gate(int slot, int phase) {
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

    // The hero's own class (figures) and the crates (each game's) keep the hero as he is.
    if (it == mobys_.end() || it->second.oclass == 0 || local_class(it->second.oclass)) {
        return false;
    }

    // The nearest player to it: the host, or a member in this level.
    Pose hero;
    float best = game_.hero(hero) ? distance2(hero.position, it->second.pose.position) : 1e30f;
    std::int32_t nearest = 0;
    Pose at;

    for (const auto& [id, member] : members_) {
        Pose p;

        if (member.level == level_ && coop_.latest(id, p) && p.health > 0) {
            const float d2 = distance2(p.position, it->second.pose.position);

            if (d2 < best) {
                best = d2;
                nearest = id;
                at = p;
            }
        }
    }

    if (nearest != 0 && game_.stand_in(at.position, coop_.figure_of(nearest))) {
        standing_ = nearest;
        bolts_before_ = game_.bolts(0);
    }

    return false;
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
