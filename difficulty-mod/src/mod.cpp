// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's entry (README.md; docs/MULTI_GAME.md): one library for Ratchet & Clank and
// Going Commando. openrac_api.game says which game loaded it, and that game's part takes over:
// src/rac1 and src/rac2, each with its own hook points and addresses, over the rules, Chaos and the
// party's rules every game shares (src/common).
#include "openrac/mod.hpp"

#include <string>

#include "rac1/game.h"
#include "rac2/game.h"

namespace {

// Hands the load to the part of the game that loaded the mod; refuses any other game.
void load(openrac::Mod& mod) {
    // Ratchet & Clank.
    if (mod.game() == "rac1") {
        rac1_load(mod);
        return;
    }

    // Going Commando.
    if (mod.game() == "rac2") {
        rac2_load(mod);
        return;
    }

    throw openrac::Error(
        "difficulty is for Ratchet & Clank (rac1) and Going Commando (rac2), not "
        + std::string(mod.game())
    );
}

}  // namespace

OPENRAC_MOD_CPP("difficulty", "1.5.0", load)
