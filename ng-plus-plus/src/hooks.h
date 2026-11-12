/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The hook points NG++ takes, by their ids: OpenRAC's, added for this mod (SDK 1.14.6;
 * sdk/include/openrac/games/rac2.h, a `hook` row in each rac2 disc's libraries.tsv). Defined here so
 * the mod builds against an SDK that does not name them yet; an OpenRAC without them refuses the
 * mod's load, as for any hook point.
 */
#ifndef WEAPON_TIERS_HOOKS_H
#define WEAPON_TIERS_HOOKS_H

#include <stdint.h>

/* The vendor's list (func_002F6608): entries may be added after the original's. */
#define WT_HOOK_Vendor_BuildList "rac2.vendor.build_list"
#define WT_SIG_Vendor_BuildList "void(i32)"
typedef void (*wt_Vendor_BuildList)(int32_t hide_unowned);

/* The price of the vendor entry under the cursor (func_002F6510). */
#define WT_HOOK_Vendor_Price "rac2.vendor.price"
#define WT_SIG_Vendor_Price "i32()"
typedef int32_t (*wt_Vendor_Price)(void);

/* The vendor's Mega purchase (func_002887B0): 1 when the item moved on. */
#define WT_HOOK_Item_Upgrade "rac2.item.upgrade"
#define WT_SIG_Item_Upgrade "i32(i32)"
typedef int32_t (*wt_Item_Upgrade)(int32_t id);

/* An item's colours (func_00336F20): three u32 at game addresses, the last its tint. */
#define WT_HOOK_Item_Colors "rac2.item.colors"
#define WT_SIG_Item_Colors "void(i32,u32,u32,u32)"
typedef void (*wt_Item_Colors)(int32_t id, uint32_t first, uint32_t second, uint32_t tint);

/* A hit posted to a moby (func_002A8E20): the request's damage at +0x1C. */
#define WT_HOOK_Moby_PostHit "rac2.combat.post_hit"
#define WT_SIG_Moby_PostHit "void(u32,u32)"
typedef void (*wt_Moby_PostHit)(uint32_t victim, uint32_t request);

/* A hit on the hero (Hero_TakeDamage, SDK 1.14.0): its points before the armour's share. */
#define WT_HOOK_Hero_TakeDamage "rac2.player.take_damage"
#define WT_SIG_Hero_TakeDamage "void(i32)"
typedef void (*wt_Hero_TakeDamage)(int32_t amount);

/* The vendor's list drawn (func_002F9C80): each row -5 to +6 around the cursor in the list, in
 * order, asks Item_VersionColor for its icon's colour (an upgrade entry: kind 2, the Mega gold).
 * OpenRAC after SDK 1.14.6; optional (no tint in the vendor without them). */
#define WT_HOOK_Vendor_DrawList "rac2.vendor.draw_list"
#define WT_SIG_Vendor_DrawList "void()"
typedef void (*wt_Vendor_DrawList)(void);

#define WT_HOOK_Item_VersionColor "rac2.item.version_color"
#define WT_SIG_Item_VersionColor "u32(i32)"
typedef uint32_t (*wt_Item_VersionColor)(int32_t kind);

/* The game's text of an id (func_00288FB8): its string's game address. OpenRAC after SDK 1.14.6;
 * optional (the vendor reads "Mega" without it). The port gives eight slots of its own for a mod's
 * texts, 96 bytes each, at the addresses the lookup returns for ids WT_TEXT_SLOT to + 7. */
#define WT_HOOK_Text_ById "rac2.text.by_id"
#define WT_SIG_Text_ById "i32(i32)"
typedef int32_t (*wt_Text_ById)(int32_t id);
#define WT_TEXT_SLOT 0x7F10
#define WT_TEXT_SLOT_BYTES 96

#endif /* WEAPON_TIERS_HOOKS_H */
