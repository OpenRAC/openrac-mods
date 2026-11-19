/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rando.h"
#include "discs.h"
#include "menu_save_api.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static uint8_t memory[0x200000];
static const rando_disc* s_disc;
static openrac_mod* s_mod;
static menu_save_api host;
#if OPENRAC_MOD_API_MINOR < 16
static const openrac_api* s_api=&host.base;
#else
static const openrac_api* s_api=&host;
#endif
static int mode=1, ap, award=-1, calls, source, save_count;
static uint32_t saved[3], disk[3];
static uint8_t saved_flags[6];
static openrac_frame_fn restore_callback;
static int get32(const uint8_t* p) { int x; memcpy(&x,p,4); return x; }
static void set32(uint8_t* p,int x) { memcpy(p,&x,4); }
static uint8_t* byte_at(uint32_t address) { return memory+address; }
static uint8_t* owned(int item) { return memory+s_disc->owned+item; }
#include "menu_items_runtime.inc"
static int menu_mode(void) { return mode; }
static void menu_collected(int index) {
    CHECK(index>=0 && index<6);
    ++calls;
    if (!ap && award>=0) menu_give_special(award);
}
static int store(openrac_mod* mod,const void* data,uint32_t n) {
    (void)mod; CHECK(n==sizeof saved); memcpy(saved,data,n); return OPENRAC_HOOK_OK;
}
static uint32_t load_data(openrac_mod* mod,void* data,uint32_t n) {
    (void)mod; if(n>=sizeof disk) memcpy(data,disk,sizeof disk); return sizeof disk;
}
static int watch(openrac_mod* mod,openrac_frame_fn fn,void* user) {
    (void)mod; (void)user; restore_callback=fn; return OPENRAC_HOOK_OK;
}
static void autosave(void) {
    menu_suspend(); /* production's outer save guard */
    for(int i=0;i<5;++i) saved_flags[i]=*menu_owned(i);
    saved_flags[5]=*owned(ITEM_PERSUADER);
    CHECK(!s_menu_shown);
    /* Nested save calls must not re-show location flags. */
    menu_suspend();
    memcpy(disk,saved,sizeof disk);
    ++save_count;
    menu_resume();
}
static void native_source(uint32_t moby) {
    (void)moby;
    if(source==3) {
        if(!*menu_owned(3)) { *menu_owned(3)=1; set32(menu_health(),5); set32(menu_max_health(),5); }
        else if(!*menu_owned(4)) { *menu_owned(4)=1; set32(menu_health(),8); set32(menu_max_health(),8); }
    } else if(source==4) {
        CHECK(*menu_owned(1)); *menu_owned(1)=0; *owned(ITEM_PERSUADER)=1;
    } else {
        *menu_owned(source)=1;
    }
    autosave();
}
static void reset(const char* version) {
    memset(memory,0,sizeof memory); memset(saved,0,sizeof saved); memset(disk,0,sizeof disk);
    s_disc=&k_rando_discs[!strcmp(version,"ntsc")];
#if OPENRAC_MOD_API_MINOR < 16
    host.base.size=sizeof host; host.base.api_minor=16; host.base.memory=memory;
#else
    host.size=sizeof host; host.api_minor=16; host.memory=memory;
#endif
    host.save_data_set=store; host.save_data_get=load_data; host.on_save_data=watch;
    calls=save_count=0; award=-1; mode=1;
    CHECK(menu_init());
    for(int i=0;i<5;++i) s_menu_updates[i]=native_source;
    set32(menu_health(),4); set32(menu_max_health(),4);
}
int main(int argc,char** argv) {
    CHECK(argc==3); ap=!strcmp(argv[2],"ap");
    for(int i=0;i<5;++i) {
        reset(argv[1]); source=i;
        if(i==4) *menu_owned(1)=1;
        menu_run(i,0);
        const int check=i==4 ? 5 : i;
        CHECK(calls==1 && menu_had(check) && save_count==1);
        CHECK(!saved_flags[check]);
        if(i==3) CHECK(get32(menu_health())==4 && get32(menu_max_health())==4);
        /* Re-entering/restoring the source never implies actual ownership. */
        restore_callback(NULL); CHECK(menu_had(check));
        if(i!=3) { if(i==4) *menu_owned(1)=1; menu_run(i,0); CHECK(calls==1); }
    }
    reset(argv[1]); source=3; menu_run(3,0); menu_run(3,0);
    CHECK(menu_had(3) && menu_had(4) && calls==2);
    CHECK(get32(menu_max_health())==4); /* neither purchase leaks vanilla health */
    reset(argv[1]); source=0; *menu_owned(0)=1; menu_run(0,0);
    CHECK(menu_had(0) && *menu_owned(0)==1 && saved_flags[0]==1);
    reset(argv[1]); source=3;
    menu_give_special(4); CHECK(get32(menu_max_health())==8);
    menu_give_special(3); CHECK(get32(menu_max_health())==8);
    menu_run(3,0); CHECK(get32(menu_max_health())==8 && menu_had(3));
    reset(argv[1]); source=4; menu_give_special(1); menu_run(4,0);
    CHECK(!*menu_owned(1) && menu_special_held(1));
    menu_give_special(1); CHECK(!*menu_owned(1)); /* replay after consumed trade */
    restore_callback(NULL); menu_give_special(1); CHECK(!*menu_owned(1));
    reset(argv[1]); source=1; award=4; menu_run(1,0);
    if(ap) { CHECK(!*menu_owned(4)); menu_give_special(4); }
    CHECK(*menu_owned(4) && get32(menu_max_health())==8 && !*menu_owned(1));
    reset(argv[1]); source=0; mode=0; menu_run(0,0);
    CHECK(*menu_owned(0) && !menu_had(0) && calls==0); /* option off is retail */
    reset(argv[1]); source=4; mode=0; *menu_owned(1)=1; menu_run(4,0);
    menu_give_special(1); CHECK(!*menu_owned(1)); /* off-slot AP replay cannot refund a trade */
    reset(argv[1]); source=0; mode=-1; menu_run(0,0); CHECK(!save_count && !calls);
    puts("Items-menu native writes, nested saves, replay, ownership and option cases passed");
    return 0;
}
