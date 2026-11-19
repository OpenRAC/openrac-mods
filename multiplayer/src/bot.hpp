// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The test bot (bot.cpp): a development build's way to play the real game by script, for the
// end-to-end tests (tests/e2e). Built only with -DMULTIPLAYER_BOT=ON, never in a release, and on
// only with OPENRAC_MP_BOT=<port>: the mod then listens on 127.0.0.1:<port> for one driver, which
// sends a command a line and gets one line back (JSON, or "ok" / "error ...").
//
//   connect HOST PORT   the connection, as Home opens it
//   create              a party, a new game chosen
//   join CODE           the party of that code
//   ready               this member's Ready
//   start               the host's Start
//   leave               Leave party
//   kick NAME           the host takes that member out
//   ban NAME            the host bans it from the party's save, which takes it out
//   unban               the host lifts the save's bans
//   status              the game and the party, one JSON object
//   mods                what the party's mod check knows, one JSON object
//   page NAME           what a page shows (lobby, refused, rules), a JSON list of its lines
//   widths NAME         how wide the game's font draws each line of that page, a JSON list
//   dump                every moby of the level (slot, key, class, flags, state, position)
//   stats               what the network layer counted
//   teleport X Y Z      the hero there, held a few ticks
//   equip ITEM          the item given, with ammunition, and in the hand
//   press MASK TICKS [LX LY]   buttons (the pad's mask, set bits pressed) and the left stick
//   hit KEY MASK        a hit from the hero on the placed moby of that key, as the game posts one
//   bolts               the bolt count
//   scene N             the level's scene N started, as its own trigger starts it
//   travel LEVEL        the host's ship to that level
//   fall                the hero out of the level's bounds: the game's own death and respawn
//   shared TAG          a shared section (shared.hpp's tags): a JSON list of the indices held
//   give TAG INDEX      one index of that section merged in, as if this game had earned it
//   damage MASK AMOUNT  a hit on this game's own hero, as a DAMAGE posts one

#ifndef MULTIPLAYER_BOT_H
#define MULTIPLAYER_BOT_H

#ifdef MULTIPLAYER_BOT

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "camscripts.hpp"
#include "connection.hpp"
#include "coop.hpp"
#include "cosave.hpp"
#include "migrate.hpp"
#include "party_mods.hpp"
#include "session.hpp"
#include "world.hpp"

namespace mp {

// The test bot: a listener on the loopback address that runs one driver's commands against the real
// game.
//
// Owns its two sockets and closes them in the destructor. Only the game's thread uses it: poll()
// runs each frame and pad() from the pad hook.
class Bot {
public:
    // The parts of the mod the bot drives. All are borrowed: they outlive the bot.
    struct Parts {
        Connection& connection;  // The server connection (connect).
        Session& session;        // The party's session.
        Game& game;              // The game, read and driven.
        Coop& coop;              // The co-op.
        World& world;            // The world's replication.
        PartyMods& mods;         // The party's mod check.
        CamScripts& camscripts;  // The camera scripts.
        Migrate& migrate;        // The change of host.
        // The hero's position written to game memory.
        std::function<void(const float*)> teleport;
        // The item given, with ammunition, and asked for in the hand.
        std::function<void(int)> equip;
        // What a page shows: lobby, refused or rules, line by line.
        std::function<std::vector<std::string>(const std::string&)> page;
        // How wide the game's font draws a line.
        std::function<int(const std::string&)> width;
        // The description a party is listed with.
        std::function<std::string()> listed;
        // The party's saves, and one of the game's own saves asked for as a gold bolt asks.
        CoSave& cosave;
        std::function<bool()> game_save;
    };

    // Creates the bot over `parts`. It listens only when OPENRAC_MP_BOT names a port that can be
    // bound; otherwise on() is false.
    explicit Bot(Parts parts);
    // Closes the driver's socket and the listening one.
    ~Bot();

    // Whether OPENRAC_MP_BOT asked for it.
    bool on() const { return listen_ >= 0; }

    // Commands read and answered; a tick's held teleport put back. Each frame, and from the pad
    // while a movie runs without frames.
    void poll();
    // The buttons and stick pressed now, into the pad's report.
    void pad(std::uint8_t* report, int size);

private:
    // Runs one command `line` and returns the answer line: JSON, "ok" or "error ...".
    std::string run(const std::string& line);
    // Returns the status command's JSON object.
    std::string status();
    // Returns the dump command's JSON list of the level's mobys.
    std::string dump();
    // Returns the stats command's JSON object of what the network layer counted.
    std::string stats();
    // Returns the mods command's JSON object of what the party's mod check knows.
    std::string mods();

    // The parts it drives.
    Parts parts_;
    long listen_ = -1;           // The listening socket; -1 when the bot is off.
    long client_ = -1;           // The driver's socket; -1 when none is connected.
    std::string input_;          // Bytes received that are not yet a whole line.
    float at_[3] = {0, 0, 0};    // Where the teleport puts the hero.
    int teleport_ticks_ = 0;     // How many more ticks the teleport is written.
    std::uint32_t mask_ = 0;     // The pressed buttons, the pad's mask.
    int press_ticks_ = 0;        // How many more reports the press is written into.
    int stick_[2] = {128, 128};  // The stick's x and y; 128 is centred.
};

}  // namespace mp

#endif  // MULTIPLAYER_BOT
#endif  // MULTIPLAYER_BOT_H
