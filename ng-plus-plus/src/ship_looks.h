// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The ship's looks (README.md, "The ship"): the ship glows in its weapon tier's colour, or in a
// look the "ship_look" setting picks, each unlocked by a tier: energy paints of one colour, a
// prismatic one and obsidian. Drawn with OpenRAC's moby_material (interface 1.25's energy, 1.24's
// obsidian) on the ship's mobys: the one the player flies in space (class 4031) and its parts
// (4305 to 4325, 4742 to 4744), the hub's hull (3412) and the ship's shots (4090), so the paint
// the game draws keeps showing under the glow.
//
// Why a setting and not more paints at the Ship Shack: the game's paints are 18 pictures in a disc
// table, its number in the equipment word's bits 16 to 20 (func_002926B8); a number past 17 reads
// past the table, and OpenRAC binds the picture as one texture for the whole level
// (texture_variant). A look drawn over the paint needs neither.
//
// Render only: no game memory is written. Only the game's thread uses it.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "bolt_api.h"
#include "game_view.h"
#include "moby_scan.h"
#include "openrac/mod.hpp"
#include "ship_upgrades.h"

namespace ng_plus_plus {

// ---- The rules ----

// The looks the setting offers.
enum class ShipLook : std::uint8_t {
    Off,        // The game's paint alone.
    Tier,       // The ship tier's colour (none before Alpha).
    Ion,        // Cyan energy, from Alpha.
    Plasma,     // Violet energy, from Beta.
    Solar,      // Gold energy, from Gamma.
    Inferno,    // Crimson energy, from Epsilon.
    Obsidian,   // Obsidian with its glow, from Eta.
    Prismatic,  // A rainbow cycling, from Omega.
};

// Returns the look the setting's value `value` names (Tier for an unknown one).
ShipLook ship_look_of(std::string_view value);

// Returns the ship tier a look needs (0 for Off and Tier).
int ship_look_tier(ShipLook look);

// How the ship is drawn: a material and its colour (0xRRGGBB), or nothing.
struct ShipPaint {
    std::uint32_t material = 0;
    std::uint32_t rgb = 0xFFFFFFu;
};

// Returns how the ship is drawn with look `look` at ship tier `tier` and game tick `tick`: a look
// not unlocked yet falls back to the tier's colour, none at tier 0 or Off.
std::optional<ShipPaint> ship_paint(ShipLook look, int tier, std::uint32_t tick);

// Returns whether moby class `moby_class` is the ship's (the one flown, its parts, the hub's hull,
// its shots).
bool is_ship_moby(int moby_class);

// ---- In the game ----

class ShipLooks {
public:
    ShipLooks(openrac::Mod& mod, const GameView& view, const ShipUpgrades& ship, ShipLook look)
        : m_mod(mod),
          m_view(view),
          m_ship(ship),
          m_scan(view),
          m_look(look) {}

    // Finds moby_material.
    void start();

    // Once a game tick: the ship's mobys drawn in the look.
    void tick(std::uint32_t tick);

private:
    openrac::Mod& m_mod;
    const GameView& m_view;
    const ShipUpgrades& m_ship;
    MobyScan m_scan;
    ShipLook m_look;
    MobyMaterialFn m_material = nullptr;
    bool m_energy = false;
    bool m_obsidian = false;
};

}  // namespace ng_plus_plus
