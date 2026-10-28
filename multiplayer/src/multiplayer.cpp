/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank: online multiplayer, the first step (README.md): a Multiplayer entry in the
 * front-end menu (main menu, Options; mod interface 1.5) whose page connects to the OpenRAC server,
 * shows the player's name (the launcher's, OPENRAC_PLAYER_NAME), finds a match (the server puts the
 * player in an open party with room, or makes one), creates a private party, or joins one by its
 * code, then shows the lobby: the code, the members with the host marked, Ready or Start, and
 * Leave. Nothing of the game is played together yet: no gameplay is sent.
 *
 * The page has three screens:
 *   Home   Find match (any open party), Create match (a private one, joined by its code) and
 *          Join by code (entered on the next screen);
 *   Code   six characters of the server's code alphabet: Up and Down change one, Left and Right
 *          move, Cross joins;
 *   Lobby  the party, and Ready (a member) or Start (the host), and Leave.
 * Cross chooses, Triangle or Circle goes back (from Home, and from the Lobby, back to the game's
 * menu: the party stays). The connection (connection.cpp) is opened the first time the page opens
 * and lasts until the game ends, reconnecting when the server goes.
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

// ---- Look ----

/* The colours: the game's menu navy, its khaki frame, light blue, yellow for what is chosen, a dim
 * grey, red for a problem, green for ready. */
constexpr std::uint32_t kBackground = OPENRAC_RGBA(14, 14, 30, 255);
constexpr std::uint32_t kFrame = OPENRAC_RGBA(150, 146, 104, 255);
constexpr std::uint32_t kText = OPENRAC_RGBA(136, 168, 255, 255);
constexpr std::uint32_t kChosen = OPENRAC_RGBA(255, 255, 64, 255);
constexpr std::uint32_t kChosenBox = OPENRAC_RGBA(36, 36, 70, 255);
constexpr std::uint32_t kTitle = OPENRAC_RGBA(230, 230, 230, 255);
constexpr std::uint32_t kDim = OPENRAC_RGBA(110, 110, 130, 255);
constexpr std::uint32_t kProblem = OPENRAC_RGBA(255, 110, 90, 255);
constexpr std::uint32_t kReady = OPENRAC_RGBA(120, 230, 120, 255);

/* Where things go on the 512 by 448 page, in its pixels. */
constexpr int kTitleY = 36;
constexpr int kNameY = 84;
constexpr int kBodyY = 140;
constexpr int kButtonGap = 34;
constexpr int kButtonWidth = 220;
constexpr int kButtonHeight = 26;
constexpr int kMemberGap = 22;
constexpr int kStatusY = 380;
constexpr int kHintY = 418;
constexpr int kMargin = 16;

/* The version HELLO gives the server: the mod's (at most 32 characters). */
constexpr const char* kVersion = "rac1-multiplayer/0.1.0";

// ---- State ----

enum class Screen {
    kHome,
    kCode,
    kLobby
};

struct State {
    std::string host;
    int port = mp::kDefaultPort;
    std::unique_ptr<mp::Connection> connection;
    std::unique_ptr<mp::Session> session;
    std::uint32_t generation = 0;  // the connection's, as the session last saw it
    Screen screen = Screen::kHome;
    int cursor = 0;  // the button chosen on Home and in the Lobby
    int slot = 0;    // the code character chosen on Code
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
std::string status(std::uint32_t& colour) {
    colour = kDim;
    const std::string where = g->host + ":" + std::to_string(g->port);

    switch (g->connection->state()) {
        case mp::Connection::State::kIdle:
        case mp::Connection::State::kConnecting:
            return "Connecting to " + where + "...";
        case mp::Connection::State::kUnreachable:
            colour = kProblem;
            return "Server unreachable (" + where + "), trying again";
        case mp::Connection::State::kLost:
            colour = kProblem;
            return "Not connected: the server went away, trying again";
        case mp::Connection::State::kConnected:
            break;
    }

    // Connected: a notice first, then what is going on.
    if (!g->notice.empty()) {
        colour = kChosen;
        return g->notice;
    }

    if (!g->session->greeted()) {
        return "Connected, saying hello...";
    }

    return g->session->waiting() ? "Waiting for the server..." : "Connected to " + where;
}

/* Brings the session up to date with the connection: hello on a new connection, the server's
 * messages, and what is shown when the party comes or goes. */
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

    // The party decides the screen: in one, the lobby; out of one, never the lobby.
    if (g->session->in_party() && g->screen != Screen::kLobby) {
        g->screen = Screen::kLobby;
        g->cursor = 0;
    } else if (!g->session->in_party() && g->screen == Screen::kLobby) {
        g->screen = Screen::kHome;
        g->cursor = 0;
    }
}

// ---- Drawing ----

