// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The lock-strafe's entry point (README.md; docs/MULTI_GAME.md for the layout): the game that
// loads the library picks its part. Only Ratchet & Clank (rac1) has one:
// src/rac1/lock_strafe.cpp.

#include "openrac/mod.hpp"

#include <string>

#include "rac1/lock_strafe.hpp"

namespace {

// Loads rac1's part for rac1 and refuses any other game. The port calls it when it loads the
// library.
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

// The id and version are the manifest's: the port checks they agree.
OPENRAC_MOD_CPP("lock-strafe", "1.2.1", load)
