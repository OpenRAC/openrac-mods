#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "openrac/openrac_mod.h"
typedef uint32_t openrac_gaddr;
enum {LEVEL_NOVALIS=1};
static uint8_t memory[1024];static uint32_t pointer=256;
static int level=1,active=1,calls;static uint8_t quest_check,kerwan_coordinates;
static struct {const char* version;} disc={"pal"},*s_disc=&disc;
static int current_level(void){return level;}
static int return_ship_active(void){return active;}
static int32_t get32(const void* p){int32_t v;memcpy(&v,p,4);return v;}
static uint8_t* byte_at(uint32_t address){assert(address==(!strcmp(disc.version,"pal")?0x13E130u:0x13E030u));return (uint8_t*)&pointer;}
#define OPENRAC_GREF(api,type,address) (*(type*)(memory+(address)))
static void native_initialize(uint32_t ship){++calls;assert(ship==256);OPENRAC_GREF(0,uint16_t,ship+0x34)&=(uint16_t)~3;}
static void* function(const char* name){assert(!strcmp(name,!strcmp(disc.version,"pal")?"func_L00_0028FB78":"FUN_L00_0028e8a0"));return (void*)native_initialize;}
static const openrac_api api={.function=function},*s_api=&api;
#include "novalis_ship.inc"
int main(void){
    for(int ntsc=0;ntsc<2;++ntsc){
        disc.version=ntsc?"ntsc":"pal";novalis_ship_init();
        OPENRAC_GREF(0,int16_t,256+0xA6)=531;OPENRAC_GREF(0,uint16_t,256+0x34)=3;
        int before=calls;novalis_ship_tick(OPENRAC_PLAY_SCENE,1);assert(calls==before);
        novalis_ship_tick(OPENRAC_PLAY_PLAYING,1);assert(calls==before+1);
        assert(!(OPENRAC_GREF(0,uint16_t,256+0x34)&3)&&!quest_check&&!kerwan_coordinates);
        novalis_ship_tick(OPENRAC_PLAY_PLAYING,1);assert(calls==before+1);
        OPENRAC_GREF(0,uint16_t,256+0x34)=1;novalis_ship_tick(OPENRAC_PLAY_PLAYING,1);assert(calls==before+2);
        novalis_ship_tick(OPENRAC_PLAY_PLAYING,2);assert(calls==before+3);
        active=0;OPENRAC_GREF(0,uint16_t,256+0x34)=3;novalis_ship_tick(OPENRAC_PLAY_PLAYING,2);assert(calls==before+3);active=1;
        level=3;novalis_ship_tick(OPENRAC_PLAY_PLAYING,2);assert(calls==before+3);level=1;
        pointer=0x1FFFFF1;novalis_ship_tick(OPENRAC_PLAY_PLAYING,2);assert(calls==before+3);pointer=256;
        OPENRAC_GREF(0,int16_t,256+0xA6)=666;novalis_ship_tick(OPENRAC_PLAY_PLAYING,2);assert(calls==before+3);
    }
    puts("Novalis ship activates independently of quest collection and Kerwan coordinates on PAL/NTSC.");return 0;
}
