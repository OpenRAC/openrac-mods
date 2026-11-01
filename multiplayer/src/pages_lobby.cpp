/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's pages: Lobby (multiplayer.cpp's header says what each page is).
 */
#include "mod_state.hpp"

namespace mpmod {

/* Whether this player, a member, is refused the host's game, or still waits to hear. */
bool refused() {
    return !g->session->is_host() && !g->mods->differences().empty();
}

bool checking() {
    return !g->session->is_host() && !g->mods->heard_host();
}

/* Lobby: Ready or Start, Leave, Party rules, then the members (the host marked, each ready or not,
 * whether its mods match, the player). Start is dim until every member is ready; a member whose
 * mods differ cannot be, its button says why instead, and the host has a line saying why. A direct
 * party's title says where it is: the host's own addresses (all that fit, the first in the title),
 * or the address joined. */
openrac::UiContent lobby_content() {
    const mp::Session& s = *g->session;
    openrac::UiContent c;
    // The host starts from the title once; a member says it is ready until then, and once the
    // game is on, back in the title, joins it again.
    const bool title = g->game->status().phase == mp::kPhaseFrontEnd;
    const bool rejoin = !s.is_host() && g->coop->playing() && title;
    const std::string first = s.is_host()           ? "Start"
                              : refused()           ? "See why you can't join"
                              : g->mods->host_silent() ? "The host's game is too old"
                              : checking()          ? "Checking your mods..."
                              : rejoin              ? "Join the game"
                              : s.ready(s.id())     ? "Not ready"
                                                    : "Ready";
    // Start waits for every member's Ready: dim until then, and the port refuses it.
    const bool can = s.is_host()  ? !s.started() && title && s.all_ready()
                     : refused()  ? true
                     : checking() ? false
                     : rejoin     ? !g->coop->receiving()
                                  : !s.started();
    c.items = {
        openrac::UiItem::button(first, can),
        openrac::UiItem::button("Leave"),
        openrac::UiItem::button(mp::kPartyRulesLabel),
    };
    // Why a member cannot play, for the host.
    std::vector<std::string> why;

    if (s.is_host()) {
        for (const mp::Member& m : s.members()) {
            if (g->mods->fit(m.id) == mp::Fit::kDiffers) {
                why.push_back(mp::lobby_why_line(m.name, g->mods->why(m.id)));
            }
        }
    }

    // Where the party is: its code, or for a direct one, where it is hosted.
    if (!s.direct()) {
        c.title = "Party " + s.code();
    } else if (g->connection->mode() == mp::Connection::Mode::kHosting) {
        const mp::HostingStatus h = g->connection->hosting();
        // The room left past the buttons, the lines above, the members and the count line, plus
        // the title.
        const std::size_t used = c.items.size() + why.size() + s.members().size() + 2;
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

    // What the party plays: the host's save (or a new game), as its INFO said.
    const std::string playing = s.is_host() ? g->coop->description() : mp::plain_description(s.description());

    if (!playing.empty()) {
        c.items.push_back(openrac::UiItem::text("Playing: " + playing));
    }

    if (g->coop->receiving()) {
        c.items.push_back(openrac::UiItem::text("Receiving the host's game..."));
    }

    // Where its saves go: never over a player's own.
    if (g->game->has_card_filter() && c.items.size() + s.members().size() + why.size() + 1 < OPENRAC_UI_ITEMS_MAX) {
        c.items.push_back(openrac::UiItem::text(mp::kCoSavesLine));
    }

    for (const mp::Member& m : s.members()) {
        const mp::Fit fit = g->mods->fit(m.id);
        mp::LobbyMemberInput line;
        line.name = m.name;
        line.host = m.id == s.host();
        line.you = m.id == s.id();
        line.ready = s.ready(m.id);
        line.differs = m.id == s.id() ? refused() : fit == mp::Fit::kDiffers;
        line.checking = m.id == s.id() ? checking() : fit == mp::Fit::kUnknown;
        c.items.push_back(openrac::UiItem::text(mp::lobby_member_line(line)));
    }

    for (const std::string& line : why) {
        c.items.push_back(openrac::UiItem::text(line));
    }

    const std::string ping = s.has_clock() ? ", ping " + std::to_string(s.rtt_ms()) + " ms" : "";
    // The host is told why Start is dim (on the count line: the list is full with four members).
    const std::string waiting =
        s.is_host() && !s.started() && title && !s.all_ready() ? ", not all ready" : "";
    c.items.push_back(
        openrac::UiItem::text(
            std::to_string(s.members().size()) + " of " + std::to_string(mp::kMaxPartySize)
            + " players" + ping + waiting
        )
    );

    c.footer = {status(), mp::kSelectLeaveHints};
    return c;
}

void show_lobby() {
    g->lobby.set(lobby_content());
}

/* Lobby's events: Ready or Start, and Leave or Triangle asking first; out of the party, back. A
 * host's Leave stops hosting. */
void on_lobby(const openrac::UiPage& page, const openrac::UiEvent& e) {
    mp::Session& s = *g->session;
    const bool hosting = g->connection->mode() == mp::Connection::Mode::kHosting;
    const bool direct_party = s.direct() || hosting
                              || g->connection->mode() == mp::Connection::Mode::kDirect;

    // Ready or Start; Leave and Triangle ask before the party is left.
    if (e.is(OPENRAC_UI_CHOOSE) && e.item() == kReadyItem) {
        g->notice.clear();

        // The host starts the game for everyone; a member says it is ready, or no longer, and one
        // that is refused sees why.
        if (s.is_host()) {
            if (!g->coop->start()) {
                page.sound(OPENRAC_UI_SOUND_DENIED);
            }
        } else if (refused()) {
            show_refused();
            g->refused.open();
        } else if (g->coop->playing() && g->game->status().phase == mp::kPhaseFrontEnd) {
            g->coop->rejoin();
        } else {
            s.toggle_ready();
        }
    } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() == kRulesItem) {
        show_rules();
        g->rules.open();
    } else if ((e.is(OPENRAC_UI_CHOOSE) && e.item() == kLeaveItem) || e.is(OPENRAC_UI_BACK)) {
        const char* question = hosting       ? "Stop hosting? The party ends for everyone."
                               : s.is_host() ? "Leave the party? It ends for everyone."
                                             : "Leave the party?";
        page.confirm(question, kAskLeave);
    } else if (e.is(OPENRAC_UI_ANSWER) && e.tag() == kAskLeave && e.value() == 1) {
        leave_party();
    }

    // The notice the action made shows at once.
    if (std::string notice = s.take_notice(); !notice.empty()) {
        g->notice = notice;
    }

    // Refused: the page that says why, by itself the first time.
    if (s.in_party() && refused() && !g->told_refused && !e.is(OPENRAC_UI_HIDE)) {
        g->told_refused = true;
        show_refused();
        g->refused.open();
    }

    // Out of the party (left, ended by the host, or the connection gone): back to Home, which
    // links to the server again after a direct party.
    if (!s.in_party() && !e.is(OPENRAC_UI_HIDE)) {
        // A direct party returns to Home, not to the Direct page.
        if (direct_party) {
            g->home.open();
        } else {
            page.back();
        }
    }

    show_lobby();
}

}  // namespace mpmod
