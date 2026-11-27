// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The shoulder camera's entry point (README.md; docs/MULTI_GAME.md for the layout): the game that
// loads the library picks its part. Only Going Commando (rac2) has one: src/rac2/shoulder_camera.cpp.

#include "openrac/mod.hpp"

#include <string>

#include "rac2/shoulder_camera.hpp"

namespace {

// Loads rac2's part for rac2 and refuses any other game. The port calls it when it loads the
// library.
void load(openrac::Mod& mod) {
    // Going Commando: its own hooks, addresses and maths.
    if (mod.game() == "rac2") {
        shoulder_camera::rac2::load(mod);
        return;
    }

    throw openrac::Error(
        "shoulder-camera is for Going Commando (rac2), not " + std::string(mod.game())
    );
}

}  // namespace

// The id and version are the manifest's: the port checks they agree.
OPENRAC_MOD_CPP("shoulder-camera", "1.0.0", load)
