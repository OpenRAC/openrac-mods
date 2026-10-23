/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * What the item randomizer uses of the game on each disc it knows: the addresses of the globals
 * it reads and writes (each through relocate_data, which leaves an address outside the level's
 * relocated range as it is), the banner function it calls by its name in that disc's build
 * (openrac_api.function), and the banner's text. PAL's are games/rac1's names; NTSC-U's are the
 * same globals in games/rac1/ntsc (its __asm__ labels) and OpenRAC's
 * port/game/rac1-ntsc/host/rac1_version.h. Each address is the decompilation's global named in its
 * comment (seen in game code); each offset below is a field of the game's own structure, with the
 * routine that uses it.
 */
#ifndef ITEM_RANDOMIZER_DISCS_H
#define ITEM_RANDOMIZER_DISCS_H

#include <stdint.h>

/* One disc's addresses (a row of k_rando_discs): the globals the mod reads and writes. */
typedef struct rando_disc {
    const char* version; /* openrac_api.version */
    uint32_t owned;      /* u8[0x25]: the items owned (D_0013D5C8; GiveItem, the hero, the menus) */
    uint32_t obtained;   /* u8[0x25]: the items given (D_0013D5F0; the pickups' and NPCs' test) */
    uint32_t planets;    /* u8[20]: the planets unlocked (D_0013DE48) */
    uint32_t gold_bolts; /* u8[20][4]: the gold bolts collected (D_0014BFC0) */
    uint32_t triggers;   /* u8[20][16]: the levels' trigger slots, 0xFF done (D_0014C150) */
    uint32_t level;      /* i32: the level loaded (D_0015EE84) */
    uint32_t vendor_stock; /* u8[12]: the vendor's stock, 0xFF-terminated (D_0015EED0) */
    uint32_t
        last_weapon; /* i32: the weapon a double tap with the wrench goes back to (D_00141760) */
    uint32_t
        vendor_menu; /* the vendor's menu, level 0's address (D_L00_001CA7C0; VENDOR_* below) */
    uint32_t
        vendor_redraw; /* i32: nonzero, the menu shows its item again next frame (D_L00_00161F34) */
    const char* show_banner; /* void (int text, int ticks): a line of text on screen (ShowBanner) */
    int32_t gold_bolt_text;  /* the gold bolt's banner (GoldBoltUpdate's) */
} rando_disc;

/* The discs the mod knows, by openrac_api.version; the fields are in the order of the struct. */
static const rando_disc k_rando_discs[] = {
    {"pal",
     0x0013D5C8u,
     0x0013D5F0u,
     0x0013DE48u,
     0x0014BFC0u,
     0x0014C150u,
     0x0015EE84u,
     0x0015EED0u,
     0x00141760u,
     0x001CA7C0u,
     0x00161F34u,
     "func_L00_00264DB8",
     0x53B8},
    {"ntsc",
     0x0013D4C0u,
     0x0013D4E8u,
     0x0013DD40u,
     0x0014BEC0u,
     0x0014C050u,
     0x0015ED84u,
     0x0015EDD0u,
     0x00141660u,
     0x001CA4C0u,
     0x00161E38u,
     "FUN_L00_00263d40",
     0x53B3},
};

/* The moby's fields it reads (games/rac1/include/moby.h; the same on both discs). */
#define MOBY_STATE 0x20u   /* u8 */
#define MOBY_PVARS 0x78u   /* u32: the pvars' address */
#define MOBY_MISSION 0xB0u /* u8: its trigger slot, 0xFF none */
/* The infobot's pvars (struct Infobot, vendor_002D1168.c): the planet it unlocks. */
#define INFOBOT_PLANET 0x04u /* i32 */
/* The state an infobot shows its planet's banner in, once it has unlocked it (InfobotUpdate). */
#define INFOBOT_STATE_BANNER 11

/* The vendor's menu (VendorBuyDialogUpdate, VendorBuildItemList; the same on both discs): the
 * item bought last, whose demo plays as the menu closes; the entry under the cursor; the buy
 * dialog's state (4: the purchase); the list's entries, each its item and whether it sells the
 * item's ammo (1) or the item (0); how many there are. */
#define VENDOR_STOCK_SIZE 12     /* the stock's bytes */
#define VENDOR_LAST_BOUGHT 0x48u /* i32 */
#define VENDOR_CURSOR 0x58u      /* i32 */
#define VENDOR_DIALOG 0x5Cu      /* i32 */
#define VENDOR_DIALOG_BUY 4
#define VENDOR_ENTRIES 0xD0u /* {i32 item, i32 ammo, ...}[16] */
#define VENDOR_ENTRY_SIZE 0x14u
#define VENDOR_MAX_ENTRIES 16
#define VENDOR_COUNT 0x210u /* i32 */

/* The levels past the game (VendorStockLevelInit gives everything there): left as the game has them. */
#define RANDO_LAST_LEVEL 0x12

#endif /* ITEM_RANDOMIZER_DISCS_H */
