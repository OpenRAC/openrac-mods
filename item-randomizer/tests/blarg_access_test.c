#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t openrac_gaddr;
enum { LEVEL_BLARG = 6, ITEM_O2_MASK = 6 };
static uint8_t memory[256], inventory[37];
static int level = LEVEL_BLARG, active = 1, calls, puzzle_completions;
static int current_level(void) { return level; }
static int blarg_o2_access_active(void) { return active; }
static uint8_t* owned(int item) { return &inventory[item]; }
#define OPENRAC_G(api, address) (memory + (address))
static void native_update(openrac_gaddr moby) {
    ++calls;
    if (memory[moby + 0x20] == 0) memory[moby + 0x20] = 1;
    else if (memory[moby + 0x20] == 3) memory[moby + 0x20] = 0xFD;
    /* A real linked-puzzle completion would be counted in state 1. */
    else if (memory[moby + 0x20] == 1 && memory[0x10]) ++puzzle_completions;
}
static int hook(void* mod, const char* name, const char* sig, void* replacement, void** original) {
    (void)mod; (void)replacement;
    assert(strcmp(name, "rac1.moby.class_1035.update") == 0);
    assert(strcmp(sig, "void(u32)") == 0);
    *original = (void*)native_update;
    return 0;
}
static struct { int (*hook)(void*, const char*, const char*, void*, void**); } api = {hook}, *s_api = &api;
static void* s_mod;
static int take(int result, const char* point) { (void)point; return result == 0; }
#include "blarg_access.inc"
int main(void) {
    assert(blarg_access_hooks());
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
    puts("Blarg O2 Mask access opens beam gates without spending the puzzle or pickup.");
    return 0;
}
