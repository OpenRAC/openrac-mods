/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AP_GOLD_WEAPONS_H
#define AP_GOLD_WEAPONS_H
#include "ap_ids.h"
#define AP_GOLD_WEAPON_COUNT 10
#define AP_GOLD_WEAPON_OFFSET 0x300
#define AP_GOLD_CASE_KIND 0x11
static const int k_ap_gold_bases[AP_GOLD_WEAPON_COUNT] = {
    ITEM_BOMB_GLOVE, ITEM_PYROCITOR, ITEM_BLASTER, ITEM_GLOVE_OF_DOOM, ITEM_MINE_GLOVE,
    ITEM_DEVASTATOR, ITEM_DECOY_GLOVE, ITEM_TESLA_CLAW, ITEM_SUCK_CANNON, ITEM_MORPH_O_RAY
};
static inline int ap_gold_weapon_index(int base) {
    for (int i = 0; i < AP_GOLD_WEAPON_COUNT; ++i) if (k_ap_gold_bases[i] == base) return i;
    return -1;
}
static inline int ap_gold_weapon_base(int64_t id) {
    int64_t base = id - AP_BASE - AP_GOLD_WEAPON_OFFSET;
    return base >= 0 && base < RANDO_ITEM_COUNT && ap_gold_weapon_index((int)base) >= 0 ? (int)base : -1;
}
static inline int64_t ap_gold_case_id(int base) {
    return AP_BASE + AP_LOCATION_OFFSET + ((int64_t)AP_GOLD_CASE_KIND << 16) + base;
}
#ifndef OPENRAC_RAC1_HOOK_GoldBolts_CountUnspent
#define OPENRAC_RAC1_HOOK_GoldBolts_CountUnspent "GoldBolts_CountUnspent"
#define OPENRAC_RAC1_SIG_GoldBolts_CountUnspent "i32()"
typedef int32_t (*openrac_rac1_GoldBolts_CountUnspent)(void);
#endif
#ifndef OPENRAC_RAC1_HOOK_Moby_GoldWeaponCase_Update
#define OPENRAC_RAC1_HOOK_Moby_GoldWeaponCase_Update "Moby_GoldWeaponCase_Update"
#define OPENRAC_RAC1_SIG_Moby_GoldWeaponCase_Update "void(u32)"
typedef void (*openrac_rac1_Moby_GoldWeaponCase_Update)(uint32_t);
#endif
#endif
