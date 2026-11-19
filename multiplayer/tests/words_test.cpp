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
#include "cosave.hpp"
#include "direct.hpp"
#include "menu.hpp"
#include "migrate.hpp"
#include "modcode.hpp"
#include "openrac/openrac_mod.h"
#include "party_mods.hpp"
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

    // Every character must be ASCII, a button glyph or one of the accented letters.
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i++]);

        // ASCII text and the two controller glyphs pass at once.
        if ((c >= 0x20 && c < 0x7F) || c == 0x10 || c == 0x12) {
            continue;
        }

        // Anything else must be a two-byte UTF-8 sequence.
        if ((c & 0xE0) != 0xC0 || i == text.size()) {
            return false;
        }

        const auto next = static_cast<unsigned char>(text[i++]);

        // The second byte of a sequence must be a continuation byte.
        if ((next & 0xC0) != 0x80) {
            return false;
        }

        const char32_t letter = ((c & 0x1F) << 6) | (next & 0x3F);

        // A two-byte letter must be in the game's accented repertoire.
        if (accented.find(letter) == std::u32string::npos) {
            return false;
        }
    }

    return true;
}

// Checks that every catalogue row has a distinct English text and a usable text in each language,
// and that the lookups give the documented answers.
void catalog() {
    std::set<std::string> english;

    // A row holds one text in each of the five languages, English first.
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

    // An index the port does not map to a language falls back to English.
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

// Checks that the status, hint, party, hosting, question, lobby and notice lines fit the byte limit
// and the game's letters in every language.
void pages() {
    using Mode = mp::Connection::Mode;
    using State = mp::Connection::State;

    // Every page is checked in each of the five languages.
    for (unsigned i = 0; i < 5; ++i) {
        const auto lang = static_cast<mp::Language>(i);

        // The status line is built for every combination of mode, state and flags.
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
                        in.hosting_port = 7770;
                        const auto line = mp::status_text(in, lang);
                        CHECK(line.size() <= OPENRAC_UI_TEXT_MAX);
                        CHECK(game_letters(line));
                    }
                }
            }
        }

        // A hint line holds the select glyph, and the back glyph only where the key names it.
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
        // Round 3's Party rules, then Leave party, last.
        CHECK(lines.size() == 5);
        CHECK(lines[3].kind == mp::PartyLine::Kind::kRules);
        CHECK(lines[4].kind == mp::PartyLine::Kind::kLeave);
        CHECK(lines[4].label == mp::translated("Leave party", lang));
        in.direct = true;
        in.where = "192.168.1.2:7770";
        CHECK(mp::party_lines(in, lang)[0].value == in.where);
        CHECK(mp::hosting_lines({"192.168.1.2"}, 7770, 2, lang)[0] == "192.168.1.2:7770");
        CHECK(
            mp::hosting_lines({}, 7770, 2, lang)[0]
            == std::string(mp::translated("Port", lang)) + " 7770"
        );
        CHECK(
            std::strcmp(
                mp::pause_leave_question(true, true, lang), mp::translated("Stop hosting?", lang)
            )
            == 0
        );
        CHECK(
            std::strcmp(
                mp::pause_leave_question(false, true, false, lang),
                mp::translated("End the party?", lang)
            )
            == 0
        );
        CHECK(
            std::strcmp(
                mp::pause_leave_question(false, false, lang), mp::translated("Leave party?", lang)
            )
            == 0
        );
        mp::LobbyMemberInput member;
        member.name = "Player_123";
        member.ready = true;
        member.you = true;
        CHECK(
            mp::lobby_member_line(member, lang)
            == std::string("Player_123") + mp::translated(" (ready)", lang)
                   + mp::translated(" - you", lang)
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
        CHECK(mp::localized_notice("Starting", lang) == mp::translated("Starting", lang));
        CHECK(
            mp::localized_notice("Host has build_123", lang)
            == std::string(mp::translated("Host has ", lang)) + "build_123"
        );
        CHECK(mp::localized_notice("Port 7770 busy", lang).find("7770") != std::string::npos);
        CHECK(mp::localized_notice("Port 7770 failed", lang).find("7770") != std::string::npos);
        CHECK(mp::localized_notice("Custom server message", lang) == "Custom server message");
        CHECK(mp::localized_notice("PlayerName", lang) == "PlayerName");
    }

    // The default helper still emits the existing network description, regardless of UI language.
    CHECK(mp::save_description("Pokitaru", 12, false) == "Pokitaru, 12 gold bolts");
    CHECK(mp::localized_notice("Port 7770 busy", mp::Language::kGerman) == "Port 7770 belegt");
    CHECK(mp::localized_notice("Host has another", mp::Language::kGerman) == "Host: andere");
}