/* A button centred across the page with its top at y: boxed and yellow when chosen, dim when it
 * cannot be used now. */
void button(
    const openrac::MenuPage& page, int y, const std::string& label, bool chosen, bool usable
) {
    const int x = (openrac::MenuPage::width - kButtonWidth) / 2;

    // The chosen one is boxed.
    if (chosen) {
        page.rect(x - 2, y - 2, kButtonWidth + 4, kButtonHeight + 4, kFrame);
        page.rect(x, y, kButtonWidth, kButtonHeight, kChosenBox);
    }

    const std::uint32_t colour = !usable ? kDim : chosen ? kChosen : kText;
    page.text_centred(y + (kButtonHeight - OPENRAC_MENU_CHAR_HEIGHT * 2) / 2 + 1, 2, colour, label);
}

/* What every screen has: the background, the title, the name, the status and the keys. */
void frame(const openrac::MenuPage& page, const std::string& hint) {
    page.rect(0, 0, openrac::MenuPage::width, openrac::MenuPage::height, kBackground);
    page.rect(kMargin, kTitleY + 34, openrac::MenuPage::width - 2 * kMargin, 1, kFrame);
    page.text_centred(kTitleY, 3, kTitle, "MULTIPLAYER");
    page.text_centred(kNameY, 2, kText, "PLAYER: " + g->session->name());
    std::uint32_t colour = kDim;
    const std::string line = status(colour);
    page.rect(kMargin, kStatusY - 8, openrac::MenuPage::width - 2 * kMargin, 1, kFrame);
    page.text_centred(kStatusY, 1, colour, line);
    page.text_centred(kHintY, 1, kDim, hint);
}

void draw_home(const openrac::MenuPage& page) {
    const bool usable = g->session->greeted();
    frame(page, "X CHOOSE   TRIANGLE BACK");
    button(page, kBodyY + 20, "FIND MATCH", g->cursor == 0, usable);
    button(page, kBodyY + 20 + kButtonGap, "CREATE MATCH", g->cursor == 1, usable);
    button(page, kBodyY + 20 + 2 * kButtonGap, "JOIN BY CODE", g->cursor == 2, usable);
}

void draw_code(const openrac::MenuPage& page) {
    frame(page, "UP/DOWN CHANGE   LEFT/RIGHT MOVE   X JOIN   TRIANGLE BACK");
    page.text_centred(kBodyY, 2, kTitle, "ENTER THE PARTY CODE");
    constexpr int kBox = 36;
    constexpr int kBoxGap = 8;
    const int count = static_cast<int>(mp::kCodeLength);
    const int left = (openrac::MenuPage::width - count * kBox - (count - 1) * kBoxGap) / 2;
    const int top = kBodyY + 40;

    // A box per character, the chosen one framed.
    for (int i = 0; i < count; ++i) {
        const int x = left + i * (kBox + kBoxGap);
        const bool chosen = i == g->slot;
        page.rect(x - 2, top - 2, kBox + 4, kBox + 8 + 4, chosen ? kChosen : kFrame);
        page.rect(x, top, kBox, kBox + 8, kChosenBox);
        const char c = mp::kCodeAlphabet[g->code[static_cast<std::size_t>(i)]];
        page.text(
            x + (kBox - (OPENRAC_MENU_CHAR_WIDTH * 4 - 4)) / 2,
            top + 8,
            4,
            chosen ? kChosen : kTitle,
            std::string(1, c)
        );
    }
}

void draw_lobby(const openrac::MenuPage& page) {
    const mp::Session& s = *g->session;
    frame(page, "X CHOOSE   TRIANGLE BACK TO THE MENU (YOU STAY IN THE PARTY)");
    page.text_centred(kBodyY - 24, 2, kTitle, "PARTY CODE: " + s.code());
    int y = kBodyY + 4;

    // A line per member: the host marked, ready ones in green, the player's own named so.
    for (const mp::Member& m : s.members()) {
        const bool host = m.id == s.host();
        std::string line = m.name + (host ? "  (HOST)" : s.ready(m.id) ? "  READY" : "");
        line += m.id == s.id() ? "  <- YOU" : "";
        page.text_centred(y, 2, host || s.ready(m.id) ? kReady : kText, line);
        y += kMemberGap;
    }

    const std::string ping = s.has_clock() ? "   PING " + std::to_string(s.rtt_ms()) + " MS" : "";
    page.text_centred(
        y,
        1,
        kDim,
        std::to_string(s.members().size()) + " OF " + std::to_string(mp::kMaxPartySize) + " PLAYERS"
            + ping
    );
    const int buttons = kBodyY + 4 + static_cast<int>(mp::kMaxPartySize) * kMemberGap + 22;
    const std::string first = s.is_host() ? "START" : s.ready(s.id()) ? "NOT READY" : "READY";
    button(page, buttons, first, g->cursor == 0, !s.started());
    button(page, buttons + kButtonGap, "LEAVE", g->cursor == 1, true);
}

