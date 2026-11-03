// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
// PAL language selection, the real font's repertoire, UI byte limits and localized page states.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <string_view>

#include "coop.hpp"
#include "direct.hpp"
#include "menu.hpp"
#include "openrac/openrac_mod.h"
#include "words.hpp"

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

namespace {
// options_menu::game_text's repertoire, plus the two controller glyphs used by these pages.
bool game_letters(std::string_view text) {
    const std::u32string accented = U"áàâäéèêëíìîïóòôöúùûüÁÀÂÄÉÈÊËÍÌÎÏÓÒÔÖÚÙÛÜÑñÇçß¡¿";
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i++]);
        if ((c >= 0x20 && c < 0x7F) || c == 0x10 || c == 0x12) {
            continue;
        }
        if ((c & 0xE0) != 0xC0 || i == text.size()) {
            return false;
        }
        const auto next = static_cast<unsigned char>(text[i++]);
        if ((next & 0xC0) != 0x80) {
            return false;
        }
        const char32_t letter = ((c & 0x1F) << 6) | (next & 0x3F);
        if (accented.find(letter) == std::u32string::npos) {
            return false;
        }
    }
    return true;
}

void catalog() {
    std::set<std::string> english;
    for (const mp::Words& row : mp::kWords) {
        CHECK(english.insert(row.text[0]).second);
        for (unsigned i = 0; i < 5; ++i) {
            CHECK(row.text[i] != nullptr && row.text[i][0] != '\0');
            CHECK(std::strlen(row.text[i]) <= OPENRAC_UI_TEXT_MAX);
            CHECK(game_letters(row.text[i]));
            CHECK(
                std::strcmp(mp::translated(row.text[0], static_cast<mp::Language>(i)), row.text[i])
                == 0
            );
        }
    }
    const char* unknown = "An unknown server message";
    CHECK(mp::translated(unknown, mp::Language::kGerman) == unknown);
    CHECK(std::strcmp(mp::translated("Join", static_cast<mp::Language>(99)), "Join") == 0);
    for (int index : {-1, 0, 1, 6, 99}) {
        CHECK(mp::language_from_game(index) == mp::Language::kEnglish);
    }
    CHECK(mp::language_from_game(2) == mp::Language::kFrench);
    CHECK(mp::language_from_game(3) == mp::Language::kGerman);
    CHECK(mp::language_from_game(4) == mp::Language::kSpanish);
    CHECK(mp::language_from_game(5) == mp::Language::kItalian);
    CHECK(mp::language_address("pal") == 0x0015EE88u);
    CHECK(mp::language_address("ntsc") == 0x0015ED88u);
    CHECK(mp::language_address("unknown") == 0);
}

