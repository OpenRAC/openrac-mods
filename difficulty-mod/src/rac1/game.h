// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's rac1 part (game.cpp): Ratchet & Clank's hook points, addresses and tables.
#ifndef DIFFICULTY_RAC1_GAME_H
#define DIFFICULTY_RAC1_GAME_H

#include "openrac/mod.hpp"

// Loads the mod in Ratchet & Clank: refuses a disc it has no addresses for, or an OpenRAC without
// the hook points it needs (openrac::Error, naming them).
void rac1_load(openrac::Mod& mod);

#endif  // DIFFICULTY_RAC1_GAME_H
