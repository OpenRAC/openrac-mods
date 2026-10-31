/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The test bot (bot.cpp): a development build's way to play the real game by script, for the
 * end-to-end tests (tests/e2e). Built only with -DMULTIPLAYER_BOT=ON, never in a release, and on
 * only with OPENRAC_MP_BOT=<port>: the mod then listens on 127.0.0.1:<port> for one driver, which
 * sends a command a line and gets one line back (JSON, or "ok" / "error ...").
 *
 *   connect HOST PORT   the connection, as Home opens it
 *   create              a party, a new game chosen
 *   join CODE           the party of that code
 *   ready               this member's Ready
 *   start               the host's Start
 *   leave               Leave party
 *   status              the game and the party, one JSON object
 *   dump                every moby of the level (slot, key, class, flags, state, position)
 *   stats               what the network layer counted
 *   teleport X Y Z      the hero there, held a few ticks
 *   press MASK TICKS [LX LY]   buttons (the pad's mask, set bits pressed) and the left stick
 *   hit KEY MASK        a hit from the hero on the placed moby of that key, as the game posts one
 *   bolts               the bolt count
 *   scene N             the level's scene N started, as its own trigger starts it
 *   travel LEVEL        the host's ship to that level
 *   fall                the hero out of the level's bounds: the game's own death and respawn
 */
#ifndef MULTIPLAYER_BOT_H
#define MULTIPLAYER_BOT_H

#ifdef MULTIPLAYER_BOT

#include <cstdint>
#include <functional>
#include <string>

#include "connection.hpp"
#include "coop.hpp"
#include "session.hpp"
#include "world.hpp"

namespace mp {

class Bot {
public:
    struct Parts {
        Connection& connection;
        Session& session;
        Game& game;
        Coop& coop;
        World& world;
        /* The hero's position written to game memory. */
        std::function<void(const float*)> teleport;
    };

    explicit Bot(Parts parts);
    ~Bot();

    /* Whether OPENRAC_MP_BOT asked for it. */
    bool on() const { return listen_ >= 0; }

    /* Commands read and answered; a tick's held teleport put back. Each frame, and from the pad
     * while a movie runs without frames. */
    void poll();
    /* The buttons and stick pressed now, into the pad's report. */
    void pad(std::uint8_t* report, int size);

private:
    std::string run(const std::string& line);
    std::string status();
    std::string dump();
    std::string stats();

    Parts parts_;
    long listen_ = -1;
    long client_ = -1;
    std::string input_;
    float at_[3] = {0, 0, 0};
    int teleport_ticks_ = 0;
    std::uint32_t mask_ = 0;
    int press_ticks_ = 0;
    int stick_[2] = {128, 128};
};

}  // namespace mp

#endif /* MULTIPLAYER_BOT */
#endif /* MULTIPLAYER_BOT_H */
