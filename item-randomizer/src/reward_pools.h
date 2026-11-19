#ifndef ITEM_RANDOMIZER_REWARD_POOLS_H
#define ITEM_RANDOMIZER_REWARD_POOLS_H
#include "rando.h"
#define RANDO_NPC_COUNT 13
#define RANDO_GOLD_WEAPON_COUNT 10
static const int k_rando_gold_bases[RANDO_GOLD_WEAPON_COUNT] = {
    ITEM_BOMB_GLOVE, ITEM_PYROCITOR, ITEM_BLASTER, ITEM_GLOVE_OF_DOOM, ITEM_MINE_GLOVE,
    ITEM_DEVASTATOR, ITEM_DECOY_GLOVE, ITEM_TESLA_CLAW, ITEM_SUCK_CANNON, ITEM_MORPH_O_RAY
};

static inline int rando_gold_index(int base) {
    for (int i = 0; i < 10; ++i) {
        if (k_rando_gold_bases[i] == base) {
            return i;
        }
    }
    return -1;
}

typedef struct rando_npc_infobot {
    int level, planet, oclass;
    const char* hook;
} rando_npc_infobot;

static const rando_npc_infobot k_rando_npcs[RANDO_NPC_COUNT] =
    {{1, 2, 774, "Moby_ThePlumber_Update"},
     {1, 3, 730, "Moby_AgnoggShipFront_Update"},
     {4, 6, 1190, "Moby_Lieutenant_Update"},
     {6, 5, 1109, "Moby_Shuttle_Update"},
     {5, 7, 919, "Moby_QwarksBouncer_Update"},
     {7, 8, 436, "Moby_Class436_Update"},
     {8, 10, 1130, "Moby_Commando_Update"},
     {8, 9, 1144, "Moby_BataliaDeserter_Update"},
     {12, 13, 1267, "Moby_HovenTurret_Update"},
     {13, 14, 1353, "Moby_Class1353_Update"},
     {14, 15, 924, "Moby_Sam_Update"},
     {15, 16, 1446, "Moby_UltramechScientist_Update"},
     {15, 17, 1419, "Moby_Class1419_Update"}};

static inline int rando_npc_index(int level, int planet) {
    for (int i = 0; i < RANDO_NPC_COUNT; ++i) {
        if (k_rando_npcs[i].level == level && k_rando_npcs[i].planet == planet) {
            return i;
        }
    }
    return -1;
}

static inline int rando_npc_row(int index) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        if (l->kind == LOC_FIXED && l->level == k_rando_npcs[index].level
            && l->vanilla.kind == REWARD_PLANET && l->vanilla.value == k_rando_npcs[index].planet) {
            return (int)i;
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
