/* SPDX-License-Identifier: GPL-3.0-or-later
 * Load the production DLL with the Items option enabled, using real reward placement. */
#define main legacy_main
#include "load_test.c"
#undef main
#include "menu_save_api.h"
#include "menu_items.h"
static uint32_t menu_data[3], menu_disk[3];
static openrac_frame_fn menu_on_restore;
static int native_source_id, menu_snapshots;
static uint8_t menu_saved_flags[6];
static uint32_t specials(void) { return !strcmp(D->version,"pal") ? 0x13D490u : 0x13D388u; }
static uint8_t* special(int i) { return s_memory+specials()+menu_native_index(i); }
static int set_data(openrac_mod* mod,const void* bytes,uint32_t n) {
    (void)mod; CHECK(n==sizeof menu_data); memcpy(menu_data,bytes,n); return OPENRAC_HOOK_OK;
}
static uint32_t get_data(openrac_mod* mod,void* bytes,uint32_t n) {
    (void)mod; if(n>=sizeof menu_data) memcpy(bytes,menu_data,sizeof menu_data); return sizeof menu_data;
}
static int watch_data(openrac_mod* mod,openrac_frame_fn fn,void* user) {
    (void)mod;(void)user; menu_on_restore=fn; return OPENRAC_HOOK_OK;
}
static int menu_setting(openrac_mod* mod,const char* key) {
    if(!strcmp(key,"shuffle_items")) return 1;
    return setting_bool(mod,key);
}
static void snapshot(uint32_t buffer) {
    (void)buffer;
    for(int i=0;i<5;++i) menu_saved_flags[i]=*special(i);
    menu_saved_flags[5]=s_memory[D->owned+ITEM_PERSUADER];
    memcpy(menu_disk,menu_data,sizeof menu_disk);
    ++menu_snapshots;
}
static void save_prepare_native(int32_t buffer,int32_t slot) {
    CHECK(buffer==0x1234 && slot==7);
    snapshot((uint32_t)buffer);
    ((void(*)(uint32_t))hook_named("rac1.save.serialize"))((uint32_t)buffer);
}
static void source_native(uint32_t moby) {
    (void)moby;
    if(native_source_id==3) {
        if(!*special(3)) *special(3)=1;
        else *special(4)=1;
        set32(s_memory+D->level+0x1C,5);
    } else if(native_source_id==4) {
        *special(1)=0; s_memory[D->owned+ITEM_PERSUADER]=1;
    } else *special(native_source_id)=1;
    ((void(*)(int32_t,int32_t))hook_named("rac1.save.prepare"))(0x1234,7);
}
static int items_hook(openrac_mod* mod,const char* name,const char* sig,void* fn,void** original) {
    const int result=host_hook(mod,name,sig,fn,original);
    if(result!=OPENRAC_HOOK_OK) return result;
    if(!strcmp(name,"rac1.save.prepare")) *original=(void*)save_prepare_native;
    if(!strcmp(name,"rac1.save.serialize")) *original=(void*)snapshot;
    if(strstr(name,"hoverboard_girl") || strstr(name,"miner.update") || strstr(name,"moby1428")
        || strstr(name,"nanotech_vendor") || strstr(name,"pokitaru_vendor")) *original=(void*)source_native;
    return result;
}
int main(int argc,char** argv) {
    CHECK(argc==3);
    s_memory=calloc(1,MEMORY_SIZE); CHECK(s_memory);
    D=&k_rando_discs[!strcmp(argv[2],"ntsc")];
    s_seed="menu-items-load"; s_keep_start=1;
    set_env("OPENRAC_MOD_DATA_DIR",!strcmp(argv[2],"pal") ? "menu-load-pal" : "menu-load-ntsc");
    void* lib=open_library(argv[1]); CHECK(lib);
    openrac_mod_load_fn load=(openrac_mod_load_fn)symbol(lib,OPENRAC_MOD_LOAD); CHECK(load);
    menu_save_api ext;
    memset(&ext,0,sizeof ext);
#if OPENRAC_MOD_API_MINOR < 16
    openrac_api* api=&ext.base;
#else
    openrac_api* api=&ext;
#endif
    *api=make_api("rac1",D->version);
    api->size=sizeof ext; api->api_minor=16; api->setting_bool=menu_setting; api->hook=items_hook;
    ext.save_data_set=set_data; ext.save_data_get=get_data; ext.on_save_data=watch_data;
    /* No gold cases; with save data Shuffle infobots also shuffles the NPC coordinates. */
    const rando_options options={s_seed,1,1,1,1,1,1,"standard",0,1,1};
    rando_settings settings; rando_settings_of(&options,NULL,0,&settings);
    rando_plan plan; CHECK(rando_solve(&settings,&plan)==0);
    /* 29, the gold cases' two and the thirteen NPC coordinates' (save data shuffles them). */
    CHECK(load(api,(openrac_mod*)1)==0 && s_hook_count==48 && menu_on_restore);
    CHECK(hook_named("rac1.moby.moby743.update") != NULL);
    CHECK(hook_named("rac1.moby.moby744.update") != NULL);
    const char* hooks[5]={"rac1.moby.hoverboard_girl.update","rac1.moby.miner.update",
        "rac1.moby.moby1428.update","rac1.moby.nanotech_vendor.update","rac1.moby.pokitaru_vendor.update"};
    for(int i=0;i<5;++i) {
        memset(s_memory+specials(),0,8); memset(s_memory+D->owned,0,RANDO_ITEM_COUNT);
        /* One save record per mod: an NPC check already in it must survive the Items check. */
        char code[RANDO_CODE_SIZE]; rando_encode(&settings,code);
        menu_data[0]=0x52465231u; menu_data[1]=rando_seed_tag(code); menu_data[2]=1u<<10;
        menu_on_restore(NULL);
        native_source_id=i; menu_snapshots=0;
        if(i==4) *special(1)=1;
        ((void(*)(uint32_t))hook_named(hooks[i]))(0x100000);
        const int index=i==4 ? 5 : i;
        /* The Items-menu checks share the mod's one save record, from bit 23. */
        CHECK((menu_data[2]>>23) & (1u<<index)); CHECK((menu_disk[2]>>23) & (1u<<index));
        CHECK(menu_data[2] & (1u<<10));
        CHECK(menu_snapshots==2); /* outer preparation and nested serialization */
        const rando_reward reward=plan.reward[menu_location_row(index)];
        const int expected=index==5 ? reward.kind==REWARD_ITEM && reward.value==ITEM_PERSUADER
            : reward.kind==REWARD_SPECIAL && reward.value==index;
        CHECK(menu_saved_flags[index]==expected);
        CHECK((index==5 ? s_memory[D->owned+ITEM_PERSUADER] : *special(index))==expected);
    }
    /* The enabled option must fail explicitly on an older host, without installing hooks. */
    s_hook_count=0; api->api_minor=14; s_failed[0]=0;
    CHECK(load(api,(openrac_mod*)1)!=0 && strstr(s_failed,"1.16") && s_hook_count==0);
    puts("Production Items-menu reward/save hooks passed");
    free(s_memory);
    return 0;
}
