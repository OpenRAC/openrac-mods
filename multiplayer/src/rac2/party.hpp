/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * A party without pages (party.cpp), for Going Commando, whose title has no mod pages: the player
 * says in the mod's settings what to do (the `party` setting, with `code` or `address`), and this
 * does it once the game runs, then keeps the co-op going with what the game does.
 *
 *   off          nothing: no connection, no party (the default);
 *   host         a party on the OpenRAC server; its code is on the banner and in the log, for the
 *                others to type into their `code`;
 *   join         the party `code` on the server;
 *   host_direct  this game hosts (Direct connect, port 7770): the others join its address;
 *   join_direct  the game hosting at `address` (host:port, 7770 when none).
 *
 * The host plays as it always does: once it is in a level the party's game is its game (Coop::adopt)
 * and each member is brought into it as a late joiner of Ratchet & Clank's is (the host's live save,
 * the host's level: play_start from the title, the member's own level left for the title first).
 * The members follow the host's level (its travels and level changes, Coop's LEVEL); the host back in
 * the title then in a level again is a new game of the party's, which the members are brought into
 * again. Everyone sees the others as figures in the same level (Coop's poses).
 *
 * Only parties of this game: the party's description carries the game's id (party_mods.hpp), and a
 * party that names another game (Ratchet & Clank's) is left at once, with why on the banner. The
 * mods a party agrees on are checked as in Ratchet & Clank (party_mods.hpp): a member whose mods
 * differ is told so and is not brought into the host's game.
 *
 * What the player reads: one line over the game (banner()): the co-op's own first, then the last
 * thing that happened here (the party's code, a refusal, someone in or out) for kNoticeMs, and the
 * party's code while its host is alone in it. Every step is a line in the log too (on_log).
 */
#ifndef MULTIPLAYER_RAC2_PARTY_H
#define MULTIPLAYER_RAC2_PARTY_H

#include <cstdint>
#include <functional>
#include <set>
#include <string>

#include "coop.hpp"
#include "party_mods.hpp"
#include "session.hpp"

namespace mp {

enum class PartyAction {
    kOff,
    kHost,
    kJoin,
    kHostDirect,
    kJoinDirect
};

/* The `party` setting's value as an action; anything else is kOff. */
PartyAction party_action(const std::string& value);

/* A code as typed: its ends trimmed, upper case. */
std::string party_code(const std::string& typed);

/* What the party asks of the connection (the mod's Connection; a test's lambdas). */
struct PartyNet {
    /* To the OpenRAC server (Connection::open). */
    std::function<void()> server;
    /* This game hosts (Connection::start_hosting), its party listed with `description`; false and
     * why when it cannot. */
    std::function<bool(const std::string& description, std::string& error)> host_direct;
    /* To the game hosting at `address` (Connection::connect_direct); false for no address. */
    std::function<bool(const std::string& address)> join_direct;
};

/* How long a notice stays on the banner; how long a create or a join waits for its answer before
 * it is asked again, and how often at most; how long the host is in the title before its next level
 * is a new game of the party's. */
constexpr std::uint64_t kNoticeMs = 8000;
constexpr std::uint64_t kAskAgainMs = 10000;
constexpr int kAsks = 3;
constexpr std::uint64_t kTitleMs = 1000;
/* What a party is listed with (party_mods.hpp's tag after it). */
constexpr const char* kRac2Description = "Going Commando";
constexpr const char* kOtherGameNotice = "That party plays another game: left it";

class PageParty {
public:
    PageParty(
        Session& session, Coop& coop, PartyMods& mods, PartyNet net, std::string game = kGameRac2
    );

    /* What the settings said (the code or the address as typed). Nothing happens before begin(). */
    void choose(PartyAction action, std::string code, std::string address);

    PartyAction action() const { return action_; }

    /* Every mod has loaded: the party's description can carry the digest of this game's mods, and
     * the action starts (the connection opened, or the direct host started). */
    void begin(std::uint64_t now);

    bool begun() const { return begun_; }

    /* Once a game tick, after the session has the connection's news; `status` the game's now. */
    void tick(std::uint64_t now, const GameStatus& status);

    /* The line over the game now, empty for none. */
    std::string banner(std::uint64_t now) const;

    /* The description the party is listed with: "Going Commando|3fa9/rac2". */
    std::string description() const;

    void on_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

private:
    void notice(std::uint64_t now, const std::string& text);

    void log(const std::string& line) const {
        if (log_) {
            log_("multiplayer: " + line);
        }
    }

    void ask(std::uint64_t now);
    void tick_party(std::uint64_t now, const GameStatus& status);

    Session& session_;
    Coop& coop_;
    PartyMods& mods_;
    PartyNet net_;
    std::string game_;
    PartyAction action_ = PartyAction::kOff;
    std::string code_;
    std::string address_;
    bool begun_ = false;
    /* The create or join asked: how many times, when last; whether the party was ever had (once it
     * goes, nothing is asked again: the player chose that party). */
    int asks_ = 0;
    std::uint64_t asked_ms_ = 0;
    bool had_party_ = false;
    bool in_party_ = false;
    /* The host: since when its game is in the title with the party's game on (0 not). */
    std::uint64_t title_since_ = 0;
    bool title_out_ = false;
    /* The members as last seen, for who came and went; whether the host's differing mods were said. */
    std::set<std::int32_t> members_;
    bool told_differs_ = false;
    std::string notice_;
    std::uint64_t notice_until_ = 0;
    std::function<void(const std::string&)> log_;
};

}  // namespace mp

#endif
