/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's pages: Saves (multiplayer.cpp's header says what each page is), and from
 * it Continue as host: the party's own saves (cosave.hpp), any of which a player can host from.
 */
#include "mod_state.hpp"

namespace mpmod {

namespace {

/* A save's row: "Save 1: Pokitaru, 3 gold bolts, 2:05", or empty. */
std::string save_row(int slot, const openrac_save_info& info, bool there) {
    const std::string name = "Save " + std::to_string(slot + 1) + ": ";

    if (!there) {
        return name + "empty";
    }

    const int minutes = info.seconds / 60;
    const std::string time = std::to_string(minutes / 60) + ":" + (minutes % 60 < 10 ? "0" : "")
                             + std::to_string(minutes % 60);
    return name + mp::save_description(g->mod->level_name(info.level), info.gold_bolts, false) + ", "
           + time;
}

/* The card's saves as the Saves page last read them (when it showed). */
struct SlotRow {
    openrac_save_info info{};
    bool there = false;
};
std::vector<SlotRow> g_slots(kSaveSlots);

std::vector<SlotRow> g_coop_slots(mp::kCoSlots);
bool g_any_coop = false;

void read_slots() {
    g_any_coop = false;

    for (int slot = 0; slot < kSaveSlots; ++slot) {
        SlotRow& row = g_slots[static_cast<std::size_t>(slot)];
        row.there = g->mod->save_slot(slot, row.info) == 1;
    }

    for (int slot = 0; slot < mp::kCoSlots; ++slot) {
        SlotRow& row = g_coop_slots[static_cast<std::size_t>(slot)];
        row.there = g->mod->save_slot2(mp::kCoFolder, slot, row.info) == 1;
        g_any_coop = g_any_coop || row.there;
    }
}

/* A co-op save's row: where it stands and whose party it was. */
std::string coop_save_row(int slot) {
    const SlotRow& row = g_coop_slots[static_cast<std::size_t>(slot)];
    mp::CoNote note;
    mp::parse_note(g->game->card_note(slot), note);
    const std::string save =
        row.there ? mp::save_description(g->mod->level_name(row.info.level), row.info.gold_bolts, false)
                  : std::string();
    return mp::coop_row(slot, save, note, row.there);
}

/* Continue as host: Back, then the party's own saves. */
void show_coop_saves() {
    openrac::UiContent c;
    c.title = "Continue as host";
    c.items.push_back(openrac::UiItem::button("Back"));

    for (int slot = 0; slot < mp::kCoSlots; ++slot) {
        c.items.push_back(openrac::UiItem::button(
            coop_save_row(slot), g_coop_slots[static_cast<std::size_t>(slot)].there
        ));
    }

    c.footer = {"Your saves from past parties", g->direct_error.empty() ? "The others keep their own copy" : g->direct_error,
                mp::kSelectBackHints};
    g->saves.set(c);
}

}  // namespace

/* Saves: New game, then the card's saves (an empty slot dim). */
void show_saves() {
    if (g->saves_coop) {
        show_coop_saves();
        return;
    }

    openrac::UiContent c;
    c.title = g->saves_for == SavesFor::kHost ? "Host with which save?" : "Play which save?";
    c.items.push_back(openrac::UiItem::button("New game"));

    for (int slot = 0; slot < kSaveSlots; ++slot) {
        const SlotRow& row = g_slots[static_cast<std::size_t>(slot)];
        c.items.push_back(openrac::UiItem::button(save_row(slot, row.info, row.there), row.there));
    }

    c.items.push_back(openrac::UiItem::button("Continue as host", g_any_coop));
    c.footer = {g->saves_for == SavesFor::kHost ? "No server: port 7770" : status(),
                g->direct_error.empty() ? "The party plays a copy; your saves are kept"
                                        : g->direct_error,
                mp::kSelectBackHints};
    g->saves.set(c);
}

/* A save chosen (or a new game): the co-op keeps it, then the party is made, on the server or by
 * this game hosting. */
void on_saves(const openrac::UiPage& page, const openrac::UiEvent& e) {
    if (e.is(OPENRAC_UI_SHOW)) {
        g->direct_error.clear();
        g->saves_coop = false;
        read_slots();
    } else if (e.is(OPENRAC_UI_CHOOSE) && !g->saves_coop && e.item() == kContinueItem) {
        g->saves_coop = true;
        page.set_cursor(0);
        show_saves();
        return;
    } else if (e.is(OPENRAC_UI_CHOOSE) && g->saves_coop && e.item() == kNewGameItem) {
        g->saves_coop = false;
        page.set_cursor(kContinueItem);
        show_saves();
        return;
    } else if (e.is(OPENRAC_UI_CHOOSE)) {
        mp::Bytes save;
        std::string description = mp::save_description("", 0, true);

        if (e.item() > kNewGameItem) {
            const int slot = e.item() - 1;
            openrac_save_info info{};
            save = g->saves_coop ? g->mod->save_read2(mp::kCoFolder, slot) : g->mod->save_read(slot);
            const int there = g->saves_coop ? g->mod->save_slot2(mp::kCoFolder, slot, info)
                                            : g->mod->save_slot(slot, info);

            if (save.empty() || there != 1) {
                page.sound(OPENRAC_UI_SOUND_DENIED);
                show_saves();
                return;
            }

            description =
                mp::save_description(g->mod->level_name(info.level), info.gold_bolts, false);
        }

        if (g->saves_coop) {
            g->cosave->choose_slot(e.item() - 1);
        } else {
            g->cosave->choose_new();
        }

        g->coop->choose(save, description);
        g->notice.clear();
        // The list says what mods the game has, for Browse to mark the ones a player's do not fit.
        const std::string listed = mp::tagged_description(description, mp::mods_digest(g->mods->own()));

        if (g->saves_for == SavesFor::kCreate) {
            g->session->create(listed);
        } else {
            // Hosting: the party's description is the save's; a port taken stays here.
            mp::HostConfig config;
            config.description = listed;
            g->connection->set_host_config(config);

            if (g->connection->start_hosting(mp::kDefaultPort)) {
                poll();
            } else {
                const std::string error = g->connection->hosting().error;
                g->direct_error = error.empty() ? "Cannot host here" : error;
                page.sound(OPENRAC_UI_SOUND_DENIED);
            }
        }
    }

    // In the party: the lobby.
    if (g->session->in_party() && !e.is(OPENRAC_UI_HIDE)) {
        g->lobby.open();
    }

    show_saves();
}

}  // namespace mpmod
