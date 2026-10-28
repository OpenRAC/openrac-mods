/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The shared world (world.cpp): the host's game runs the level (its enemies, crates, doors,
 * platforms, bolts) and the members' games show it. No socket and no game here: given the Session,
 * the Game (the SDK's 1.9 world calls) and the Coop (the players' poses and figures), on the game's
 * thread, once a tick, plus the game's gate and hits as they come.
 *
 * A placed moby's own update hides and shows it (an enemy waiting to appear, a platform out of
 * sight), so a member, whose game no longer runs the update of one it drives, would keep it as it
 * was when the drive began: WORLD carries the host's (hidden or not), and the member hides or
 * shows its copy so.
 *
 * With the `despawn_log` setting, a line for each moby that leaves the level, or is hidden or
 * shown, here (despawn.hpp), on either side.
 *
 * A death is known only from the game saying so (a placed moby listed dead), never from a moby
 * missing from a listing; a tick the game lists nothing (loading) changes nothing.
 *
 * Host. Each moby the level placed is named by its key (its record in the level's file, the same in
 * every game). The host sends each member in its level, as state 15 times a second, the mobys
 * nearest that member that moved or animated (WORLD, at most kWorldEntries a datagram), tells
 * everyone of the placed ones that died (KILLED) and of the ones it spawns that count (creatures,
 * bolts: SPAWN and DESPAWN, by a net id of its own), and gives a member that arrives in its level
 * the placed mobys dead there (DEAD) and what it has spawned. Every moby's update runs with
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
 *              anim time u8 (/255), anim speed u8 (/32), flags u8 (1: hidden, drawn by nothing)
 *                                                                  the host to a member, as state;
 *   10 KILLED  level i8, count u16, keys u16...                   the host to everyone;
 *   11 SPAWN   level i8, net u16, class i16, position 3 x f32, yaw i16     the host;
 *   12 DESPAWN level i8, count u16, nets u16...                   the host;
 *   13 HIT     id u16, mask u32, damage f32, position 3 x f32, velocity 3 x f32   a member;
 *   14 DAMAGE  attacker id u16 (0xFFFF none), mask u32, damage f32, position, velocity   the host;
 *   15 BOLTS   count i32                                          the host to a member;
 *   16 DEAD    level i8, count u16 (records), then a bit a record, 1 dead   the host to a member;
 *   17 BROKE   level i8, key u16                                  a member broke a crate.
 */
#ifndef MULTIPLAYER_WORLD_H
#define MULTIPLAYER_WORLD_H

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop.hpp"
#include "despawn.hpp"
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
    kDead = 16,
    kBroke = 17
};

/* The gate's moments (the SDK's OPENRAC_MOBY_BEFORE and _AFTER). */
constexpr int kMobyBefore = 0;
constexpr int kMobyAfter = 1;

/* A spawned moby's id on the wire: 0x8000 | its net id. */
constexpr std::uint16_t kSpawnedId = 0x8000;
constexpr std::uint16_t kNoId = 0xFFFF;
/* WORLD's entries a datagram (each 25 bytes), how often it goes to a member, how long an unchanged
 * moby near a member waits before it is sent again, and how near counts. */
constexpr std::size_t kWorldEntries = 46;
constexpr std::size_t kWorldEntryBytes = 25;
constexpr std::uint64_t kWorldMs = 66;
constexpr std::uint64_t kWorldRefreshMs = 1000;
constexpr float kWorldNear = 80.0f;
/* How long a crate replaying another game's break may take to break before it is deleted. */
constexpr std::uint64_t kReplayMs = 3000;
/* How far a moby may move between two snapshots before its buffer starts again (a respawn). */
constexpr float kWorldSnap = 12.0f;
/* How each class of the level's mobys is shared (WORLD.md's class inventory, from the catalogue):
 *
 * crates (500 bolt, 501 nanotech, 502 metal, 505 oom-oom box, 511 ammo crate): every game runs its
 *   own (one breaks under its game's own hero), and a crate broken in one game breaks in all (BROKE
 *   to the host, KILLED from it): the others post it a wrench's hit, so it breaks there with its
 *   debris and sound, its drop left out. Its bolts are once in the world: whoever broke it.
 * pickups (placed bolts, gold bolts, ammo, ammo and health crates of the ship levels, nanotech):
 *   every game runs its own, so every player can collect one; the first collected is gone in every
 *   game (removed, without its effect or a gold bolt's scene).
 * interactables (vendors, the ship and its pads, people, infobots, help desks and triggers, the
 *   respawn point, gold-weapon cases, story pickups, the hero's own equipment): each game's own, for
 *   its own player; nothing is shared.
 * everything else the level placed with an update of its own: the host's (members drive it).
 */
inline bool any_of_class(int oclass, std::initializer_list<int> classes) {
    for (const int c : classes) {
        if (c == oclass) {
            return true;
        }
    }

    return false;
}

inline bool crate_class(int oclass) {
    return any_of_class(oclass, {500, 501, 502, 505, 511});
}

