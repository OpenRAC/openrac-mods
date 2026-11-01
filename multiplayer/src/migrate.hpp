/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Losing a connection, coming back, and the party going on without its host (DECISIONS 6). No socket
 * and no game here: given the session's word on who dropped, who came back and who hosts now, on the
 * game's thread, once a tick, it tells the co-op and the world what that means for them.
 *
 * A dropped player keeps its slot for kResumeMs (Session, the server's RESUME). Its id stays, so
 * what the party knows of it (its figure, its mods, its ban, its ready) stands. A member who comes
 * back goes through the late-join path: the world again (HELLO, DEAD, SPAWN, SYNCED), its place beside
 * the host, the party's progress quietly. Its own bolts, ammo and nanotech were never the party's.
 * While it is gone its figure stands where it was, its enemies look for someone else, its grabs end,
 * and it holds up no wait. Direct connect does the same, its host being the server, and goes no
 * further: its party ends with its host.
 *
 * The host dropping: everyone waits for kHostGraceMs with a line saying so and their heroes still.
 * Back in time, it plays on: what went into its reliable stream meanwhile never left, so every member
 * asks for the world again.
 *
 * Out of time, or a host that left on purpose (at once): the server gives the party to the first of
 * the host's list (SUCCESSOR) who is there. The list is the members who hold the world and may
 * play, by their link (the round trip, TCP-only behind UDP), then by when they joined; either disc will
 * do. Nobody on it, or nobody left able: END, and any member can Continue as host from its co-op slot.
 *
 * The ledger is what makes it possible. A new host's copy of a level "will not be identical": its
 * members' mobys were driven, and a door that never got its word can stop a mission for good. So the
 * party keeps a little state off the host: what is dead, what was spawned, the dug piles, the moby
 * ids, the level, the travel and scene counts and the revive rule, whole (never a piece of a change),
 * versioned and numbered, on the reliable stream to everyone at each change, at most once a second.
 * Positions stay in the games; nothing in it is an enemy's mind.
 *
 * Takeover, on the successor. It asks everyone for their ledger (a member may have got one this game
 * did not) and takes the newest by (epoch, generation): an old host can never overwrite a newer one,
 * and its traffic is refused by the epoch (Session). It matches its game to the ledger, each moby by
 * itself so one that fails stops nothing, lets the game run them, and hosts. Each member gives up what
 * the old host spawned and says HELLO: the one way the world ever arrives. A host that cannot (a
 * joiner, one in the title, no ledger) declines, and the next is asked.
 *
 * What becomes of what, on a change of host:
 *
 * World. party_dead_, dug_, spared_: the ledger's. shared_, next_net_: the spawned net ids go on as
 * they were, each member's copies made anew from SPAWN. driven_: released to the game. poses_,
 * hidden_, stamps_, host_ms_: gone, the game runs them. members_ and everything in them (WORLD's
 * tracked, window, sequence, rtt, hit_ms, priorities): new, from a member's HELLO. A WORLD ack of the
 * old host: none (no_world_ack), and old datagrams fall to the epoch. aims_: gone, so an enemy picks
 * its target afresh; a keeps_hero_class enemy kept nothing here, its update having been off. holds_
 * and a held hero: every grab ends (a member's game has let go by HOLD's kHoldLeaseMs, 1.5 s, inside
 * the wait; a host that left on purpose: at the event, release_holds), and a new grab starts from a new
 * update. The host's own hero's locks went with it. The port's stand-in guards live for one update
 * and carry nothing. deaths_, downs_: gone: a player down stays down by its own game's timer
 * (kDownedMs). flights_: fly on where they are. claimed_, broke_, reported_, host_killed_, digs_: gone:
 * a claim or a dig not answered is dropped (the pile stands, the bolt lies there); what a game had
 * already broken comes in as dead at the next listing. restored_: those mobys are this game's now.
 * level_own_: spawned loot of this game's own, unshared. revive_rule_: the ledger's.
 *
 * Coop. play_, applied_, the slot: the same game. told_: all who are in. chosen_: the new host's
 * co-op slot, for one who asks from the title. LEVEL: at once. Scenes: the old host's scene is skipped
 * in the new host's game, then in each member's by LEVEL (kHostOutMs); reset_scenes, the sequence
 * going on from the ledger. A ship travel: a new host on its way finishes the trip and the rest follow
 * by LEVEL, its barrier gone; nobody is waited for. A wait or its votes: gone. Joining: a joiner goes
 * on with the new host, its PLAY asked for again if it was cut. A place being asked: goes to the
 * new host. Remotes: stay; an old host's figure stands until its slot is given up.
 *
 * Shared, Progress. Whole again from the new host (again()): every game has what it was given, the
 * new host's game is the party's progress from here on. Mission rows: its own.
 *
 * Camera scripts: any being shown ends (reset), the next the new host's.
 *
 * Mods. The rules (Difficulty, a seed) stand: the new host's own mods play by them and keep what
 * was delivered, and it passes them on instead of its own settings. It checks members against the list
 * they were all checked against, not its own, and sends that list under its own disc, so "Enemies
 * follow the host's game" (party_mods) comes and goes by the new host's disc. Each member sends it
 * its mods and its key, so bans go on. Bans are on every slot already (COPARTY).
 *
 * Saves. key(): the same party, the same slots; each member's slot() is untouched. A save on its
 * way is dropped: the slot keeps its last whole one. The new host's saves go to everyone from now on.
 *
 * An old host that comes back within its slot finds a member's place: it leaves its game for the title
 * and comes in as a late joiner, since its world is no one's now. After that: the party's code.
 *
 * Wire (kinds.hpp, 40 to 43; 43 is the Session's):
 *   40 SUCCESSOR  count u8, ids i32...     the host to everyone: who follows it, in order; the same
 *                                          list goes to the server (HOST_CHANGED);
 *   41 LEDGER     (encode_ledger)          the host to everyone at each change; a member to a new
 *                                          host that asks;
 *   42 LEDGER_ASK                          a new host to everyone.
 */
#ifndef MULTIPLAYER_MIGRATE_H
#define MULTIPLAYER_MIGRATE_H

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "camscripts.hpp"
#include "coop.hpp"
#include "cosave.hpp"
#include "party_mods.hpp"
#include "progress.hpp"
#include "protocol.hpp"
#include "session.hpp"
#include "shared.hpp"
#include "world.hpp"

namespace mp {

/* How long the party waits for a host that dropped: the server's HOST_GRACE_SECONDS. How often the
 * host may say its list and its ledger, how long a new host waits for the others' ledgers, how long
 * a member stands still for a new host's world, and how long the line about it stays. */
constexpr std::uint64_t kHostGraceMs = 15000;
constexpr std::uint64_t kSuccessorMs = 3000;
constexpr std::uint64_t kLedgerMs = 1000;
constexpr std::uint64_t kGatherMs = 1000;
constexpr std::uint64_t kNewHostMs = 6000;
constexpr std::uint64_t kNewHostLineMs = 5000;

/* A ledger's format, the first byte on the wire. */
constexpr std::uint8_t kLedgerVersion = 1;

/* The whole of what every member keeps. */
struct Ledger {
    std::uint16_t epoch = 0;
    std::uint32_t generation = 0;
    std::int32_t play = 0;
    std::uint16_t travel = 0;
    std::uint16_t scene = 0;
    LedgerWorld world;
};

/* A ledger's bytes (the kind first) and back; false for one cut short, or of another version. */
Bytes encode_ledger(const Ledger& ledger);
bool decode_ledger(const Bytes& bytes, Ledger& out);

/* Whether `a` is newer than `b`: an epoch, then its generation. */
bool ledger_newer(const Ledger& a, const Ledger& b);

/* The members in the order they would follow the host: the link's worth first, then as given. */
struct Candidate {
    std::int32_t id = 0;
    Link link;
};

std::vector<std::int32_t> successor_order(std::vector<Candidate> candidates);

/* What the players read. */
constexpr const char* kDroppedLine = "Connection lost: getting back in";
constexpr const char* kYouHostLine = "You are the party's host now";

class Migrate {
public:
    Migrate(Session& session, Game& game, Coop& coop, World& world, Progress& progress, Shared& shared,
            PartyMods& mods, CoSave& cosave, CamScripts& cam, std::function<std::uint64_t()> clock = {});

    /* One of the DATA messages of the change of host. */
    void handle(std::int32_t sender, const Bytes& bytes);

    /* Once a tick, ahead of the Coop's. */
    void tick(std::uint64_t now);

    /* The line over the game, empty for none. */
    std::string banner(std::uint64_t now) const;

    /* Whether the party is waiting for its host. */
    bool host_gone() const { return host_gone_ms_ != 0; }

    /* The host is leaving on purpose: the last of the ledger, while it can still go. */
    void leaving();

    /* Whether someone would follow this host (the Leave question says so). */
    bool followed() const { return !named_.empty(); }

    /* Whether this game took the party over: the host's rules are the party's, not its settings. */
    bool took_over() const { return took_over_; }

    const Ledger& ledger() const { return ledger_; }

    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

private:
    void take(const PartyEvent& event, std::uint64_t now);
    void host_changed(std::int32_t host, std::uint64_t now);
    void finish_gather(std::uint64_t now);
    void tick_host(std::uint64_t now);
    void tick_stand(std::uint64_t now);
    void clear();
    void send_ledger(std::uint64_t now);
    std::string name_of(std::int32_t member) const;

    void log(const std::string& line) const {
        if (log_) {
            log_(line);
        }
    }

    Session& session_;
    Game& game_;
    Coop& coop_;
    World& world_;
    Progress& progress_;
    Shared& shared_;
    PartyMods& mods_;
    CoSave& cosave_;
    CamScripts& cam_;
    std::function<std::uint64_t()> clock_;
    std::function<void(const std::string&)> log_;
    /* Since when the host has been gone (0: it has not); whether this game was the host last tick. */
    std::uint64_t host_gone_ms_ = 0;
    bool was_host_ = false;
    /* The ledger: the newest this game has, whole. */
    Ledger ledger_;
    std::uint64_t ledger_ms_ = 0;
    Bytes ledger_said_;
    std::size_t ledger_members_ = 0;
    /* The host: who it named last, when; a member: who is next. */
    std::vector<std::int32_t> named_;
    std::uint64_t named_ms_ = 0;
    std::vector<std::int32_t> next_;
    /* A new host waiting for the ledgers: since when (0: not), and who answered. */
    std::uint64_t gather_ms_ = 0;
    std::set<std::int32_t> answered_;
    bool took_over_ = false;
    /* A member waiting for a new host's world: since when (0: not). The line about the new host: who,
     * until when. */
    std::uint64_t stand_ms_ = 0;
    std::int32_t new_host_ = 0;
    std::uint64_t new_host_until_ = 0;
};

}  // namespace mp

#endif /* MULTIPLAYER_MIGRATE_H */
