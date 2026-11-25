#ifndef AP_ACQUISITION_RULES_H
#define AP_ACQUISITION_RULES_H
#include "ap_gold_weapons.h"
#include "ap_ids.h"

enum {
    AP_ACQUIRE_NONE,
    AP_ACQUIRE_GOLD,
    AP_ACQUIRE_WEAPON,
    AP_ACQUIRE_INFOBOT
};

static inline int ap_acquisition_kind(int64_t id, int same, int* item) {
    *item = 0;
    if (!same) {
        return AP_ACQUIRE_NONE;
    }
    const int gold = ap_gold_weapon_base(id);
    if (gold >= 0) {
        *item = gold;
        return AP_ACQUIRE_WEAPON;
    }
    const rando_reward reward = ap_reward_of(id);
    if (reward.kind == REWARD_GOLD_BOLT) {
        return AP_ACQUIRE_GOLD;
    }
    if (reward.kind == REWARD_PLANET) {
        *item = reward.value;
        return AP_ACQUIRE_INFOBOT;
    }
    if (reward.kind == REWARD_ITEM && reward.value >= ITEM_SUCK_CANNON
        && reward.value <= ITEM_DECOY_GLOVE && reward.value != ITEM_SWINGSHOT
        && reward.value != ITEM_HYDRODISPLACER) {
        *item = reward.value;
        return AP_ACQUIRE_WEAPON;
    }
    return AP_ACQUIRE_NONE;
}
#endif
