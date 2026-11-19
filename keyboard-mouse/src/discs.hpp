// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// What the keyboard and mouse mod reads and calls of the game on each disc it knows, by the names
// each disc's build of the port gives them (openrac_api.function). The PAL disc's are games/rac1's
// in OpenRAC, the NTSC-U disc's its rac1/ntsc decompilation's, as in the lock-strafe's discs.hpp.
// Evidence: the per-disc addresses are (documented) by those decompilations; the structure offsets
// below are (assumed) where a comment gives no other source.

#pragma once

#include <cstdint>

namespace kbm {

// Where one disc keeps what the mod reads, and the names of the game's functions it calls.
struct Disc {
    // The value of openrac_api.version that names the disc.
    const char* version;

    // The executable's, used as they are: the hero (Hero), the Quick Select's eight slots (item
    // ids, 0 for an empty one) and the boot program's game mode (the title 0, the main menu 3,
    // ...).
    std::uint32_t hero;
    std::uint32_t quick_select;
    std::uint32_t boot_mode;

    // The executable's, through relocate: the menus' state (+4 the page shown, 0 while one
    // changes) and the freeze dialog (mode 4; +0 its kind).
    std::uint32_t page_menu;
    std::uint32_t dialog;

    // DrawMapScreen: the map widget's handler, which reads Circle to travel. Its name, not its
    // address: in a level the menus run from that level's copy of it, at another address.
    const char* map_screen;

    // Level 0's program's, through relocate_level(0, ...): the game mode (0 in play, 3 the pause
    // menu, 5 a vendor, ...).
    std::uint32_t level_mode;

    // The executable's, through relocate: the pad's sticks as floats, -1..1, x right, y down
    // (right x, y, ...), and the options' vertical camera invert word (the follow camera reads the
    // stick's y negated while it is nonzero).
    std::uint32_t pad_sticks;
    std::uint32_t tilt_invert;

    // Level 0's program's, through relocate_level(0, ...): the camera block (0x3A0 bytes; +0x180
    // the current camera, +0x1B0 up).
    std::uint32_t cameras;

    // The game's functions the camera's reset calls, by the name each disc's port gives them.
    const char* fast_cos;           // float (float)
    const char* fast_sin;           // float (float)
    const char* camera_look_along;  // void (u32 direction, float, float): look along a heading

    // The updates of the two kinds of menu widget the d-pad moves the focus between (a widget's
    // update is at +0): an item grid's (update_menu_grid_selection) and a text list's
    // (update_menu_entry_actions).
    const char* grid_update;
    const char* list_update;

