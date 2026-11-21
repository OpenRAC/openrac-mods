#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t openrac_gaddr;
enum { LEVEL_BLARG=6, LEVEL_ARIDIA=2, ITEM_HYDRODISPLACER=22, ITEM_TRESPASSER=26, MOBY_STATE=32 };
static uint8_t memory[512], inventory[37];
static int s_by_item[37], level=LEVEL_BLARG, active=1, collected[2], rider, calls;
static const struct { int level; } rando_locations[]={{LEVEL_BLARG},{LEVEL_ARIDIA}};
static const struct { unsigned hero_clank; } disc={300}, *s_disc=&disc;
static void *s_mod;
static int current_level(void){return level;}
static int kerwan_randomizer_active(void){return active;}
static int had(int row){return collected[row];}
static uint8_t *owned(int item){return inventory+item;}
static uint8_t *byte_at(unsigned p){return memory+p;}
#define OPENRAC_GREF(api,type,address) (*(type*)(memory+(address)))
static struct {uint8_t *at;uint8_t before;} swaps[4];
static int depth;
static void swap_push(uint8_t *at,int row){swaps[depth].at=at;swaps[depth++].before=*at;*at=row==-2?1:had(row);}
static void swap_pop(void){--depth;*swaps[depth].at=swaps[depth].before;}
static void native_bridge(openrac_gaddr p){++calls;if(memory[p+32]==1&&inventory[22]&&memory[300])memory[p+32]=3;}
static void native_elevator(openrac_gaddr p){++calls;if(memory[p+32]==1&&(rider||inventory[26]))memory[p+32]=2;}
static int hook(void* mod,const char* name,const char* sig,void* replacement,void** original){
    (void)mod;(void)replacement;assert(!strcmp(sig,"void(u32)"));
    *original=!strcmp(name,"rac1.moby.drawbridge.update")?(void*)native_bridge:(void*)native_elevator;return 0;
}
static struct {int(*hook)(void*,const char*,const char*,void*,void**);} api={hook},*s_api=&api;
static int take(int result,const char* point){(void)point;return result==0;}
#include "pickup_platforms.inc"
int main(void){
    memset(s_by_item,-1,sizeof s_by_item);s_by_item[22]=0;s_by_item[26]=1;
    assert(pickup_platform_hooks());
    inventory[22]=1;memory[300]=1;memory[32]=1;
    blarg_bridge_update(0);assert(memory[32]==1&&inventory[22]==1); /* remote Hydrodisplacer */
    inventory[22]=0;collected[0]=1;
    blarg_bridge_update(0);assert(memory[32]==3&&inventory[22]==0&&memory[300]==1);
    memory[32]=1;memory[300]=0;
    blarg_bridge_update(0);assert(memory[32]==3&&memory[300]==0); /* early O2 Ratchet */
    memory[32]=6;blarg_bridge_update(0);assert(memory[32]==6); /* native completed state */
    level=LEVEL_ARIDIA;memory[32]=1;inventory[26]=1;
    aridia_elevator_update(0);assert(memory[32]==1&&inventory[26]==1);
    inventory[26]=0;rider=1;
    aridia_elevator_update(0);assert(memory[32]==2&&inventory[26]==0); /* rider still works */
    rider=0;memory[32]=1;collected[1]=1;
    aridia_elevator_update(0);assert(memory[32]==2&&inventory[26]==0);
    active=0;memory[32]=1;collected[1]=0;inventory[26]=1;
    aridia_elevator_update(0);assert(memory[32]==2); /* foreign save stays native */
    assert(depth==0&&calls==8);
    puts("Blarg bridge and Aridia elevator follow local pickup checks, preserving inventory and rider activation.");
    return 0;
}
