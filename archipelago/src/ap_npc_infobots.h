/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AP_NPC_INFOBOTS_H
#define AP_NPC_INFOBOTS_H
#include "ap_ids.h"
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
#endif
