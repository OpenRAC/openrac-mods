#include "openrac/openrac_mod.h"
#include "rando.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t memory[0x2000000];
static openrac_api api;
static const openrac_api* s_api=&api;
static openrac_mod* s_mod=(openrac_mod*)1;
static struct { const char* version; } disc={"ntsc"}, *s_disc=&disc;
static int level, target, unlocked, travels, result;
static uint8_t* byte_at(uint32_t a) { return memory+a; }
static uint32_t get32(const void* p) { uint32_t v; memcpy(&v,p,4); return v; }
static int current_level(void) { return level; }
static int random_start_level(void) { return target; }
static void s_planet_unlock(int p) { unlocked=p; }
static void log_line(int l,const char* s) { (void)l;(void)s; }
static int travel(openrac_mod* mod,int p) { assert(mod==s_mod && p==target); ++travels; return result; }
#include "random_start.inc"
int main(void) {
    api.memory=memory; api.play_travel=travel;
    result=OPENRAC_HOOK_OK; target=LEVEL_KERWAN; level=LEVEL_VELDIN;
    random_start_tick(OPENRAC_PLAY_LOADING,1); assert(!travels);
    random_start_tick(OPENRAC_PLAY_SCENE,1); assert(travels==1 && unlocked==target);
    random_start_tick(OPENRAC_PLAY_PLAYING,1); assert(travels==1);
    level=LEVEL_EUDORA; random_start_tick(OPENRAC_PLAY_PLAYING,2); assert(travels==1);
    level=LEVEL_VELDIN; target=-1; random_start_tick(OPENRAC_PLAY_PLAYING,2); assert(travels==1);
    target=0; random_start_tick(OPENRAC_PLAY_PLAYING,2); assert(travels==1);
    target=LEVEL_BLARG; result=OPENRAC_HOOK_TAKEN;
    random_start_tick(OPENRAC_PLAY_PLAYING,2); assert(travels==2);
    result=OPENRAC_HOOK_OK; random_start_tick(OPENRAC_PLAY_PLAYING,2); assert(travels==3);
    return 0;
}
