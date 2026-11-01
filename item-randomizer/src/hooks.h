/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The hook points the item randomizer takes that the mod SDK it is pinned to (sdk.lock) does not
 * name: their names, signatures and types. The first five are OpenRAC's, added for this mod
 * (sdk/include/openrac/games/rac1.h, a `hook` row in each disc's libraries.tsv; ids rac1.item.give,
 * rac1.planet.unlock, rac1.vendor.init_level_stock, rac1.vendor.build_item_list,
 * rac1.gold_bolt.count_collected); the moby updates are the behaviour catalogue's hook points
 * (sdk/bindings/rac1.ids.lock: rac1.moby.infobot.update, rac1.moby.moby1290.update,
 * rac1.moby.quest_giver.update, rac1.moby.guide.update, rac1.moby.moby90.update,
 * rac1.moby.edwina.update, rac1.moby.moby22.update, rac1.moby.moby914.update), named here by their
 * Moby_<Class>_Update aliases. Help_Request and Ship_InitTransition came with OpenRAC's interface
 * 1.12: an OpenRAC without them still runs the mod, as the load's log says. An SDK header that has
 * one wins: each is defined here only when the SDK's did not.
 */
#ifndef ITEM_RANDOMIZER_HOOKS_H
#define ITEM_RANDOMIZER_HOOKS_H

#include "openrac/games/rac1.h"

