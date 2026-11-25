/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
/* Compile the production hooks with a deterministic game-memory host. */
typedef uint32_t openrac_gaddr;
enum { LEVEL_KERWAN = 3, ITEM_HELI_PACK = 2, ITEM_SWINGSHOT = 12,
       OPENRAC_PLAY_PLAYING = 1, OPENRAC_PLAY_SCENE = 2, OPENRAC_PLAY_MENU = 3 };
static uint8_t memory[0x10000], inventory[37], collected[37];
#define OPENRAC_G(api, address) (memory + (address))
static struct { uint32_t moby_base; } disc = {0x100}, *s_disc = &disc;
static unsigned loads = 1;
static int level = 3, phase = 1, active = 1, buy, grant_swing, course_start;
static int npc_saw;
static uint8_t* owned(int item) { return inventory + item; }
static int current_level(void) { return level; }
static int play_phase(uint32_t* out) { if (out) *out = loads; return phase; }
static int kerwan_randomizer_active(void) { return active; }
static uint8_t* level_byte_at(uint32_t a) { return memory + a; }
static int32_t get32(const uint8_t* p) { int32_t n; memcpy(&n,p,4); return n; }
static void set32(uint8_t* p, int32_t n) { memcpy(p,&n,4); }
static int32_t i32_at(uint32_t a) { return get32(memory+a); }
static void native_helga(uint32_t m) {
    (void)m;
    npc_saw = inventory[ITEM_SWINGSHOT];
    if (course_start) set32(memory+0x4000+0x8C,1);
    if (buy) {
        set32(memory+0x4000+0x7C,0);
        set32(memory+0x4000+0x8C,0);
        set32(memory+0x4000+0xA0,99);
        collected[ITEM_SWINGSHOT] = 1;
    }
}
static void native_al(uint32_t m) {
    (void)m;
    npc_saw = inventory[ITEM_HELI_PACK];
    if (buy) { memory[0x1220] = 1; collected[ITEM_HELI_PACK] = 1; }
}
static void (*s_helga_update)(uint32_t) = native_helga;
static void (*s_big_al_update)(uint32_t) = native_al;
static void run_shown(void (*fn)(uint32_t), uint32_t moby, int item) {
    const uint8_t real = inventory[item];
    inventory[item] = collected[item];
    fn(moby);
    inventory[item] = real;
    if (grant_swing) inventory[ITEM_SWINGSHOT] = 1;
}
#include "../src/kerwan_access.inc"
static void fixture(void) {
    memset(memory,0,sizeof memory);
    memset(inventory,0,sizeof inventory);
    memset(collected,0,sizeof collected);
    ++loads; level=3; phase=1; active=1; buy=grant_swing=course_start=0;
    set32(memory+0x100,0x1000);
    set32(memory+0x1078,0x3000); /* Helga */
    set32(memory+0x1178,0x3200); /* Al */
    set32(memory+0x1378,0x4000); /* linked taxi */
    set32(memory+0x3000+0xD4,3);
    set32(memory+0x3000+0xD8,99);
    set32(memory+0x3200+0x144,2);
    const uint16_t door=0x3E5, taxi=1012;
    memcpy(memory+0x12A6,&door,2); memcpy(memory+0x13A6,&taxi,2);
    set32(memory+0x407C,1); set32(memory+0x408C,1); set32(memory+0x40A0,42);
}
int main(void) {
    for (int have=0; have<2; ++have) for (int purchased=0; purchased<2; ++purchased) {
        fixture(); inventory[ITEM_HELI_PACK]=(uint8_t)have; buy=purchased;
        big_al_update(0x1100);
        assert(memory[0x1220] == (have || purchased));
        assert(collected[ITEM_HELI_PACK] == purchased && npc_saw == 0);
        assert(inventory[ITEM_HELI_PACK] == have);
        fixture(); inventory[ITEM_SWINGSHOT]=(uint8_t)have; buy=purchased;
        helga_update(0x1000);
        assert(get32(memory+0x407C) == !have);
        assert(get32(memory+0x408C) == !have);
        assert(get32(memory+0x40A0) == (have ? 99 : 42));
        assert(collected[ITEM_SWINGSHOT] == purchased && npc_saw == 0);
        assert(inventory[ITEM_SWINGSHOT] == have);
    }
    /* Remote reward arrives after Helga has been deleted: the frame hook handles it. */
    fixture(); buy=1; helga_update(0x1000); big_al_update(0x1100);
    memory[0x1220]=0; inventory[ITEM_SWINGSHOT]=inventory[ITEM_HELI_PACK]=1;
    kerwan_access_tick();
    assert(get32(memory+0x407C)==0 && memory[0x1220]==1);
    /* Subsequent frames do not repeatedly reset a running taxi. */
    set32(memory+0x407C,1); kerwan_access_tick(); assert(get32(memory+0x407C)==1);
    /* Reloaded collected Helga still activates the course taxi without owning Swingshot. */
    fixture(); collected[ITEM_SWINGSHOT]=1; course_start=1;
    set32(memory+0x408C,0); helga_update(0x1000);
    assert(npc_saw==1 && get32(memory+0x408C)==1);
    /* The native dialogue can reset the taxi before its purchase completes. */
    fixture(); phase=OPENRAC_PLAY_SCENE; buy=1; helga_update(0x1000);
    assert(get32(memory+0x407C)==1 && get32(memory+0x40A0)==42);
    phase=OPENRAC_PLAY_PLAYING; helga_update(0x1000);
    assert(get32(memory+0x407C)==1);
    /* Receiving the actual Swingshot at Helga removes it immediately. */
    fixture(); buy=grant_swing=1; helga_update(0x1000); assert(get32(memory+0x407C)==0);
    /* Owning a remote Swingshot must survive Helga's later course/dialogue activation. */
    fixture(); inventory[ITEM_SWINGSHOT]=1; helga_update(0x1000);
    course_start=1; phase=OPENRAC_PLAY_SCENE; helga_update(0x1000);
    assert(get32(memory+0x407C)==0 && get32(memory+0x408C)==0);
    /* Cached links cannot write into the next level or another load of Kerwan. */
    fixture(); helga_update(0x1000); big_al_update(0x1100);
    inventory[ITEM_SWINGSHOT]=inventory[ITEM_HELI_PACK]=1; ++loads;
    kerwan_access_tick(); assert(memory[0x1220]==0 && get32(memory+0x407C)==1);
    fixture(); helga_update(0x1000); inventory[ITEM_SWINGSHOT]=1; level=4;
    kerwan_access_tick(); assert(get32(memory+0x407C)==1);
    fixture(); big_al_update(0x1100); inventory[ITEM_HELI_PACK]=1; active=0;
    kerwan_access_tick(); assert(memory[0x1220]==0);
    /* Absent/incorrect links are ignored, including legacy mock hosts with no pvars. */
    fixture(); set32(memory+0x1078,0); helga_update(0x1000);
    set32(memory+0x3200+0x144,-1); big_al_update(0x1100);
    puts("Kerwan door/taxi inventory and collection cases passed");
    return 0;
}
