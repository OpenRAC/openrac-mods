/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NPC_REWARD_TEXT_H
#define NPC_REWARD_TEXT_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rando.h"

/* Classes and reward identities from RAC1's PAL/NTSC NPC updates. Fixed coordinates
 * are included: the item randomizer may leave them vanilla while AP shuffles them. */
typedef struct npc_reward_source {
    int level, cls, kind, value;
    const char* fixed;
} npc_reward_source;

static const npc_reward_source npc_reward_sources[] =
    {{1, 774, REWARD_PLANET, 2, 0},
     {1, 730, REWARD_PLANET, 3, 0},
     {2, 788, REWARD_ITEM, ITEM_HOVERBOARD, 0},
     {2, 786, REWARD_ITEM, ITEM_SONIC_SUMMONER, 0},
     {3, 909, REWARD_ITEM, ITEM_HELI_PACK, 0},
     {3, 890, REWARD_ITEM, ITEM_SWINGSHOT, 0},
     {4, 1190, REWARD_PLANET, 6, 0},
     {6, 1109, REWARD_PLANET, 5, 0},
     {6, 1105, REWARD_ITEM, ITEM_GRINDBOOTS, 0},
     {5, 919, REWARD_PLANET, 7, 0},
     {5, 925, REWARD_ITEM, ITEM_RYNO, 0},
     {5, 918, REWARD_NONE, 0, "Platinum Zoomerator"},
     {7, 436, REWARD_PLANET, 8, 0},
     {8, 1130, REWARD_PLANET, 10, 0},
     {8, 1144, REWARD_PLANET, 9, 0},
     {8, 1283, REWARD_ITEM, ITEM_METAL_DETECTOR, 0},
     {10, 282, REWARD_NONE, 0, "Raritanium"},
     {11, 90, REWARD_ITEM, ITEM_THRUSTER_PACK, 0},
     {11, 114, REWARD_ITEM, ITEM_O2_MASK, 0},
     {11, 298, REWARD_ITEM, ITEM_PERSUADER, 0},
     {12, 328, REWARD_ITEM, ITEM_HYDRO_PACK, 0},
     {12, 1267, REWARD_PLANET, 13, 0},
     {13, 1353, REWARD_PLANET, 14, 0},
     {14, 851, REWARD_ITEM, ITEM_PDA, 0},
     {14, 924, REWARD_PLANET, 15, 0},
     {15, 1446, REWARD_PLANET, 16, 0},
     {15, 1419, REWARD_PLANET, 17, 0},
     {16, 1455, REWARD_ITEM, ITEM_HOLOGUISE, 0},
     {16, 1377, REWARD_ITEM, ITEM_MAP_O_MATIC, 0}};

static const npc_reward_source* npc_reward_source_for(int level, int cls) {
    for (size_t i = 0; i < sizeof npc_reward_sources / sizeof *npc_reward_sources; ++i) {
        if (npc_reward_sources[i].level == level && npc_reward_sources[i].cls == cls) {
            return &npc_reward_sources[i];
        }
    }
    return NULL;
}

/* The Items pool (PR #202) supplies this row when integrated. Matching its
 * location identity avoids confusing the starter with Skid or inventory ownership. */
static int npc_race_row(void) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (rando_locations[i].level == LEVEL_RILGAR
            && strcmp(rando_locations[i].name, "Rilgar: the Hoverboard race") == 0) {
            return (int)i;
        }
    }
    return -1;
}

static uint32_t npc_race_native_flag(const char* version) {
    return strcmp(version, "pal") == 0 ? 0x13D490u : 0x13D388u;
}
static int npc_reward_row(const npc_reward_source* source) {
    if (source->level == LEVEL_RILGAR && source->cls == 918) {
        return npc_race_row();
    }
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* row = &rando_locations[i];
        if (row->level == source->level && row->vanilla.kind == source->kind
            && row->vanilla.value == source->value && row->kind != LOC_VENDOR
            && row->kind != LOC_INFOBOT && row->kind != LOC_START) {
            return (int)i;
        }
    }
    return -1;
}

/* Retail formats the prompt into its small panel. Escape external item/player
 * names without changing the native button, price or format arguments. */
static int npc_named_prompt(char out[120], const char* prefix, const char* reward) {
    const size_t n = strlen(prefix);
    if (!n || n + 1 >= 120 || !reward[0]) {
        return 0;
    }
    memcpy(out, prefix, n);
    size_t at = n;
    for (size_t i = 0; reward[i]; ++i) {
        const unsigned char ch = (unsigned char)reward[i];
        if (ch < 32 || ch > 126) {
            continue;
        }
        const size_t bytes = ch == '%' ? 2 : 1;
        if (at + bytes >= 120) {
            break;
        }
        if (ch == '%') {
            out[at++] = '%';
        }
        out[at++] = (char)ch;
    }
    out[at] = 0;
    return at > n;
}

static int npc_reward_prompt(char out[120], const char* original, const char* reward) {
    const size_t n = strlen(original);
    if (!n || n + 12 >= 120) {
        return 0;
    }
    char prefix[120];
    snprintf(prefix, sizeof prefix, "%s - Reward: ", original);
    return npc_named_prompt(out, prefix, reward);
}

/* Only the native race entry, not the missing-Hoverboard message. */
static int npc_race_prompt(char out[120], const char* original, const char* reward) {
    if ((unsigned char)original[0] != 0x12 || strcmp(original + 1, " Enter race") != 0) {
        return 0;
    }
    return npc_named_prompt(out, "\x12 Enter race to win ", reward);
}

/* Only the bribe action; the affordability warning keeps its native text. */
static int npc_bouncer_prompt(char out[120], const char* original, const char* reward) {
    if ((unsigned char)original[0] != 0x12
        || strcmp(original + 1, " Bribe Bouncer with 4,000 bolts") != 0) {
        return 0;
    }
    return npc_named_prompt(out, "\x12 Bribe Bouncer with 4,000 bolts to get ", reward);
}
#endif