#ifndef OPENRAC_RAC1_HOOK_Item_Give
#define OPENRAC_RAC1_HOOK_Item_Give "Item_Give"
#define OPENRAC_RAC1_SIG_Item_Give "void(i32,i32)"
typedef void (*openrac_rac1_Item_Give)(int32_t id, int32_t equip);
#endif
#ifndef OPENRAC_RAC1_HOOK_Planet_Unlock
#define OPENRAC_RAC1_HOOK_Planet_Unlock "Planet_Unlock"
#define OPENRAC_RAC1_SIG_Planet_Unlock "void(i32)"
typedef void (*openrac_rac1_Planet_Unlock)(int32_t planet);
#endif
#ifndef OPENRAC_RAC1_HOOK_Vendor_InitLevelStock
#define OPENRAC_RAC1_HOOK_Vendor_InitLevelStock "Vendor_InitLevelStock"
#define OPENRAC_RAC1_SIG_Vendor_InitLevelStock "void()"
typedef void (*openrac_rac1_Vendor_InitLevelStock)(void);
#endif
#ifndef OPENRAC_RAC1_HOOK_Vendor_BuildItemList
#define OPENRAC_RAC1_HOOK_Vendor_BuildItemList "Vendor_BuildItemList"
#define OPENRAC_RAC1_SIG_Vendor_BuildItemList "void(i32)"
typedef void (*openrac_rac1_Vendor_BuildItemList)(int32_t skip_unflagged);
#endif
#ifndef OPENRAC_RAC1_HOOK_GoldBolts_CountCollected
#define OPENRAC_RAC1_HOOK_GoldBolts_CountCollected "GoldBolts_CountCollected"
#define OPENRAC_RAC1_SIG_GoldBolts_CountCollected "i32()"
typedef int32_t (*openrac_rac1_GoldBolts_CountCollected)(void);
#endif
#ifndef OPENRAC_RAC1_HOOK_Moby_Infobot_Update
#define OPENRAC_RAC1_HOOK_Moby_Infobot_Update "Moby_Infobot_Update"
#define OPENRAC_RAC1_SIG_Moby_Infobot_Update "void(u32)"
typedef void (*openrac_rac1_Moby_Infobot_Update)(openrac_gaddr moby);
#endif
#ifndef OPENRAC_RAC1_HOOK_Moby_PilotsHelmet_Update
#define OPENRAC_RAC1_HOOK_Moby_PilotsHelmet_Update "Moby_PilotsHelmet_Update"
#define OPENRAC_RAC1_SIG_Moby_PilotsHelmet_Update "void(u32)"
typedef void (*openrac_rac1_Moby_PilotsHelmet_Update)(openrac_gaddr moby);
#endif
#ifndef OPENRAC_RAC1_HOOK_Moby_Helga_Update
#define OPENRAC_RAC1_HOOK_Moby_Helga_Update "Moby_Helga_Update"
#define OPENRAC_RAC1_SIG_Moby_Helga_Update "void(u32)"
typedef void (*openrac_rac1_Moby_Helga_Update)(openrac_gaddr moby);
#endif
#ifndef OPENRAC_RAC1_HOOK_Moby_BigAl_Update
#define OPENRAC_RAC1_HOOK_Moby_BigAl_Update "Moby_BigAl_Update"
#define OPENRAC_RAC1_SIG_Moby_BigAl_Update "void(u32)"
typedef void (*openrac_rac1_Moby_BigAl_Update)(openrac_gaddr moby);
#endif
#ifndef OPENRAC_RAC1_HOOK_Moby_Bob_Update
#define OPENRAC_RAC1_HOOK_Moby_Bob_Update "Moby_Bob_Update"
#define OPENRAC_RAC1_SIG_Moby_Bob_Update "void(u32)"
typedef void (*openrac_rac1_Moby_Bob_Update)(openrac_gaddr moby);
#endif
#ifndef OPENRAC_RAC1_HOOK_Moby_Edwina_Update
#define OPENRAC_RAC1_HOOK_Moby_Edwina_Update "Moby_Edwina_Update"
#define OPENRAC_RAC1_SIG_Moby_Edwina_Update "void(u32)"
typedef void (*openrac_rac1_Moby_Edwina_Update)(openrac_gaddr moby);
#endif
#ifndef OPENRAC_RAC1_HOOK_Help_Request
#define OPENRAC_RAC1_HOOK_Help_Request "Help_Request"
#define OPENRAC_RAC1_SIG_Help_Request "i32(i32,i32)"
typedef int32_t (*openrac_rac1_Help_Request)(int32_t message, int32_t slot);
#endif
#ifndef OPENRAC_RAC1_HOOK_Ship_InitTransition
#define OPENRAC_RAC1_HOOK_Ship_InitTransition "Ship_InitTransition"
#define OPENRAC_RAC1_SIG_Ship_InitTransition "void(i32)"
typedef void (*openrac_rac1_Ship_InitTransition)(int32_t mode);
#endif
#ifndef OPENRAC_RAC1_HOOK_Vendor_MenuUpdate
#define OPENRAC_RAC1_HOOK_Vendor_MenuUpdate "Vendor_MenuUpdate"
#define OPENRAC_RAC1_SIG_Vendor_MenuUpdate "void()"
typedef void (*openrac_rac1_Vendor_MenuUpdate)(void);
#endif
#ifndef OPENRAC_RAC1_HOOK_Ammo_ChooseDrop
#define OPENRAC_RAC1_HOOK_Ammo_ChooseDrop "Ammo_ChooseDrop"
#define OPENRAC_RAC1_SIG_Ammo_ChooseDrop "i32(u32,u32)"
typedef int32_t (*openrac_rac1_Ammo_ChooseDrop)(openrac_gaddr moby, openrac_gaddr out_item);
#endif
#ifndef OPENRAC_RAC1_HOOK_Moby_Class22_Update
#define OPENRAC_RAC1_HOOK_Moby_Class22_Update "Moby_Class22_Update"
#define OPENRAC_RAC1_SIG_Moby_Class22_Update "void(u32)"
typedef void (*openrac_rac1_Moby_Class22_Update)(openrac_gaddr moby);
#endif
#ifndef OPENRAC_RAC1_HOOK_Moby_Class23_Update
#define OPENRAC_RAC1_HOOK_Moby_Class23_Update "Moby_Class23_Update"
#define OPENRAC_RAC1_SIG_Moby_Class23_Update "void(u32)"
typedef void (*openrac_rac1_Moby_Class23_Update)(openrac_gaddr moby);
#endif
#ifndef OPENRAC_RAC1_HOOK_Moby_RobotQwark_Update
#define OPENRAC_RAC1_HOOK_Moby_RobotQwark_Update "Moby_RobotQwark_Update"
#define OPENRAC_RAC1_SIG_Moby_RobotQwark_Update "void(u32)"
typedef void (*openrac_rac1_Moby_RobotQwark_Update)(openrac_gaddr moby);
#endif

#endif /* ITEM_RANDOMIZER_HOOKS_H */
