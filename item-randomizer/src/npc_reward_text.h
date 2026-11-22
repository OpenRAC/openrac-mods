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
     {12, 282, REWARD_NONE, 0, "Raritanium"},
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

/* RAC1's HUD newline is byte 1. Wrap at word boundaries without splitting a
 * format conversion or changing prices/buttons; colour controls take no width. */
static void npc_reward_wrap(char* text) {
    size_t start = 0, last_space = (size_t)-1;
    unsigned columns = 0;
    for (size_t i = 0; text[i]; ++i) {
        const unsigned char ch = (unsigned char)text[i];
        if (ch == 1) {
            start = i + 1;
            columns = 0;
            last_space = (size_t)-1;
            continue;
        }
        if (ch == ' ') {
            last_space = i;
        }
        if (ch >= 32 || ch == 0x12) {
            ++columns;
        }
        if (columns > 38 && last_space != (size_t)-1 && last_space >= start) {
            text[last_space] = 1;
            start = last_space + 1;
            columns = 0;
            for (size_t j = start; j <= i; ++j) {
                if ((unsigned char)text[j] >= 32) {
                    ++columns;
                }
            }
            last_space = (size_t)-1;
        }
    }
}

/* Retail formats the prompt into its small panel. Escape external item/player
 * names without changing the native button, price or format arguments. */
static int npc_label_between(
    char out[120], const char* prefix, const char* reward, const char* suffix
) {
    const size_t n = strlen(prefix), tail = strlen(suffix);
    if (n + tail + 1 >= 120 || !reward[0]) {
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
        if (at + bytes + tail >= 120) {
            break;
        }
        if (ch == '%') {
            out[at++] = '%';
        }
        out[at++] = (char)ch;
    }
    if (at == n) {
        return 0;
    }
    memcpy(out + at, suffix, tail + 1);
    return 1;
}

static int npc_named_prompt(char out[120], const char* prefix, const char* reward) {
    return npc_label_between(out, prefix, reward, "");
}

/* Replace only the purchased reward. Native price/trade costs, button glyphs,
 * colour codes and format arguments remain the game's own. */
static int npc_reward_prompt(char out[120], const char* original, const char* reward) {
    const char* buy = strstr(original, "Buy ");
    const char* cost = buy ? strstr(buy + 4, " for ") : NULL;
    const char* name = NULL;
    char prefix[120], suffix[120];
    if (cost) {
        name = buy + 4;
        size_t n = (size_t)(name - original);
        if ((unsigned char)*name == 0x0C) {
            ++n;
        }
        if (n >= sizeof prefix || strlen(cost) + 2 >= sizeof suffix) {
            return 0;
        }
        memcpy(prefix, original, n);
        prefix[n] = 0;
        snprintf(
            suffix,
            sizeof suffix,
            "%s%s",
            cost > name && (unsigned char)cost[-1] == 8 ? "\x08" : "",
            cost
        );
        return npc_label_between(out, prefix, reward, suffix);
    }
    /* Pay <price> for <reward>, Trade <cost> for <reward>, and the matching
     * affordability text all put the reward last. Never replace the trade cost. */
    if (strstr(original, "Pay ") || strstr(original, "Trade ") || strstr(original, "You need ")) {
        const char* sep = strstr(original, " for ");
        if (!sep) {
            sep = strstr(original, " to buy ");
        }
        if (sep) {
            name = sep + (strncmp(sep, " for ", 5) == 0 ? 5 : 8);
            size_t n = (size_t)(name - original);
            if ((unsigned char)*name == 0x0C) {
                ++n;
            }
            if (n >= sizeof prefix) {
                return 0;
            }
            memcpy(prefix, original, n);
            prefix[n] = 0;
            const size_t length = strlen(original);
            return npc_label_between(
                out,
                prefix,
                reward,
                length && (unsigned char)original[length - 1] == 8 ? "\x08" : ""
            );
        }
    }
    const size_t n = strlen(original);
    if (!n || n + 12 >= 120) {
        return 0;
    }
    snprintf(prefix, sizeof prefix, "%s - Reward: ", original);
    return npc_named_prompt(out, prefix, reward);
}

/* Orxon's machine uses help messages, not an NPC dialog request. These ids
 * identify the offered location even when the player already owns an upgrade. */
static int npc_nanotech_special(int id) {
    switch (id) {
        case 0x271E:
        case 0x2720:
            return SPECIAL_PREMIUM_NANOTECH;
        case 0x271F:
        case 0x2721:
            return SPECIAL_ULTRA_NANOTECH;
        default:
            return -1;
    }
}

static int npc_nanotech_prompt(char out[120], const char* text, const char* reward, int special) {
    const char* names[2] =
        {special == SPECIAL_PREMIUM_NANOTECH ? "Premium Nanotech" : "Ultra Nanotech",
         special == SPECIAL_PREMIUM_NANOTECH ? "Nanotech Premium" : "Nanotech Ultra"};
    for (int variant = 0; variant < 2; ++variant) {
        const char* name = names[variant];
        const size_t length = strlen(name);
        for (const char* at = text; *at; ++at) {
            size_t i = 0;
            for (; i < length && at[i]; ++i) {
                unsigned char a = (unsigned char)at[i], b = (unsigned char)name[i];
                if (a == 1) {
                    a = ' '; /* retail HUD line break */
                }
                if (a >= 'A' && a <= 'Z') {
                    a += 'a' - 'A';
                }
                if (b >= 'A' && b <= 'Z') {
                    b += 'a' - 'A';
                }
                if (a != b) {
                    break;
                }
            }
            if (i == length) {
                const size_t prefix_length = (size_t)(at - text);
                char prefix[120];
                if (prefix_length >= sizeof prefix) {
                    return 0;
                }
                memcpy(prefix, text, prefix_length);
                prefix[prefix_length] = 0;
                return npc_label_between(out, prefix, reward, at + length);
            }
        }
    }
    return npc_reward_prompt(out, text, reward);
}

/* Only the native race entry, not the missing-Hoverboard message. */
static int npc_race_prompt(char out[120], const char* original, const char* reward) {
    if ((unsigned char)original[0] != 0x12 || strcmp(original + 1, " Enter race") != 0) {
        return 0;
    }
    return npc_named_prompt(out, "\x12 Enter race - Reward: ", reward);
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
