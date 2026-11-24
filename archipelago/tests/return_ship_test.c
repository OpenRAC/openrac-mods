#include <assert.h>
#include <stdio.h>
#include "openrac/openrac_mod.h"
#include "ship_shortcut.h"
static int active=1,phase=OPENRAC_PLAY_PLAYING,level=3,health=4,travels,registered;
static uint32_t loads=1;
static uint8_t clank;
static const struct {unsigned hero_clank;} disc={0},*s_disc=&disc;
static openrac_mod *s_mod;
static openrac_pad_fn pad_callback;
static const char *setting="L1+R1+L2+R2+Start";
static int current_level(void){return level;}
static int playing_level(void){return level>=0&&level<=18;}
static int return_ship_active(void){return active;}
static int play_phase(uint32_t *out){if(out)*out=loads;return phase;}
static uint8_t *byte_at(unsigned p){(void)p;return &clank;}
static void log_line(int severity,const char *text){(void)severity;(void)text;}
static const char *get_setting(openrac_mod *mod,const char *key){(void)mod;assert(!strcmp(key,"return_to_ship_buttons"));return setting;}
static int on_pad(openrac_mod *mod,openrac_pad_fn fn,void *user){(void)mod;(void)user;pad_callback=fn;++registered;return 0;}
static int hero_pose(openrac_pose *p){p->health=health;return 0;}
static int travel(openrac_mod *mod,int destination){(void)mod;assert(destination==level);++travels;return 0;}
static const openrac_api api={.setting_string=get_setting,.on_pad=on_pad,.hero_pose=hero_pose,.play_travel=travel},*s_api=&api;
#include "return_ship.inc"
static uint16_t send(int port,uint16_t down){
    const uint16_t raw=(uint16_t)~down;uint8_t report[18]={(uint8_t)raw,(uint8_t)(raw>>8),128,128,128,128};
    pad_callback(NULL,port,report,18);return (uint16_t)~(report[0]|((uint16_t)report[1]<<8));
}
int main(void){
    ship_shortcut parsed={0};
    assert(ship_shortcut_parse(&parsed,NULL)&&parsed.held==0xF00&&parsed.trigger==8);
    assert(ship_shortcut_parse(&parsed," l1 + r1 + Triangle ")&&parsed.held==0xC00&&parsed.trigger==4096);
    assert(!ship_shortcut_parse(&parsed,"Start"));
    assert(!ship_shortcut_parse(&parsed,"L1+L1+Start"));
    assert(!ship_shortcut_parse(&parsed,"L1++Start"));
    assert(!ship_shortcut_parse(&parsed,"L1+Bogus"));
    return_ship_init();assert(registered==1&&pad_callback);
    assert(send(0,8)&8);return_ship_tick(phase,loads);assert(travels==0); /* normal pause */
    send(0,0);send(0,0xF00);assert(!(send(0,0xF08)&8));
    return_ship_tick(phase,loads);assert(travels==1);
    assert(!(send(0,0xF08)&8));return_ship_tick(phase,loads);assert(travels==1);
    send(0,0);assert(send(0,8)&8); /* suppression ends on release */
    send(0,0);assert(send(0,0xF08)&8);return_ship_tick(phase,loads);assert(travels==1); /* hold first */
    send(0,0);send(1,0xF00);assert(send(1,0xF08)&8);assert(travels==1);
    const int blocked[]={OPENRAC_PLAY_MENU,OPENRAC_PLAY_SCENE,OPENRAC_PLAY_LOADING,OPENRAC_PLAY_FRONT_END};
    for(unsigned i=0;i<sizeof blocked/sizeof blocked[0];++i){phase=blocked[i];send(0,0);send(0,0xF00);assert(send(0,0xF08)&8);return_ship_tick(phase,loads);}
    phase=OPENRAC_PLAY_PLAYING;clank=1;send(0,0);send(0,0xF00);assert(send(0,0xF08)&8);
    clank=0;health=0;send(0,0);send(0,0xF00);assert(send(0,0xF08)&8);
    health=4;send(0,0);send(0,0xF00);send(0,0xF08);++loads;return_ship_tick(phase,loads);assert(travels==1);
    send(0,0);send(0,0xF00);send(0,0xF08);health=0;
    return_ship_tick(phase,loads);assert(travels==1);health=4;
    setting="L3+Select";return_ship_init();send(0,2);assert(!(send(0,3)&1));return_ship_tick(phase,loads);assert(travels==2);
    setting="Off";return_ship_init();send(0,0xF00);assert(send(0,0xF08)&8);return_ship_tick(phase,loads);assert(travels==2);
    setting="L1+Oops";return_ship_init();send(0,0xF00);assert(send(0,0xF08)&8);
    puts("Configurable return shortcut: sequencing, pause suppression, gameplay guards and one-shot travel passed.");return 0;
}
