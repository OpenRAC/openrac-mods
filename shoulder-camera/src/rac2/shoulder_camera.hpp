// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Going Commando's part of the shoulder camera (shoulder_camera.cpp).

#pragma once

#include "openrac/mod.hpp"

namespace shoulder_camera::rac2 {

// Takes the follow camera's stick read, pad 0's reports and the overlay for the running disc, and
// reads the settings. Throws openrac::Error for a disc the mod does not know.
void load(openrac::Mod& mod);

}  // namespace shoulder_camera::rac2