inline bool pickup_class(int oclass) {
    return any_of_class(
        oclass,
        {13, 14, 15, 16, 1134, 204, 213, 214, 222, 223, 225, 226, 1006, 1438, 1447, 1449, 224, 228,
         1218, 1220, 806}
    );
}

/* Pickups an enemy of the host's drops that every game gets its own of (ammo, health). */
inline bool loot_class(int oclass) {
    return pickup_class(oclass) && oclass > 16 && oclass != 1134;
}

inline bool interactable_class(int oclass) {
    return any_of_class(
               oclass,
               {11,   298,  644,  1432, 1326, 925,  1973, 530,  666,  712,  816,  1012, 38,   1474,
                998,  424,  557,  1109, 1448, 1766, 484,  1120, 318,  1135, 1190, 750,  774,  1283,
                890,  909,  1377, 1455, 328,  90,   114,  1105, 805,  1324, 1341, 1342, 1348, 1561,
                558,  304,  23,   18,   1354, 1038, 1040, 1297, 1100, 1101, 1102, 1531, 1532, 1559,
                404,  405,  1388, 585,  607,  618,  849,  1289, 1290, 229,  1143, 71,   74,   121,
                153,  163,  172,  203,  270,  457}
           )
           || (oclass >= 1456 && oclass <= 1465);
}

/* Every game runs its own copy: crates, pickups, interactables. */
inline bool local_class(int oclass) {
    return crate_class(oclass) || pickup_class(oclass) || interactable_class(oclass);
}

/* The bolt classes (bolts, hexnut, starnut, circlenut), spawned and shared. */
inline bool bolt_class(int oclass) {
    return oclass >= 13 && oclass <= 16;
}

/* One moby in WORLD. */
struct WorldEntry {
    std::uint16_t id = 0;
    Pose pose;
    bool hidden = false;
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

    /* The game records a moby's death (its bolts dropped, its killed bits): false when the death is
     * a crate's broken here only to show another game's break, whose bolts are not this game's. */
    bool quiet_death(int slot);

    /* The party or the level is gone: nothing is kept. */
    void reset();

    /* What it does, for the pages and the tests. */
    bool hosting() const { return hosting_; }

    std::size_t driven() const { return driven_.size(); }

    std::size_t spawned() const { return net_slot_.size(); }

    std::size_t mobys() const { return mobys_.size(); }

    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

    /* The despawn log on or off (the `despawn_log` setting); the game recorded a moby's death. */
    void despawn_log(bool on) { despawn_on_ = on; }

    void note_death(int slot) {
        if (despawn_on_) {
            despawn_.note_death(slot);
        }
    }

    const DespawnLog& despawn() const { return despawn_; }

private:
    struct Sent {
        Pose pose;
        bool hidden = false;
        std::uint64_t ms = 0;
    };

    struct Member {
        int level = -1;
        std::uint32_t told_load = 0;  // the host's load it was given DEAD and SPAWNs for
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
    void send_dead(std::int32_t member, int level);
    void log_counts(std::uint64_t now);
    /* A crate broken in another game broken here too (a wrench's hit posted to it: its own debris
     * and sound), its drop left out; deleted if it has not broken by the deadline. */
    void replay_break(int slot, std::uint64_t now);
    void send_spawn(std::int32_t target, int level, std::uint16_t net, const MobyState& moby);
    std::uint16_t id_of(int slot) const;
    int slot_of(std::uint16_t id) const;
    void tick_despawn(std::uint64_t now, const std::vector<MobyState>& listing);
    void note(int slot, const std::string& why, std::int32_t sender) {
        if (despawn_on_) {
            despawn_.note_sync(slot, why, sender);
        }
    }
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
    std::set<int> dead_keys_;
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
    /* Crates breaking here for another game's break: their slots, until when they may take. */
    std::map<int, std::uint64_t> replays_;
    /* Member: the host's loot made here as this game's own, by net id. */
    std::set<std::uint16_t> loot_;
    std::map<int, int> placed_class_;
    /* Member: the mobys it drives, their poses by id, the host's spawned ones by net id. */
    std::set<int> driven_;
    std::map<std::uint16_t, Interpolator> poses_;
    std::map<std::uint16_t, int> net_slot_;
    std::uint16_t world_sequence_ = 0;
    /* Member: whether the host's copy of each is hidden, and when the host last sent each. */
    std::map<std::uint16_t, bool> hidden_;
    std::map<std::uint16_t, std::uint64_t> host_ms_;
    bool despawn_on_ = false;
    DespawnLog despawn_;
    /* What it did since the last line in the log (every kWorldLogMs when something happened). */
    struct Counts {
        int snapshots = 0, hits = 0, kills = 0, spawns = 0, damage = 0, bolts = 0;
    } counts_;
    std::uint64_t counts_ms_ = 0;
};

/* How often the world says in the log what it did. */
constexpr std::uint64_t kWorldLogMs = 5000;

}  // namespace mp

#endif /* MULTIPLAYER_WORLD_H */
