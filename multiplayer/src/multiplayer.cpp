/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: online multiplayer, the first step (README.md): a Multiplayer item in the
 * title's main menu, under Load Game (mod interface 1.7), whose pages are the game's own menus:
 * they connect to the OpenRAC server, show the player's name (the `name` setting, else the
 * launcher's, OPENRAC_PLAYER_NAME), find a match (the server puts the player in an open party with
 * room, or makes one), create a private party, or join one by its code, then show the lobby: the
 * code, the members with the host marked, Ready or Start, and Leave. Direct connect plays without
 * a server: this game hosts (connection.hpp's embedded host), or joins another's by its address.
 * Nothing of the game is played together yet: no gameplay is sent.
 *
 * The pages, drawn by the game (its frames, list, font, prompt box and sounds):
 *   Home    Find match (any open party), Create match (a private one, joined by its code), Join
 *           by code and Direct connect (the next pages), and the player's name;
 *   Code    a row per character of the code, Left and Right step it through the server's code
 *           alphabet, then Join;
 *   Direct  Host directly and Join by address; Join by address's rows (the address's four octets
 *           and the port, direct.hpp's rings) and Join take the page's place;
 *   Lobby   Ready (a member) or Start (the host), Leave, and the party's members; a direct one
 *           says where it is hosted instead of a code. Leave and Triangle ask first, in the
 *           game's prompt box. Leaving a direct party goes back to the server.
 * Cross chooses, Triangle goes back a page (from Home, to the game's menu). The footer says what
 * the connection is doing. The connection (connection.cpp) is opened the first time Home shows and
 * lasts until the game ends, reconnecting when the server goes; the session follows it every tick.
 *
 * Settings (openrac-mod.json):
 *   server  (string, default localhost:7770)  The OpenRAC server, host:port.
 *   name    (string, default empty)  The player's name (3 to 16 letters, digits, underscores);
 *           empty or invalid: the launcher's Multiplayer name.
 */
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "connection.hpp"
#include "direct.hpp"
#include "openrac/mod.hpp"
#include "protocol.hpp"
#include "session.hpp"

namespace {

/* The version HELLO gives the server: the mod's (at most 32 characters). */
constexpr const char* kVersion = "rac1-multiplayer/0.3.0";

/* The question's tag: leaving the party (or hosting it). */
constexpr std::uint32_t kAskLeave = 1;

/* Home's buttons. */
constexpr int kFindItem = 0;
constexpr int kCreateItem = 1;
constexpr int kCodeItem = 2;
constexpr int kDirectItem = 3;

/* The Lobby's buttons, then its lines; the Code page's characters, then Join. */
constexpr int kReadyItem = 0;
constexpr int kLeaveItem = 1;
constexpr int kJoinItem = static_cast<int>(mp::kCodeLength);

/* The Direct page's buttons; Join by address's rows: four octets, the port by one and by a
 * hundred, then Join. */
constexpr int kHostItem = 0;
constexpr int kByAddressItem = 1;
constexpr int kPortItem = 4;
constexpr int kPortCoarseItem = 5;
constexpr int kJoinAddressItem = 6;

/* What the Direct page shows: its two buttons, or Join by address's rows. */
enum class DirectView {
    kMenu,
    kJoin
};

// ---- State ----

struct State {
    std::string host;
    int port = mp::kDefaultPort;
    std::unique_ptr<mp::Connection> connection;
    std::unique_ptr<mp::Session> session;
    std::uint32_t generation = 0;  // the connection's, as the session last saw it
    openrac::UiPage home, code_page, direct_page, lobby;
    std::vector<int> code = std::vector<int>(mp::kCodeLength, 0);  // indices into kCodeAlphabet
    std::string notice;  // the last refusal or news, until the next action
    DirectView view = DirectView::kMenu;
    mp::DirectAddress address;  // Join by address's, the last joined kept for the game
    mp::Acceleration acceleration;
    std::string direct_error;  // why Host directly or Join failed, on the Direct page
};

std::unique_ptr<State> g;

/* The player's name: the mod's `name` setting, else the launcher's (OPENRAC_PLAYER_NAME), else
 * one of the mod's own (neither valid, or started without the launcher). */
std::string player_name(const std::string& setting) {
    const char* launcher = std::getenv("OPENRAC_PLAYER_NAME");
    const std::string name = mp::choose_name(setting, launcher != nullptr ? launcher : "");

    // Neither is a valid name: one of the mod's own.
    if (!name.empty()) {
        return name;
    }

    return "Player" + std::to_string(mp::now_ms() % 1000);
}

/* The status line: the connection, or the last notice. */
std::string status() {
    using Mode = mp::Connection::Mode;
    const Mode mode = g->connection->mode();
    const bool direct = mode == Mode::kDirect;
    const std::string where =
        direct ? g->connection->address() : g->host + ":" + std::to_string(g->port);

    // Hosting is connected from the start; nothing linked is the moment after a failed host.
    if (mode == Mode::kHosting) {
        const mp::HostingStatus h = g->connection->hosting();

        if (!g->notice.empty()) {
            return g->notice;
        }

        return "Hosting on port " + std::to_string(h.port) + (h.udp ? "" : ", TCP only");
    } else if (mode == Mode::kNone) {
        return "Not connected";
    }

    switch (g->connection->state()) {
        case mp::Connection::State::kIdle:
        case mp::Connection::State::kConnecting:
            return "Connecting to " + where + "...";
        case mp::Connection::State::kUnreachable:
            return direct ? "Cannot reach " + where + ", trying again"
                          : "Server unreachable, trying again";
        case mp::Connection::State::kLost:
            return direct ? "Not connected: the host went away"
                          : "Not connected: the server went away";
        case mp::Connection::State::kConnected:
            break;
    }

    // Connected: a notice first, then what is going on.
    if (!g->notice.empty()) {
        return g->notice;
    }

    if (!g->session->greeted()) {
        return "Connected, saying hello...";
    }

    return g->session->waiting() ? "Waiting for the server..." : "Connected to " + where;
}

/* Brings the session up to date with the connection: hello on a new connection, the server's
 * messages, the last notice. Once a game tick. */
void poll() {
    const std::uint32_t generation = g->connection->generation();
    const bool connected = g->connection->state() == mp::Connection::State::kConnected;

    // A new connection: a new session on the server.
    if (connected && generation != g->generation) {
        g->generation = generation;
        g->session->connected();
    } else if (!connected && g->session->greeted()) {
        // The connection went: so did the party.
        g->session->disconnected();
    }

    for (const mp::Message& m : g->connection->receive()) {
        g->session->handle(m);
    }

    // A refusal or news replaces the last.
    if (std::string notice = g->session->take_notice(); !notice.empty()) {
        g->notice = notice;
    }
}

/* The code as the server reads it, a character per slot. */
std::string code_text() {
    std::string code;

    for (const int i : g->code) {
        code += mp::kCodeAlphabet[i];
    }

    return code;
}

// ---- The pages, in the game's own menus ----

/* Home: Find match, Create match, Join by code (usable once the server said hello), Direct
 * connect (always: it needs no server), the name. */
void show_home() {
    const bool usable =
        g->session->greeted() && g->connection->mode() == mp::Connection::Mode::kServer;
    openrac::UiContent c;
    c.title = "Multiplayer";
    c.items = {
        openrac::UiItem::button("Find match", usable),
        openrac::UiItem::button("Create match", usable),
        openrac::UiItem::button("Join by code", usable),
        openrac::UiItem::button("Direct connect"),
        openrac::UiItem::text("Playing as " + g->session->name()),
    };
    c.footer = {status(), OPENRAC_UI_CROSS " Select   " OPENRAC_UI_TRIANGLE " Back"};
    g->home.set(c);
}

/* Code: a choice of the code alphabet per character, then Join. */
void show_code() {
    const std::string alphabet = mp::kCodeAlphabet;
    std::vector<std::string> letters;

    for (const char c : alphabet) {
        letters.emplace_back(1, c);
    }

    openrac::UiContent c;
    c.title = "Party code " + code_text();

    for (std::size_t i = 0; i < mp::kCodeLength; ++i) {
        c.items.push_back(
            openrac::UiItem::choice_of(
                "Character " + std::to_string(i + 1),
                letters,
                static_cast<std::uint32_t>(g->code[i])
            )
        );
    }

    c.items.push_back(openrac::UiItem::button("Join", g->session->greeted()));
    c.footer = {status(), "Left, Right change   " OPENRAC_UI_CROSS " Join"};
    g->code_page.set(c);
}

/* A ring row (direct.hpp): its window of values around `value`, the current one in the middle. */
openrac::UiItem ring_row(const std::string& label, int value, const mp::Ring& ring) {
    return openrac::UiItem::choice_of(
        label, mp::ring_choices(value, ring), static_cast<std::uint32_t>(mp::kRingCentre)
    );
}

/* Direct: Host directly and Join by address; or, in Join by address's place, the address's rows
 * and Join. The footer: the connection, the last failure, the buttons. */
void show_direct() {
    openrac::UiContent c;
    std::string hints;

    if (g->view == DirectView::kMenu) {
        c.title = "Direct connect";
        c.items = {
            openrac::UiItem::button("Host directly"),
            openrac::UiItem::button("Join by address"),
            openrac::UiItem::text(
                "No server: port " + std::to_string(mp::kDefaultPort) + ", TCP and UDP"
            ),
        };
        hints = OPENRAC_UI_CROSS " Select   " OPENRAC_UI_TRIANGLE " Back";
    } else {
        const mp::DirectAddress& a = g->address;
        c.title = "Join " + a.text();

        for (std::size_t i = 0; i < a.octets.size(); ++i) {
            c.items.push_back(
                ring_row("Address part " + std::to_string(i + 1), a.octets[i], mp::kOctetRing)
            );
        }

        c.items.push_back(ring_row("Port", a.port, mp::kPortRing));
        c.items.push_back(ring_row("Port, by 100", a.port, mp::kPortCoarseRing));
        c.items.push_back(openrac::UiItem::button("Join"));
        hints = "Left, Right change   " OPENRAC_UI_CROSS " Join   " OPENRAC_UI_TRIANGLE " Back";
    }

    // Before a host or a join, the server's status says nothing of Direct connect.
    const mp::Connection::Mode mode = g->connection->mode();
    const bool linked =
        mode == mp::Connection::Mode::kDirect || mode == mp::Connection::Mode::kHosting;
    c.footer = {linked ? status() : "Plays without the server"};

    if (!g->direct_error.empty()) {
        c.footer.push_back(g->direct_error);
    }

    c.footer.push_back(hints);
    g->direct_page.set(c);
}

/* Lobby: Ready or Start, Leave, then the members (the host marked, the ready ones, the player). A
 * direct party's title says where it is: the host's own addresses (all that fit, the first in the
 * title), or the address joined. */
void show_lobby() {
    const mp::Session& s = *g->session;
    openrac::UiContent c;
    const std::string first = s.is_host() ? "Start" : s.ready(s.id()) ? "Not ready" : "Ready";
    c.items = {
        openrac::UiItem::button(first, !s.started()),
        openrac::UiItem::button("Leave"),
    };

    // Where the party is: its code, or for a direct one, where it is hosted.
    if (!s.direct()) {
        c.title = "Party " + s.code();
    } else if (g->connection->mode() == mp::Connection::Mode::kHosting) {
        const mp::HostingStatus h = g->connection->hosting();
        // The room left past the buttons, the members and the count line, plus the title.
        const std::size_t used = c.items.size() + s.members().size() + 1;
        const std::size_t room =
            1 + (OPENRAC_UI_ITEMS_MAX > used ? OPENRAC_UI_ITEMS_MAX - used : 0);
        const std::vector<std::string> lines = mp::hosting_lines(h.addresses, h.port, room);
        c.title = lines[0];

        for (std::size_t i = 1; i < lines.size(); ++i) {
            c.items.push_back(openrac::UiItem::text(lines[i]));
        }
    } else {
        c.title = "Party at " + g->connection->address();
    }

    for (const mp::Member& m : s.members()) {
        const bool host = m.id == s.host();
        std::string line = m.name + (host ? " (host)" : s.ready(m.id) ? " (ready)" : "");
        line += m.id == s.id() ? " - you" : "";
        c.items.push_back(openrac::UiItem::text(line));
    }

    const std::string ping = s.has_clock() ? ", ping " + std::to_string(s.rtt_ms()) + " ms" : "";
    c.items.push_back(
        openrac::UiItem::text(
            std::to_string(s.members().size()) + " of " + std::to_string(mp::kMaxPartySize)
            + " players" + ping
        )
    );
    c.footer = {status(), OPENRAC_UI_CROSS " Select   " OPENRAC_UI_TRIANGLE " Leave"};
    g->lobby.set(c);
}

/* Back to the configured server, from a direct link or none (a host stopped, a join given up), so
 * Find, Create and Join by code work again. Nothing while linked to the server already. */
void back_to_server() {
    const mp::Connection::Mode mode = g->connection->mode();

    if (mode == mp::Connection::Mode::kNone) {
        g->connection->open(g->host, g->port);
    } else if (mode != mp::Connection::Mode::kServer) {
        g->connection->connect_server(g->host, g->port);
        poll();
    }
}

/* Home's events: the connection opens the first time it shows (and goes back to the server after
 * Direct connect); in a party, the lobby. */
void on_home(const openrac::UiPage& page, const openrac::UiEvent& e) {
    mp::Session& s = *g->session;

    // Showing: linked to the server, opened the first time; it stays for the rest of the game.
    if (e.is(OPENRAC_UI_SHOW)) {
        back_to_server();
        g->notice.clear();
    } else if (e.is(OPENRAC_UI_CHOOSE)) {
        g->notice.clear();

        // Find match and Create match ask the server for a party; Join by code asks for a code,
        // Direct connect for a host or an address.
        if (e.item() == kFindItem) {
            s.find();
        } else if (e.item() == kCreateItem) {
            s.create();
        } else if (e.item() == kCodeItem) {
            g->code_page.open();
        } else if (e.item() == kDirectItem) {
            g->view = DirectView::kMenu;
            g->direct_error.clear();
            g->direct_page.open();
        }
    }

    // In a party (found, made, or kept from before): the lobby.
    if (s.in_party()) {
        g->lobby.open();
    }

    show_home();
    (void)page;
}

/* Code's events: a character stepped, Join; once in the party, the lobby in this page's place. */
void on_code(const openrac::UiPage& page, const openrac::UiEvent& e) {
    // A character's choice is its index in the alphabet.
    if (e.is(OPENRAC_UI_CHANGE) && e.item() >= 0 && e.item() < kJoinItem) {
        g->code[static_cast<std::size_t>(e.item())] = e.value();
    } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() == kJoinItem) {
        g->notice.clear();
        g->session->join(code_text());
    }

    // Joined: the lobby.
    if (g->session->in_party()) {
        g->lobby.open();
    }

    show_code();
    (void)page;
}

/* Join by address's row `item` changed to `choice`: the octet or the port it steps. */
void step_address(int item, int choice) {
    mp::DirectAddress& a = g->address;
    const int times = g->acceleration.times(item, mp::now_ms());

    if (item >= 0 && item < static_cast<int>(a.octets.size())) {
        int& octet = a.octets[static_cast<std::size_t>(item)];
        octet = mp::ring_step(octet, choice, mp::kOctetRing, times);
    } else if (item == kPortItem) {
        a.port = mp::ring_step(a.port, choice, mp::kPortRing, times);
    } else if (item == kPortCoarseItem) {
        a.port = mp::ring_step(a.port, choice, mp::kPortCoarseRing, times);
    }
}

/* The Direct page's view changes in place, with the game's page-change sound, the cursor on
 * `item`. */
void switch_view(const openrac::UiPage& page, DirectView view, int item) {
    g->view = view;
    g->direct_error.clear();
    g->acceleration.reset();
    show_direct();
    page.set_cursor(item);
    page.sound(OPENRAC_UI_SOUND_PAGE);
}

/* Direct's events: Host directly hosts at once; Join by address shows the address's rows, whose
 * Join connects; once in the party (the hello answered), the lobby in this page's place. Triangle
 * goes from the rows back to the two buttons (giving up a join that is connecting), and from the
 * buttons back to Home. */
void on_direct(const openrac::UiPage& page, const openrac::UiEvent& e) {
    using Mode = mp::Connection::Mode;
    mp::Connection& connection = *g->connection;

    if (e.is(OPENRAC_UI_BACK)) {
        // From the rows: the two buttons again, and no join pending.
        if (g->view == DirectView::kJoin) {
            if (connection.mode() == Mode::kDirect) {
                back_to_server();
            }

            switch_view(page, DirectView::kMenu, kByAddressItem);
            return;
        }

        page.back();
        return;
    }

    if (g->view == DirectView::kMenu && e.is(OPENRAC_UI_CHOOSE)) {
        // Host directly: this game hosts; a port taken stays here, saying so.
        if (e.item() == kHostItem) {
            g->notice.clear();
            g->direct_error.clear();

            if (connection.start_hosting(mp::kDefaultPort)) {
                poll();
            } else {
                const std::string error = connection.hosting().error;
                g->direct_error = error.empty() ? "Cannot host here" : error;
                page.sound(OPENRAC_UI_SOUND_DENIED);
            }
        } else if (e.item() == kByAddressItem) {
            switch_view(page, DirectView::kJoin, 0);
            return;
        }
    } else if (g->view == DirectView::kJoin) {
        if (e.is(OPENRAC_UI_CHANGE)) {
            step_address(e.item(), e.value());
        } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() == kJoinAddressItem) {
            g->notice.clear();
            g->direct_error.clear();

            // Only an address with no host is refused; the rows always give one.
            if (connection.connect_direct(g->address.text())) {
                poll();
            } else {
                g->direct_error = "Not an address: " + g->address.text();
            }
        }
    }

    // The connection's failures (unreachable, the host gone) and the host's refusals (a full
    // party) show in the status line.
    // In the party (hosting, or the host's hello answered): the lobby.
    if (g->session->in_party() && g->session->direct() && !e.is(OPENRAC_UI_HIDE)) {
        g->lobby.open();
    }

    show_direct();
}

/* Lobby's events: Ready or Start, and Leave or Triangle asking first; out of the party, back. A
 * host's Leave stops hosting. */
void on_lobby(const openrac::UiPage& page, const openrac::UiEvent& e) {
    mp::Session& s = *g->session;
    const bool hosting = g->connection->mode() == mp::Connection::Mode::kHosting;

    // Ready or Start; Leave and Triangle ask before the party is left.
    if (e.is(OPENRAC_UI_CHOOSE) && e.item() == kReadyItem) {
        g->notice.clear();

        // The host starts; a member says it is ready, or no longer.
        if (s.is_host()) {
            s.start();
        } else {
            s.toggle_ready();
        }
    } else if ((e.is(OPENRAC_UI_CHOOSE) && e.item() == kLeaveItem) || e.is(OPENRAC_UI_BACK)) {
        const char* question = hosting       ? "Stop hosting? The party ends for everyone."
                               : s.is_host() ? "Leave the party? It ends for everyone."
                                             : "Leave the party?";
        page.confirm(question, kAskLeave);
    } else if (e.is(OPENRAC_UI_ANSWER) && e.tag() == kAskLeave && e.value() == 1) {
        g->notice.clear();

        // The host's game stops listening (its clients get END); anyone else leaves.
        if (hosting) {
            g->connection->stop_hosting();
            poll();
        } else {
            s.leave();
        }
    }

    // The notice the action made shows at once.
    if (std::string notice = s.take_notice(); !notice.empty()) {
        g->notice = notice;
    }

    // Out of the party (left, ended by the host, or the connection gone): back to Home, which
    // links to the server again after a direct party.
    if (!s.in_party() && !e.is(OPENRAC_UI_HIDE)) {
        page.back();
    }

    show_lobby();
}

/* The port calls this when it loads the library: checks the game and the port, reads the server
 * address, makes the pages and puts Multiplayer in the main menu under Load Game. Throws
 * openrac::Error to refuse to run. */
void load(openrac::Mod& mod) {
    // The pages are Ratchet & Clank's front end.
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
    }

    // The pages of the game's own menus are interface 1.7's.
    if (!mod.has_ui()) {
        throw openrac::Error(
            "this OpenRAC has no pages of the game's menus for mods (interface 1.7)"
        );
    }

    // An earlier load's connection would keep its thread: stop it first.
    if (g) {
        g->connection->close();
    }

    g = std::make_unique<State>();
    const std::string server = mod.setting<std::string>("server");

    // An address that does not read is the default's.
    if (!mp::parse_address(server, g->host, g->port)) {
        g->host = "localhost";
        g->port = mp::kDefaultPort;
        mod.warn("server \"" + server + "\" is not host:port; using localhost:7770");
    }

    g->connection = std::make_unique<mp::Connection>();
    g->session = std::make_unique<mp::Session>(
        *g->connection, kVersion, player_name(mod.setting<std::string>("name"))
    );
    g->home = mod.ui_page(on_home);
    g->code_page = mod.ui_page(on_code);
    g->direct_page = mod.ui_page(on_direct, OPENRAC_UI_OWN_BACK);
    g->lobby = mod.ui_page(on_lobby, OPENRAC_UI_OWN_BACK);
    show_home();
    show_code();
    show_direct();
    show_lobby();
    mod.ui_main_item(OPENRAC_UI_LOAD_GAME, "Multiplayer", g->home);

    // The session follows the server every tick, whether a page shows or not.
    mod.on_frame([] { poll(); });
    mod.info(
        "multiplayer: " + g->session->name() + ", server " + g->host + ":" + std::to_string(g->port)
    );
}

}  // namespace

/* The id and version are the manifest's: the port checks they agree. */
OPENRAC_MOD_CPP("multiplayer", "0.3.0", load)

/* The port calls this at its exit: the connection's thread goes with it. */
extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    // The state exists only if load got as far as making it.
    if (g) {
        g->connection->close();
        g.reset();
    }
}
