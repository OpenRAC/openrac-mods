// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's rac1 part (game.cpp): Ratchet & Clank's hook points, addresses and tables.

#pragma once

#include "openrac/mod.hpp"

namespace difficulty::rac1 {

// Loads the mod in Ratchet & Clank: refuses a disc it has no addresses for, or an OpenRAC without
// the hook points it needs (openrac::Error, naming them).
void load(openrac::Mod& mod);

}  // namespace difficulty::rac1
