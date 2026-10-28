/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The pages' logic that needs no game (multiplayer.cpp draws them): which of Home's items the
 * connection allows, and the status line's words. Header only, so the tests use it as the pages do.
 *
 * Only Find match, Create match and Join by code depend on the OpenRAC server. Direct connect (and
 * Host directly, Join by address) never do: they are always selectable, and the server's state
 * never shows on their pages.
 */
#ifndef MULTIPLAYER_MENU_H
#define MULTIPLAYER_MENU_H

#include <cstddef>
#include <cstring>
#include <string>

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

}  // namespace mp

#endif /* MULTIPLAYER_MENU_H */
