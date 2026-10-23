/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The ammo-max mod (README.md): where the vendor's buy dialog keeps what it asks, on each disc, and
 * the count the dialog starts at.
 *
 * The dialog (VendorBuyDialogUpdate: PAL func_L00_0029CDE0, games/rac1
 * src/overlays/shared/update_0029B6A0.c; NTSC-U FUN_L00_0029ba78, the NTSC decomp's
 * src/overlays/shared/gameplay/entities/00295100.c) runs a state at a time: 1 when it opens, which
 * sets the count to 1 and, for ammo the player has room for and bolts to pay a round of, the flag
 * that lets Up raise it; then 2, where Up and Down move the count between 1 and the most the bolts
 * pay for and the weapon has room for, and Square buys. The mod sets the count to that most once,
 * the tick the dialog is first seen in state 2.
 */
#ifndef AMMO_MAX_H
#define AMMO_MAX_H

#include <stdint.h>

typedef struct ammo_max_disc {
    const char* version; /* openrac_api.version */

    /* The executable's, through relocate_data. */
    uint32_t bolts; /* gBolts, s32 */
    uint32_t ammo;  /* the ammo of each item id, s32[37] */

    /* Level 0's program's, through relocate_level_data(0, ...). */
    uint32_t item_table; /* the items' records, 0x18 bytes per item id: +8 and +0xA the round's
                            price (u16; +0xA while the dialog's +0x40 is set), +0xE max ammo */
    uint32_t dialog;     /* the vendor menu: +0x40 which price, +0x58 the row chosen, +0x5C the buy
                            dialog's state, +0xD0 the rows, 0x14 bytes each: +0 the item id, +4 1
                            for its ammo */
    uint32_t count;      /* the count the dialog asks for, s32 */
    uint32_t can_raise;  /* 1 when Up may raise it, s32 */
} ammo_max_disc;

static const ammo_max_disc k_ammo_max_discs[] = {
    {"pal", 0x0015EE98u, 0x0013D530u, 0x001C43B0u, 0x001CA7C0u, 0x00161F30u, 0x00161F10u},
    {"ntsc", 0x0015ED98u, 0x0013D428u, 0x001C40B0u, 0x001CA4C0u, 0x00161E34u, 0x00161E10u},
};

#define AMMO_MAX_ITEMS 37 /* item ids 0..0x24 */
#define AMMO_MAX_ITEM_SIZE 0x18u
#define AMMO_MAX_ROWS 16
#define AMMO_MAX_ROW_SIZE 0x14u

/* The count the dialog starts at: as many rounds as the bolts pay for and the weapon has room
 * for, the most Up would reach; 0 when that is none (a round costs nothing, or no room, or too
 * few bolts: the game's 1 stays). */
static inline int32_t ammo_max_count(int32_t bolts, int32_t price, int32_t ammo, int32_t max) {
    if (price <= 0 || bolts < price || ammo >= max) {
        return 0;
    }
    const int32_t n = bolts / price;
    const int32_t room = max - ammo;
    return n < room ? n : room;
}

#endif /* AMMO_MAX_H */
