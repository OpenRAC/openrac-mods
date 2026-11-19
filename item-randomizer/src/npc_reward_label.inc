/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "npc_reward_text.h"
static int npc_reward_label(const npc_reward_source* source, char* out, size_t size) {
    if (!rando_on()) {
        return 0;
    }
    const int row = npc_reward_row(source);
    if (row >= 0 && had(row)) {
        return 0;
    }
    /* Without the optional race pool, the retail first-win ownership flag is
     * authoritative. With that pool, had(row) above reads its separate save check. */
    if (source->level == LEVEL_RILGAR && source->cls == 918 && row < 0
        && *byte_at(npc_race_native_flag(s_disc->version))) {
        return 0;
    }
    if (row >= 0 && s_plan.shuffled[row]) {
        rando_reward_name(s_plan.reward[row], out, size);
        return 1;
    }
    if (source->fixed) {
        snprintf(out, size, "%s", source->fixed);
        return 1;
    }
    rando_reward reward = {(uint8_t)source->kind, (uint8_t)source->value};
    if ((source->kind == REWARD_ITEM && *owned(source->value))
        || (source->kind == REWARD_PLANET && *planet_flag(source->value))) {
        return 0;
    }
    rando_reward_name(reward, out, size);
    return 1;
}
