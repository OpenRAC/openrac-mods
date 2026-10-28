/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The shared world (world.cpp): the host's game runs the level (its enemies, crates, doors,
 * platforms, bolts) and the members' games show it. No socket and no game here: given the Session,
 * the Game (the SDK's 1.9 world calls) and the Coop (the players' poses and figures), on the game's
 * thread, once a tick, plus the game's gate and hits as they come.
 *
 * Host. Each moby the level placed is named by its key (its record in the level's file, the same in
 * every game). The host sends each member in its level, as state 15 times a second, the mobys
 * nearest that member that moved or animated (WORLD, at most kWorldEntries a datagram), tells
 * everyone of the placed ones that died (KILLED) and of the ones it spawns that count (creatures,
 * bolts: SPAWN and DESPAWN, by a net id of its own), and gives a member that arrives in its level
 * the placed mobys still alive there (ALIVE) and what it has spawned. Every moby's update runs with
 * the nearest player standing in for the hero (the gate): an enemy chases and hits that player's
 * figure, a bolt flies to it; a hit on a figure goes to its player (DAMAGE), the bolts a member's
 * stand-in collected go to it (BOLTS). Mobys near the members run as if near the host (eyes).
 *
 * Member. In the host's level it drives every placed moby that has an update of its own (the game
 * no longer runs it) and poses it from WORLD through a buffer per moby (play.hpp's, a delay
 * behind), kills what the host killed, makes what the host spawned (driven too). A hit its game
 * posts to a driven moby goes to the host (HIT) instead of the moby; a DAMAGE is posted to its own
 * hero (who is hurt as the game hurts him); BOLTS are added to its own.
 *
 * The world's DATA kinds, after coop.hpp's:
 *    9 WORLD   level i8, stamp i64, count u8, then per moby: id u16 (a key; 0x8000 | a net id for
 *              one spawned), position 3 x f32, rotation 3 x i16 (1/10,000 rad), seq u8, frame u8,
 *              anim time u8 (/255), anim speed u8 (/32)            the host to a member, as state;
 *   10 KILLED  level i8, count u16, keys u16...                   the host to everyone;
 *   11 SPAWN   level i8, net u16, class i16, position 3 x f32, yaw i16     the host;
 *   12 DESPAWN level i8, count u16, nets u16...                   the host;
 *   13 HIT     id u16, mask u32, damage f32, position 3 x f32, velocity 3 x f32   a member;
 *   14 DAMAGE  attacker id u16 (0xFFFF none), mask u32, damage f32, position, velocity   the host;
 *   15 BOLTS   count i32                                          the host to a member;
 *   16 ALIVE   level i8, count u16 (records), then a bit a record, 1 alive   the host to a member;
 *   17 BROKE   level i8, key u16                                  a member broke a crate.
 */
#ifndef MULTIPLAYER_WORLD_H
#define MULTIPLAYER_WORLD_H

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop.hpp"
#include "play.hpp"
#include "protocol.hpp"
#include "session.hpp"

namespace mp {

enum class WorldKind : std::uint8_t {
    kWorld = 9,
    kKilled = 10,
    kSpawn = 11,
    kDespawn = 12,
    kHit = 13,
    kDamage = 14,
    kBolts = 15,
    kAlive = 16,
    kBroke = 17
};

/* The gate's moments (the SDK's OPENRAC_MOBY_BEFORE and _AFTER). */
constexpr int kMobyBefore = 0;
constexpr int kMobyAfter = 1;

/* A spawned moby's id on the wire: 0x8000 | its net id. */
constexpr std::uint16_t kSpawnedId = 0x8000;
constexpr std::uint16_t kNoId = 0xFFFF;
/* WORLD's entries a datagram (each 24 bytes), how often it goes to a member, how long an unchanged
 * moby near a member waits before it is sent again, and how near counts. */
constexpr std::size_t kWorldEntries = 48;
constexpr std::size_t kWorldEntryBytes = 24;
constexpr std::uint64_t kWorldMs = 66;
constexpr std::uint64_t kWorldRefreshMs = 1000;
constexpr float kWorldNear = 80.0f;
/* How far a moby may move between two snapshots before its buffer starts again (a respawn). */
constexpr float kWorldSnap = 12.0f;
/* The crates (classes 500 to 540: bolt, nanotech, metal, ammo crates...): every game runs its own,
 * since a crate looks at the wrench's own collision to break, and a crate broken in one game is
 * gone in all (BROKE to the host, KILLED from it). Its bolts are for whoever broke it. */
inline bool local_class(int oclass) {
    return oclass >= 500 && oclass <= 540;
}

/* The bolt classes (bolts, hexnut, starnut, circlenut), spawned and shared. */
inline bool bolt_class(int oclass) {
    return oclass >= 13 && oclass <= 16;
}

/* One moby in WORLD. */
struct WorldEntry {
    std::uint16_t id = 0;
    Pose pose;
};

Bytes encode_world(int level, std::int64_t stamp, const std::vector<WorldEntry>& entries);
bool decode_world(const Bytes& bytes, int& level, std::int64_t& stamp, std::vector<WorldEntry>& out);

class World {
public:
    World(Session& session, Game& game, Coop& coop, std::function<std::uint64_t()> clock = {});