    // The executable's, through relocate: an item grid icon's width and height (two floats, in the
    // units of the frame the grid is laid out in; draw_menu_item_grid).
    std::uint32_t grid_icon_size;
};

// A menu page (page_menu +4): its focused widget, its widgets, the widget it moves to next.
inline constexpr std::uint32_t kPageFocus = 0x40u;
inline constexpr std::uint32_t kPageWidgets = 0x44u;  // 14 pointers, 0 for none
inline constexpr int kPageWidgetCount = 14;
inline constexpr std::uint32_t kPagePending = 0x80u;
// A widget: its rectangle on the screen, written each frame (x, y, w, h, s32), and a text list's
// data (flags, items, the selected one). An item is 12 bytes, its s16 text 0 at the list's end.
inline constexpr std::uint32_t kWidgetRect = 0x18u;
inline constexpr std::uint32_t kListFlags = 0x30u;
inline constexpr std::uint32_t kListItems = 0x34u;
inline constexpr std::uint32_t kListSelected = 0x40u;
inline constexpr std::uint32_t kListItemSize = 12u;
// The links the d-pad moves the focus along (games/rac1 pause.c, MenuItemGridData and
// MenuTextListData): an item grid's up, down, left and right; a text list's up and down. 0 for
// none.
inline constexpr std::uint32_t kGridLinks = 0x4Cu;
inline constexpr int kGridLinkCount = 4;
// An item grid (MenuItemGridData, draw_menu_item_grid): its flags, margins (two floats), selected
// cell, rows and columns; and the frame it is laid out in, its menu moby's (+0x14): the moby's
// +0x78 points at it, whose +0x40 is its width and height (two floats).
inline constexpr std::uint32_t kGridFlags = 0x30u;
inline constexpr std::uint32_t kGridMargins = 0x34u;
inline constexpr std::uint32_t kGridSelected = 0x3Cu;
inline constexpr std::uint32_t kGridRows = 0x40u;
inline constexpr std::uint32_t kGridCols = 0x44u;
inline constexpr std::uint32_t kWidgetMoby = 0x14u;
inline constexpr std::uint32_t kMenuMobyFrame = 0x78u;
inline constexpr std::uint32_t kFrameSize = 0x40u;
inline constexpr std::uint32_t kListUp = 0x38u;
inline constexpr std::uint32_t kListDown = 0x3Cu;

// The camera block (cameras): the current camera, and the follow camera's up.
inline constexpr std::uint32_t kCamerasCurrent = 0x180u;
inline constexpr std::uint32_t kCamerasUp = 0x1B0u;
// A camera: its rows (+0 the way it looks, in first person), its type's state, its id (0 the
// follow camera behind Ratchet, 4 the first-person view).
inline constexpr std::uint32_t kCameraState = 0x70u;
inline constexpr std::uint32_t kCameraId = 0x86u;
inline constexpr int kCameraFirstPerson = 4;
// The follow camera's state: the eye (where it was put last), the point it orbits, the orbit (from
// that point to the eye) and its smoothed copy the eye is placed by; the tilt's eased target w and
// its rate; the turn's and tilt's locks (bit 0: no turning by the stick, bit 1: the tilt held by
// the game).
inline constexpr std::uint32_t kFollowEye = 0x00u;
inline constexpr std::uint32_t kFollowTarget = 0x90u;
inline constexpr std::uint32_t kFollowOrbit = 0x130u;
inline constexpr std::uint32_t kFollowOrbitSmooth = 0x140u;
inline constexpr std::uint32_t kFollowTilt = 0x1B0u;
inline constexpr std::uint32_t kFollowTiltRate = 0x1B4u;
inline constexpr std::uint32_t kFollowLocks = 0x1B8u;
// The first-person view's state: how far it looks down (radians).
inline constexpr std::uint32_t kFirstPersonTilt = 0x10u;

// In the hero, on both discs.
inline constexpr std::uint32_t kHeroFacing = 0x98u;  // f32: the way he faces (rot.z)
inline constexpr std::uint32_t kHeroItem = 0x10B8u;  // i32: the item in hand (items[0].item_id)
inline constexpr std::uint32_t kHeroMoby =
    0x2080u;  // u32: his moby; +0xE0 his up (the first person's turn's axis)
inline constexpr std::uint32_t kHeroVehicle = 0x20A4u;  // u8: 0 on foot
inline constexpr std::uint32_t kHeroPending =
    0x20B8u;  // i32: the item to take next (what the Quick Select writes)

// The discs the mod knows, in the field order of Disc: matched by openrac_api.version at load.
inline constexpr Disc kDiscs[] = {
    {"pal",
     0x0013F450u,
     0x00141FA0u,
     0x0015F6E8u,
     0x001D5F70u,
     0x00193400u,
     "func_0021CE60",
     0x0015F6A8u,
     0x0013CB40u,
     0x0015EEDCu,
     0x00166D80u,
     "func_001F9F90",
     "func_001F9FA8",
     "func_L00_002E9DC8",
     "func_0021C840",
     "func_0021BB90",
     0x00160390u},
    {"ntsc",
     0x0013F350u,
     0x00141EA0u,
     0x0015F604u,
     0x001D5BF0u,
     0x00193300u,
     "FUN_0021be60",
     0x0015F5C4u,
     0x0013CA40u,
     0x0015EDDCu,
     0x00166C80u,
     "FUN_001f9dc8",
     "FUN_001f9de0",
     "FUN_L00_002e8918",
     "FUN_0021b858",
     "FUN_0021abf8",
     0x00160290u},
};

}  // namespace kbm
