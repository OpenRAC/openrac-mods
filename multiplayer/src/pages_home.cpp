/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's pages: Home, Code, Direct connect and the pause menu's page (multiplayer.cpp's header says what each page is).
 */
#include "mod_state.hpp"

namespace mpmod {

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
        mp::server_items_enabled(g->connection->mode(), g->session->greeted());
    openrac::UiContent c;
    c.title = "Multiplayer";
    c.items = {
        openrac::UiItem::button("Browse games", usable),
        openrac::UiItem::button("Create match", usable),
        openrac::UiItem::button("Join by code", usable),
        openrac::UiItem::button("Direct connect"),
        openrac::UiItem::text("Playing as " + g->session->name()),
    };
    c.footer = {status(), mp::kSelectBackHints};
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
    c.footer = {status(), mp::kCodeHints};
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
        hints = mp::kSelectBackHints;
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
        hints = mp::kJoinHints;
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

/* The pause menu's Multiplayer page (Options, in a level; menu.hpp's party_lines): the party's code
 * (or where a direct one is), the host's level, each member with its round trip and UDP or TCP
 * (the host marked), and Leave party; out of a party, that it is in none. */
void show_pause() {
    const mp::Session& s = *g->session;
    mp::PartyPageInput in;
    in.in_party = s.in_party();
    in.direct = s.direct();
    in.code = s.code();

    if (g->connection->mode() == mp::Connection::Mode::kHosting) {
        in.where = "port " + std::to_string(g->connection->hosting().port);
    } else {
        in.where = g->connection->address();
    }

    // The host's level: the host's own, a member's as the host's LEVEL said.
    const mp::GameStatus status = g->game->status();
    const int level = s.is_host() ? (mp::in_level(status.phase) ? status.level : -1)
                                  : g->coop->host_level();
    in.host_level = level >= 0 ? g->mod->level_name(level) : "";
    in.go_to_host = g->coop->can_go();

    for (const mp::Member& m : s.members()) {
        const mp::Link link = s.link(m.id);
        in.members.push_back({m.name, m.id == s.host(), m.id == s.id(), link.ping_ms, link.udp, m.id});
    }

    in.removes = s.is_host();

    openrac::UiContent c;
    c.title = mp::kPartyPageTitle;
    g->pause_leave = -1;
    g->pause_rules = -1;
    g->pause_go = -1;
    g->pause_save = -1;
    g->pause_rows.clear();

    // Where the party's saves go, and saving now, with the party's game on.
    if (g->cosave->on() && s.in_party()) {
        c.items.push_back(openrac::UiItem::text(g->cosave->where()));
        g->pause_save = static_cast<int>(c.items.size());
        c.items.push_back(openrac::UiItem::button(mp::kSavePartyLabel));
    }

    for (const mp::PartyLine& line : mp::party_lines(in)) {
        switch (line.kind) {
            case mp::PartyLine::Kind::kInfo:
                c.items.push_back(openrac::UiItem::choice_of(line.label, {line.value}, 0));
                break;
            case mp::PartyLine::Kind::kLeave:
                g->pause_leave = static_cast<int>(c.items.size());
                c.items.push_back(openrac::UiItem::button(line.label));
                break;
            case mp::PartyLine::Kind::kGoToHost:
                g->pause_go = static_cast<int>(c.items.size());
                c.items.push_back(openrac::UiItem::button(line.label));
                break;
            case mp::PartyLine::Kind::kRules:
                g->pause_rules = static_cast<int>(c.items.size());
                c.items.push_back(openrac::UiItem::button(line.label));
                break;
            case mp::PartyLine::Kind::kText:
                c.items.push_back(openrac::UiItem::text(line.label));
                break;
            case mp::PartyLine::Kind::kMember:
                g->pause_rows.emplace_back(static_cast<int>(c.items.size()), line.member);
                c.items.push_back(openrac::UiItem::button(line.label + "  " + line.value));
                break;
        }
    }

    g->pause_page.set(c);
}

/* Leaves the party (Lobby's and the pause page's Leave): a host of a direct party stops hosting
 * (its clients get END); anyone else leaves. */
void leave_party() {
    g->notice.clear();
    g->leaving = true;
    // The last save and the ledger, while the party can still get them.
    g->cosave->leaving();
    g->migrate->leaving();
    g->session->flush();

    if (g->connection->mode() == mp::Connection::Mode::kHosting) {
        g->connection->stop_hosting();
        poll();
    } else {
        g->session->leave();
    }
}

/* The pause page's events: Leave party asks first; the page follows the party each tick. */
void on_pause(const openrac::UiPage& page, const openrac::UiEvent& e) {
    const mp::Session& s = *g->session;

    if (e.is(OPENRAC_UI_CHOOSE) && e.item() == g->pause_leave && g->pause_leave >= 0) {
        const bool hosting = g->connection->mode() == mp::Connection::Mode::kHosting;
        page.confirm(mp::pause_leave_question(hosting, s.is_host(), g->migrate->followed()), kAskLeave);
    } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() == g->pause_save && g->pause_save >= 0) {
        page.sound(g->cosave->save_party() ? OPENRAC_UI_SOUND_SELECT : OPENRAC_UI_SOUND_DENIED);
    } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() == g->pause_go && g->pause_go >= 0) {
        // The hero goes when the menu closes; the banner says it will.
        if (!g->coop->go_to_host(mp::now_ms())) {
            page.sound(OPENRAC_UI_SOUND_DENIED);
        }
    } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() == g->pause_rules && g->pause_rules >= 0) {
        show_rules();
        g->rules.open();
    } else if (const std::int32_t member = row_member(g->pause_rows, e); member != 0) {
        open_remove(member);
    } else if (e.is(OPENRAC_UI_ANSWER) && e.tag() == kAskLeave && e.value() == 1 && s.in_party()) {
        g->mod->info("multiplayer: left the party from the pause menu");
        leave_party();
    }

    show_pause();
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

