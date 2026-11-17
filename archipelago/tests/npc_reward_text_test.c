/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>

#include "ap_npc_infobots.h"
#include "npc_reward_text.h"
static int s_from_title, s_judge_wait, active = 0, mode = 1, scouted = 1, checked, npc_checked;
static uint8_t inventory[37], planets[20];
static int64_t last_location;

static int ap_client_new_game_plus(void) {
    return active;
}

static int ap_client_npc_infobots(void) {
    return mode;
}

static int npc_had(int index) {
    (void)index;
    return npc_checked;
}

static int had(int row) {
    (void)row;
    return checked;
}

static uint8_t* owned(int item) {
    return inventory + item;
}

static uint8_t* planet_flag(int planet) {
    return planets + planet;
}

static int ap_client_location_hint(int64_t location, char* out, size_t size) {
    last_location = location;
    if (!scouted) {
        return 0;
    }
    snprintf(out, size, "Other Game Item for Player %%s");
    return 1;
}

#include "../src/npc_reward_label.inc"

int main(void) {
    char text[192];
    const npc_reward_source* al = npc_reward_source_for(3, 909);
    assert(npc_reward_label(al, text, sizeof text));
    assert(strcmp(text, "Other Game Item for Player %s") == 0);
    assert(last_location == ap_location_id(&rando_locations[npc_reward_row(al)]));
    inventory[ITEM_HELI_PACK] = 1;
    assert(npc_reward_label(al, text, sizeof text)); /* ownership does not collect source */
    checked = 1;
    assert(!npc_reward_label(al, text, sizeof text));
    checked = 0;
    scouted = 0;
    assert(npc_reward_label(al, text, sizeof text));
    assert(strcmp(text, "Unscouted reward") == 0);
    scouted = 1;
    const npc_reward_source* plumber = npc_reward_source_for(1, 774);
    planets[2] = 1;
    assert(npc_reward_label(plumber, text, sizeof text));
    assert(last_location == ap_npc_id(ap_npc_index(1, 2)));
    npc_checked = 1;
    assert(!npc_reward_label(plumber, text, sizeof text));
    npc_checked = 0;
    mode = 0;
    planets[2] = 0;
    assert(npc_reward_label(plumber, text, sizeof text));
    assert(strcmp(text, "Aridia Coordinates") == 0);
    planets[2] = 1;
    assert(!npc_reward_label(plumber, text, sizeof text));
    active = -1;
    assert(!npc_reward_label(al, text, sizeof text));
    active = 0;
    s_from_title = 1;
    assert(!npc_reward_label(al, text, sizeof text));
    s_from_title = 0;
    s_judge_wait = 1;
    assert(!npc_reward_label(al, text, sizeof text));
    s_judge_wait = 0;
    puts("AP NPC scout, recipient, collection and vanilla-coordinate cases passed");
    return 0;
}
