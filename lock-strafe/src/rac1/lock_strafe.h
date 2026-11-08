// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Ratchet & Clank's part of the lock-strafe (lock_strafe.cpp), called by the entry point (mod.cpp).
#ifndef LOCK_STRAFE_RAC1_LOCK_STRAFE_H
#define LOCK_STRAFE_RAC1_LOCK_STRAFE_H

#include "openrac/mod.hpp"

namespace lock_strafe::rac1 {

// Checks the disc, reads the settings, finds the game's functions and takes the hook points.
// Throws openrac::Error to refuse to run.
void load(openrac::Mod& mod);

}  // namespace lock_strafe::rac1

#endif  // LOCK_STRAFE_RAC1_LOCK_STRAFE_H
