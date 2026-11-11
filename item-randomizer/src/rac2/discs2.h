/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Going Commando: what the item randomizer reads and writes of the game on each disc (rac2.c), each
 * through relocate_data. NTSC-U v1.01's are rac2-decomp's globals named in the comments; PAL's and
 * USA v2.00's are the same globals at those discs' addresses, as OpenRAC's release tables give them
 * (port/game/rac2-<disc>/host/rac2_addresses.h, release_names.json) and as the discs' own code
 * forms them: Endako's level start (func_L03_003F09C0 and each disc's copy) reads the Swingshot's
 * owned byte and chunk 5's +0x0D at the addresses below, and the vendor's table is the same 16
 * pairs, at its own place on each disc. The clock and chunk 5 are the save tables' own entries
 * (the global table at 0x00256138 on NTSC-U v1.01 and PAL, 0x002560B8 on USA v2.00: chunk 4 and
 * chunk 5's addresses on each). The data is the same on the three discs: item ids, levels,
 * the vendor's table.
 */
#ifndef ITEM_RANDOMIZER_DISCS2_H
#define ITEM_RANDOMIZER_DISCS2_H

#include <stdint.h>
#include <string.h>

/* One disc's places (a row of k_r2_discs). */
typedef struct r2_disc {
    const char* version;  /* openrac_api.version */
    uint32_t owned;       /* u8[56]: g_inventoryOwned, a byte per item (D_001A7AF8) */
    uint32_t new_flag;    /* u8[56]: g_inventoryNewFlag (D_001A7B30) */
    uint32_t stock_table; /* {i32 item, i32 first level}[16], then -2 (D_002403C0) */
    uint32_t progress;    /* chunk 5 of the save, the story's bytes (D_001395B8) */
    uint32_t clock;       /* u8[8]: chunk 4, the clock stamped when the game saves (D_001A7360) */
} r2_disc;

static const r2_disc k_r2_discs[] = {
    {"ntsc", 0x001A7AF8u, 0x001A7B30u, 0x002403C0u, 0x001395B8u, 0x001A7360u},
    {"pal", 0x001A7B80u, 0x001A7BB8u, 0x002403C0u, 0x00139638u, 0x001A73E0u},
    {"ntsc2", 0x001A7B00u, 0x001A7B38u, 0x00240340u, 0x001395B8u, 0x001A7360u},
};

/* The disc of openrac_api.version, or null. */
static inline const r2_disc* r2_disc_of(const char* version) {
    for (size_t i = 0; version != NULL && i < sizeof k_r2_discs / sizeof k_r2_discs[0]; ++i) {
        if (strcmp(version, k_r2_discs[i].version) == 0) {
            return &k_r2_discs[i];
        }
    }
    return NULL;
}

#endif /* ITEM_RANDOMIZER_DISCS2_H */
