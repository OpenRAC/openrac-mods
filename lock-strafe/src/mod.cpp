// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The lock-strafe's entry point (README.md; docs/MULTI_GAME.md for the layout): the game that
// loads the library picks its part. Only Ratchet & Clank (rac1) has one: src/rac1/lock_strafe.cpp.
#include "openrac/mod.hpp"

#include <string>

#include "rac1/lock_strafe.h"

namespace {

// The port calls this when it loads the library: rac1's part for rac1, a refusal for any other game.
void load(openrac::Mod& mod) {
    // Ratchet & Clank: its own hooks, addresses and tables.
    if (mod.game() == "rac1") {
        lock_strafe::rac1::load(mod);
        return;
    }

    throw openrac::Error(
        "lock-strafe is for Ratchet & Clank (rac1), not " + std::string(mod.game())
    );
}

}  // namespace

OPENRAC_MOD_CPP("lock-strafe", "1.1.0", load)
