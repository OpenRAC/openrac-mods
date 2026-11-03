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
#include "words.hpp"

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
inline std::string status_text(const StatusInput& in, Language language = Language::kEnglish) {
    using Mode = Connection::Mode;
    using State = Connection::State;
    const bool direct = in.mode == Mode::kDirect;

    // Hosting is connected from the start; nothing linked is the moment after a failed host.
    if (in.mode == Mode::kHosting) {
        if (!in.notice.empty()) {
            return localized_notice(in.notice, language);
        }

        return translated("Hosting on port ", language) + std::to_string(in.hosting_port)
               + (in.hosting_udp ? "" : translated(", TCP only", language));
    } else if (in.mode == Mode::kNone) {
        return translated("Not connected", language);
    }

    switch (in.state) {
        case State::kIdle:
        case State::kConnecting:
            return direct ? translated("Joining ", language) + in.address
                          : translated("Connecting...", language);
        case State::kUnreachable:
            return direct ? translated("Host unreachable", language)
                          : translated("Server unreachable", language);
        case State::kLost:
            return direct ? translated("The host went away", language)
                          : translated("The server went away", language);
        case State::kConnected:
            break;
    }

    // Connected: a notice first, then what is going on.
    if (!in.notice.empty()) {
        return localized_notice(in.notice, language);
    }

    if (!in.greeted) {
        return translated("Connected, saying hello...", language);
    }

    if (in.waiting) {
        return translated("Waiting for the server...", language);
    }

    return direct ? translated("Joined ", language) + in.address
                  : translated("Connected to the server", language);
}

/* Why the party went, for Home's status line when the player is brought back to it: nothing when
 * the player left it (or stopped hosting), the session's word when it said one (the host ended
 * it), the link that went (`lost`: the direct host's, or the server's), else that it is over. */
inline std::string leave_reason(
    bool left,
    bool lost,
    bool direct,
    const std::string& notice,
    Language language = Language::kEnglish
) {
    if (left) {
        return "";
    } else if (!notice.empty()) {
        return localized_notice(notice, language);
    } else if (lost) {
        return direct ? translated("The host went away", language)
                      : translated("The server went away", language);
    }

    return translated("The party is over", language);
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
    bool direct = false;                   // a Direct connect party: no code, `where` instead
    std::string code;                      // the party's code
    std::string where;                     // a direct party's address ("192.168.1.5:7770")
    std::string host_level;                // the host's level's name, empty when it is in none
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
inline std::vector<PartyLine> party_lines(
    const PartyPageInput& in, Language language = Language::kEnglish
) {
    using Kind = PartyLine::Kind;

    if (!in.in_party) {
        return {{Kind::kText, translated("Not in a party", language), ""}};
    }

    std::vector<PartyLine> lines;
    lines.push_back(
        in.direct ? PartyLine{Kind::kInfo, translated("Direct", language), in.where}
                  : PartyLine{Kind::kInfo, translated("Party code", language), in.code}
    );
    lines.push_back(
        {Kind::kInfo,
         translated("Host's level", language),
         in.host_level.empty() ? translated("None", language) : in.host_level}
    );

    for (const PartyMemberLine& m : in.members) {
        const std::string mark = m.host && m.you ? translated(" (host, you)", language)
                                 : m.host        ? translated(" (host)", language)
                                 : m.you         ? translated(" (you)", language)
                                                 : "";
        lines.push_back({Kind::kInfo, m.name + mark, link_text(m.ping_ms, m.udp)});
    }

    lines.push_back({Kind::kLeave, translated(kLeavePartyLabel, language), ""});
    return lines;
}

/* The question Leave party asks, short for the page's title box: the host leaving (or stopping a
 * direct party it hosts) ends the party for everyone. */
inline const char* pause_leave_question(
    bool hosting, bool host, Language language = Language::kEnglish
) {
    return hosting ? translated("Stop hosting?", language)
           : host  ? translated("End the party?", language)
                   : translated("Leave the party?", language);
}

}  // namespace mp

#endif /* MULTIPLAYER_MENU_H */
