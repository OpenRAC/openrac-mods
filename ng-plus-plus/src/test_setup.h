// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// A setup for hidden test runs only (CMake option NGPP_TEST_SETUP, off in every package): from the
// environment variable NGPP_TEST ("ITEM:LEVEL,...;hold=ITEM;near=1", NTSC-U only), once a level has
// run 90 ticks, each weapon named is owned at its last version with full ammo and the tier given
// (55 the armour tier, with the game's best armour), the weapon `hold` is equipped, the nearest
// enemies are logged, and with near=1 the hero is put 6 units from the nearest one every 20
// seconds. Nothing is kept in the book (a test run's clock is zeros).

#pragma once

#include <cstdint>

#include "game_view.h"
#include "openrac/mod.hpp"
#include "save_tiers.h"
#include "weapons.h"

namespace ng_plus_plus {

#ifdef NGPP_TEST_SETUP
// The test setup.
class TestSetup {
public:
    TestSetup(openrac::Mod& mod, GameView& view, SaveTiers& tiers, const Weapons& weapons)
        : m_mod(mod),
          m_view(view),
          m_tiers(tiers),
          m_weapons(weapons) {}

    // Once a game tick.
    void tick();

private:
    void apply();
    void bring_near();

    openrac::Mod& m_mod;
    GameView& m_view;
    SaveTiers& m_tiers;
    const Weapons& m_weapons;
    int m_level = -1;
    int m_ticks = 0;
    bool m_near = false;
    int m_hold_ticks = 0;
    float m_spot[3]{};
};
#endif

}  // namespace ng_plus_plus
