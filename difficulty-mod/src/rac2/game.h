// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The difficulty mod's rac2 part (game.cpp): Going Commando's hook points and addresses.

#pragma once

#include "openrac/mod.hpp"

namespace difficulty::rac2 {

// Loads the mod in Going Commando: refuses a disc it has no addresses for, or an OpenRAC without
// the hook points it needs (openrac::Error, naming them).
void load(openrac::Mod& mod);

}  // namespace difficulty::rac2
