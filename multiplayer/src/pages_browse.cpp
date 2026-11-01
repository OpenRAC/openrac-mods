/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's pages: Browse (multiplayer.cpp's header says what each page is).
 */
#include "mod_state.hpp"

namespace mpmod {

namespace {

/* Whether a party's game would not take this player's: its host's digest is not this game's. */
bool row_differs(const mp::Listing& l) {
    const std::string tag = mp::description_tag(l.description);
    return !tag.empty() && tag != mp::digest_tag(mp::mods_digest(g->mods->own()));
}

/* A party's row in Browse: "Clank - Pokitaru, 3 gold bolts 2/4", cut to the page's width, marked
 * (differs) when this player's mods do not fit it. */
std::string party_row(const mp::Listing& l) {
    const std::string description = mp::plain_description(l.description);
    const std::string count = " " + std::to_string(l.members) + "/" + std::to_string(l.size)
                              + (row_differs(l) ? " (differs)" : "");
    std::string text = l.host + (description.empty() ? "" : " - " + description);
    const std::size_t room = OPENRAC_UI_TEXT_MAX - count.size();

    if (text.size() > room) {
        text = text.substr(0, room - 3) + "...";
    }

    return text + count;
}

}  // namespace

/* Browse: Refresh, then the parties with room; none: a line saying so. */
void show_browse() {
    const mp::Session& s = *g->session;
    openrac::UiContent c;
    c.title = "Browse games";
    c.items.push_back(openrac::UiItem::button("Refresh", s.greeted()));
    const std::vector<mp::Listing>& parties = s.parties();
    bool differs = false;

    for (std::size_t i = 0; i < parties.size() && i < kBrowseRows; ++i) {
        c.items.push_back(openrac::UiItem::button(party_row(parties[i])));
        differs = differs || row_differs(parties[i]);
    }

    if (parties.empty()) {
        c.items.push_back(
            openrac::UiItem::text(
                s.lists() == 0 ? "Asking the server..." : "No games are hosted: Create match hosts one"
            )
        );
    }

    c.footer = {status()};

    if (differs) {
        c.footer.push_back("(differs): needs other mods or settings");
    }

    c.footer.push_back(mp::kSelectBackHints);
    g->browse.set(c);
}

/* Browse's events: the list asked for when it shows and every few seconds; Refresh asks at once;
 * a row joins its party (by its code); in the party, the lobby. */
void on_browse(const openrac::UiPage& page, const openrac::UiEvent& e) {
    mp::Session& s = *g->session;
    const std::uint64_t now = mp::now_ms();

    if (e.is(OPENRAC_UI_SHOW) || (e.is(OPENRAC_UI_FRAME) && now - g->listed_ms >= kListMs)
        || (e.is(OPENRAC_UI_CHOOSE) && e.item() == kRefreshItem)) {
        g->listed_ms = now;
        s.list();
    } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() > kRefreshItem) {
        const auto row = static_cast<std::size_t>(e.item() - 1);

        if (row < s.parties().size()) {
            g->notice.clear();
            s.join(s.parties()[row].code);
        }
    }

    if (s.in_party() && !e.is(OPENRAC_UI_HIDE)) {
        g->lobby.open();
    }

    show_browse();
    (void)page;
}

}  // namespace mpmod
