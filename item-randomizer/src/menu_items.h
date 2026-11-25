/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RAC1_MENU_ITEMS_H
#define RAC1_MENU_ITEMS_H
#include "rando.h"
#define MENU_ITEM_CHECKS 6
static inline int menu_native_index(int special) {
    static const int indices[SPECIAL_COUNT] = {0, 1, 2, 4, 5};
    return indices[special];
}
static inline int menu_location_row(int index) {
    for (size_t i=0;i<rando_location_count;++i)
        if (rando_locations[i].kind==LOC_SPECIAL && rando_locations[i].key==index) return (int)i;
    return -1;
}
#endif
