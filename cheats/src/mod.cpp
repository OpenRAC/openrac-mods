// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The cheats mod's entry (README.md; docs/MULTI_GAME.md): openrac_api.game says which game loaded
// it, and that game's part takes over. Ratchet & Clank only for now; src/common holds what another
// game's part would share.
#include "openrac/mod.hpp"

#include <string>

#include "rac1/game.h"

namespace {

// Hands the load to the part of the game that loaded the mod; refuses any other game.
void load(openrac::Mod& mod) {
    // Ratchet & Clank.
    if (mod.game() == "rac1") {
        rac1_load(mod);
        return;
    }

    throw openrac::Error("cheats is for Ratchet & Clank (rac1), not " + std::string(mod.game()));
}

}  // namespace

OPENRAC_MOD_CPP("cheats", "0.3.1", load)
