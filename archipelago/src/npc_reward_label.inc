/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "npc_reward_text.h"
static int npc_reward_label(const npc_reward_source* source, char* out, size_t size) {
    if (s_from_title || s_judge_wait || ap_client_new_game_plus() < 0) {
        return 0;
    }
    const int row = npc_reward_row(source);
    if (row >= 0 && had(row)) {
        return 0;
    }
    if (source->kind == REWARD_PLANET && ap_client_npc_infobots() == 1) {
        const int index = ap_npc_index(source->level, source->value);
        if (index >= 0) {
            if (npc_had(index)) {
                return 0;
            }
            if (!ap_client_location_hint(ap_npc_id(index), out, size)) {
                snprintf(out, size, "Unscouted reward");
            }
            return 1;
        }
    }
    if (row >= 0 && ap_is_location(&rando_locations[row])) {
        if (!ap_client_location_hint(ap_location_id(&rando_locations[row]), out, size)) {
            snprintf(out, size, "Unscouted reward");
        }
        return 1;
    }
    if (source->fixed) {
        snprintf(out, size, "%s", source->fixed);
        return 1;
    }
    if ((source->kind == REWARD_ITEM && *owned(source->value))
        || (source->kind == REWARD_PLANET && *planet_flag(source->value))) {
        return 0;
    }
    if (source->kind == REWARD_ITEM) {
        snprintf(out, size, "%s", rando_item_name(source->value));
    } else {
        snprintf(out, size, "%s Coordinates", rando_planet_name(source->value));
    }
    return 1;
}
