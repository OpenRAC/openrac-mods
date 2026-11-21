#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "openrac/openrac_mod.h"
enum { LEVEL_ORXON = 10, TRIGGER_SLOTS = 16, ORXON_CLANK_MISSION = 2,
       TRIGGER_DONE = 255, ITEM_O2_MASK = 6 };
static uint8_t memory[400], inventory[37];
static const struct { uint32_t triggers, hero_clank; } disc = {0, 350}, *s_disc = &disc;
static int level = LEVEL_ORXON, travels, refuse;
static openrac_mod *s_mod;
static int current_level(void) { return level; }
static uint8_t *byte_at(uint32_t p) { return memory + p; }
static uint8_t *owned(int item) { return inventory + item; }
static void log_line(int severity, const char *text) { (void)severity; (void)text; }
static int travel(openrac_mod *mod, int destination) {
    (void)mod; assert(destination == LEVEL_ORXON); ++travels;
    return refuse ? OPENRAC_HOOK_UNKNOWN : OPENRAC_HOOK_OK;
}
static const openrac_api api = {.play_travel = travel}, *s_api = &api;
#include "orxon_access.inc"
static void tick(int phase, uint32_t load) { orxon_access_tick(phase, load, 1); }
int main(void) {
    uint8_t *done = memory + LEVEL_ORXON * TRIGGER_SLOTS + ORXON_CLANK_MISSION;
    uint8_t *clank = memory + disc.hero_clank;
    inventory[ITEM_O2_MASK] = 1;
    *done = 255; tick(OPENRAC_PLAY_PLAYING, 1); assert(travels == 0); /* completed save */
    *done = 0; *clank = 1; tick(OPENRAC_PLAY_PLAYING, 1);
    *done = 255; tick(OPENRAC_PLAY_SCENE, 1); assert(travels == 0);
    *clank = 0; tick(OPENRAC_PLAY_SCENE, 1); tick(OPENRAC_PLAY_MENU, 1); assert(travels == 0);
    tick(OPENRAC_PLAY_PLAYING, 1); assert(travels == 1);
    tick(OPENRAC_PLAY_PLAYING, 1); tick(OPENRAC_PLAY_LOADING, 2);
    tick(OPENRAC_PLAY_PLAYING, 3); assert(travels == 1); /* no repeat */
    *done = 0; *clank = 1; tick(OPENRAC_PLAY_PLAYING, 3);
    inventory[ITEM_O2_MASK] = 0; *done = 255; *clank = 0;
    tick(OPENRAC_PLAY_PLAYING, 3); assert(travels == 1); /* no Ratchet access */
    inventory[ITEM_O2_MASK] = 1; refuse = 1;
    tick(OPENRAC_PLAY_PLAYING, 3); assert(travels == 2);
    refuse = 0; tick(OPENRAC_PLAY_PLAYING, 3); assert(travels == 3); /* retry */
    *done = 0; *clank = 1; tick(OPENRAC_PLAY_PLAYING, 3);
    level = 6; tick(OPENRAC_PLAY_PLAYING, 3);
    level = LEVEL_ORXON; *done = 255; *clank = 0;
    tick(OPENRAC_PLAY_PLAYING, 3); assert(travels == 3); /* other planet resets */
    *done = 0; *clank = 1; tick(OPENRAC_PLAY_PLAYING, 3);
    orxon_access_tick(OPENRAC_PLAY_PLAYING, 3, 0);
    *done = 255; *clank = 0; tick(OPENRAC_PLAY_PLAYING, 3); assert(travels == 3);
    *done = 0; *clank = 1; tick(OPENRAC_PLAY_PLAYING, 3);
    *done = 255; *clank = 0; tick(OPENRAC_PLAY_PLAYING, 4); assert(travels == 3);
    puts("Orxon reload waits for Clank completion and runs once through normal travel.");
    return 0;
}