// ---- Fit: nothing the pages say may be wider than the panel it is drawn in ----
//
// The game's per-character widths are on the disc, not in the repository, so this is a model: each
// character's width in the screen's 512-unit space, measured from a capture of the pages in Going
// Commando and rounded up about 10%. A modelled line is within about 8% of the measured one.
// Going Commando's panels are fixed (port/game/rac2/host/mod_pages.c), and the narrowest a game
// uses: the title's box holds about 215 units in the large font (x1.13), the list's panel 380, and
// the hint box under it, which holds the status and the footer's lines, 160 (165 modelled).
// Ratchet & Clank's pages stretch to fit their texts (mod_ui.cpp's menu_fit), so these hold for
// both.
constexpr double kTitleUnits = 215.0;
constexpr double kListUnits = 380.0;
constexpr double kFooterUnits = 165.0;
constexpr double kLargeFont = 1.13;

// Returns the modelled width of a text in screen units, kept a little above what was measured.
double units(std::string_view text) {
    double total = 0;

    // Each UTF-8 character is counted once, at the width of its class.
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);

        if ((c & 0xC0) == 0x80) {
            continue;  // a UTF-8 continuation: the letter was counted
        }

        // Characters fall into classes of width: narrow, medium, wide and the button glyphs.
        if (c == ' ') {
            total += 6;
        } else if (std::strchr("ijl'.,:;!|", ch) != nullptr) {
            total += 5.4;
        } else if (std::strchr("tfr()-", ch) != nullptr) {
            total += 7.8;
        } else if (ch == 'm' || ch == 'w') {
            total += 15;
        } else if (ch == 'M' || ch == 'W') {
            total += 15.6;
        } else if (c == 0x10 || c == 0x12) {
            total += 12;  // a button's glyph
        } else if (c >= 0x80 || (c >= 'a' && c <= 'z')) {
            total += 10.1;
        } else if (ch == 'I') {
            total += 6;
        } else if (c >= 'A' && c <= 'Z') {
            total += 11.4;
        } else if (c >= '0' && c <= '9') {
            total += 11.5;
        } else {
            total += 10.8;
        }
    }

    return total;
}

// The number of lines that overflowed their box.
int g_overflows = 0;
// The overflows already reported, by place and text.
std::set<std::string> g_reported;

// Reports an overflow, once per place and text, when a line at a font scale is wider than its
// budget.
void fits(const char* where, std::string_view text, double budget, double font = 1.0) {
    const double width = units(text) * font;

    // Only a line wider than its box is reported.
    if (width > budget) {
        const std::string key = std::string(where) + std::string(text);

        // A text already reported at this place is not reported again.
        if (!g_reported.insert(key).second) {
            return;
        }

        std::fprintf(
            stderr,
            "overflow %s: \"%.*s\" is %.0f of %.0f\n",
            where,
            static_cast<int>(text.size()),
            text.data(),
            width,
            budget
        );
        ++g_overflows;
    }
}

// Checks a line against the title box, which uses the large font.
void title(const char* where, std::string_view text) {
    fits(where, text, kTitleUnits, kLargeFont);
}

// Checks a line against the list's panel.
void item(const char* where, std::string_view text) {
    fits(where, text, kListUnits);
}

// Checks a line against the hint box under the list.
void footer(const char* where, std::string_view text) {
    fits(where, text, kFooterUnits);
}

