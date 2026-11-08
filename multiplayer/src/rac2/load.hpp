/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod in Going Commando (load.cpp): multiplayer.cpp's load hands rac2 to it.
 */
#ifndef MULTIPLAYER_RAC2_LOAD_H
#define MULTIPLAYER_RAC2_LOAD_H

#include "openrac/mod.hpp"

namespace mpmod {

/* The mod's load in Going Commando: the settings' party, the co-op on the play calls. Throws
 * openrac::Error to refuse (a port without them). */
void rac2_load(openrac::Mod& mod);

/* Its end: the connection's thread stops. Nothing when it did not load. */
void rac2_unload();

}  // namespace mpmod

#endif
