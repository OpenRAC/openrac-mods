/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Ratchet & Clank in Archipelago: the ids the multiworld knows this game's items and locations by.
 * The mod and the APWorld agree on them: the APWorld reads them from the table tools/dump_table.c
 * prints, and that tool uses these functions.
 *
 * An id is built from what the game itself calls the thing, never from a row's place in logic.c's
 * table, so a new row there does not move the others:
 *   item      AP_BASE + the game's item id           (GiveItem's ids, 0x02..0x23)
 *   planet    AP_BASE + 0x100 + the planet           (UnlockPlanet's, the levels 0..18)
 *   gold bolt AP_BASE + 0x200
 *   location  AP_BASE + 0x10000 + kind << 12 | level << 8 | key   (rando.h's LOC_*)
 *   skill point  the same, with its own kind AP_SKILL_KIND and the game's flag as the key
 */
#ifndef AP_IDS_H
#define AP_IDS_H

#include <stdint.h>

#include "rando.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's name in the multiworld: the APWorld's `game`. */
#define AP_GAME "Ratchet & Clank"

/* The first id of this game ("RC" in ASCII, then zeroes). */
#define AP_BASE INT64_C(0x52430000)
#define AP_PLANET_OFFSET 0x100
#define AP_GOLD_BOLT_OFFSET 0x200
#define AP_SPECIAL_OFFSET 0x400
#define AP_LOCATION_OFFSET 0x10000
#define AP_SKILL_KIND 0x10 /* past rando.h's LOC_*, which may grow */

/* The id of a reward as an item of the multiworld; -1 for none. */
static inline int64_t ap_item_id(rando_reward r) {
    switch (r.kind) {
        case REWARD_SPECIAL:
            return AP_BASE + AP_SPECIAL_OFFSET + r.value;
        case REWARD_ITEM:
            return AP_BASE + r.value;
        case REWARD_PLANET:
            return AP_BASE + AP_PLANET_OFFSET + r.value;
        case REWARD_GOLD_BOLT:
            return AP_BASE + AP_GOLD_BOLT_OFFSET;
        default:
            return -1;
    }
}

/* The reward an item id of the multiworld stands for; kind REWARD_NONE for one not this game's. */
static inline rando_reward ap_reward_of(int64_t id) {
    rando_reward r = {REWARD_NONE, 0};
    const int64_t v = id - AP_BASE;

    // Each range of ids is one kind of reward.
    if (v >= AP_SPECIAL_OFFSET && v < AP_SPECIAL_OFFSET + SPECIAL_COUNT) {
        r.kind = REWARD_SPECIAL; r.value = (uint8_t)(v-AP_SPECIAL_OFFSET);
    } else if (v >= 0 && v < RANDO_ITEM_COUNT) {
        r.kind = REWARD_ITEM;
        r.value = (uint8_t)v;
    } else if (v >= AP_PLANET_OFFSET && v < AP_PLANET_OFFSET + LEVEL_COUNT) {
        r.kind = REWARD_PLANET;
        r.value = (uint8_t)(v - AP_PLANET_OFFSET);
    } else if (v == AP_GOLD_BOLT_OFFSET) {
        r.kind = REWARD_GOLD_BOLT;
    }

    return r;
}

/* NPC-spawned infobots (ap_npc_infobots.h). */
#define AP_NPC_KIND 0x12
#define AP_NPC_COUNT 13
/* Bit positions are save-format identities: do not reorder. */
typedef struct ap_npc_infobot { int level, planet, oclass; const char* hook; } ap_npc_infobot;
static const ap_npc_infobot k_ap_npcs[AP_NPC_COUNT] = {
 {1,2,774,"Moby_ThePlumber_Update"},
 {1,3,730,"Moby_AgnoggShipFront_Update"},
 {4,6,1190,"Moby_Lieutenant_Update"},
 {6,5,1109,"Moby_Shuttle_Update"},
 {5,7,919,"Moby_QwarksBouncer_Update"},
 {7,8,436,"Moby_Class436_Update"},
 {8,10,1130,"Moby_Commando_Update"},
 {8,9,1144,"Moby_BataliaDeserter_Update"},
 {12,13,1267,"Moby_HovenTurret_Update"},
 {13,14,1353,"Moby_Class1353_Update"},
 {14,15,924,"Moby_Sam_Update"},
 {15,16,1446,"Moby_UltramechScientist_Update"},
 {15,17,1419,"Moby_Class1419_Update"}
};
static inline int64_t ap_npc_id(int index) {
 return AP_BASE + AP_LOCATION_OFFSET + ((int64_t)AP_NPC_KIND << 16)
     + ((int64_t)k_ap_npcs[index].level << 8) + k_ap_npcs[index].planet;
}
static inline int ap_id_is_npc(int64_t id) { return ((id-AP_BASE-AP_LOCATION_OFFSET)>>16)==AP_NPC_KIND; }
static inline int ap_npc_index(int level, int planet) {
 for (int i=0;i<AP_NPC_COUNT;++i) if(k_ap_npcs[i].level==level && k_ap_npcs[i].planet==planet)return i;
 return -1;
}

/* The id of a location of logic.c's table as a location of the multiworld: its kind, level and
 * key each in their own bits (the levels reach 0x12, past four bits, so the kind is above 16). */
static inline int64_t ap_location_id(const rando_location* l) {
    /* An NPC's coordinates (and the infobot it spawns) are the NPC's check. */
    if (l->kind == LOC_FIXED && l->vanilla.kind == REWARD_PLANET) {
        const int npc = ap_npc_index(l->level, l->vanilla.value);
        if (npc >= 0) return ap_npc_id(npc);
    }
    return AP_BASE + AP_LOCATION_OFFSET + ((int64_t)l->kind << 16) + ((int64_t)l->level << 8)
           + l->key;
}

/* A skill point's id (ap_skills.h). */
static inline int64_t ap_skill_location_id(int level, int flag) {
    return AP_BASE + AP_LOCATION_OFFSET + ((int64_t)AP_SKILL_KIND << 16) + ((int64_t)level << 8) + flag;
}

/* Whether a location id is a skill point's: only a room with the option has them. */
static inline int ap_id_is_skill(int64_t id) {
    const int64_t kind = id - AP_BASE - AP_LOCATION_OFFSET;
    return (kind >> 16) == AP_SKILL_KIND;
}

/* Whether a row of the table is a location of the multiworld: every row but the fixed ones (an
 * NPC's coordinates, the Persuader, Drek), which the APWorld has as events. */
static inline int ap_is_location(const rando_location* l) {
    return l->kind != LOC_FIXED;
}

#ifdef __cplusplus
}
#endif

#endif /* AP_IDS_H */
