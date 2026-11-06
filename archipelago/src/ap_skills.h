/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The skill points as locations of the multiworld: an option of the APWorld, off by default. This
 * mod's own table, not logic.c's: the item randomizer has no way to take an item from them, and its
 * seeds must not move.
 *
 * The game keeps 30 flags (gSkillPoints, D_0013D510 on PAL; item_unlocked, D_0013D408 on NTSC-U).
 * The game sets a flag itself and shows its banner; nothing here changes how one is earned. `flag`
 * is the flag's index, `level` the only level whose code sets it (games/rac1's award sites; the
 * shared overlays test the level). Places are numbered in flag order: the game's text names them,
 * and no build reads that text here.
 *
 * A skill point's `hard` is every progression item, as a gold bolt asks for every gadget: they are
 * optional challenges the game does not say the needs of. `extra` is what its code tests beyond
 * that: the Morph-o-Ray's update (0x08), a Visibomb missile (0x12, already progression).
 */
#ifndef AP_SKILLS_H
#define AP_SKILLS_H

#include <stdint.h>
#include <string.h>

#include "ap_ids.h"
#include "rando.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AP_SKILL_POINT_COUNT 30

typedef struct ap_skill_point {
    const char* name;
    uint8_t flag;
    uint8_t level;
    uint64_t extra;
} ap_skill_point;

static const ap_skill_point k_ap_skill_points[AP_SKILL_POINT_COUNT] = {
    {"Novalis: skill point", 0x00, LEVEL_NOVALIS, 0},
    {"Aridia: skill point 1", 0x01, LEVEL_ARIDIA, 0},
    {"Aridia: skill point 2", 0x02, LEVEL_ARIDIA, 0},
    {"Kerwan: skill point 1", 0x03, LEVEL_KERWAN, 0},
    {"Kerwan: skill point 2", 0x04, LEVEL_KERWAN, 0},
    {"Kerwan: skill point 3", 0x05, LEVEL_KERWAN, 0},
    {"Eudora: skill point", 0x06, LEVEL_EUDORA, 0},
    {"Rilgar: skill point 1", 0x07, LEVEL_RILGAR, 0},
    {"Rilgar: skill point 2", 0x08, LEVEL_RILGAR, BIT(ITEM_MORPH_O_RAY)},
    {"Rilgar: skill point 3", 0x09, LEVEL_RILGAR, 0},
    {"Blarg: skill point", 0x0A, LEVEL_BLARG, 0},
    {"Batalia: skill point 1", 0x0B, LEVEL_BATALIA, 0},
    {"Batalia: skill point 2", 0x0C, LEVEL_BATALIA, 0},
    {"Batalia: skill point 3", 0x0D, LEVEL_BATALIA, 0},
    {"Gaspar: skill point 1", 0x0E, LEVEL_GASPAR, 0},
    {"Gaspar: skill point 2", 0x0F, LEVEL_GASPAR, 0},
    {"Orxon: skill point 1", 0x10, LEVEL_ORXON, 0},
    {"Orxon: skill point 2", 0x11, LEVEL_ORXON, 0},
    {"Pokitaru: skill point 1", 0x12, LEVEL_POKITARU, 0},
    {"Pokitaru: skill point 2", 0x13, LEVEL_POKITARU, 0},
    {"Hoven: skill point 1", 0x14, LEVEL_HOVEN, 0},
    {"Hoven: skill point 2", 0x15, LEVEL_HOVEN, 0},
    {"Gemlik: skill point", 0x16, LEVEL_GEMLIK, 0},
    {"Oltanis: skill point 1", 0x17, LEVEL_OLTANIS, 0},
    {"Oltanis: skill point 2", 0x18, LEVEL_OLTANIS, 0},
    {"Kalebo III: skill point 1", 0x19, LEVEL_KALEBO, 0},
    {"Kalebo III: skill point 2", 0x1A, LEVEL_KALEBO, 0},
    {"Drek's Fleet: skill point 1", 0x1B, LEVEL_FLEET, 0},
    {"Drek's Fleet: skill point 2", 0x1C, LEVEL_FLEET, 0},
    {"Veldin (return): skill point", 0x1D, LEVEL_VELDIN2, 0},
};

/* The flags' address on a disc (openrac_api.version); 0 for one not known. */
static inline uint32_t ap_skill_flags_address(const char* version) {
    if (strcmp(version, "pal") == 0) {
        return 0x0013D510u;
    }

    return strcmp(version, "ntsc") == 0 ? 0x0013D408u : 0u;
}

/* What a skill point is taken to need. */
static inline uint64_t ap_skill_hard(const ap_skill_point* s) {
    return rando_progression_items | s->extra;
}

/* Its id in the multiworld (ap_ids.h). */
static inline int64_t ap_skill_id(const ap_skill_point* s) {
    return ap_skill_location_id(s->level, s->flag);
}

#ifdef __cplusplus
}
#endif

#endif /* AP_SKILLS_H */
