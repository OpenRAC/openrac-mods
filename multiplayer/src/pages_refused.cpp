/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's pages: Refused (multiplayer.cpp's header says what each page is).
 */
#include "mod_state.hpp"

namespace mpmod {

/* Why this game is refused: what differs from the host's, or the mod that cannot be played in a
 * party, and what to do. */
openrac::UiContent refused_content() {
    const mp::Session& s = *g->session;
    const std::vector<mp::Difference> differences =
        s.in_party() ? g->mods->differences() : mp::solo_mods(g->mods->own());
    const bool solo = !mp::solo_mods(g->mods->own()).empty();
    openrac::UiContent c;
    c.title = s.in_party() ? "You can't join this game" : "You can't play in a party";
    c.items.push_back(openrac::UiItem::button(s.in_party() ? mp::kLeavePartyLabel : "Back"));

    // The host's mod code: what gets this game the host's mods.
    const std::string code = s.in_party() ? g->mods->host_list().modcode : "";

    if (!code.empty()) {
        c.items.push_back(openrac::UiItem::button(mp::kOpenInLauncher));
        c.items.push_back(openrac::UiItem::text(mp::kModCodeLabel + code));
        c.items.push_back(openrac::UiItem::text(mp::kModCodeHint));
    }

    for (const std::string& line : mp::refusal_lines(differences)) {
        if (c.items.size() == OPENRAC_UI_ITEMS_MAX) {
            break;
        }

        c.items.push_back(openrac::UiItem::text(line));
    }

    c.footer = {solo ? mp::kSoloHint1 : mp::kMatchHint1, solo ? mp::kSoloHint2 : mp::kMatchHint2, mp::kBackHints};
    return c;
}

void show_refused() {
    g->refused.set(refused_content());
}

/* The page of why a game is refused: Leave party (or Back, out of one); Triangle goes back to the
 * lobby, or Home. */
void on_refused(const openrac::UiPage& page, const openrac::UiEvent& e) {
    const bool in_party = g->session->in_party();
    const bool chose = e.is(OPENRAC_UI_CHOOSE) && e.item() == 0;

    if (e.is(OPENRAC_UI_SHOW)) {
        g->refused_in_party = in_party;
    }

    if (e.is(OPENRAC_UI_CHOOSE) && e.item() == kRefusedOpenItem) {
        // The player's own choice, on this page: the launcher then asks to close the game.
        g->mod->open_link(mp::mod_code_link(g->mods->host_list().modcode));
    } else if (chose && in_party) {
        leave_party();
    } else if (chose || e.is(OPENRAC_UI_BACK)) {
        if (in_party) {
            g->lobby.open();
        } else {
            page.back();
        }
    }

    // The party gone under it, or left here.
    if (g->refused_in_party && !g->session->in_party() && !e.is(OPENRAC_UI_HIDE)) {
        g->refused_in_party = false;
        g->home.open();
    }

    show_refused();
}

}  // namespace mpmod
