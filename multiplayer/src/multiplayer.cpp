/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: online multiplayer, the first step (README.md): a Multiplayer item in the
 * title's main menu, under Load Game (mod interface 1.7), whose pages are the game's own menus:
 * they connect to the OpenRAC server, show the player's name (the launcher's,
 * OPENRAC_PLAYER_NAME), find a match (the server puts the player in an open party with room, or
 * makes one), create a private party, or join one by its code, then show the lobby: the code, the
 * members with the host marked, Ready or Start, and Leave. Nothing of the game is played together
 * yet: no gameplay is sent.
 *
 * The pages, drawn by the game (its frames, list, font, prompt box and sounds):
 *   Home   Find match (any open party), Create match (a private one, joined by its code) and
 *          Join by code (the next page), and the player's name;
 *   Code   a row per character of the code, Left and Right step it through the server's code
 *          alphabet, then Join;
 *   Lobby  Ready (a member) or Start (the host), Leave, and the party's members. Leave and
 *          Triangle ask first, in the game's prompt box.
 * Cross chooses, Triangle goes back a page (from Home, to the game's menu). The footer says what
 * the connection is doing. The connection (connection.cpp) is opened the first time Home shows and
 * lasts until the game ends, reconnecting when the server goes; the session follows it every tick.
 *
 * Settings (openrac-mod.json):
 *   server  (string, default localhost:7770)  The OpenRAC server, host:port.
 */
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "connection.hpp"
#include "openrac/mod.hpp"
#include "protocol.hpp"
#include "session.hpp"

namespace {

/* The version HELLO gives the server: the mod's (at most 32 characters). */
constexpr const char* kVersion = "rac1-multiplayer/0.2.0";

/* The question's tag: leaving the party. */
constexpr std::uint32_t kAskLeave = 1;

/* The Lobby's buttons, then its lines; the Code page's characters, then Join. */
constexpr int kReadyItem = 0;
constexpr int kLeaveItem = 1;
constexpr int kJoinItem = static_cast<int>(mp::kCodeLength);

// ---- State ----

struct State {
    std::string host;
    int port = mp::kDefaultPort;
    std::unique_ptr<mp::Connection> connection;
    std::unique_ptr<mp::Session> session;
    std::uint32_t generation = 0;  // the connection's, as the session last saw it
    openrac::UiPage home, code_page, lobby;
    std::vector<int> code = std::vector<int>(mp::kCodeLength, 0);  // indices into kCodeAlphabet
    std::string notice;  // the last refusal or news, until the next action
};

std::unique_ptr<State> g;

/* The launcher's name, or one of the mod's own when it gives none (started without the launcher). */
std::string player_name() {
    const char* name = std::getenv("OPENRAC_PLAYER_NAME");

    // The launcher passes only a valid one; anything else is not used.
    if (name != nullptr && mp::valid_name(name)) {
        return name;
    }

    return "Player" + std::to_string(mp::now_ms() % 1000);
}

/* The status line: the connection, or the last notice. */
std::string status() {
    const std::string where = g->host + ":" + std::to_string(g->port);

    switch (g->connection->state()) {
        case mp::Connection::State::kIdle:
        case mp::Connection::State::kConnecting:
            return "Connecting to " + where + "...";
        case mp::Connection::State::kUnreachable:
            return "Server unreachable, trying again";
        case mp::Connection::State::kLost:
            return "Not connected: the server went away";
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

/* Home: Find match, Create match, Join by code (usable once the server said hello), the name. */
void show_home() {
    const bool usable = g->session->greeted();
    openrac::UiContent c;
    c.title = "Multiplayer";
    c.items = {
        openrac::UiItem::button("Find match", usable),
        openrac::UiItem::button("Create match", usable),
        openrac::UiItem::button("Join by code", usable),
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

/* Lobby: Ready or Start, Leave, then the members (the host marked, the ready ones, the player). */
void show_lobby() {
    const mp::Session& s = *g->session;
    openrac::UiContent c;
    c.title = "Party " + s.code();
    const std::string first = s.is_host() ? "Start" : s.ready(s.id()) ? "Not ready" : "Ready";
    c.items = {
        openrac::UiItem::button(first, !s.started()),
        openrac::UiItem::button("Leave"),
    };

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

/* Home's events: the connection opens the first time it shows; in a party, the lobby. */
void on_home(const openrac::UiPage& page, const openrac::UiEvent& e) {
    mp::Session& s = *g->session;

    // The first showing starts the connection; it stays for the rest of the game.
    if (e.is(OPENRAC_UI_SHOW)) {
        g->connection->open(g->host, g->port);
        g->notice.clear();
    } else if (e.is(OPENRAC_UI_CHOOSE)) {
        g->notice.clear();

        // Find match and Create match ask the server for a party; Join by code asks for a code.
        if (e.item() == 0) {
            s.find();
        } else if (e.item() == 1) {
            s.create();
        } else if (e.item() == 2) {
            g->code_page.open();
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

/* Lobby's events: Ready or Start, and Leave or Triangle asking first; out of the party, back. */
void on_lobby(const openrac::UiPage& page, const openrac::UiEvent& e) {
    mp::Session& s = *g->session;

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
        page.confirm(
            s.is_host() ? "Leave the party? It ends for everyone." : "Leave the party?", kAskLeave
        );
    } else if (e.is(OPENRAC_UI_ANSWER) && e.tag() == kAskLeave && e.value() == 1) {
        g->notice.clear();
        s.leave();
    }

    // The notice the action made shows at once.
    if (std::string notice = s.take_notice(); !notice.empty()) {
        g->notice = notice;
    }

    // Out of the party (left, ended by the host, or the connection gone): back to Home.
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
    g->session = std::make_unique<mp::Session>(*g->connection, kVersion, player_name());
    g->home = mod.ui_page(on_home);
    g->code_page = mod.ui_page(on_code);
    g->lobby = mod.ui_page(on_lobby, OPENRAC_UI_OWN_BACK);
    show_home();
    show_code();
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
OPENRAC_MOD_CPP("multiplayer", "0.2.0", load)

/* The port calls this at its exit: the connection's thread goes with it. */
extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    // The state exists only if load got as far as making it.
    if (g) {
        g->connection->close();
        g.reset();
    }
}
