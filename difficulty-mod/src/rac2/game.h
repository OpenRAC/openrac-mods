// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's rac2 part (game.cpp): Going Commando's hook points and addresses.
#ifndef DIFFICULTY_RAC2_GAME_H
#define DIFFICULTY_RAC2_GAME_H

#include "openrac/mod.hpp"

// Loads the mod in Going Commando: refuses a disc it has no addresses for, or an OpenRAC without
// the hook points it needs (openrac::Error, naming them).
void rac2_load(openrac::Mod& mod);

#endif  // DIFFICULTY_RAC2_GAME_H
