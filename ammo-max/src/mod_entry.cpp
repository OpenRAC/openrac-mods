// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
// The same native module offers the original RAC1 dialog maximum and
// the optional one-button refill for the supported RAC1/RAC2 vendors.
#include "openrac/mod.hpp"

void load_ammo_max(openrac::Mod& mod);
void load_buy_all(openrac::Mod& mod);

namespace {
void load(openrac::Mod& mod) {
    if (mod.game() == "rac1") {
        // Preserve the original Keros feature and its independently tracked dialog state.
        load_ammo_max(mod);
    } else if (mod.game() != "rac2") {
        throw openrac::Error("ammo-max supports RAC1 and RAC2");
    }
    load_buy_all(mod);
}
} // namespace

OPENRAC_MOD_CPP("ammo-max", "1.1.0", load)
