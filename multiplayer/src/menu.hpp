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

/* The longest line of the lobby's list. */
constexpr std::size_t kWhyLineMax = 47;

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
    bool left, bool lost, bool direct, const std::string& notice, Language language = Language::kEnglish
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
    std::int32_t id = 0;
};

/* What the page is made of. */
struct PartyPageInput {
    bool in_party = false;
    bool removes = false;                  // the player hosts: a member's line is a button
    bool direct = false;                   // a Direct connect party: no code, `where` instead
    std::string code;                      // the party's code
    std::string where;                     // a direct party's address ("192.168.1.5:7770")
    std::string host_level;                // the host's level's name, empty when it is in none
    std::vector<PartyMemberLine> members;  // the host first
    bool go_to_host = false;               // this player can ask to go to the host
};

/* A line of the page: a label and its value (a line of information, or the Leave button). */
struct PartyLine {
    enum class Kind {
        kInfo,
        kLeave,
        kText,
        kRules,
        kGoToHost,
        kMember
    };
    Kind kind = Kind::kInfo;
    std::string label;
    std::string value;
    std::int32_t member = 0;  // kMember: whose line
};

constexpr const char* kPartyPageTitle = "Multiplayer";
constexpr const char* kLeavePartyLabel = "Leave party";
constexpr const char* kPartyRulesLabel = "Party rules";
constexpr const char* kGoToHostLabel = "Go to host";
constexpr const char* kCopyCodeLabel = "Copy mod code";
constexpr const char* kCopiedLabel = "Copied";

/* A link as the page says it: "38 ms UDP", "... TCP" while the round trip is not known yet. */
inline std::string link_text(std::int32_t ping_ms, bool udp) {
    const std::string ping = ping_ms < 0 ? "..." : std::to_string(ping_ms) + " ms";
    return ping + (udp ? " UDP" : " TCP");
}

/* The pause page's item that saves the party (cosave.hpp). */
constexpr const char* kSavePartyLabel = "Save the party";

/* The page's lines: out of a party, only that; in one, its code (or where a direct one is), the
 * host's level, a line per member (the host marked, the player's own), then Go to host (a member
 * in the host's level), Party rules and Leave party. */
inline std::vector<PartyLine> party_lines(const PartyPageInput& in, Language language = Language::kEnglish) {
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
        const bool others = in.removes && !m.you && !m.host;
        lines.push_back({others ? Kind::kMember : Kind::kInfo, m.name + mark, link_text(m.ping_ms, m.udp), others ? m.id : 0});
    }

    if (in.go_to_host) {
        lines.push_back({Kind::kGoToHost, translated(kGoToHostLabel, language), ""});
    }

    lines.push_back({Kind::kRules, translated(kPartyRulesLabel, language), ""});
    lines.push_back({Kind::kLeave, translated(kLeavePartyLabel, language), ""});
    return lines;
}

/* A member's line in the lobby: its name, the host marked, one whose mods differ or are still being
 * checked, ready or not, and the player's own. */
struct LobbyMemberInput {
    std::string name;
    bool host = false;
    bool you = false;
    bool ready = false;
    bool differs = false;
    bool checking = false;
};

inline std::string lobby_member_line(const LobbyMemberInput& m, Language language = Language::kEnglish) {
    const char* state = m.host       ? translated(" (host)", language)
                        : m.differs  ? translated(" (mods differ)", language)
                        : m.checking ? translated(" (checking mods)", language)
                        : m.ready    ? translated(" (ready)", language)
                                     : translated(" (not ready, mods match)", language);
    const std::string end = std::string(state) + (m.you ? translated(" - you", language) : "");
    // A long name gives way: what the line says of the player is what it is for.
    return m.name.substr(0, kWhyLineMax - end.size()) + end;
}

/* The host's line saying why a member cannot play: "Bob: Difficulty settings, 1 more". */
inline std::string lobby_why_line(const std::string& name, const std::string& why) {
    const std::string line = name + ": " + why;
    return line.size() > kWhyLineMax ? line.substr(0, kWhyLineMax - 3) + "..." : line;
}

/* The words of taking a player out of the party. */
constexpr const char* kRemoveHint = "Choose a player to remove them";
constexpr const char* kRemoveLabel = "Remove from the party";
constexpr const char* kBanLabel = "Ban from this save";
constexpr const char* kBannedSent = "A banned player was sent away";
constexpr const char* kRemoveNote = "They can come back with the code";
constexpr const char* kBanNote = "The ban stays with the party's save";

/* What the host's Party rules page offers for the bans of its save: "Lift 2 bans". */
inline std::string lift_bans_label(std::size_t count) {
    return "Lift " + std::to_string(count) + (count == 1 ? " ban" : " bans");
}

/* What the page of a refused game says to do next. */
constexpr const char* kMatchHint1 = "Match the host in the launcher's Mods tab,";
constexpr const char* kMatchHint2 = "then restart the game and join again.";
constexpr const char* kSoloHint1 = "Turn it off in the launcher's Mods tab,";
constexpr const char* kSoloHint2 = "then restart the game.";
constexpr const char* kBackHints = "\x10 Select   \x12 Back";

/* The question Leave party asks, short for the page's title box: the host leaving (or stopping a
 * direct party it hosts) ends the party for everyone. */
inline const char* pause_leave_question(
    bool hosting, bool host, bool followed = false, Language language = Language::kEnglish
) {
    return hosting     ? translated("Stop hosting?", language)
           : !host     ? translated("Leave the party?", language)
           : followed  ? translated("Leave the party? Another player takes over.", language)
                       : translated("End the party?", language);
}

/* The same, in `language`, for a host no other player follows. */
inline const char* pause_leave_question(bool hosting, bool host, Language language) {
    return pause_leave_question(hosting, host, false, language);
}

}  // namespace mp

#endif /* MULTIPLAYER_MENU_H */