// ---- Input ----

/* One screen's buttons: the cursor moves between `count` of them with Up and Down. */
void move_cursor(const openrac::MenuFrame& f, int count) {
    // Up and Down wrap around.
    if (f.pressed(OPENRAC_PAD_UP)) {
        g->cursor = (g->cursor + count - 1) % count;
    } else if (f.pressed(OPENRAC_PAD_DOWN)) {
        g->cursor = (g->cursor + 1) % count;
    }
}

/* The page's frame: input, then drawing. True to close the page. */
bool page_frame(const openrac::MenuPage& page, const openrac::MenuFrame& f) {
    // The first time the page opens, the connection starts; it stays for the rest of the game.
    if (f.opened()) {
        g->connection->open(g->host, g->port);
        g->notice.clear();
    }

    poll();
    const bool back = f.pressed(OPENRAC_PAD_TRIANGLE | OPENRAC_PAD_CIRCLE);
    const bool choose = f.pressed(OPENRAC_PAD_CROSS);
    mp::Session& s = *g->session;

    switch (g->screen) {
        case Screen::kHome:
            // Back from Home is back to the game's menu.
            if (back) {
                return true;
            }

            move_cursor(f, 3);

            // Find match and Create match ask the server for a party; Join by code asks for a code.
            if (choose && s.greeted()) {
                g->notice.clear();

                // Each button its own action.
                if (g->cursor == 0) {
                    s.find();
                } else if (g->cursor == 1) {
                    s.create();
                } else {
                    g->screen = Screen::kCode;
                    g->slot = 0;
                }
            }

            draw_home(page);
            break;
        case Screen::kCode: {
            // Back to Home, the code kept for another try.
            if (back) {
                g->screen = Screen::kHome;
                g->cursor = 2;
                draw_home(page);
                break;
            }

            const int letters = static_cast<int>(std::string(mp::kCodeAlphabet).size());
            int& c = g->code[static_cast<std::size_t>(g->slot)];
            const int count = static_cast<int>(mp::kCodeLength);

            // Up and Down step the character, Left and Right the slot, all around the ends.
            if (f.pressed(OPENRAC_PAD_UP)) {
                c = (c + 1) % letters;
            } else if (f.pressed(OPENRAC_PAD_DOWN)) {
                c = (c + letters - 1) % letters;
            } else if (f.pressed(OPENRAC_PAD_RIGHT)) {
                g->slot = (g->slot + 1) % count;
            } else if (f.pressed(OPENRAC_PAD_LEFT)) {
                g->slot = (g->slot + count - 1) % count;
            }

            // Cross joins the party of that code.
            if (choose) {
                std::string code;

                // The code as the server reads it, a character per slot.
                for (const int i : g->code) {
                    code += mp::kCodeAlphabet[i];
                }

                g->notice.clear();
                s.join(code);
            }

            draw_code(page);
            break;
        }
        case Screen::kLobby:
            // Back to the game's menu; the party goes on, and the page comes back to it.
            if (back) {
                return true;
            }

            move_cursor(f, 2);

            // Ready or Start, or Leave.
            if (choose && g->cursor == 0) {
                g->notice.clear();

                // The host starts; a member says it is ready, or no longer.
                if (s.is_host()) {
                    s.start();
                } else {
                    s.toggle_ready();
                }
            } else if (choose && g->cursor == 1) {
                g->notice.clear();
                s.leave();
                g->screen = Screen::kHome;
                g->cursor = 0;
                draw_home(page);
                break;
            }

            // The notice the action made shows on this frame already.
            if (std::string notice = s.take_notice(); !notice.empty()) {
                g->notice = notice;
            }

            draw_lobby(page);
            break;
    }

    return false;
}

/* The port calls this when it loads the library: checks the game and the port, reads the server
 * address, and adds the menu entry. Throws openrac::Error to refuse to run. */
void load(openrac::Mod& mod) {
    // The page is Ratchet & Clank's front end.
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
    }

    // The menu page is interface 1.5's.
    if (!mod.has_menu()) {
        throw openrac::Error("this OpenRAC has no menu pages for mods (interface 1.5)");
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
    mod.menu_entry("Multiplayer", page_frame);
    mod.info(
        "multiplayer: " + g->session->name() + ", server " + g->host + ":" + std::to_string(g->port)
    );
}

}  // namespace

/* The id and version are the manifest's: the port checks they agree. */
OPENRAC_MOD_CPP("multiplayer", "0.1.0", load)

/* The port calls this at its exit: the connection's thread goes with it. */
extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    // The state exists only if load got as far as making it.
    if (g) {
        g->connection->close();
        g.reset();
    }
}
