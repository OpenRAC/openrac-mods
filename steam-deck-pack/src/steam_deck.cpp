// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "deck.hpp"
#include "font_bank.h"
#include "openrac/mod.hpp"
#include "paint.h"

namespace {
struct State {
    openrac::Mod* mod = nullptr;
    openrac::UiPage page;
    deck::Settings settings;
    deck::Motion motion;
    const controller_icons_disc* disc = nullptr;
    std::uint32_t failed_icons = 0;
    std::filesystem::path file;
    int capture = -1;
};

std::unique_ptr<State> g;

// Original UI text; the PAL language word uses 0/1 English, 2 French, 3 German, 4 Spanish, 5 Italian.
const char* words(const char* en, const char* fr, const char* de, const char* es, const char* it) {
    const auto address = g->mod->version() == "pal" ? 0x0015EE88u : 0x0015ED88u;
    const int language = g->mod->memory().read<std::int32_t>(address);
    switch (language) {
        case 2:
            return fr;
        case 3:
            return de;
        case 4:
            return es;
        case 5:
            return it;
        default:
            return en;
    }
}

std::vector<std::string> off_on() {
    return {words("Off", "Non", "Aus", "No", "No"), words("On", "Oui", "An", "Sí", "Sì")};
}

void save() {
    if (!g->file.empty()) {
        std::ofstream out(g->file);
        deck::write_settings(out, g->settings);
        out.flush();
        if (!out) {
            g->mod->warn("controller preferences could not be saved");
        }
    }
}

void show() {
    openrac::UiContent c;
    c.title = "Steam Deck";
    std::vector<std::string> gains;
    for (int value = 25; value <= 200; value += 25) {
        gains.push_back(std::to_string(value) + "%");
    }
    c.items = {
        openrac::UiItem::choice_of(
            words(
                "Gyro / trackpad",
                "Gyro / pavé tactile",
                "Gyro / Trackpad",
                "Giro / panel táctil",
                "Giro / trackpad"
            ),
            off_on(),
            g->settings.enabled
        ),
        openrac::UiItem::choice_of(
            words("Sensitivity", "Sensibilité", "Empfindlichkeit", "Sensibilidad", "Sensibilità"),
            gains,
            g->settings.sensitivity / 25 - 1
        ),
        openrac::UiItem::choice_of(
            words(
                "Invert vertical look",
                "Inverser l'axe vertical",
                "Vertikal umkehren",
                "Invertir eje vertical",
                "Inverti asse verticale"
            ),
            off_on(),
            g->settings.invert_y
        ),
        openrac::UiItem::button(
            words("Reset", "Réinitialiser", "Zurücksetzen", "Restablecer", "Ripristina")
        ),
    };
    // Empty footer: the port draws its own localized Select/Back hints and controller glyphs.
    g->page.set(c);
}

// Built into this library: no xbox-icons mod needs to be installed or enabled.
void paint_icons() {
    controller_icons_fonts fonts{};
    if (!controller_icons_find_fonts(
            g->mod->memory().base(),
            [](uint32_t address) { return g->mod->relocate(address); },
            g->disc,
            &fonts
        )) {
        return;
    }
    const auto hash = controller_icons_hash(fonts.fonts, fonts.palette);
    if (!controller_icons_is_game_font(hash)) {
        return;
    }
    if (controller_icons_paint_style(fonts.fonts, fonts.palette, CONTROLLER_ICONS_STYLE_STEAM_DECK)
            != 0
        && g->failed_icons != hash) {
        g->failed_icons = hash;
        g->mod->warn("not enough free font palette entries for Steam Deck button icons");
    }
}

bool playing(const openrac_input& in) {
    return g->settings.enabled && in.focused != 0 && in.overlay == 0
           && g->mod->play_status().phase == OPENRAC_PLAY_PLAYING;
}

void capture(bool wanted) {
    if (g->capture != static_cast<int>(wanted) || (wanted && !g->mod->input().captured())) {
        g->capture = wanted;
        g->mod->capture_mouse(wanted);
    }
}

void load(openrac::Mod& mod) {
    if (mod.game() != "rac1" || (mod.version() != "pal" && mod.version() != "ntsc")) {
        throw openrac::Error("Steam Deck Pack supports RAC1 PAL and NTSC-U");
    }
    if (!mod.has_input() || !mod.has_ui() || !mod.has_play()) {
        throw openrac::Error("Steam Deck Pack needs input, UI and play support");
    }
    g = std::make_unique<State>();
    g->mod = &mod;
    g->disc = controller_icons_disc_for(std::string(mod.version()).c_str());
    g->settings.enabled = mod.setting<bool>("enabled");
    g->settings.sensitivity =
        deck::sensitivity(static_cast<int>(mod.setting<std::int64_t>("sensitivity")));
    g->settings.invert_y = mod.setting<bool>("invert_y");
    const auto folder = mod.data_dir();
    if (!folder.empty()) {
        g->file = std::filesystem::path(folder) / "controls.ini";
        std::ifstream in(g->file);
        if (in) {
            g->settings = deck::read_settings(in, g->settings);
        }
    }
    g->page = mod.ui_page([](const auto&, const openrac::UiEvent& e) {
        if (e.is(OPENRAC_UI_CHANGE)) {
            if (e.item() == 0 && e.value() >= 0 && e.value() <= 1) {
                g->settings.enabled = e.value() != 0;
            } else if (e.item() == 1 && e.value() >= 0 && e.value() < 8) {
                g->settings.sensitivity = (e.value() + 1) * 25;
            } else if (e.item() == 2 && e.value() >= 0 && e.value() <= 1) {
                g->settings.invert_y = e.value() != 0;
            } else {
                return;
            }
            save();
        } else if (e.is(OPENRAC_UI_CHOOSE) && e.item() == 3) {
            g->settings = {};
            save();
        }
        show();
    });
    show();
    // The pack belongs in the in-game Options page, never as a separate title-menu row.
    mod.ui_pause_item("Steam Deck", g->page);
    mod.take_input(OPENRAC_INPUT_MOUSE);
    mod.on_pad([](int port, std::uint8_t* report, int size) {
        if (port != 0 || report == nullptr || size < 6) {
            return;
        }
        const auto& in = g->mod->input().raw();
        const bool active = playing(in);
        capture(active);
        const auto stick = g->motion.sample(
            in.tick, in.mouse_dx, in.mouse_dy, active && in.captured != 0, g->settings
        );
        if (active && in.captured != 0) {
            deck::apply(report, size, stick);
        }
    });
    mod.on_frame([] {
        paint_icons();
        const auto& in = g->mod->input().raw();
        const bool active = playing(in);
        capture(active);
        // Discard motion while the overlay/menu owns input, including a pause without a pad read.
        if (!active) {
            g->motion.sample(in.tick, 0, 0, false, g->settings);
        }
    });
    mod.info("Steam Input mouse camera enabled; configure gyro and trackpads in Steam");
}
}  // namespace

OPENRAC_MOD_CPP("steam-deck-pack", "0.3.0", load)

extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    if (g) {
        g->mod->capture_mouse(false);
        g.reset();
    }
}
