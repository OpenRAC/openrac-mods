// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Ratchet & Clank: the cheats mod's rac1 part (README.md; src/mod.cpp loads it for rac1). Each cheat
// is applied where the game takes the value, through a hook point where there is one; with every
// cheat off each hook calls the game's own function as it was called, and nothing is written.
//
//   invincibility     rac1.player.take_damage: a hit is not passed on to the game
//   infinite ammo     rac1.weapon.take_ammo: the shot is allowed and nothing is taken
//   infinite oxygen   no hook point: after each tick the hero's air is put back to a full breath,
//                     so the tick's breath taken never brings it to 0 (discs.h)
//   bolt multiplier   rac1.moby.collect_bolt: after the game counts a bolt, the rest of the
//                     multiple is added (openrac_api.bolts)
//
// The pause menu's Options page has a Cheats item (interface 1.9) whose page switches them, and
// two buttons: a refill of every owned weapon's ammo to its max, and a gift of bolts. OpenRAC's
// overlay has a Cheats menu with the same (interface 1.10). The settings say what is on at the
// start. A line in the HUD (interface 1.12) names what is on.
#include "game.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cheats.h"
#include "discs.h"
#include "openrac/games/rac1.hpp"
#include "pad_intent.h"

namespace {

using openrac::gaddr;
namespace rac1 = openrac::rac1;

// The level whose program holds the items' table.
constexpr int kVendorLevel = 0;

// The bytes of an s32.
constexpr uint32_t kWord = 4u;

// The pause page's rows, in order.
enum Row : int {
    ROW_INVINCIBLE = 0,
    ROW_AMMO = 1,
    ROW_AIR = 2,
    ROW_BOLTS = 3,
    ROW_REFILL = 4,
    ROW_GIFT = 5,
};

// Where the HUD's line sits: the bottom of the game's 4:3 screen, centred, in the small font.
constexpr float kStatusX = 0.5f;
constexpr float kStatusY = 0.94f;
constexpr float kStatusScale = 1.0f;

// The mod, its disc and the cheats on now.
openrac::Mod* s_mod;
const cheats_disc* s_disc;
cheats::State s_state;
bool s_show_status;
bool s_trace;

// The pause page, when the port has pause items and another mod did not take them all.
openrac::UiPage s_page;
bool s_has_page;

// The HUD's line as last shown, and the level load it was shown in (a load takes it off).
std::string s_shown;
uint32_t s_shown_loads;
bool s_shown_once;

// What the first pad's last report says the player does (pad_intent.h).
cheats::Intent s_intent = cheats::Intent::Unknown;

// Logs `text` when the detailed log is on.
void trace(const std::string& text) {
    // The detailed log is on.
    if (s_trace) {
        s_mod->info(text);
    }
}

// Returns whether the game is in a level: the refill and the gift need one.
bool in_level() {
    // A port without play's calls: the pause page itself only opens in a level.
    if (!s_mod->has_play()) {
        return true;
    }

    const openrac_play_status status = s_mod->play_status();
    return status.level >= 0 && status.phase != OPENRAC_PLAY_LOADING
           && status.phase != OPENRAC_PLAY_FRONT_END;
}

// Fills every owned weapon's ammo to its max (the items' table's, as the game's ammo pickups and
// vendor read it). Returns how many weapons it filled.
int refill_ammo() {
    // Outside a level the level's items' table is not there.
    if (!in_level()) {
        return 0;
    }

    const openrac::Memory memory = s_mod->memory();
    const gaddr ammo = s_mod->relocate(s_disc->ammo);
    const gaddr owned = s_mod->relocate(s_disc->owned);
    const gaddr table = s_mod->relocate_level(kVendorLevel, s_disc->item_table);
    int filled = 0;

    // Each item id: an owned weapon with an ammo max, and less than that.
    for (uint32_t id = 0; id < CHEATS_ITEMS; ++id) {
        // Not owned.
        if (memory.read<uint8_t>(owned + id) == 0) {
            continue;
        }

        const int32_t max =
            memory.read<uint16_t>(table + id * CHEATS_ITEM_SIZE + CHEATS_ITEM_MAX_AMMO);
        const gaddr slot = ammo + id * kWord;

        // No ammo (a gadget), or full already.
        if (max <= 0 || memory.read<int32_t>(slot) >= max) {
            continue;
        }

        memory.write<int32_t>(slot, max);
        ++filled;
    }

    trace("refilled " + std::to_string(filled) + " weapons");
    return filled;
}

// Puts the hero's air back to a full breath, with infinite oxygen on and in a level.
void keep_air() {
    // The cheat is off, or there is no hero.
    if (!s_state.infinite_air || !in_level()) {
        return;
    }

    const openrac::Memory memory = s_mod->memory();
    const gaddr air = s_mod->relocate(s_disc->hero) + CHEATS_HERO_AIR;

    // Some air was taken since the last tick.
    if (memory.read<int32_t>(air) < CHEATS_FULL_AIR) {
        memory.write<int32_t>(air, CHEATS_FULL_AIR);
    }
}

// Gives the pause page's gift of bolts.
void give_bolts() {
    const int32_t add = cheats::gift_bolts(cheats::k_bolt_gift, s_mod->bolts(0));

    // Something to add.
    if (add > 0) {
        s_mod->bolts(add);
    }

    trace("gave " + std::to_string(add) + " bolts");
}

// Shows the HUD's line for the cheats on now, or takes it off; only when it changes or a level load
// took it off.
void show_status() {
    const std::string line = s_show_status ? cheats::status_line(s_state) : std::string();
    const uint32_t loads = s_mod->has_play() ? s_mod->play_status().loads : 0u;

    // The same line is up already in this level.
    if (s_shown_once && line == s_shown && loads == s_shown_loads) {
        return;
    }

    std::vector<openrac_hud_text> lines;

    // A line to show.
    if (!line.empty()) {
        openrac_hud_text text{};
        line.copy(text.text, sizeof text.text - 1);
        text.x = kStatusX;
        text.y = kStatusY;
        text.scale = kStatusScale;
        text.rgba[0] = 128;
        text.rgba[1] = 128;
        text.rgba[2] = 128;
        text.rgba[3] = 200;
        text.flags = OPENRAC_HUD_SMALL | OPENRAC_HUD_SHADOW;
        lines.push_back(text);
    }

    // Outside a level the port refuses the line: try again at the next frame.
    if (!s_mod->hud_text_set(lines) && !line.empty()) {
        return;
    }

    s_shown = line;
    s_shown_loads = loads;
    s_shown_once = true;
}

void show_page();

// Puts the cheats on now on the pause page.
void show_page() {
    // No page: nothing to show.
    if (!s_has_page) {
        return;
    }

    const std::vector<std::string> off_on = {"Off", "On"};
    openrac::UiContent content;
    content.title = "Cheats";
    content.items = {
        openrac::UiItem::choice_of("Invincibility", off_on, s_state.invincible ? 1u : 0u),
        openrac::UiItem::choice_of("Infinite ammo", off_on, s_state.infinite_ammo ? 1u : 0u),
        openrac::UiItem::choice_of("Infinite oxygen", off_on, s_state.infinite_air ? 1u : 0u),
        openrac::UiItem::choice_of(
            "Bolt multiplier",
            cheats::multiplier_names(),
            static_cast<uint32_t>(cheats::multiplier_index(s_state.multiplier))
        ),
        openrac::UiItem::button("Refill ammo"),
        openrac::UiItem::button("Give " + std::to_string(cheats::k_bolt_gift) + " bolts"),
    };
    s_page.set(content);
}

// Returns the index a choice row shows for the cheats on now, and how many choices it has; -1 for
// a row that is not a choice.
int row_choice(int row, int& count) {
    count = 2;

    // Which row.
    switch (row) {
        case ROW_INVINCIBLE:
            return s_state.invincible ? 1 : 0;
        case ROW_AMMO:
            return s_state.infinite_ammo ? 1 : 0;
        case ROW_AIR:
            return s_state.infinite_air ? 1 : 0;
        case ROW_BOLTS:
            count = static_cast<int>(cheats::k_multipliers.size());
            return cheats::multiplier_index(s_state.multiplier);
        default:
            return -1;
    }
}

// The pause page's events: a choice stepped switches its cheat, a button does its deed. Each is
// checked against what the pad says the player does (pad_intent.h): the stick pushed down onto a
// row steps nothing, Left steps back, and only Cross (or a press the pad does not show) chooses a
// button.
void on_page(const openrac::UiPage&, const openrac::UiEvent& event) {
    // A choice stepped.
    if (event.is(OPENRAC_UI_CHANGE)) {
        int count = 0;
        const int current = row_choice(event.item(), count);

        // Not one of the choices.
        if (current < 0) {
            return;
        }

        const std::optional<int> next =
            cheats::step_choice(s_intent, current, count, event.value());

        // The cursor moving onto the row: the port's step taken back.
        if (!next) {
            trace(
                "row " + std::to_string(event.item())
                + " left as it was: the stick moved the cursor"
            );
            show_page();
            return;
        }

        // Which row's choice.
        switch (event.item()) {
            case ROW_INVINCIBLE:
                s_state.invincible = *next != 0;
                trace(std::string("invincibility ") + (s_state.invincible ? "on" : "off"));
                break;
            case ROW_AMMO:
                s_state.infinite_ammo = *next != 0;
                trace(std::string("infinite ammo ") + (s_state.infinite_ammo ? "on" : "off"));

                // Switched on: every weapon starts full, so an empty one fires too.
                if (s_state.infinite_ammo) {
                    refill_ammo();
                }

                break;
            case ROW_AIR:
                s_state.infinite_air = *next != 0;
                trace(std::string("infinite oxygen ") + (s_state.infinite_air ? "on" : "off"));
                break;
            case ROW_BOLTS:
                s_state.multiplier = cheats::multiplier_at(*next);
                trace("bolt multiplier x" + std::to_string(s_state.multiplier));
                break;
            default:
                break;
        }

        // The page shows the cheats (the port may have shown another step).
        show_page();
        return;
    }

    // A button chosen: only by Cross, not by Left, Right or the stick.
    if (event.is(OPENRAC_UI_CHOOSE) && cheats::chooses(s_intent)) {
        // The refill.
        if (event.item() == ROW_REFILL) {
            refill_ammo();
        }

        // The gift of bolts.
        if (event.item() == ROW_GIFT) {
            give_bolts();
        }

        s_page.sound(OPENRAC_UI_SOUND_SELECT);
    }
}

// Finds this disc's addresses; refuses a disc the mod does not know.
void find_disc(openrac::Mod& mod) {
    s_disc = nullptr;

    // Each disc the mod knows: the one whose version this is.
    for (const auto& disc : k_cheats_discs) {
        // This is the disc's version.
        if (mod.version() == disc.version) {
            s_disc = &disc;
        }
    }

    // A disc the mod has no addresses for.
    if (s_disc == nullptr) {
        throw openrac::Error(
            "cheats knows the PAL and NTSC-U discs, not " + std::string(mod.version())
        );
    }
}

// Adds the Cheats item to the pause menu's Options; without room for it the cheats still play as
// the settings say.
void add_page(openrac::Mod& mod) {
    s_has_page = false;

    // A port without pause items (before interface 1.9).
    if (!mod.has_pause_items()) {
        mod.warn("this OpenRAC has no pause menu items: the cheats stay as the settings say");
        return;
    }

    // The port refuses past its most items for all mods together.
    try {
        s_page = mod.ui_page(on_page);
        mod.ui_pause_item("Cheats", s_page);
        s_has_page = true;
    } catch (const openrac::Error& e) {
        mod.warn(
            std::string("no Cheats item in the pause menu (") + e.what()
            + "): the cheats stay as the settings say"
        );
        return;
    }

    show_page();
}

// The overlay's Cheats menu (interface 1.10, developer_menu): the same cheats as the pause page,
// in OpenRAC's own menu (the window's menu bar, and the drawer on a touch screen), at any time in
// the game. Each switch's label says whether it is on; the multiplier's steps on at each press.
enum Action : uint32_t {
    ACTION_INVINCIBLE = 1,
    ACTION_AMMO = 2,
    ACTION_AIR = 3,
    ACTION_BOLTS = 4,
    ACTION_REFILL = 5,
    ACTION_GIFT = 6,
};

bool s_has_menu;

// The switches' labels as the overlay shows them now.
std::array<std::string, ACTION_BOLTS> s_labels;

// The labels as the cheats on now say them.
std::string action_label(uint32_t action) {
    const auto on_off = [](const char* name, bool on) {
        return std::string(name) + (on ? ": On" : ": Off");
    };

    // Which item.
    switch (action) {
        case ACTION_INVINCIBLE:
            return on_off("Invincibility", s_state.invincible);
        case ACTION_AMMO:
            return on_off("Infinite ammo", s_state.infinite_ammo);
        case ACTION_AIR:
            return on_off("Infinite oxygen", s_state.infinite_air);
        case ACTION_BOLTS:
            return "Bolt multiplier: x" + std::to_string(s_state.multiplier);
        case ACTION_REFILL:
            return "Refill ammo";
        case ACTION_GIFT:
            return "Give " + std::to_string(cheats::k_bolt_gift) + " bolts";
        default:
            return std::string();
    }
}

// The overlay's labels as the cheats on now: only from the tick-boundary callback.
void show_menu() {
    // No menu.
    if (!s_has_menu) {
        return;
    }

    const openrac_api& api = s_mod->api();

    // Each switch's label that changed.
    for (uint32_t action = ACTION_INVINCIBLE; action <= ACTION_BOLTS; ++action) {
        std::string label = action_label(action);
        std::string& shown = s_labels[action - 1];

        // The same label is up.
        if (label == shown) {
            continue;
        }

        api.developer_menu_set_label(s_mod->handle(), action, label.c_str());
        shown = std::move(label);
    }
}

// The overlay's presses, queued by the port and taken at a tick's boundary: the cheat switched as
// on the pause page, then the page and the labels showing it.
void take_menu() {
    const openrac_api& api = s_mod->api();
    bool changed = false;

    // Each press since the last boundary.
    while (const uint32_t action = api.take_developer_menu_action(s_mod->handle())) {
        // Which item.
        switch (action) {
            case ACTION_INVINCIBLE:
                s_state.invincible = !s_state.invincible;
                trace(std::string("overlay: invincibility ") + (s_state.invincible ? "on" : "off"));
                break;
            case ACTION_AMMO:
                s_state.infinite_ammo = !s_state.infinite_ammo;
                trace(
                    std::string("overlay: infinite ammo ") + (s_state.infinite_ammo ? "on" : "off")
                );

                // Switched on: every weapon starts full, as from the page.
                if (s_state.infinite_ammo) {
                    refill_ammo();
                }

                break;
            case ACTION_AIR:
                s_state.infinite_air = !s_state.infinite_air;
                trace(
                    std::string("overlay: infinite oxygen ") + (s_state.infinite_air ? "on" : "off")
                );
                break;
            case ACTION_BOLTS: {
                const int next = (cheats::multiplier_index(s_state.multiplier) + 1)
                                 % static_cast<int>(cheats::k_multipliers.size());
                s_state.multiplier = cheats::multiplier_at(next);
                trace("overlay: bolt multiplier x" + std::to_string(s_state.multiplier));
                break;
            }
            case ACTION_REFILL:
                refill_ammo();
                break;
            case ACTION_GIFT:
                // Bolts only in a level, as the page's button (it opens only there).
                if (in_level()) {
                    give_bolts();
                }

                break;
            default:
                break;
        }

        changed = changed || action <= ACTION_BOLTS;
    }

    // A switch changed: the page and the labels follow.
    if (changed) {
        show_page();
    }
}

// Adds the Cheats menu to OpenRAC's overlay; without it (a port before 1.10, or another mod's
// menu of that title) the pause page and the settings still switch them.
void add_menu(openrac::Mod& mod) {
    s_has_menu = false;
    const openrac_api& api = mod.api();

    // A port without mods' menus.
    if (api.api_minor < 10 || api.developer_menu == nullptr
        || api.take_developer_menu_action == nullptr || api.developer_menu_set_label == nullptr) {
        return;
    }

    std::vector<std::string> labels;
    std::vector<openrac_developer_menu_item> items;

    // The items, their labels kept alive while the port copies them.
    for (uint32_t action = ACTION_INVINCIBLE; action <= ACTION_GIFT; ++action) {
        labels.push_back(action_label(action));
    }

    for (uint32_t action = ACTION_INVINCIBLE; action <= ACTION_GIFT; ++action) {
        items.push_back({labels[action - 1].c_str(), nullptr, action, nullptr});
    }

    // Another mod's menu has the title, or the port refused the items.
    if (api.developer_menu(
            mod.handle(), "Cheats", items.data(), static_cast<uint32_t>(items.size())
        )
        != OPENRAC_HOOK_OK) {
        mod.warn("no Cheats menu in OpenRAC's overlay: the pause page still switches them");
        return;
    }

    s_has_menu = true;

    // The labels the menu was made with.
    for (uint32_t action = ACTION_INVINCIBLE; action <= ACTION_BOLTS; ++action) {
        s_labels[action - 1] = labels[action - 1];
    }

    // At each tick's boundary: the presses taken, and the labels following every switch (the
    // page's too).
    mod.on_tick_boundary([](uint32_t) {
        take_menu();
        show_menu();
    });
}

}  // namespace

