/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The museum's drawn effects (museum_fx.c). */
#ifndef MUSEUM_FX_H
#define MUSEUM_FX_H

#include "openrac/openrac_mod.h"

void museum_fx_init(const openrac_api* api, openrac_mod* mod);
/* A new level load: its props are made again on the next tick. */
void museum_fx_new_level(void);
/* Each tick in the museum while playing: HERO Ratchet's place, STATION the tile he stands on (-1). */
void museum_fx_tick(const float hero[3], int station);
/* Hides every effect (a menu is up, or the museum is left). */
void museum_fx_hide(void);

#endif
