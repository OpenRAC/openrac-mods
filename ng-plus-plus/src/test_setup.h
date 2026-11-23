// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// A setup for hidden test runs only (CMake option NGPP_TEST_SETUP, off in every package): from the
// environment variable NGPP_TEST ("ITEM:LEVEL,...;hold=ITEM;near=1", NTSC-U only), once a level has
// run 90 ticks, each weapon named is owned at its last version with full ammo and the tier given
// (55 the armour tier, with the game's best armour; 54 the ship's tier), the weapon `hold` is
// equipped, `cycle=N` sets the challenge mode counter, the nearest
// enemies are logged, and with near=1 the hero is put 6 units from the nearest one every 20
// seconds. armour=1 holds him at the armour vendor (a class-11 vendor whose pvars +0x94 is 2) and
// logs its list and the armour tiers' row. Nothing is kept in the book (a test run's clock is zeros).

#pragma once

#include <cstdint>
#include <string>

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
    void bring_to_vendor();
    void watch_vendor();
    std::string shown_text(std::int32_t id) const;

    openrac::Mod& m_mod;
    GameView& m_view;
    SaveTiers& m_tiers;
    const Weapons& m_weapons;
    int m_level = -1;
    int m_ticks = 0;
    bool m_near = false;
    bool m_vendor = false;     // vendor=1: the hero held at the weapon vendor.
    bool m_armour = false;     // armour=1: at the armour vendor (pvars +0x94 of 2).
    std::string m_armour_seen;  // The armour list last logged.
    int m_at_class = 11;       // The class he is held at: the vendor's, or at=CLASS's.
    int m_buy = -1;            // buy=ITEM: the cursor kept on ITEM's tier entry.
    int m_xp_item = -1;        // xp=ITEM:N: N experience given to ITEM after the setup.
    std::int32_t m_xp = 0;
    int m_vendor_seen = -1;    // The vendor's count at the last dump.
    int m_hold_ticks = 0;
    float m_spot[3]{};
    float m_yaw = 0.0f;        // The heading held with the spot, when m_face.
    bool m_face = false;
    std::uint32_t m_vendor_moby = 0;  // The moby he is held at.
};
#endif

}  // namespace ng_plus_plus