// The rac1 part's entry (game.h).
void rac1_load(openrac::Mod& mod) {
    s_mod = &mod;
    find_disc(mod);

    s_state = {};
    s_state.invincible = mod.setting<bool>("invincible");
    s_state.infinite_ammo = mod.setting<bool>("infinite_ammo");
    s_state.infinite_air = mod.setting<bool>("infinite_air");
    s_state.multiplier = cheats::parse_multiplier(mod.setting<std::string>("bolt_multiplier"));
    s_show_status = mod.setting<bool>("show_status");
    s_trace = mod.setting<bool>("trace");
    s_shown.clear();
    s_shown_loads = 0;
    s_shown_once = false;

    // Invincibility: a hit with health to take is not passed on.
    mod.hook<rac1::Hero_TakeDamage>([](auto original, int32_t amount) {
        // Invincible, and the hit would take health.
        if (s_state.invincible && amount > 0) {
            return;
        }

        original(amount);
    });

    // Infinite ammo: a shot that takes rounds is allowed and takes none.
    mod.hook<rac1::Weapon_TakeAmmo>([](auto original, int32_t item, int32_t amount) -> int32_t {
        // Infinite ammo, and the shot would take rounds.
        if (s_state.infinite_ammo && amount > 0) {
            return 1;
        }

        return original(item, amount);
    });

    // The bolt multiplier: the game counts the bolt, then the rest of the multiple is added.
    mod.hook<rac1::Moby_CollectBolt>([](auto original, uint32_t bolt) {
        // No multiplier: the game's own count.
        if (s_state.multiplier <= 1) {
            original(bolt);
            return;
        }

        const int32_t before = s_mod->bolts(0);
        original(bolt);
        const int32_t after = s_mod->bolts(0);
        const int32_t extra = cheats::extra_bolts(after - before, s_state.multiplier, after);

        // Something to add.
        if (extra > 0) {
            s_mod->bolts(extra);
        }
    });

    add_page(mod);
    add_menu(mod);

    // Each report of the first pad, the one the menus read: what the player does on the page. The
    // report itself is left as it is.
    s_intent = cheats::Intent::Unknown;
    mod.on_pad([](int port, uint8_t* report, int size) {
        // The first pad only.
        if (port == 0) {
            s_intent = cheats::read_intent(report, size);
        }
    });

    // After each tick: the hero's air kept full, and the HUD's line following the cheats and the
    // level loads.
    mod.on_frame([] {
        keep_air();
        show_status();
    });

    mod.info(
        std::string("cheats ready: ")
        + (s_has_page ? "Cheats in the pause menu's Options" : "no pause page")
        + (s_has_menu ? " and OpenRAC's overlay" : "")
        + (cheats::status_line(s_state).empty() ? ", none on" : ", " + cheats::status_line(s_state))
    );
}
