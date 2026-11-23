// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The Zodiac and the Plasma Coil hit every enemy in their range, on screen or not (README.md,
// "Off-screen enemies"). The game picks those weapons' targets only among the mobys it drew last
// frame, so the Zodiac's pulse and the Plasma Coil's and Plasma Storm's arcs miss what is behind
// the camera or off screen: the PS2's view, not a rule of the weapons. Interface 1.30's
// weapon_ignore_culling has their picks take every enemy in range (their range, angles and caps
// stay the game's). It is read from the port's table while the mods build against an older
// released SDK (sdk.lock), like weapon_api.h's calls, and asked once at load for every NG++ tier
// and the base game alike, unless the "offscreen_targets" setting is off. A port older than 1.30
// keeps the game's picks.

#pragma once

#include <cstdint>

#include "openrac/mod.hpp"

namespace ng_plus_plus {

// The interface minor with weapon_ignore_culling.
inline constexpr std::uint16_t kCullingMinor = 30;

// The weapons whose picks NG++ opens: the Zodiac and the Plasma Coil (its Mega, the Plasma Storm,
// is the same item), and since OpenRAC's next port (#767; an older one refuses them, and keeps the
// game's picks) the Synthenoid and Kilonoid drones, the Seeker Gun's seekers, the Miniturret
// Glove's turrets, the Shield Charger and Tesla Barrier, and the Tesla Claw's held target: all
// round the hero by distance, in sight (no wall between), with the game's range and caps.
inline constexpr std::int32_t kCullingItems[] = {43, 28, 31, 24, 41, 45, 18};

using IgnoreCullingFn = int (*)(openrac_mod*, std::int32_t, std::int32_t);

// Returns `api`'s weapon_ignore_culling, or null on a port older than 1.30 (or whose table ends
// first).
IgnoreCullingFn ignore_culling_of(const openrac_api& api);

// Asks the port for the off-screen picks of kCullingItems when `enabled` (the setting), says in the
// log what it gets. Only during load. Returns the weapons the port took.
int start_culling(openrac::Mod& mod, bool enabled);

}  // namespace ng_plus_plus
