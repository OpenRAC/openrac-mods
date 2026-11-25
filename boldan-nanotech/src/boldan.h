/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Boldan's Nanotech Upgrade and the clock that gates it (README.md), read and written in game
 * memory. Going Commando's Boldan (level 13) has a clock tower whose update (UpdateBoldanClock,
 * rac2-decomp src/levels/13_boldan/L13_004067F8-g8.c; func_L13_004067F8 on NTSC-U v1.01,
 * func_PAL_L13_0040B090 on PAL, 0x00407D60 on NTSC-U v2.00) sets its hands from the console's
 * clock and, once a second of its own (pvars +0, +8, +4: hour, minute, second), shows one moby
 * on an even hour of the twelve and hides it on an odd one: the Nanotech Upgrade (class 0x11C4),
 * whose moby array index is the clock's pvars +0x28. Hidden is flags |= 0x43, 0x1000 cleared and
 * no collision; shown is 0x41 and 0x2 cleared, 0x1000 set and the class's collision back.
 *
 * The mod shows it again at once whenever the clock's hour is odd: the pickup is then the game's
 * own on an even hour, so Ratchet collects it as he would (its update marks its slot done). Its
 * pvars +0x10 and +0x14 are the minute and hour hands (classes 0xE9F and 0xEA0, which the clock
 * makes): with the upgrade at +0x28, they tell the clock from any other moby.
 */
#ifndef BOLDAN_NANOTECH_H
#define BOLDAN_NANOTECH_H

#include <stdint.h>
#include <string.h>

#define BN_LEVEL_BOLDAN 13
#define BN_CLASS_UPGRADE 0x11C4
#define BN_CLASS_MINUTE_HAND 0xE9F
#define BN_CLASS_HOUR_HAND 0xEA0

/* A moby (0x100 bytes) and the fields read here. */
#define BN_MOBY_SIZE 0x100u
#define BN_MOBY_STATE 0x20u     /* u8: 0 arming, 1 live; the upgrade's 2 is its collection */
#define BN_MOBY_CLASS_DATA 0x24u /* u32: its class's header; +0x10 the collision */
#define BN_MOBY_FLAGS 0x34u     /* u16 */
#define BN_MOBY_PVARS 0x68u     /* u32 */
#define BN_MOBY_COLLISION 0x98u /* u32 */
#define BN_MOBY_CLASS 0xAAu     /* s16 */

/* The clock's pvars. */
#define BN_CLOCK_HOUR 0x00u
#define BN_CLOCK_MINUTE_HAND 0x10u
#define BN_CLOCK_HOUR_HAND 0x14u
#define BN_CLOCK_UPGRADE 0x28u

/* The console's main memory: every address read is checked against it. */
#define BN_RAM 0x02000000u

/* One disc: g_mobyTableBase in the boot program (the loaded level's copy through relocate_data):
 * the first moby, and past the last 8 bytes on (g_mobyTableEnd). OpenRAC's
 * port/game/rac2-<disc>/host/rac2_addresses.h. */
typedef struct bn_disc {
    const char* version;
    uint32_t moby_table;
} bn_disc;

static const bn_disc k_bn_discs[] = {
    {"ntsc", 0x001B1B5Cu},  /* NTSC-U v1.01 */
    {"pal", 0x001B1B5Cu},   /* PAL */
    {"ntsc2", 0x001B1ADCu}, /* NTSC-U v2.00 */
};

static inline const bn_disc* bn_disc_of(const char* version) {
    for (size_t i = 0; version != NULL && i < sizeof k_bn_discs / sizeof k_bn_discs[0]; ++i) {
        if (strcmp(version, k_bn_discs[i].version) == 0) {
            return &k_bn_discs[i];
        }
    }
    return NULL;
}

static inline uint32_t bn_u32(const uint8_t* mem, uint32_t a) {
    uint32_t v;
    memcpy(&v, mem + a, 4);
    return v;
}
static inline uint16_t bn_u16(const uint8_t* mem, uint32_t a) {
    uint16_t v;
    memcpy(&v, mem + a, 2);
    return v;
}
static inline void bn_put_u32(uint8_t* mem, uint32_t a, uint32_t v) {
    memcpy(mem + a, &v, 4);
}
static inline void bn_put_u16(uint8_t* mem, uint32_t a, uint16_t v) {
    memcpy(mem + a, &v, 2);
}

