#ifndef ITEM_RANDOMIZER_REWARD_POOLS_H
#define ITEM_RANDOMIZER_REWARD_POOLS_H
#include "rando.h"
#define RANDO_GOLD_WEAPON_COUNT 10
static const int k_rando_gold_bases[10] = {10, 16, 15, 20, 17, 11, 21, 25, 9, 12};

static inline int rando_gold_index(int base) {
    for (int i = 0; i < 10; ++i) {
        if (k_rando_gold_bases[i] == base) {
            return i;
        }
    }
    return -1;
}

static inline int rando_case_row(int base) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (rando_locations[i].kind == LOC_GOLD_CASE && rando_locations[i].key == base) {
            return (int)i;
        }
    }
    return -1;
}
#endif
