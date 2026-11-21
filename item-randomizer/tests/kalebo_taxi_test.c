#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t openrac_gaddr;
enum {LEVEL_KALEBO=16,ITEM_MAP_O_MATIC=33};
static int level=LEVEL_KALEBO,active=1,collected,calls,stop;
static uint8_t inventory[37],state,visible,before;
static int s_by_item[37];
static void *s_mod;
static int current_level(void){return level;}
static int kalebo_taxi_mode(void){return active;}
static uint8_t *owned(int item){return inventory+item;}
static void swap_push(uint8_t *at,int row){assert(row==7);before=*at;*at=collected;}
static void swap_pop(void){inventory[33]=before;}
static void native_update(openrac_gaddr moby){
    (void)moby;++calls;
    if(state==0){visible=inventory[33]||stop==2;state=visible?1:6;}
    else if(state==6&&inventory[33]){visible=1;state=1;}
}
static int hook(void* mod,const char* name,const char* sig,void* replacement,void** original){
    (void)mod;(void)replacement;assert(!strcmp(name,"rac1.moby.path_pickup.update"));
    assert(!strcmp(sig,"void(u32)"));*original=(void*)native_update;return 0;
}
static struct{int(*hook)(void*,const char*,const char*,void*,void**);}api={hook},*s_api=&api;
static int take(int result,const char* point){(void)point;return result==0;}
#include "kalebo_taxi.inc"
int main(void){
    s_by_item[33]=7;assert(kalebo_taxi_hooks());
    inventory[33]=1;kalebo_taxi_update(0);assert(state==6&&!visible&&inventory[33]==1);
    kalebo_taxi_update(0);assert(state==6&&!visible); /* mapper received elsewhere */
    inventory[33]=0;collected=1;kalebo_taxi_update(0);assert(state==1&&visible&&inventory[33]==0);
    state=0;visible=0;kalebo_taxi_update(0);assert(state==1&&visible); /* reload after check */
    state=4;kalebo_taxi_update(0);assert(state==4); /* native trip continues */
    state=0;visible=0;collected=0;stop=2;kalebo_taxi_update(0);assert(state==1&&visible);
    stop=0;active=0;inventory[33]=1;state=0;kalebo_taxi_update(0);assert(state==1&&visible);
    active=1;level=3;state=0;kalebo_taxi_update(0);assert(state==1&&visible);
    level=LEVEL_KALEBO;active=-1;state=0;visible=0;
    kalebo_taxi_update(0);assert(calls==8&&state==0&&!visible); /* wait for AP save judgement */
    active=1;kalebo_taxi_update(0);assert(state==6&&!visible&&inventory[33]==1);
    assert(calls==9);puts("Kalebo taxi follows the local help-desk reward, preserving inventory and native travel.");
    return 0;
}
