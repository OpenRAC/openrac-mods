#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t openrac_gaddr;
enum { LEVEL_ARIDIA = 2 };
static uint8_t memory[256];
static int level = LEVEL_ARIDIA, collected, calls;
static int current_level(void) { return level; }
static int aridia_reward_collected(void) { return collected; }
#define OPENRAC_G(api, address) (memory + (address))
static void native_update(openrac_gaddr moby) {
    ++calls;
    if (!memory[moby + 0x20]) {
        memory[moby + 0x30] = 42; /* retail home-position initialization */
        memory[moby + 0x20] = 1;
    }
}
static int hook(void* mod, const char* name, const char* sig, void* replacement, void** original) {
    (void)mod; (void)name; (void)sig; (void)replacement;
    *original = (void*)native_update;
    return 0;
}
static struct { int (*hook)(void*, const char*, const char*, void*, void**); } api = {hook}, *s_api = &api;
static void* s_mod;
static int take(int result, const char* point) { (void)point; return result == 0; }
#include "aridia_access.inc"
int main(void) {
    assert(aridia_access_hooks());
    for (int panel = 0; panel < 2; ++panel) {
        void (*update)(openrac_gaddr) = panel ? aridia_door2_update : aridia_door1_update;
        uint8_t* state = memory + 0x20;
        collected = 0; level = LEVEL_ARIDIA; *state = 0;
        update(0);
        assert(*state == 1 && memory[0x30] == 42);
        collected = 1;
        update(0); assert(*state == 2);
        *state = 3; update(0); assert(*state == 3);
        *state = 0; update(0); assert(*state == 2); /* return/load */
        level = 1; *state = 1; update(0); assert(*state == 1);
        level = LEVEL_ARIDIA; collected = 0; update(0); assert(*state == 1);
    }
    assert(calls == 12);
    puts("Aridia exit panels follow collection and preserve retail initialization.");
    return 0;
}
