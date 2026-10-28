/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The pages' logic that needs no game (multiplayer.cpp draws them): which of Home's items the
 * connection allows, the status line's words, and the lines of the pause menu's Multiplayer page. Header only, so the tests use it as the pages do.
 *
 * Only Find match, Create match and Join by code depend on the OpenRAC server. Direct connect (and
 * Host directly, Join by address) never do: they are always selectable, and the server's state
 * never shows on their pages.
 */
#ifndef MULTIPLAYER_MENU_H
#define MULTIPLAYER_MENU_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "connection.hpp"

namespace mp {

/* The footer's button lines (the game's glyphs: \x10 Cross, \x12 Triangle). */
constexpr const char* kSelectBackHints = "\x10 Select   \x12 Back";
constexpr const char* kSelectLeaveHints = "\x10 Select   \x12 Leave";
constexpr const char* kCodeHints = "Left, Right change   \x10 Join";
constexpr const char* kJoinHints = "Left, Right change   \x10 Join   \x12 Back";

/* The longest footer line a page has; a status line may not be longer. */
constexpr std::size_t kFooterMax = std::char_traits<char>::length(kJoinHints);

/* What the status line is made of. */
struct StatusInput {
    Connection::Mode mode = Connection::Mode::kNone;
    Connection::State state = Connection::State::kIdle;
    bool greeted = false;  // the session said hello and was answered
    bool waiting = false;  // the session waits for the server's answer
    std::string notice;    // the last refusal or news
    std::string address;   // the direct host's, "host:port"
    int hosting_port = 0;
    bool hosting_udp = false;
};

/* Whether Find match, Create match and Join by code can be chosen: linked to the server, which
 * said hello. Direct connect is not asked: it is always enabled. */
inline bool server_items_enabled(Connection::Mode mode, bool greeted) {
    return mode == Connection::Mode::kServer && greeted;
}

/* Whether the server reconnect loop may run: only linked to the server (not hosting, not joined
 * directly, and not between the two). */
inline bool server_loop_wanted(Connection::Mode mode) {
    return mode == Connection::Mode::kServer;
}

/* The status line: the connection, or the last notice. Short: kFooterMax characters or fewer
 * (an address is at most 21: "255.255.255.255:65535"). */
inline std::string status_text(const StatusInput& in) {
    using Mode = Connection::Mode;
    using State = Connection::State;
    const bool direct = in.mode == Mode::kDirect;

    // Hosting is connected from the start; nothing linked is the moment after a failed host.
    if (in.mode == Mode::kHosting) {
        if (!in.notice.empty()) {
            return in.notice;
        }

        return "Hosting on port " + std::to_string(in.hosting_port)
               + (in.hosting_udp ? "" : ", TCP only");
    } else if (in.mode == Mode::kNone) {
        return "Not connected";
    }

    switch (in.state) {
        case State::kIdle:
        case State::kConnecting:
            return direct ? "Joining " + in.address : "Connecting...";
        case State::kUnreachable:
            return direct ? "Host unreachable" : "Server unreachable";
        case State::kLost:
            return direct ? "The host went away" : "The server went away";
        case State::kConnected:
            break;
    }

    // Connected: a notice first, then what is going on.
    if (!in.notice.empty()) {
        return in.notice;
    }

    if (!in.greeted) {
        return "Connected, saying hello...";
    }

    if (in.waiting) {
        return "Waiting for the server...";
    }

    return direct ? "Joined " + in.address : "Connected to the server";
}

/* ---- The pause menu's Multiplayer page (Options, in a level) ---- */

/* A member as the page shows it: its name, whether it hosts, whether it is the player, its link
 * (session.hpp's Link: the round trip, -1 not known, and UDP or TCP). */
struct PartyMemberLine {
    std::string name;
    bool host = false;
    bool you = false;
    std::int32_t ping_ms = -1;
    bool udp = false;
};

/* What the page is made of. */
struct PartyPageInput {
    bool in_party = false;
    bool direct = false;     // a Direct connect party: no code, `where` instead
    std::string code;        // the party's code
    std::string where;       // a direct party's address ("192.168.1.5:7770")
    std::string host_level;  // the host's level's name, empty when it is in none
    std::vector<PartyMemberLine> members;  // the host first
};

/* A line of the page: a label and its value (a line of information, or the Leave button). */
struct PartyLine {
    enum class Kind {
        kInfo,
        kLeave,
        kText
    };
    Kind kind = Kind::kInfo;
    std::string label;
    std::string value;
};

constexpr const char* kPartyPageTitle = "Multiplayer";
constexpr const char* kLeavePartyLabel = "Leave party";

/* A link as the page says it: "38 ms UDP", "... TCP" while the round trip is not known yet. */
inline std::string link_text(std::int32_t ping_ms, bool udp) {
    const std::string ping = ping_ms < 0 ? "..." : std::to_string(ping_ms) + " ms";
    return ping + (udp ? " UDP" : " TCP");
}

/* The page's lines: out of a party, only that; in one, its code (or where a direct one is), the
 * host's level, a line per member (the host marked, the player's own), then Leave party. */
inline std::vector<PartyLine> party_lines(const PartyPageInput& in) {
    using Kind = PartyLine::Kind;

    if (!in.in_party) {
        return {{Kind::kText, "Not in a party", ""}};
    }

    std::vector<PartyLine> lines;
    lines.push_back(in.direct ? PartyLine{Kind::kInfo, "Direct", in.where}
                              : PartyLine{Kind::kInfo, "Party code", in.code});
    lines.push_back({Kind::kInfo, "Host's level", in.host_level.empty() ? "None" : in.host_level});

    for (const PartyMemberLine& m : in.members) {
        const std::string mark = m.host && m.you ? " (host, you)"
                                 : m.host        ? " (host)"
                                 : m.you         ? " (you)"
                                                 : "";
        lines.push_back({Kind::kInfo, m.name + mark, link_text(m.ping_ms, m.udp)});
    }

    lines.push_back({Kind::kLeave, kLeavePartyLabel, ""});
    return lines;
}

/* The question Leave party asks, short for the page's title box: the host leaving (or stopping a
 * direct party it hosts) ends the party for everyone. */
inline const char* pause_leave_question(bool hosting, bool host) {
    return hosting ? "Stop hosting?" : host ? "End the party?" : "Leave the party?";
}

}  // namespace mp

#endif /* MULTIPLAYER_MENU_H */
