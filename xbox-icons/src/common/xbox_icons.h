/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The xbox-icons mod (README.md): what a game's tables say, and the finding and painting every game
 * shares.
 *
 * Ratchet & Clank and Going Commando keep their three fonts alike: effect textures 1, 2 and 3, each
 * a record of 16 bytes in the effect texture table, +8 its texels' offset and +0xA its palette's,
 * in quadwords from the effect bank the level loaded, whose address the game keeps in a word. Each
 * game's folder (src/rac1, src/rac2) names, per disc, where that word and that table are, and the
 * hashes of the game's own fonts; no address is kept here.
 */
#ifndef XBOX_ICONS_H
#define XBOX_ICONS_H

#include <stddef.h>
#include <stdint.h>

#include "openrac/openrac_mod.h"

typedef struct xbox_icons_disc {
    const char* version; /* openrac_api.version */

    /* The executable's, through relocate_data. */
    uint32_t bank;  /* the effect bank's address, a word */
    uint32_t table; /* the effect texture table, 16 bytes a texture */
} xbox_icons_disc;

typedef struct xbox_icons_game {
    const char* game;               /* openrac_api.game */
    const xbox_icons_disc* discs;
    size_t disc_count;
    const char* discs_known;        /* "xbox-icons knows <this>, not <version>" */
    const uint32_t* fonts;          /* the game's fonts (xbox_icons_hash); others are left alone */
    size_t font_count;
} xbox_icons_game;

#define XBOX_ICONS_RAM 0x02000000u /* the console's 32 MB: a font must lie within */
#define XBOX_ICONS_FONT_BYTES (256u * 128u)

/* Each game's tables (src/rac1/rac1.c, src/rac2/rac2.c). */
extern const xbox_icons_game xbox_icons_rac1;
extern const xbox_icons_game xbox_icons_rac2;

#endif /* XBOX_ICONS_H */
