/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The mod code of the set in place: the launcher leaves <data>/plugins/modset.json for the game
 * when a player shares its mods, and takes it away when they change. The game shows the code on
 * the host's Rules page and, passed on in MODS, on a refused player's page, only while what this
 * game runs is what the file names: the file is the launcher's word, and it is checked, not trusted.
 * The other way round, a host's code is only ever text of a code's shape: a member makes the link
 * from it, so no party can hand a player a link to anywhere else.
 */
#ifndef MULTIPLAYER_MODCODE_H
#define MULTIPLAYER_MODCODE_H

#include <string>
#include <utility>
#include <vector>

#include "party_mods.hpp"

namespace mp {

/* The launcher's file, format 1. `code` is empty when the server could not be reached: the set has
 * only its long code then, which is the launcher's to show. */
struct ModSetFile {
    std::string code;
    std::string link;
    std::vector<std::pair<std::string, std::string>> mods;  // id, version
};

/* False for anything but a whole file of format 1. */
bool parse_modset(const std::string& text, ModSetFile& out);

/* Whether `own`'s mods a party compares are the file's, at those versions, in any order. */
bool modset_matches(const ModSetFile& file, const ModList& own);

/* A short code as the launcher shows it, K7M2-9QXR: nothing else is shown or linked. */
bool valid_mod_code(const std::string& code);

/* The link of a valid code, for open_link; "" for anything else. */
std::string mod_code_link(const std::string& code);

/* What `folder`'s file (the SDK's plugins_folder) says for this game, from `own`. */
struct ModCode {
    std::string code;    // a valid short code, or empty
    bool shared = false; // the file is there and matches, short code or not
};

ModCode read_mod_code(const std::string& folder, const ModList& own);

/* The words of a page. */
constexpr const char* kModCodeLabel = "Mod code: ";
constexpr const char* kModCodeHint = "Enter it in the launcher, under Mods";
constexpr const char* kModCodeInLauncher = "Your mod code is in the launcher";
constexpr const char* kOpenInLauncher = "Open in launcher";

}  // namespace mp

#endif
