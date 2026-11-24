#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "openrac/openrac_mod.h"
typedef uint32_t openrac_gaddr;
enum {LEVEL_VELDIN=0,LEVEL_NOVALIS=1};
enum {SHIP=256, CLASS=512, CLASS_COLLISION=0xC011};
static uint8_t memory[1024];static uint32_t pointer=SHIP;
static int level=1,active=1,agnogg_calls;
static struct {const char* version;} disc={"pal"},*s_disc=&disc;
static int current_level(void){return level;}
static int return_ship_active(void){return active;}
static int start_target;static int random_start_level(void){return start_target;}
static int32_t get32(const void* p){int32_t v;memcpy(&v,p,4);return v;}
static uint8_t* byte_at(uint32_t address){assert(address==(!strcmp(disc.version,"pal")?0x13E130u:0x13E030u));return (uint8_t*)&pointer;}
#define OPENRAC_GREF(api,type,address) (*(type*)(memory+(address)))
static const openrac_api api={0},*s_api=&api;
#include "novalis_ship.inc"
#define FLAGS OPENRAC_GREF(0,uint16_t,SHIP+0x34)
#define COLLISION OPENRAC_GREF(0,uint32_t,SHIP+0x94)
/* agnogg_ship_front_update before its quest: deactivate_recorded_moby on the recorded ship. */
static void agnogg_update(uint32_t moby){assert(moby==0x700);++agnogg_calls;FLAGS|=3;COLLISION=0;}
static void reset(uint16_t flags,uint32_t collision){FLAGS=flags;COLLISION=collision;}
int main(void){
    OPENRAC_GREF(0,uint32_t,SHIP+0x24)=CLASS;OPENRAC_GREF(0,uint32_t,CLASS+0x10)=CLASS_COLLISION;
    for(int ntsc=0;ntsc<2;++ntsc){
        disc.version=ntsc?"ntsc":"pal";
        OPENRAC_GREF(0,int16_t,SHIP+0xA6)=531;
        /* Hidden since the level loaded: shown once playing, collision from its class. */
        reset(3,0);novalis_ship_tick(OPENRAC_PLAY_SCENE,1);assert(FLAGS==3&&COLLISION==0);
        novalis_ship_tick(OPENRAC_PLAY_PLAYING,1);assert(FLAGS==0&&COLLISION==CLASS_COLLISION);
        /* Agnogg hides it again every frame: undone right after its update. */
        int before=agnogg_calls;
        novalis_ship_around_agnogg(agnogg_update,0x700);assert(agnogg_calls==before+1);
        assert(FLAGS==0&&COLLISION==CLASS_COLLISION);
        /* Other flags stay, and the take-off's inactive bit is kept. */
        reset(0x41,CLASS_COLLISION);novalis_ship_around_agnogg(agnogg_update,0x700);assert(FLAGS==0x41&&COLLISION==CLASS_COLLISION);
        reset(1,CLASS_COLLISION);novalis_ship_around_agnogg(agnogg_update,0x700);assert(FLAGS==1&&COLLISION==CLASS_COLLISION);
        /* A ship already hidden before the update is left to the tick. */
        reset(3,0);novalis_ship_around_agnogg(agnogg_update,0x700);assert(FLAGS==3&&COLLISION==0);
        /* Off, elsewhere, or not a ship: the game's own update only. */
        active=0;reset(0,CLASS_COLLISION);novalis_ship_around_agnogg(agnogg_update,0x700);assert(FLAGS==3&&COLLISION==0);
        reset(3,0);novalis_ship_tick(OPENRAC_PLAY_PLAYING,1);assert(FLAGS==3);
        /* A random starting planet needs the ship too, with the shortcut off. */
        start_target=-1;novalis_ship_tick(OPENRAC_PLAY_PLAYING,1);assert(FLAGS==3);
        start_target=3;novalis_ship_tick(OPENRAC_PLAY_PLAYING,1);assert(FLAGS==0&&COLLISION==CLASS_COLLISION);
        novalis_ship_around_agnogg(agnogg_update,0x700);assert(FLAGS==0&&COLLISION==CLASS_COLLISION);
        start_target=0;active=1;
        level=3;reset(0,CLASS_COLLISION);novalis_ship_around_agnogg(agnogg_update,0x700);assert(FLAGS==3);
        novalis_ship_tick(OPENRAC_PLAY_PLAYING,1);assert(FLAGS==3);level=1;
        pointer=0x1FFFFF1;reset(3,0);novalis_ship_tick(OPENRAC_PLAY_PLAYING,1);assert(FLAGS==3);
        before=agnogg_calls;novalis_ship_around_agnogg(agnogg_update,0x700);assert(agnogg_calls==before+1);pointer=SHIP;
        OPENRAC_GREF(0,int16_t,SHIP+0xA6)=666;reset(3,0);novalis_ship_tick(OPENRAC_PLAY_PLAYING,1);assert(FLAGS==3);
        OPENRAC_GREF(0,int16_t,SHIP+0xA6)=533;OPENRAC_GREF(0,uint8_t,SHIP+0x20)=0xFE;novalis_ship_tick(OPENRAC_PLAY_PLAYING,1);assert(FLAGS==3);
        OPENRAC_GREF(0,uint8_t,SHIP+0x20)=0;
    }
    puts("Novalis ship stays drawn and solid through Agnogg's per-frame hide on PAL/NTSC, without the ship-record init.");return 0;
}
