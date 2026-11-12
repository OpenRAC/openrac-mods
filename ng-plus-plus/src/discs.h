/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Going Commando: what NG++ reads and writes of the game on each disc, each through
 * relocate_data (below 0x1A7C80 every program shares the word; above it the boot program's address
 * stands for the loaded level's copy). NTSC-U v1.01's are rac2-decomp's globals named in the
 * comments; PAL's and USA v2.00's are the same globals at those discs' addresses (rac2-decomp's
 * symbol_addrs/eu and usa-v2, OpenRAC's port/game/rac2-<disc>/host/rac2_addresses.h, and the
 * discs' own code: the vendor's state is what func_002F6510 loads on each).
 */
#ifndef WEAPON_TIERS_DISCS_H
#define WEAPON_TIERS_DISCS_H

#include <stdint.h>
#include <string.h>

typedef struct wt_disc {
    const char* version;   /* openrac_api.version */
    uint32_t weapon_table; /* the items' rows, 0xE0 bytes each (g_weaponTable, D_00239BA0) */
    uint32_t slot;         /* u8 per item: its row (g_itemEquippedSlot, D_00139568) */
    uint32_t exp;          /* s32 per item: its experience, << 5 (g_weaponXp, D_00139868) */
    uint32_t ammo;         /* s32 per item: its ammo (g_weaponAmmo, D_00139688) */
    uint32_t owned;        /* u8 per item (g_inventoryOwned, D_001A7AF8) */
    uint32_t hero;         /* the hero (D_00189E20): +0x1248 the item in hand, +0x2290 his moby */
    uint32_t vendor;       /* the vendor's state (g_vendorUi, D_0021F240) */
    uint32_t clock;        /* u8[8]: the save's chunk 4, stamped when the game saves (D_001A7360) */
    const char* text;      /* the game's text by id (func_00288FB8): its string's address */
    uint32_t progress;     /* the save block (g_boltCount): +0x12 the challenge mode counter */
} wt_disc;

static const wt_disc k_wt_discs[] = {
    {"ntsc",
     0x00239BA0u,
     0x00139568u,
     0x00139868u,
     0x00139688u,
     0x001A7AF8u,
     0x00189E20u,
     0x0021F240u,
     0x001A7360u,
     "func_00288FB8",
     0x001A79F8u},
    {"pal",
     0x00239BA0u,
     0x001395E8u,
     0x001398E8u,
     0x00139708u,
     0x001A7B80u,
     0x00189EA0u,
     0x0021F1C0u,
     0x001A73E0u,
     "func_002898E8",
     0x001A7A80u},
    {"ntsc2",
     0x00239B20u,
     0x00139568u,
     0x00139868u,
     0x00139688u,
     0x001A7B00u,
     0x00189E20u,
     0x0021F1C0u,
     0x001A7360u,
     "func_002899F8",
     0x001A7A00u},
};

/* The disc of openrac_api.version, or null. */
static inline const wt_disc* wt_disc_of(const char* version) {
    for (size_t i = 0; version != NULL && i < sizeof k_wt_discs / sizeof k_wt_discs[0]; ++i) {
        if (strcmp(version, k_wt_discs[i].version) == 0) {
            return &k_wt_discs[i];
        }
    }
    return NULL;
}

/* An item's row (the same on the three discs). */
#define WT_ROW_SIZE 0xE0u
#define WT_ROW_VERSION 0x04u /* u8: its version, from 0 (func_00288870) */
#define WT_ROW_TEXT 0x08u    /* s32: the name's text id the vendor's dialog prints */
#define WT_ROW_CLASS 0x14u   /* s32: the class the vendor shows */
#define WT_ROW_NAME 0x48u    /* s16: its name's text id */
#define WT_ROW_NEXT 0x4Au    /* s16: the next version's row, 0 at the last */
#define WT_ROW_PARENT 0x4Cu  /* s16: the version before's row, 0 at the first */
#define WT_ROW_EXP 0x6Cu     /* s32: the experience (>> 5) to the next version; <= 0: a full bar */
#define WT_ROW_PRICE 0x80u   /* s32: its price (the Mega's: what the vendor's upgrade costs) */
#define WT_ROW_AMMO 0x8Eu    /* u16: the most ammo */

/* The hero's fields. */
#define WT_HERO_HELD 0x1248u /* s32: the item in hand */
#define WT_HERO_MOBY 0x2290u /* u32: his moby */

/* The vendor's state (func_002F6608, func_002F6510, func_002F8918). */
#define WT_VENDOR_CURSOR 0x80u   /* s32: the entry under the cursor */
#define WT_VENDOR_ENTRIES 0x140u /* the list */
#define WT_VENDOR_ENTRY_SIZE 0x18u
#define WT_VENDOR_MAX_ENTRIES 64
#define WT_VENDOR_COUNT 0x740u /* s32 */
/* An entry's fields. */
#define WT_ENTRY_ITEM 0x00u
#define WT_ENTRY_KIND 0x04u  /* 1: the item's ammo */
#define WT_ENTRY_CLASS 0x08u /* the preview model's class (func_L00_0037A2E8 loads it) */
#define WT_ENTRY_PRICE 0x0Cu /* the price's text id, 0xCDB */
#define WT_ENTRY_MARKED 0x10u
#define WT_ENTRY_UPGRADE 0x14u /* 1: an upgrade */
#define WT_PRICE_TEXT 0xCDB
/* The label under an upgrade entry's icon in the vendor's list ("Mega", func_002F9C80). */
#define WT_TEXT_UPGRADE 0x325F

/* The challenge mode counter, s16 (+1 at each new game in challenge mode, func_002DE990). */
#define WT_PROGRESS_CYCLE 0x12u

/* A hit request (Moby_PostHit): its damage. */
#define WT_HIT_DAMAGE 0x1Cu

#endif /* WEAPON_TIERS_DISCS_H */
