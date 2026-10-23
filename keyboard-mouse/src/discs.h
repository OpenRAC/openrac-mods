/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * What the keyboard and mouse mod reads and calls of the game on each disc it knows, by the names
 * each disc's build of the port gives them (openrac_api.function). The PAL disc's are games/rac1's
 * in OpenRAC, the NTSC-U disc's its rac1/ntsc decompilation's, as in the lock-strafe's discs.h.
 */
#ifndef KBM_DISCS_H
#define KBM_DISCS_H

#include <stdint.h>

typedef struct kbm_disc {
    const char* version; /* openrac_api.version */
    /* The executable's: used as they are. */
    uint32_t hero;         /* the hero (Hero) */
    uint32_t quick_select; /* the Quick Select's eight slots: item ids, 0 for an empty one */
    uint32_t boot_mode;    /* the boot program's game mode: the title (0), the main menu (3), ... */
    /* The executable's, through relocate_data. */
    uint32_t page_menu;  /* the menus' state: +4 the page shown (0 while one changes) */
    uint32_t dialog;     /* the freeze dialog (mode 4): +0 its kind */
    uint32_t map_screen; /* DrawMapScreen: the map widget's handler, which reads Circle to travel */
    /* Level 0's program's, through relocate_level_data(0, ...). */
    uint32_t level_mode; /* the game mode: 0 in play, 3 the pause menu, 5 a vendor, ... */
    /* The executable's, through relocate_data. */
    uint32_t pad_sticks;  /* the pad's sticks as floats, -1..1, x right, y down: right x, y, ... */
    uint32_t tilt_invert; /* the options' vertical camera invert word (the follow camera reads the
                           * stick's y negated while it is nonzero) */
    /* Level 0's program's, through relocate_level_data(0, ...). */
    uint32_t cameras; /* the camera block (0x3A0 bytes): +0x180 the current camera, +0x1B0 up */
    /* The game's functions the camera's reset calls. */
    const char* fast_cos;          /* float (float) */
    const char* fast_sin;          /* float (float) */
    const char* camera_look_along; /* void (u32 direction, float, float): look along a heading */
} kbm_disc;

/* A menu page (page_menu +4): its focused widget, its widgets, the widget it moves to next. */
#define KBM_PAGE_FOCUS 0x40u
#define KBM_PAGE_WIDGETS 0x44u /* 14 pointers, 0 for none */
#define KBM_PAGE_WIDGET_COUNT 14
#define KBM_PAGE_PENDING 0x80u
/* A widget: its rectangle on the screen, written each frame (x, y, w, h, s32), and a text list's
 * data (flags, items, the selected one). An item is 12 bytes, its s16 text 0 at the list's end. */
#define KBM_WIDGET_RECT 0x18u
#define KBM_LIST_FLAGS 0x30u
#define KBM_LIST_ITEMS 0x34u
#define KBM_LIST_SELECTED 0x40u
#define KBM_LIST_ITEM_SIZE 12u

/* The camera block (cameras): the current camera, and the follow camera's up. */
#define KBM_CAMERAS_CURRENT 0x180u
#define KBM_CAMERAS_UP 0x1B0u
/* A camera: its rows (+0 the way it looks, in first person), its type's state, its id (0 the
 * follow camera behind Ratchet, 4 the first-person view). */
#define KBM_CAMERA_STATE 0x70u
#define KBM_CAMERA_ID 0x86u
#define KBM_CAMERA_FIRST_PERSON 4
/* The follow camera's state: the eye (where it was put last), the point it orbits, the orbit (from
 * that point to the eye) and its smoothed copy the eye is placed by; the tilt's eased target w and
 * its rate; the turn's and tilt's locks (bit 0: no turning by the stick, bit 1: the tilt held by
 * the game). */
#define KBM_FOLLOW_EYE 0x00u
#define KBM_FOLLOW_TARGET 0x90u
#define KBM_FOLLOW_ORBIT 0x130u
#define KBM_FOLLOW_ORBIT_SMOOTH 0x140u
#define KBM_FOLLOW_TILT 0x1B0u
#define KBM_FOLLOW_TILT_RATE 0x1B4u
#define KBM_FOLLOW_LOCKS 0x1B8u
/* The first-person view's state: how far it looks down (radians). */
#define KBM_FIRST_PERSON_TILT 0x10u

/* In the hero, on both discs. */
#define KBM_HERO_FACING 0x98u    /* f32: the way he faces (rot.z) */
#define KBM_HERO_ITEM 0x10B8u    /* i32: the item in hand (items[0].item_id) */
#define KBM_HERO_MOBY 0x2080u    /* u32: his moby; +0xE0 his up (the first person's turn's axis) */
#define KBM_HERO_VEHICLE 0x20A4u /* u8: 0 on foot */
#define KBM_HERO_PENDING 0x20B8u /* i32: the item to take next (what the Quick Select writes) */

static const kbm_disc k_kbm_discs[] = {
    {"pal",
     0x0013F450u,
     0x00141FA0u,
     0x0015F6E8u,
     0x001D5F70u,
     0x00193400u,
     0x0021CE60u,
     0x0015F6A8u,
     0x0013CB40u,
     0x0015EEDCu,
     0x00166D80u,
     "func_001F9F90",
     "func_001F9FA8",
     "func_L00_002E9DC8"},
    {"ntsc",
     0x0013F350u,
     0x00141EA0u,
     0x0015F604u,
     0x001D5BF0u,
     0x00193300u,
     0x0021BE60u,
     0x0015F5C4u,
     0x0013CA40u,
     0x0015EDDCu,
     0x00166C80u,
     "FUN_001f9dc8",
     "FUN_001f9de0",
     "FUN_L00_002e8918"},
};

#endif /* KBM_DISCS_H */
