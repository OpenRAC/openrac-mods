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
 * With the verbose log on (the `verbose_log` setting: trace.hpp), a line for each moby that leaves
 * the level, or is hidden or shown, here (despawn.hpp), on either side, and the world's pauses,
 * run lists, snapshots, drives, messages and a census each second (world_trace.cpp).
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
 * figure; a hit on a figure goes to its player (DAMAGE), the bolts a member's stand-in collected go
 * to it (BOLTS). Mobys near the members run as if near the host (eyes).
 *
 * Bolts are the host's, once in the world. Only the host's game drops them (its crates, those a
 * member broke too, and its enemies); a member's game drops none of its own (Moby_RecordDeath:
 * quiet_drop). Each one the host drops goes to the members (SPAWN); the member's copy flies out as
 * the host's does (WORLD poses it) and, once the host's has landed (WORLD's loose flag), runs in the
 * member's game, so it hovers and flies to that member's own Ratchet. A bolt collected in a member's
 * game (Moby_CollectBolt: collect) adds nothing there: it is a claim to the host (CLAIM, by net id
 * or a placed bolt's key). The host grants the first claim of each bolt, while its own copy is still
 * there and nobody (the host included) has taken it: it deletes its copy (DESPAWN or KILLED for
 * everyone) and gives the claimant the bolt's value (BOLTS). A later claim is refused, and its
 * player gets nothing. The host's own collection is the host's credit, and goes from every game.
 *
 * A bolt taken goes as the game takes one in every game that runs it: the host tells who took it
 * (TAKEN), and each game where it hovers by itself (the host's for a member's claim, a member's
 * for a bolt that landed, or a placed bolt) has it fly to that player (the taker's figure standing
 * in for the hero during its update, the game's own pickup flight), then the bolt update deletes
 * it there, nothing added (collect). DESPAWN or KILLED leave it to that flight; a bolt that has
 * not arrived within kFlightMs is deleted.
 *
 * Member. In the host's level it drives every placed moby that has an update of its own (the game
 * no longer runs it) and poses it from WORLD through a buffer per moby (play.hpp's, a delay
 * behind), kills what the host killed, makes what the host spawned (driven too). A hit its game
 * posts to a driven moby goes to the host (HIT) instead of the moby; a DAMAGE is posted to its own
 * hero (who is hurt as the game hurts him); BOLTS are added to its own, the only bolts it gets in the
 * host's level.
 *
 * The world's DATA kinds, after coop.hpp's:
 *    9 WORLD   level i8, stamp i64, count u8, then per moby: id u16 (a key; 0x8000 | a net id for
 *              one spawned), position 3 x f32, rotation 3 x i16 (1/10,000 rad), seq u8, frame u8,
 *              anim time u8 (/255), anim speed u8 (/32), flags u8 (1: hidden, drawn by nothing;
 *              2: loose, a bolt landed in the host's game: the member's copy runs as its own)
 *                                                                  the host to a member, as state;
 *   10 KILLED  level i8, count u16, keys u16...                   the host to everyone;
 *   11 SPAWN   level i8, net u16, class i16, position 3 x f32, yaw i16     the host;
 *   12 DESPAWN level i8, count u16, nets u16...                   the host;
 *   13 HIT     id u16, mask u32, damage f32, position 3 x f32, velocity 3 x f32   a member;
 *   14 DAMAGE  attacker id u16 (0xFFFF none), mask u32, damage f32, position, velocity   the host;
 *   15 BOLTS   count i32                                          the host to a member;
 *   16 DEAD    level i8, count u16 (records), then a bit a record, 1 dead   the host to a member;
 *   17 BROKE   level i8, key u16                                  a member broke a crate;
 *   22 CLAIM   level i8, id u16 (a key; 0x8000 | a net id)        a member collected a bolt;
 *   24 TAKEN   level i8, id u16 (as CLAIM's), player i32          the host: who took a bolt;
 *   26 HELLO   level i8, load u32                                 a member: its game has this
 *              level's mobys as made by this load, nothing of the host's world on them yet;
 *   27 RESET   level i8                                           the host: its level was made
 *              again (a death): what it spawned is gone, each member asks for the world again;
 *   28 SYNCED  level i8, load u32 (HELLO's)                       the host to a member: all of
 *              the world came, ahead of this message on the reliable stream.
 *
 * What a game's own load does. A level is made more often than it is loaded: each death of a hero
 * (the game's own respawn) runs the level's loader again in one frame, every moby back as the
 * level file has it, the spawned ones gone (status.loads changes, level does not). So the world
 * is never assumed to last. A member whose mobys were made, for whatever reason, says so (HELLO,
 * again each kHelloMs until answered) and the host gives it the whole world as it stands, in order
 * on the reliable stream: every placed moby dead (DEAD), every spawned one (SPAWN), SYNCED; WORLD
 * goes to it from then on. A joiner's first load is no different, so a late joiner gets the same.
 * What is dead for the party is the host's party_dead_, which outlives a respawn in the host's own
 * game: there the mobys it kept are deleted again at once, and the members told (RESET) to drop
 * what its game no longer has. One player's death puts nothing back for the others.
 *
 * WORLD is state and its datagrams are lost, so no moby's place rests on one datagram: a moby that
 * changed goes again in the next kWorldRepeats snapshots however it stands, each moby in the
 * snapshots at least every kWorldRefreshMs (kWorldFarMs far from the member), and a snapshot
 * refused by the session's bucket counts as not sent. Entries older than one already taken are
 * dropped, per moby, so a late datagram brings nothing back.
 */
#ifndef MULTIPLAYER_WORLD_H
#define MULTIPLAYER_WORLD_H

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop.hpp"
#include "despawn.hpp"
#include "trace.hpp"
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
    kBroke = 17,
    kClaim = 22,
    kTaken = 24,
    kHello = 26,
    kReset = 27,
    kSynced = 28
};

/* The gate's moments (the SDK's OPENRAC_MOBY_BEFORE and _AFTER). */
constexpr int kMobyBefore = 0;
constexpr int kMobyAfter = 1;

/* A spawned moby's id on the wire: 0x8000 | its net id. */
constexpr std::uint16_t kSpawnedId = 0x8000;
constexpr std::uint16_t kNoId = 0xFFFF;
/* WORLD's entries a datagram (each 25 bytes), how often it goes to a member (at the least: the
 * host's state budget of kWorldBudget a second is shared by the members), how long an unchanged moby
 * waits before it is sent again near a member and far from one, how near counts, how many
 * snapshots after a change a moby stays in them, and how often a member says HELLO. */
constexpr std::size_t kWorldEntries = 46;
constexpr std::size_t kWorldEntryBytes = 25;
constexpr std::uint64_t kWorldMs = 66;
constexpr std::uint64_t kWorldBudget = 30;
constexpr std::uint64_t kWorldRefreshMs = 1000;
constexpr std::uint64_t kWorldFarMs = 3000;
constexpr float kWorldNear = 80.0f;
constexpr int kWorldRepeats = 2;
constexpr std::uint64_t kHelloMs = 1000;
/* How often a member's verbose log says how the host's mobys are shown. */
constexpr std::uint64_t kWorldPoseTraceMs = 1000;
/* The bolt update's state while the bolt hovers where it landed (bolt_update's 3). */
constexpr std::uint32_t kBoltHovering = 3;
/* Its state while it flies to the Ratchet who takes it (bolt_update's 4), and how near that
 * Ratchet stands in for its update while it has not set off (well within the hero's reach, which
 * bolt_update's hover checks). How long a bolt taken by another player may fly before it is
 * deleted. */
constexpr std::uint32_t kBoltFlying = 4;
constexpr float kBoltReach = 0.5f;
constexpr std::uint64_t kFlightMs = 2000;
/* How long a crate replaying another game's break may take to break before it is deleted. */
constexpr std::uint64_t kReplayMs = 3000;
/* How far a moby may move between two snapshots before its buffer starts again (a respawn). */
constexpr float kWorldSnap = 12.0f;
/* How each class of the level's mobys is shared (WORLD.md's class inventory, from the catalogue):
 *
 * crates (500 bolt, 501 nanotech, 502 metal, 505 oom-oom box, 511 ammo crate): every game runs its
 *   own (one breaks under its game's own hero), and a crate broken in one game breaks in all (BROKE
 *   to the host, KILLED from it): the others post it a wrench's hit, so it breaks there with its
 *   debris and sound. Its drop is the host's game's only (a member's is left out), its bolts
 *   shared as the host's enemies' are (above).
 * pickups (placed bolts, gold bolts, ammo, ammo and health crates of the ship levels, nanotech):
 *   every game runs its own, so every player can collect one; the first collected is gone in every
 *   game (removed, without its effect or a gold bolt's scene). A placed bolt is a claim as a
 *   dropped one is (above); the others a member takes are its own and gone from the host's (BROKE).
 * interactables (vendors, the ship and its pads, people, infobots, help desks and triggers, the
 *   respawn point, gold-weapon cases, story pickups, the hero's own equipment), and what works on the
 *   player standing in it (1378 updraft_vent, 1039 respawn_volume, 22 hero_relocation_trigger, 605
 *   buried_bolts for the metal detector, 613 water_current, 610 wind_control, 679 grindrail): each
 *   game's own, for its own player; nothing is shared. Host-driven, those would give a member nothing
 *   and, with a member standing in for the host's hero, work on the host's hero instead: a member
 *   in a 1039 kill volume would respawn the host.
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
               {11,   298,  644,  1432, 1326, 925,  530,  666,  712,  816,  1012, 38,   1474,
                998,  424,  557,  1109, 1448, 1766, 484,  1120, 318,  1135, 1190, 750,  774,
                1283, 890,  909,  1377, 1455, 328,  90,   114,  1105, 805,  1324, 1341, 1342,
                1348, 1561, 558,  304,  23,   18,   1354, 1038, 1040, 1297, 1559, 1388, 585,
                607,  618,  849,  1289, 1290, 229,  1143, 71,   74,   121,  153,  163,  172,
                203,  270,  457,  1378, 1039, 22,   605,  613,  610,  679}
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

/* What a bolt of the class is worth (collect_bolt's: 1, 5, 20, 50), 0 for another class. */
inline int bolt_value(int oclass) {
    switch (oclass) {
        case 13:
            return 1;
        case 14:
            return 5;
        case 15:
            return 20;
        case 16:
            return 50;
        default:
            return 0;
    }
}

/* One moby in WORLD. */
struct WorldEntry {
    std::uint16_t id = 0;
    Pose pose;
    bool hidden = false;
    bool loose = false;
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

    /* The game records a moby's death (its killed bits, then its bolts dropped): true when its drop
     * is left out (its killed bits kept), as in a member's game in the host's level, whose bolts are
     * all the host's game's. */
    bool quiet_drop(int slot);

    /* The game collects a bolt (collect_bolt): true when its value is this game's to add. In a
     * member's game in the host's level, a claim to the host instead (false). */
    bool collect(int slot);

    /* The party or the level is gone: nothing is kept. */
    void reset();

    /* What it does, for the pages and the tests. */
    bool hosting() const { return hosting_; }

    std::size_t driven() const { return driven_.size(); }

    std::size_t spawned() const { return net_slot_.size(); }

    std::size_t mobys() const { return mobys_.size(); }

    /* A member: whether the host gave it the world of the level as it stands. */
    bool synced() const { return synced_; }

    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

    /* The verbose log (trace.hpp; null or off: none), its despawn lines with it; the game recorded
     * a moby's death. */
    void trace(Trace* trace) {
        trace_ = trace;
        despawn_on_ = tracing();
    }

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
        bool loose = false;
        std::uint64_t ms = 0;
        int repeats = 0;  // snapshots it stays in though unchanged
    };

    struct Member {
        int level = -1;
        bool want = false;        // HELLO heard, the world not given yet
        std::uint32_t load = 0;   // that HELLO's load
        bool synced = false;      // given the world: WORLD goes to it
        std::uint64_t world_ms = 0;
        std::map<std::uint16_t, Sent> sent;
        int bolts = 0;
    };

    struct Net {
        std::uint16_t id = 0;
        int oclass = 0;
    };

    /* New mobys (status.loads, the level): nothing of the last ones stands. */
    void follow_load(const GameStatus& status);
    void tick_host(std::uint64_t now, const GameStatus& status);
    void tick_member(std::uint64_t now, const GameStatus& status);
    void send_world(std::int32_t member, Member& m, std::uint64_t now, const Pose& at);
    void send_dead(std::int32_t member, int level);
    /* Host: the whole world to one member that said HELLO (DEAD, SPAWNs, SYNCED). */
    void give_world(std::int32_t id, Member& m);
    /* Host: its level made again: the party's dead deleted here again, the members told. */
    void remade();
    /* Host: the members' breaks heard since the last listing, done here. */
    void take_broke();
    /* Member: what the host spawned, gone (RESET). */
    void drop_spawned();
    /* Host: a member's claim of a bolt, by its id (a key, or kSpawnedId | a net id). */
    void claim(std::int32_t member, int level, std::uint16_t id);
    /* Host: who took a bolt (TAKEN), to every member. */
    void send_taken(std::uint16_t id, std::int32_t player);
    /* A bolt taken by `player` flies to that player here before it goes (the host's copy of one a
     * member claimed, or a member's copy that runs by itself); false when it cannot (no figure). */
    bool fly(int slot, std::uint16_t id, std::int32_t player, std::uint64_t now);
    /* A flight's stand-in before its bolt's update; the flights done or out of time. */
    void stand_flight(int slot);
    void tick_flights(std::uint64_t now);
    /* An eye (Game::eyes) on each bolt flying, so it runs wherever the camera is. */
    void add_flight_eyes(std::vector<float>& eyes) const;
    void log_counts(std::uint64_t now);
    /* A crate broken in another game broken here too (a wrench's hit posted to it: its own debris
     * and sound), its drop left out; deleted if it has not broken by the deadline. */
    void replay_break(int slot, std::uint64_t now);
    void send_spawn(std::int32_t target, int level, std::uint16_t net, const MobyState& moby);
    std::uint16_t id_of(int slot) const;
    int slot_of(std::uint16_t id) const;
    void tick_world(std::uint64_t now);
    void tick_despawn(std::uint64_t now, const std::vector<MobyState>& listing);
    /* The verbose log (world_trace.cpp): each tick's pause, run, vis, drive and census lines; a
     * line of `what`; a WORLD's ids against the last one's; a moby deleted by the sync. */
    bool tracing() const { return trace_ != nullptr && trace_->on(); }
    void trace_tick(std::uint64_t now);
    void trace_line(const std::string& what, const std::string& text) {
        if (tracing()) {
            trace_->line(what, text);
        }
    }
    void trace_snapshot(const std::string& how, std::set<std::uint16_t>& last, const std::set<std::uint16_t>& ids);
    bool kill(int slot, const std::string& why);
    std::string moby_name(int slot) const;
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
    /* Host: every placed moby dead for the party in this level, whatever became of this game's
     * mobys since; whether the level was made again and that is still to be dealt with; loot the
     * level made itself at its load (a crate's own nanotech), not one of the host's to share. */
    std::set<int> party_dead_;
    bool remade_ = false;
    /* Host: the members' BROKE to act on at the next listing, by key: who said so. */
    std::map<int, std::int32_t> broke_;
    std::set<int> level_own_;
    /* Member: whether the host gave it the world of this load, and when it last asked. */
    bool synced_ = false;
    std::uint64_t hello_ms_ = 0;
    bool placed_driven_ = false;
    /* Host: the spawned mobys it shares (by slot), the next net id, the members, the stand-in of the
     * update running (its player, 0 none) and the bolts before it. */
    std::map<int, Net> shared_;
    std::uint16_t next_net_ = 1;
    std::map<std::int32_t, Member> members_;
    std::int32_t standing_ = 0;
    int bolts_before_ = 0;
    /* Host: the bolts taken (granted to a member, or collected here), by id. */
    std::set<std::uint16_t> claimed_;
    /* The bolts flying to the player who took them (fly), by slot: who, that player's figure here
     * (-1: this game's Ratchet), its id, until when. */
    struct Flight {
        std::int32_t player = 0;
        int figure = -1;
        std::uint16_t id = 0;
        std::uint64_t until = 0;
    };
    std::map<int, Flight> flights_;
    /* Member: whether its eyes are on flying bolts (to be cleared once none fly). */
    bool flight_eyes_ = false;
    /* Member: the crates the host killed (not to be told back), last tick's placed mobys' classes. */
    std::set<int> host_killed_;
    /* Crates breaking here for another game's break: their slots, until when they may take. */
    std::map<int, std::uint64_t> replays_;
    /* Member: the host's loot made here as this game's own, by net id. */
    std::set<std::uint16_t> loot_;
    std::map<int, int> placed_class_;
    /* Member: the crates and pickups gone here that the host was told of. */
    std::set<int> reported_;
    /* Member: the mobys it drives, their poses by id, the host's spawned ones by net id. */
    std::set<int> driven_;
    std::map<std::uint16_t, Interpolator> poses_;
    /* Member, for the verbose log: where each driven moby was put last tick (to see whether
     * anything else moved it since), how many were, and when the log last said how they are shown. */
    std::map<int, std::array<float, 3>> posed_at_;
    int moved_since_ = 0;
    std::uint64_t poses_traced_ms_ = 0;
    std::map<std::uint16_t, int> net_slot_;
    /* Member: the host's bolts landed there and so run here, by net id. */
    std::set<std::uint16_t> loose_;
    std::uint16_t world_sequence_ = 0;
    /* Member: whether the host's copy of each is hidden, when the host last sent each, and the
     * newest stamp taken of each (older datagrams leave it alone). */
    std::map<std::uint16_t, bool> hidden_;
    std::map<std::uint16_t, std::uint64_t> host_ms_;
    std::map<std::uint16_t, std::int64_t> stamps_;
    bool despawn_on_ = false;
    DespawnLog despawn_;
    Trace* trace_ = nullptr;
    /* What the verbose log last saw: the phase, the level's mode, whether a menu was open and its
     * far mobys listed, the menu updates; each slot's run (in range or not) and flags; the driven;
     * the census's ids; the eyes; the snapshots' ids, sent by member and received. */
    struct Traced {
        int phase = -1;
        std::uint32_t loads = 0xFFFFFFFFu;
        int level_mode = -999;
        bool menu = false;
        bool menu_listed = false;
        std::uint32_t menu_ticks = 0;
        std::map<int, bool> in_range;
        std::map<int, MobyState> listed;
        std::set<int> driven;
        std::set<std::string> census;
        std::uint64_t census_ms = 0;
        std::uint64_t run_ms = 0;
        std::size_t eyes = 0;
        std::uint64_t eyes_ms = 0;
        std::map<std::int32_t, std::set<std::uint16_t>> sent;
        std::set<std::uint16_t> received;
    } traced_;
    /* What it did since the last line in the log (every kWorldLogMs when something happened). */
    struct Counts {
        int snapshots = 0, hits = 0, kills = 0, spawns = 0, despawns = 0, damage = 0, bolts = 0;
    } counts_;
    std::uint64_t counts_ms_ = 0;
};

/* How often the world says in the log what it did. */
constexpr std::uint64_t kWorldLogMs = 5000;

}  // namespace mp

#endif /* MULTIPLAYER_WORLD_H */
