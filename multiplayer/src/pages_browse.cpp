// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The multiplayer mod's pages: Browse (multiplayer.cpp's header says what each page is).

#include "mod_state.hpp"

namespace mpmod {

namespace {

// Whether a party's game would not take this player's: its host's digest is not this game's.
bool row_differs(const mp::Listing& l) {
    const std::string tag = mp::description_tag(l.description);
    return mp::other_game(l.description, game_tag())
           || (!tag.empty() && tag != mp::digest_tag(mp::mods_digest(g->mods->own())));
}

// A party's row in Browse: "Clank - Pokitaru, 3 gold bolts 2/4", cut to the page's width, marked
// (differs) when this player's mods do not fit it.
std::string party_row(const mp::Listing& l) {
    const std::string description = mp::plain_description(l.description);
    const std::string count = " " + std::to_string(l.members) + "/" + std::to_string(l.size)
                              + (row_differs(l) ? text(" (differs)") : "");
    std::string row = l.host + (description.empty() ? "" : " - " + description);
    const std::size_t room = mp::kItemMax - count.size();

    // A long row is cut so the count stays readable.
    if (row.size() > room) {
        row = mp::fit_ui_text(row, room);
    }

    return row + count;
}

// The parties this game could play: an older server lists every game's, so another game's (its
// description's tag says so) are left out here too.
std::vector<mp::Listing> shown_parties() {
    std::vector<mp::Listing> out;

    // An older server lists every game's parties, so the list is filtered here.
    for (const mp::Listing& l : g->session->parties()) {
        // Another game's party cannot be joined from here.
        if (!mp::other_game(l.description, game_tag())) {
            out.push_back(l);
        }
    }

    return out;
}

}  // namespace

// Browse: Refresh, then the parties with room; none: a line saying so.
void show_browse() {
    const mp::Session& s = *g->session;
    openrac::UiContent c;
    c.title = text("Browse games");
    c.items.push_back(openrac::UiItem::button(text("Refresh"), s.greeted()));
    const std::vector<mp::Listing> parties = shown_parties();
    bool differs = false;

    // One button per party that fits on the page; the rest are not shown.
    for (std::size_t i = 0; i < parties.size() && i < kBrowseRows; ++i) {
        c.items.push_back(openrac::UiItem::button(party_row(parties[i])));
        differs = differs || row_differs(parties[i]);
    }

    // With nothing listed, a line says whether the server was asked yet.
    if (parties.empty()) {
        c.items.push_back(
            openrac::UiItem::text(s.lists() == 0 ? text("Asking...") : text("No games"))
        );
    }

    c.footer = {status()};

    // A row marked (differs) is explained once, in the footer.
    if (differs) {
        c.footer.push_back(text("(differs): mods"));
    }

    c.footer.push_back(text(mp::kSelectBackHints));
    set_content(g->browse, c);
}

// Browse's events: the list asked for when it shows and every few seconds; Refresh asks at once;
// a row joins its party (by its code); in the party, the lobby.
void on_browse(const openrac::UiPage& page, const openrac::UiEvent& e) {
    mp::Session& s = *g->session;
    const std::uint64_t now = mp::now_ms();

    // The list is asked for when the page shows, every few seconds while it is up, and by Refresh.
    if (e.is(OPENRAC_UI_SHOW) || (e.is(OPENRAC_UI_FRAME) && now - g->listed_ms >= kListMs)
        || (e.is(OPENRAC_UI_CHOOSE) && e.item() == kRefreshItem)) {
        g->listed_ms = now;
        s.list();
    } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() > kRefreshItem) {
        // Row 0 is Refresh, so a party's row is the item less one.
        const auto row = static_cast<std::size_t>(e.item() - 1);
        const std::vector<mp::Listing> parties = shown_parties();

        // A row past the list (a party gone since) joins nothing.
        if (row < parties.size()) {
            g->notice.clear();
            s.join(parties[row].code);
        }
    }

    // Once in the party the lobby takes this page's place, but not while the page hides.
    if (s.in_party() && !e.is(OPENRAC_UI_HIDE)) {
        g->lobby.open();
    } else if (!g->refusal.empty() && !e.is(OPENRAC_UI_HIDE)) {
        // Refused for another game's party: why.
        show_refused();
        g->refused.open();
    }

    show_browse();
    (void)page;
}

}  // namespace mpmod
