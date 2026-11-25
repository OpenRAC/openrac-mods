/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>

#include "npc_reward_text.h"
static rando_plan s_plan;
static int active = 1, checked;
static uint8_t inventory[RANDO_ITEM_COUNT], planets[RANDO_PLANET_COUNT], won;

static struct {
    const char* version;
} disc = {"pal"}, *s_disc = &disc;

static int rando_on(void) {
    return active;
}

static int had(int row) {
    return s_plan.shuffled[row] ? checked : won;
}

static uint8_t* owned(int item) {
    return inventory + item;
}

static uint8_t* planet_flag(int planet) {
    return planets + planet;
}

static uint8_t* byte_at(uint32_t address) {
    assert(address == npc_race_native_flag(s_disc->version));
    return &won;
}

#include "../src/npc_reward_label.inc"

int main(void) {
    char label[192];
    const npc_reward_source* starter = npc_reward_source_for(LEVEL_RILGAR, 918);
    assert(starter && !npc_reward_source_for(LEVEL_RILGAR, 717));
    assert(npc_reward_label(starter, label, sizeof label));
    assert(strcmp(label, "Platinum Zoomerator") == 0);
    won = 1;
    assert(!npc_reward_label(starter, label, sizeof label));
    s_disc->version = "ntsc";
    assert(!npc_reward_label(starter, label, sizeof label));
    won = 0;
    assert(npc_reward_label(starter, label, sizeof label));
    const int row = npc_reward_row(starter);
    if (row >= 0) {
        s_plan.shuffled[row] = 1;
        s_plan.reward[row] = (rando_reward){REWARD_ITEM, ITEM_SWINGSHOT};
        won = 1;
        inventory[ITEM_SWINGSHOT] = 1;
        checked = 0;
        assert(npc_reward_label(starter, label, sizeof label));
        assert(strcmp(label, "Swingshot") == 0); /* inventory never consumes the source */
        checked = 1;
        won = 0;
        assert(!npc_reward_label(starter, label, sizeof label));
        checked = 0;
        s_plan.reward[row] = (rando_reward){REWARD_GOLD_BOLT, 0};
        assert(npc_reward_label(starter, label, sizeof label));
        assert(strstr(label, "gold bolt"));
    }
    active = 0;
    assert(!npc_reward_label(starter, label, sizeof label));
    puts("Race reward placement and saved first-win cases passed");
    return 0;
}