/* Whether a moby at a fits in memory. */
static inline int bn_moby_ok(uint32_t a) {
    return a != 0 && a <= BN_RAM - BN_MOBY_SIZE;
}
static inline int bn_class(const uint8_t* mem, uint32_t moby) {
    return (int16_t)bn_u16(mem, moby + BN_MOBY_CLASS);
}

/* The level's moby array: *first and *count, 0 when it is not one. */
static inline int bn_mobys(const uint8_t* mem, uint32_t table, uint32_t* first, uint32_t* count) {
    if (table == 0 || table > BN_RAM - 12u) {
        return 0;
    }
    const uint32_t a = bn_u32(mem, table), end = bn_u32(mem, table + 8u);
    if (!bn_moby_ok(a) || end <= a || end > BN_RAM || (end - a) % BN_MOBY_SIZE != 0) {
        return 0;
    }
    *first = a;
    *count = (end - a) / BN_MOBY_SIZE;
    return 1;
}

/* The upgrade the clock at `clock` gates, or 0 when `clock` is not Boldan's clock. */
static inline uint32_t bn_upgrade_of(const uint8_t* mem, uint32_t first, uint32_t count,
                                     uint32_t clock) {
    const uint32_t vars = bn_u32(mem, clock + BN_MOBY_PVARS);
    if (vars == 0 || vars > BN_RAM - 0x30u) {
        return 0;
    }
    const uint32_t minute = bn_u32(mem, vars + BN_CLOCK_MINUTE_HAND);
    const uint32_t hour = bn_u32(mem, vars + BN_CLOCK_HOUR_HAND);
    const uint32_t index = bn_u32(mem, vars + BN_CLOCK_UPGRADE);
    if (!bn_moby_ok(minute) || !bn_moby_ok(hour) || index >= count
        || bn_class(mem, minute) != BN_CLASS_MINUTE_HAND || bn_class(mem, hour) != BN_CLASS_HOUR_HAND) {
        return 0;
    }
    const uint32_t upgrade = first + index * BN_MOBY_SIZE;
    return bn_class(mem, upgrade) == BN_CLASS_UPGRADE ? upgrade : 0;
}

/* Boldan's clock among the level's mobys: its index, -1 for none. */
static inline int bn_find_clock(const uint8_t* mem, uint32_t first, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (bn_upgrade_of(mem, first, count, first + i * BN_MOBY_SIZE) != 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Whether the clock's hour hides the upgrade: odd on the twelve-hour dial, as its update has it
 * (an hour past 12 less 12; 0, 2, ..., 12 show it). */
static inline int bn_hour_hides(const uint8_t* mem, uint32_t clock) {
    const uint32_t vars = bn_u32(mem, clock + BN_MOBY_PVARS);
    int32_t hour;
    memcpy(&hour, mem + vars + BN_CLOCK_HOUR, 4);
    if (hour > 12) {
        hour -= 12;
    }
    return hour >= 0 && hour <= 12 && (hour & 1) != 0;
}

/* Shows the upgrade as the clock does on an even hour, if it is still there to collect (state 0
 * or 1: not collected) and hidden. Returns 1 when it changed it. */
static inline int bn_show(uint8_t* mem, uint32_t upgrade) {
    const uint8_t state = mem[upgrade + BN_MOBY_STATE];
    const uint32_t class_data = bn_u32(mem, upgrade + BN_MOBY_CLASS_DATA);
    if (state > 1 || class_data == 0 || class_data > BN_RAM - 0x14u) {
        return 0;
    }
    const uint16_t flags = bn_u16(mem, upgrade + BN_MOBY_FLAGS);
    const uint32_t collision = bn_u32(mem, class_data + 0x10u);
    const uint16_t shown = (uint16_t)(((flags & 0xFFBEu) | 0x1000u) & 0xFFFDu);
    if (flags == shown && bn_u32(mem, upgrade + BN_MOBY_COLLISION) == collision) {
        return 0;
    }
    bn_put_u16(mem, upgrade + BN_MOBY_FLAGS, shown);
    bn_put_u32(mem, upgrade + BN_MOBY_COLLISION, collision);
    return 1;
}

#endif /* BOLDAN_NANOTECH_H */
