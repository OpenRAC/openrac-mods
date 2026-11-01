/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's pages: Remove (multiplayer.cpp's header says what each page is).
 */
#include "mod_state.hpp"

namespace mpmod {

namespace {

const mp::Member* member_of(std::int32_t id) {
    for (const mp::Member& m : g->session->members()) {
        if (m.id == id) {
            return &m;
        }
    }

    return nullptr;
}

/* Back where the page was opened from: the pause menu in a level, else the lobby. */
void go_back() {
    if (mp::in_level(g->game->status().phase)) {
        g->pause_page.open();
    } else {
        g->lobby.open();
    }
}

}  // namespace

/* What the host can do about a member: remove it, which a code undoes, or ban it from this save.
 * A game that gave no key cannot be banned, only removed. */
openrac::UiContent remove_content() {
    const mp::Member* m = member_of(g->remove_member);
    const bool level = mp::in_level(g->game->status().phase);
    openrac::UiContent c;
    c.title = m != nullptr ? "Remove " + m->name : "Remove a player";
    c.items.push_back(openrac::UiItem::button(mp::kRemoveLabel, m != nullptr));
    c.items.push_back(openrac::UiItem::button(mp::kBanLabel, m != nullptr && !g->mods->key(g->remove_member).empty()));
    c.items.push_back(openrac::UiItem::button("Back"));

    if (!g->session->direct()) {
        c.items.push_back(openrac::UiItem::text(mp::kRemoveNote));
    }

    c.items.push_back(openrac::UiItem::text(mp::kBanNote));

    if (!level) {
        c.footer = {status(), mp::kBackHints};
    }

    return c;
}

void show_remove() {
    g->remove.set(remove_content());
}

void open_remove(std::int32_t member) {
    g->remove_member = member;
    show_remove();
    g->remove.open();
}

void remove_member(std::int32_t member) {
    const mp::Member* m = member_of(member);

    if (m != nullptr && g->session->kick(member)) {
        g->mod->info("multiplayer: " + m->name + " removed from the party");
    }
}

/* The host: anyone in the party under a key banned from this save goes (a ban's whole effect:
 * whoever it was comes back by another name or address, and its MODS says who it is). */
void remove_banned() {
    const auto key_of = [](std::int32_t member) { return g->mods->key(member); };

    for (const mp::Member& m : g->cosave->banned_members(key_of)) {
        // Each is told once: a kick already asked for is no news.
        if (g->session->kick(m.id, mp::KickReason::kBanned)) {
            g->notice = mp::kBannedSent;
            g->mod->info("multiplayer: " + m.name + " is banned from this save");
        }
    }
}

/* The Remove page's events: either button does it and goes back; so do Back and Triangle, and a
 * member gone meanwhile leaves nothing to do here. */
void on_remove(const openrac::UiPage&, const openrac::UiEvent& e) {
    const std::int32_t member = g->remove_member;
    const bool here = member_of(member) != nullptr;

    if (e.is(OPENRAC_UI_CHOOSE) && e.item() == kRemoveItem && here) {
        remove_member(member);
        go_back();
    } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() == kBanItem && here) {
        // The key stays banned even if the player has gone by the time the word gets there.
        g->cosave->ban(g->mods->key(member));
        remove_banned();
        go_back();
    } else if ((e.is(OPENRAC_UI_CHOOSE) && e.item() == kRemoveBackItem) || e.is(OPENRAC_UI_BACK)) {
        go_back();
    } else if (!g->session->in_party() && !e.is(OPENRAC_UI_HIDE)) {
        // The party gone under it: the pause menu's page says so; at the title, Home.
        if (mp::in_level(g->game->status().phase)) {
            g->pause_page.open();
        } else {
            g->home.open();
        }
    }

    show_remove();
}

}  // namespace mpmod
