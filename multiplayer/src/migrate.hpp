/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Losing a connection, and coming back (DECISIONS 6). No socket and no game here: given the session's
 * word on who dropped and who came back, on the game's thread, once a tick, it tells the co-op and
 * the world what that means for them.
 *
 * A dropped player keeps its slot for kResumeMs (Session, the server's RESUME). Its id stays, so
 * what the party knows of it (its figure, its mods, its ban, its ready) stands. A member who comes
 * back goes through the late-join path: the world again (HELLO, DEAD, SPAWN, SYNCED), its place beside
 * the host, the party's progress quietly. Its own bolts, ammo and nanotech were never the party's.
 * While it is gone its figure stands where it was, its enemies look for someone else, its grabs end,
 * and it holds up no wait. Direct connect does the same, its host being the server.
 *
 * The host dropping: everyone waits for kHostGraceMs with a line saying so and their heroes still.
 * Back in time, it plays on: what went into its reliable stream meanwhile never left, so every member
 * asks for the world again.
 */
#ifndef MULTIPLAYER_MIGRATE_H
#define MULTIPLAYER_MIGRATE_H

#include <cstdint>
#include <functional>
#include <string>

#include "coop.hpp"
#include "cosave.hpp"
#include "party_mods.hpp"
#include "progress.hpp"
#include "protocol.hpp"
#include "session.hpp"
#include "shared.hpp"
#include "world.hpp"

namespace mp {

/* How long the party waits for a host that dropped: the server's HOST_GRACE_SECONDS. */
constexpr std::uint64_t kHostGraceMs = 15000;

/* What the players read. */
constexpr const char* kDroppedLine = "Connection lost: getting back in";

class Migrate {
public:
    Migrate(Session& session, Coop& coop, World& world, Progress& progress, Shared& shared,
            std::function<std::uint64_t()> clock = {});

    /* One of the DATA messages of the change of host. */
    void handle(std::int32_t sender, const Bytes& bytes);

    /* Once a tick, ahead of the Coop's. */
    void tick(std::uint64_t now);

    /* The line over the game, empty for none. */
    std::string banner(std::uint64_t now) const;

    /* Whether the party is waiting for its host. */
    bool host_gone() const { return host_gone_ms_ != 0; }

    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

private:
    void take(const PartyEvent& event, std::uint64_t now);
    void set_hold(bool on);
    std::string name_of(std::int32_t member) const;

    void log(const std::string& line) const {
        if (log_) {
            log_(line);
        }
    }

    Session& session_;
    Coop& coop_;
    World& world_;
    Progress& progress_;
    Shared& shared_;
    std::function<std::uint64_t()> clock_;
    std::function<void(const std::string&)> log_;
    /* Since when the host has been gone (0: it has not). */
    std::uint64_t host_gone_ms_ = 0;
};

}  // namespace mp

#endif /* MULTIPLAYER_MIGRATE_H */