/* Stops the server link and its reconnecting (a hosted or direct party is the only link then),
 * nothing when hosting already. Home links to the server again when it shows. */
void stop_server_loop() {
    if (mp::server_loop_wanted(g->connection->mode())
        || g->connection->mode() == mp::Connection::Mode::kDirect) {
        g->connection->close();
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
        // Brought back by a party that went (coop.hpp: leaving): why, on the status line.
        g->notice = g->leave_reason;
        g->leave_reason.clear();
        g->coop->home_shown();
    } else if (e.is(OPENRAC_UI_CHOOSE)) {
        g->notice.clear();

        // Browse games lists the parties; Create match asks for a save first; Join by code asks
        // for a code, Direct connect for a host or an address.
        if (e.item() == kBrowseItem) {
            g->browse.open();
        } else if (e.item() == kCreateItem && g->mods->solo()) {
            show_refused();
            g->refused.open();
        } else if (e.item() == kCreateItem) {
            g->saves_for = SavesFor::kCreate;
            g->saves.open();
        } else if (e.item() == kCodeItem) {
            g->code_page.open();
        } else if (e.item() == kDirectItem) {
            // Direct connect needs no server: its retries stop here, and resume at Home.
            stop_server_loop();
            g->view = DirectView::kMenu;
            g->direct_error.clear();
            g->direct_page.open();
        }
    }

    // The server's hello came while Home showed: the cursor, left on Direct connect while the rest
    // was dim, goes up to Browse games.
    if (s.greeted() && !g->home_greeted && page.cursor() == kDirectItem) {
        page.set_cursor(kBrowseItem);
    }

    g->home_greeted = s.greeted();

    // In a party (found, made, or kept from before): the lobby.
    if (s.in_party()) {
        g->lobby.open();
    }

    show_home();
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
    mp::Connection& connection = *g->connection;

    // This page never has the server: showing it (also back from a party) stops its retries.
    if (e.is(OPENRAC_UI_SHOW)) {
        stop_server_loop();
    }

    if (e.is(OPENRAC_UI_BACK)) {
        // From the rows: the two buttons again, and no join pending.
        if (g->view == DirectView::kJoin) {
            // Giving up a join: no link, and no server retries on this page.
            stop_server_loop();

            switch_view(page, DirectView::kMenu, kByAddressItem);
            return;
        }

        page.back();
        return;
    }

    if (g->view == DirectView::kMenu && e.is(OPENRAC_UI_CHOOSE)) {
        // Host directly: this game hosts; a port taken stays here, saying so.
        if (e.item() == kHostItem && g->mods->solo()) {
            show_refused();
            g->refused.open();
            return;
        } else if (e.item() == kHostItem) {
            g->notice.clear();
            g->direct_error.clear();
            g->saves_for = SavesFor::kHost;
            g->saves.open();
            return;
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

}  // namespace mpmod