void pages() {
    using Mode = mp::Connection::Mode;
    using State = mp::Connection::State;
    for (unsigned i = 0; i < 5; ++i) {
        const auto lang = static_cast<mp::Language>(i);
        for (auto mode : {Mode::kNone, Mode::kServer, Mode::kDirect, Mode::kHosting}) {
            for (auto state :
                 {State::kIdle,
                  State::kConnecting,
                  State::kUnreachable,
                  State::kLost,
                  State::kConnected}) {
                for (bool greeted : {false, true}) {
                    for (bool waiting : {false, true}) {
                        mp::StatusInput in;
                        in.mode = mode;
                        in.state = state;
                        in.greeted = greeted;
                        in.waiting = waiting;
                        in.address = "255.255.255.255:65535";
                        in.hosting_port = 65535;
                        const auto line = mp::status_text(in, lang);
                        CHECK(line.size() <= OPENRAC_UI_TEXT_MAX);
                        CHECK(game_letters(line));
                    }
                }
            }
        }
        for (const char* key :
             {mp::kSelectBackHints, mp::kSelectLeaveHints, mp::kCodeHints, mp::kJoinHints}) {
            const std::string line = mp::translated(key, lang);
            CHECK(line.find('\x10') != std::string::npos);
            CHECK(
                (line.find('\x12') != std::string::npos) == (std::strchr(key, '\x12') != nullptr)
            );
        }
        mp::PartyPageInput in;
        CHECK(mp::party_lines(in, lang)[0].label == mp::translated("Not in a party", lang));
        in.in_party = true;
        in.code = "ABCD23";
        in.host_level = "Pokitaru";
        in.members = {{"Player_123", true, true, 38, true}};
        const auto lines = mp::party_lines(in, lang);
        CHECK(lines[0].label == mp::translated("Party code", lang));
        CHECK(lines[0].value == "ABCD23");
        CHECK(lines[1].value == "Pokitaru");
        CHECK(lines[2].label == std::string("Player_123") + mp::translated(" (host, you)", lang));
        CHECK(lines[2].value == "38 ms UDP");
        CHECK(lines[3].label == mp::translated("Leave party", lang));
        in.direct = true;
        in.where = "192.168.1.2:7770";
        CHECK(mp::party_lines(in, lang)[0].value == in.where);
        CHECK(
            mp::hosting_lines({"192.168.1.2"}, 7770, 2, lang)[0]
            == std::string(mp::translated("Hosting on ", lang)) + "192.168.1.2:7770"
        );
        CHECK(
            mp::hosting_lines({}, 7770, 2, lang)[0]
            == std::string(mp::translated("Hosting on port ", lang)) + "7770"
        );
        CHECK(
            std::strcmp(
                mp::pause_leave_question(true, true, lang), mp::translated("Stop hosting?", lang)
            )
            == 0
        );
        CHECK(mp::save_description("", 0, true, lang) == mp::translated("New game", lang));
        CHECK(
            mp::save_description("Pokitaru", 1, false, lang)
            == std::string("Pokitaru, 1") + mp::translated(" gold bolt", lang)
        );
        CHECK(
            mp::save_description("Pokitaru", 12, false, lang)
            == std::string("Pokitaru, 12") + mp::translated(" gold bolts", lang)
        );
        CHECK(mp::localized_notice("Game starting", lang) == mp::translated("Game starting", lang));
        CHECK(
            mp::localized_notice("Incompatible version: the host has build_123", lang)
            == std::string(mp::translated("Incompatible version: the host has ", lang))
                   + "build_123"
        );
        CHECK(mp::localized_notice("Port 7770 is in use", lang).find("7770") != std::string::npos);
        CHECK(mp::localized_notice("Custom server message", lang) == "Custom server message");
        CHECK(mp::localized_notice("PlayerName", lang) == "PlayerName");
    }
    // The default helper still emits the existing network description, regardless of UI language.
    CHECK(mp::save_description("Pokitaru", 12, false) == "Pokitaru, 12 gold bolts");
    CHECK(
        mp::localized_notice("Port 7770 is in use", mp::Language::kGerman) == "Port 7770 ist belegt"
    );
    CHECK(
        mp::localized_notice("Incompatible version: the host has another", mp::Language::kGerman)
        == "Inkompatible Version: Host hat eine andere Version"
    );
}

void clipping() {
    CHECK(mp::fit_ui_text("été", 47) == "été");
    // The cutoff falls inside é: never hand an incomplete UTF-8 character to the port.
    const std::string long_label = std::string(43, 'a') + "ééé";
    const std::string clipped = mp::fit_ui_text(long_label, OPENRAC_UI_TEXT_MAX);
    CHECK(clipped == std::string(43, 'a') + "...");
    CHECK(game_letters(clipped));
    CHECK(mp::fit_ui_text("éé", 1).empty());
    CHECK(mp::fit_ui_text("abc", 0).empty());
    CHECK(mp::fit_ui_text("abc", 2) == "ab");
}
}  // namespace

int main() {
    catalog();
    pages();
    clipping();
    std::puts("multiplayer translations: OK");
}