// Checks that no line any page can show is wider than the box it is drawn in, in every language.
void panels() {
    // The model against what was measured on screen (it may only be above it).
    CHECK(units("The party saves in a slot of its own") >= 301);
    CHECK(units("Hosting on port 7770") >= 170);
    CHECK(units("Playing: New game") >= 160);
    CHECK(units("\x10 Select   \x12 Leave") >= 148);

    // Where each row of the catalogue is drawn: a title, a footer line (the status, the hints), or
    // a list line (the default, every fragment and label).
    const std::set<std::string> titles = {
        "Multiplayer",      "Browse games",     "Direct connect", "Team tools",
        "Host which save?", "Play which save?", "Leave party?",   "Stop hosting?",
        "End the party?",   "Direct party",     "Hosting",        "Join by address",
        "Continue as host", "Remove a player",  "Can't join",     "Can't play",
        "Leave party",      "Create match",     "Join by code",   "Party code",
        "Host directly",
    };
    const std::set<std::string> footers = {
        "Hosting",
        " (TCP)",
        "Not connected",
        "Connecting...",
        "Joining...",
        "Joined",
        "Connected",
        "Host offline",
        "Server offline",
        "Host left",
        "Server lost",
        "Waiting...",
        "Party over",
        "No such code",
        "Party full",
        "Already in party",
        "No party",
        "Refused",
        "Host only",
        "Starting",
        "Can't start",
        "Can't start here",
        "Port 7770",
        "No server",
        "Plays a copy",
        "Can't host",
        "Hold L3 + R3",
        "(differs): mods",
        "Past parties",
        "Own copies",
        "Bad address",
        "\x10 Select   \x12 Back",
        "\x10 Select   \x12 Leave",
        "Host has ",
    };

    // Every row is checked against the box it is drawn in.
    for (const mp::Words& row : mp::kWords) {
        const bool is_title = titles.count(row.text[0]) != 0;
        const bool is_footer = footers.count(row.text[0]) != 0;

        for (unsigned i = 0; i < 5; ++i) {
            const std::string what = std::string(row.text[0]) + " [" + std::to_string(i) + "]";
            std::string line = row.text[i];

            // A row with a {0} placeholder is measured with a port number filled in.
            if (line.find("{0}") != std::string::npos) {
                line.replace(line.find("{0}"), 3, "7770");
            }

            // A row is measured against the box where it is drawn, which the sets above name.
            if (is_title) {
                title(what.c_str(), line);
            } else if (is_footer) {
                // A line that ends in a value gets its worst: a port, or a version.
                footer(
                    what.c_str(),
                    row.text[0][std::strlen(row.text[0]) - 1] == ' ' ? line + "7770" : line
                );
            } else {
                item(what.c_str(), line);
            }
        }
    }

    // The composed lines, in every language: the status line, the pause page, the lobby's lines.
    using Mode = mp::Connection::Mode;
    using State = mp::Connection::State;

    // The composed lines are checked in each language.
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
                        in.hosting_port = 7770;
                        footer("status line", mp::status_text(in, lang));
                        in.notice = "Host has another";
                        footer("status notice", mp::status_text(in, lang));
                        in.notice = "Port 7770 failed";
                        footer("status notice", mp::status_text(in, lang));
                    }
                }
            }
        }

        for (const char* hints :
             {mp::kSelectBackHints,
              mp::kSelectLeaveHints,
              mp::kCodeHints,
              mp::kJoinHints,
              mp::kBackHints}) {
            footer("hints", mp::translated(hints, lang));
        }

        for (const char* notice :
             {"Removed",          "Banned",         "Host left",      "Continue in Saves",
              "Spot lost",        "Games differ",   "Bad save",       "Bad host save",
              "No card support",  "Host restarted", "Reconnecting",   "You host now",
              "Ban applied",      "Pick to remove", "Different disc", "Can't start",
              "Can't start here", "Starting",       "Refused",        "Host only",
              "Port 7770 busy",   "Bad address"}) {
            footer(notice, mp::localized_notice(notice, lang));
        }

        // The lobby's member lines (a 16-letter name) and its count line.
        for (const bool host : {false, true}) {
            for (const bool differs : {false, true}) {
                for (const bool ready : {false, true}) {
                    mp::LobbyMemberInput m;
                    m.name = "Kerosene_Ratchet";
                    m.host = host;
                    m.you = true;
                    m.differs = differs;
                    m.ready = ready;
                    item("lobby member", mp::lobby_member_line(m, lang));
                    m.checking = true;
                    item("lobby member", mp::lobby_member_line(m, lang));
                }
            }
        }

        item(
            "lobby count",
            "4/4 " + std::string(mp::translated(" players", lang)).substr(1) + ", 999 ms"
                + mp::translated(", waiting", lang)
        );
        item("why line", mp::lobby_why_line("Kerosene_Ratchet", "Difficulty settings, 1 more"));
        // The pause page.
        mp::PartyPageInput in;
        in.in_party = true;
        in.removes = true;
        in.code = "ABCD23";
        in.host_level = "Marcadia";
        in.go_to_host = true;
        in.members =
            {{"Kerosene_Ratchet", true, false, 120, true, 1},
             {"Kerosene_Ratchet", false, true, -1, false, 2}};

        // Every label and value of the pause page must fit the list.
        for (const mp::PartyLine& line : mp::party_lines(in, lang)) {
            item("pause label", line.label);

            // A line with no value has nothing more to measure.
            if (!line.value.empty()) {
                item("pause value", line.value);
            }
        }

        // Direct hosting's lines, in the list, and a ring row's values.
        for (const std::string& line :
             mp::hosting_lines({"255.255.255.255", "10.255.255.255"}, 65535, 4, lang)) {
            item("hosting line", line);
        }

        // The question's boxes.
        for (const bool hosting : {false, true}) {
            // The question is drawn in the title box for each pair of hosting and host.
            for (const bool host : {false, true}) {
                title(
                    "question",
                    mp::translated(mp::pause_leave_question(hosting, host, false, lang), lang)
                );
            }
        }
    }

    // What the pages say in English only (no row of the catalogue).
    for (const char* line :
         {mp::kRemoveLabel,
          mp::kBanLabel,
          mp::kRemoveNote,
          mp::kBanNote,
          mp::kModCodeHint,
          mp::kModCodeInLauncher,
          mp::kOpenInLauncher,
          mp::kCopyCodeLabel,
          mp::kCopiedLabel,
          mp::kCoSavesLine,
          mp::kSavePartyLabel,
          mp::kPartyRulesLabel,
          mp::kGoToHostLabel,
          "Saving in Co-op 4",
          "Press Start when ready"}) {
        item(line, line);
    }

    item(
        "mod code",
        mp::fit_ui_text(
            std::string(mp::kModCodeLabel) + std::string("ABCDEFGHJKLMNPQRSTUVWXYZ23456789"),
            mp::kItemMax
        )
    );

    for (const char* line :
         {mp::kMatchHint1,
          mp::kMatchHint2,
          mp::kSoloHint1,
          mp::kSoloHint2,
          mp::kRemoveHint,
          mp::kKickedNotice,
          mp::kBannedNotice,
          mp::kHostLeftNotice,
          mp::kContinueNotice,
          mp::kLostSlotNotice,
          mp::kBadSaveHost,
          mp::kBadSaveMember,
          mp::kNoCardFilter,
          mp::kDroppedLine,
          mp::kYouHostLine,
          mp::kBannedSent,
          mp::kDifferentDisc}) {
        footer(line, line);
    }

    // Wrapped lines of the refused and rules pages (the pause menu's are narrower).
    mp::Difference d;
    d.mod = "Item Randomizer";
    d.theirs = "0.12.0";
    d.mine = "0.11.0";
    d.settings = {"Difficulty", "Ammo Max", "Bolt Multiplier"};

    // Each kind of difference gives its own refusal text.
    for (const mp::Difference::Kind kind :
         {mp::Difference::Kind::kDisc,
          mp::Difference::Kind::kOpenRac,
          mp::Difference::Kind::kMultiplayer,
          mp::Difference::Kind::kHostOnly,
          mp::Difference::Kind::kYouOnly,
          mp::Difference::Kind::kVersion,
          mp::Difference::Kind::kSettings,
          mp::Difference::Kind::kSolo,
          mp::Difference::Kind::kPack,
          mp::Difference::Kind::kPackOrder}) {
        d.kind = kind;

        // The title page and the pause menu wrap the same text at different widths.
        for (const std::string& line : mp::refusal_lines({d}, mp::kTitleLine)) {
            item("refusal line", line);
        }

        for (const std::string& line : mp::refusal_lines({d}, mp::kPauseLine)) {
            item("refusal line (pause)", line);
        }
    }

    CHECK(g_overflows == 0);
}

// Checks that fit_ui_text cuts at the byte limit without splitting a UTF-8 character.
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
    panels();
    std::puts("multiplayer translations: OK");
}