    /* One of the world's DATA messages (WorldKind first), from `sender`. */
    void handle(std::int32_t sender, const Bytes& bytes);

    /* Once a game tick, after the Coop's. */
    void tick(std::uint64_t now_ms);

    /* The game's gate around a moby's update (the SDK's openrac_moby_update_fn): true before skips
     * it. And a hit the game posted: true takes it back. */
    bool gate(int slot, int phase);
    bool hit(const Hit& hit);

    /* The party or the level is gone: nothing is kept. */
    void reset();

    /* What it does, for the pages and the tests. */
    bool hosting() const { return hosting_; }

    std::size_t driven() const { return driven_.size(); }

    std::size_t spawned() const { return net_slot_.size(); }

    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

private:
    struct Sent {
        Pose pose;
        std::uint64_t ms = 0;
    };

    struct Member {
        int level = -1;
        std::uint32_t told_load = 0;  // the host's load it was given ALIVE and SPAWNs for
        std::uint64_t world_ms = 0;
        std::map<std::uint16_t, Sent> sent;
        int bolts = 0;
    };

    struct Net {
        std::uint16_t id = 0;
        int oclass = 0;
    };

    void tick_host(std::uint64_t now, const GameStatus& status);
    void tick_member(std::uint64_t now, const GameStatus& status);
    void send_world(std::int32_t member, Member& m, std::uint64_t now, const Pose& at);
    void send_alive(std::int32_t member, int level);
    void send_spawn(std::int32_t target, int level, std::uint16_t net, const MobyState& moby);
    std::uint16_t id_of(int slot) const;
    int slot_of(std::uint16_t id) const;
    void log(const std::string& line) const {
        if (log_) {
            log_(line);
        }
    }

    Session& session_;
    Game& game_;
    Coop& coop_;
    std::function<std::uint64_t()> clock_;
    std::function<void(const std::string&)> log_;
    bool hosting_ = false;
    /* The level load the state below is of (this game's), and its level. */
    std::uint32_t loads_ = 0;
    int level_ = -1;
    /* This tick's mobys by slot, and the placed ones' keys alive. */
    std::map<int, MobyState> mobys_;
    std::set<int> alive_keys_;
    bool listed_ = false;
    /* Host: the spawned mobys it shares (by slot), the next net id, the members, the stand-in of the
     * update running (its player, 0 none) and the bolts before it. */
    std::map<int, Net> shared_;
    std::uint16_t next_net_ = 1;
    std::map<std::int32_t, Member> members_;
    std::int32_t standing_ = 0;
    int bolts_before_ = 0;
    /* Member: the crates the host killed (not to be told back), last tick's placed mobys' classes. */
    std::set<int> host_killed_;
    std::map<int, int> placed_class_;
    /* Member: the mobys it drives, their poses by id, the host's spawned ones by net id. */
    std::set<int> driven_;
    std::map<std::uint16_t, Interpolator> poses_;
    std::map<std::uint16_t, int> net_slot_;
    std::uint16_t world_sequence_ = 0;
    /* What it did since the last line in the log (every kWorldLogMs when something happened). */
    struct Counts {
        int snapshots = 0, hits = 0, kills = 0, spawns = 0, damage = 0, bolts = 0;
    } counts_;
    std::uint64_t counts_ms_ = 0;
};

/* How often the world says in the log what it did. */
constexpr std::uint64_t kWorldLogMs = 10000;

}  // namespace mp

#endif /* MULTIPLAYER_WORLD_H */
