/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's pages: Rules (multiplayer.cpp's header says what each page is).
 */
#include "mod_state.hpp"

namespace mpmod {

/* The party's rules: what the host's gameplay mods play by, and what stays the player's own; on the
 * pause menu's narrower pages too. */
openrac::UiContent rules_content() {
    const bool level = mp::in_level(g->game->status().phase);
    openrac::UiContent c;
    c.title = mp::kPartyRulesLabel;
    c.items.push_back(openrac::UiItem::button("Back"));
    const std::vector<std::string> lines = mp::rules_lines(
        g->mods->host_list(),
        g->mods->rules(),
        g->mods->own(),
        level ? mp::kPauseLine : mp::kTitleLine
    );

    // The host's mod code, so that friends can get these mods.
    if (g->session->is_host() && !g->mods->own().modcode.empty()) {
        c.items.push_back(openrac::UiItem::text(mp::kModCodeLabel + g->mods->own().modcode));
    } else if (g->session->is_host() && g->mod_code_long) {
        c.items.push_back(openrac::UiItem::text(mp::kModCodeInLauncher));
    }

    for (const std::string& line : lines) {
        if (c.items.size() == OPENRAC_UI_ITEMS_MAX) {
            break;
        }

        c.items.push_back(openrac::UiItem::text(line));
    }

    if (!level) {
        c.footer = {g->session->is_host() ? "Everyone plays by your rules" : "Everyone plays by the host's rules", mp::kBackHints};
    }

    return c;
}

void show_rules() {
    g->rules.set(rules_content());
}

/* The party's rules: Back and Triangle go back to the lobby, or the pause menu's Multiplayer page. */
void on_rules(const openrac::UiPage& page, const openrac::UiEvent& e) {
    const bool level = mp::in_level(g->game->status().phase);

    if ((e.is(OPENRAC_UI_CHOOSE) && e.item() == 0) || e.is(OPENRAC_UI_BACK)) {
        if (level) {
            g->pause_page.open();
        } else if (g->session->in_party()) {
            g->lobby.open();
        } else {
            page.back();
        }
    } else if (!level && !g->session->in_party() && !e.is(OPENRAC_UI_HIDE)) {
        g->home.open();
    }

    show_rules();
}

}  // namespace mpmod
