// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Ratchet & Clank's part of the cheats mod (game.cpp), which src/mod.cpp loads for rac1.
#pragma once

#include "openrac/mod.hpp"

// Checks the disc, reads the settings, takes the hook points and adds the pause menu's page.
// Throws openrac::Error to refuse the load.
void rac1_load(openrac::Mod& mod);
