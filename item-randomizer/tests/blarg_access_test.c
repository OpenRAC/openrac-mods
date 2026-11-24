#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t openrac_gaddr;
enum { LEVEL_BLARG = 6, ITEM_O2_MASK = 6, ITEM_GRINDBOOTS = 0x1D };
static uint8_t memory[0x8000], inventory[37];
static int level = LEVEL_BLARG, active = 1, calls, puzzle_completions, fred_calls, hooks;
static openrac_gaddr pool = 0x2000;
static int current_level(void) { return level; }
static int blarg_o2_access_active(void) { return active; }
static openrac_gaddr blarg_moby_pool(void) { return pool; }
static uint8_t* owned(int item) { return &inventory[item]; }
#define OPENRAC_G(api, address) (memory + (address))
static void native_update(openrac_gaddr moby) {
    ++calls;
    if (memory[moby + 0x20] == 0) memory[moby + 0x20] = 1;
    else if (memory[moby + 0x20] == 3) memory[moby + 0x20] = 0xFD;
    /* A real linked-puzzle completion would be counted in state 1. */
    else if (memory[moby + 0x20] == 1 && memory[0x10]) ++puzzle_completions;
}
static void native_fred_update(openrac_gaddr moby) { (void)moby; ++fred_calls; }
static int hook(void* mod, const char* name, const char* sig, void* replacement, void** original) {
    (void)mod; (void)replacement;
    assert(strcmp(sig, "void(u32)") == 0);
    if (strcmp(name, "rac1.moby.class_1035.update") == 0) *original = (void*)native_update;
    else if (strcmp(name, "rac1.moby.moby1105.update") == 0) *original = (void*)native_fred_update;
    else assert(!"unexpected hook");
    ++hooks;
    return 0;
}
static struct { int (*hook)(void*, const char*, const char*, void*, void**); } api = {hook}, *s_api = &api;
static void* s_mod;
static int take(int result, const char* point) { (void)point; return result == 0; }
#include "blarg_access.inc"

/* Fred at 0x1000 (pvars 0x1200), his shuttle door in pool slot 3 (pvars 0x3000), its seven
 * pieces from 0x4000. */
enum { FRED = 0x1000, FRED_VARS = 0x1200, DOOR = 0x2300, DOOR_VARS = 0x3000, PIECES = 0x4000 };
static void put32(openrac_gaddr a, uint32_t v) { memcpy(memory + a, &v, 4); }
static uint32_t get32_at(openrac_gaddr a) { uint32_t v; memcpy(&v, memory + a, 4); return v; }
static void reset_fred(void) {
    put32(FRED + 0x78, FRED_VARS);
    put32(FRED_VARS + 0x164, 3);
    memory[FRED + 0x20] = 5; /* bought: waiting for a grind */
    put32(DOOR + 0x78, DOOR_VARS);
    memory[DOOR + 0x20] = 2; /* closed */
    memory[DOOR + 0xBC] = 0;
    put32(DOOR_VARS + 0x14, 0x3F800000u);
    put32(DOOR_VARS + 0x18, 0x3F800000u);
    for (int i = 0; i < 7; ++i) {
        put32(DOOR_VARS + 0x24 + 4 * i, PIECES + 0x100 * i);
        memory[PIECES + 0x100 * i + 0x20] = 3;
    }
    inventory[ITEM_GRINDBOOTS] = 0;
    level = LEVEL_BLARG;
    active = 1;
    pool = 0x2000;
}
static int door_untouched(void) {
    if (memory[DOOR + 0x20] != 2 || memory[DOOR + 0xBC] != 0) return 0;
    for (int i = 0; i < 7; ++i)
        if (memory[PIECES + 0x100 * i + 0x20] != 3) return 0;
    return get32_at(DOOR_VARS + 0x14) == 0x3F800000u;
}

static void check_fred_door(void) {
    /* Not the Grindboots: the shuttle door opens as a grind would open it. */
    reset_fred();
    blarg_fred_update(FRED);
    assert(memory[DOOR + 0x20] == 4 && memory[DOOR + 0xBC] == 1);
    assert(get32_at(DOOR_VARS + 0x14) == 0 && get32_at(DOOR_VARS + 0x18) == 0);
    for (int i = 0; i < 7; ++i) assert(memory[PIECES + 0x100 * i + 0x20] == 5);
    assert(memory[FRED + 0x20] == 5 && inventory[ITEM_GRINDBOOTS] == 0);
    /* Once it is opening, later ticks leave it to the game. */
    memory[PIECES + 0x20] = 6;
    blarg_fred_update(FRED);
    assert(memory[DOOR + 0x20] == 4 && memory[PIECES + 0x20] == 6);

    /* Owned Grindboots keep the retail grind-to-open path. */
    reset_fred(); inventory[ITEM_GRINDBOOTS] = 1;
    blarg_fred_update(FRED); assert(door_untouched());
    /* Before the purchase, Fred's own dialogue drives the door. */
    reset_fred(); memory[FRED + 0x20] = 1;
    blarg_fred_update(FRED); assert(door_untouched());
    /* Another planet, or the mod off. */
    reset_fred(); level = 2; blarg_fred_update(FRED); assert(door_untouched());
    reset_fred(); active = 0; blarg_fred_update(FRED); assert(door_untouched());
    /* No moby pool, a bad slot or a missing piece: nothing is written. */
    reset_fred(); pool = 0; blarg_fred_update(FRED); assert(door_untouched());
    reset_fred(); put32(FRED_VARS + 0x164, 0xFFFFFFFFu); blarg_fred_update(FRED); assert(door_untouched());
    reset_fred(); put32(DOOR_VARS + 0x24 + 4 * 6, 0); blarg_fred_update(FRED);
    assert(memory[DOOR + 0x20] == 2 && memory[DOOR + 0xBC] == 0);
    /* A door that is not closed is left alone. */
    reset_fred(); memory[DOOR + 0x20] = 6; blarg_fred_update(FRED);
    assert(memory[DOOR + 0x20] == 6 && memory[PIECES + 0x20] == 3);
    assert(fred_calls == 10);
}

int main(void) {
    assert(blarg_access_hooks() && hooks == 2);
    uint32_t collision = 123;
    memcpy(memory + 0x94, &collision, 4);
    blarg_beam_update(0); assert(memory[0x20] == 1); /* no mask: normal Clank puzzle */
    inventory[ITEM_O2_MASK] = 1;
    blarg_beam_update(0); assert(memory[0x20] == 0xFD);
    memcpy(&collision, memory + 0x94, 4); assert(collision == 0);
    memory[0x20] = 0; blarg_beam_update(0); assert(memory[0x20] == 0xFD); /* revisit/load */
    memory[0x20] = 1; level = 2; blarg_beam_update(0); assert(memory[0x20] == 1);
    level = LEVEL_BLARG; active = 0; blarg_beam_update(0); assert(memory[0x20] == 1);
    active = 1; memory[0x20] = 2; blarg_beam_update(0); assert(memory[0x20] == 2);
    assert(calls == 6 && puzzle_completions == 0 && inventory[ITEM_O2_MASK] == 1);
    check_fred_door();
    puts("Blarg O2 Mask access opens beam gates without spending the puzzle or pickup;"
         " Fred's shuttle door opens when his reward was not the Grindboots.");
    return 0;
}
