/* SPDX-License-Identifier: GPL-3.0-or-later
 * Retail special-item writes are location checks; inventory remains independent.
 * Collection metadata uses the host's save-scoped mod data (interface 1.16).
 */
#include "menu_items.h"
#include "menu_save_api.h"
static int menu_mode(void);
static void menu_collected(int index);
static const menu_save_api* s_menu_save;
static uint32_t s_menu_checks, s_menu_consumed;
static int s_menu_running = -1, s_menu_shown, s_menu_count;
static uint8_t* s_menu_at[2];
static uint8_t s_menu_real[2];
static int s_menu_index[2];
static int32_t s_menu_hp, s_menu_max;
static uint32_t s_menu_load;

static uint8_t* menu_owned(int special) {
    const uint32_t base = strcmp(s_disc->version,"pal")==0 ? 0x13D490u : 0x13D388u;
    return byte_at(base + (uint32_t)menu_native_index(special));
}
static uint8_t* menu_health(void) {
    return byte_at((strcmp(s_disc->version,"pal")==0 ? 0x13F450u : 0x13F350u) + 0x22A8u);
}
static uint8_t* menu_max_health(void) { return byte_at(s_disc->level + 0x1Cu); }
static int menu_special_held(int special) {
    return *menu_owned(special) || (special==SPECIAL_RARITANIUM && (s_menu_consumed & 1u));
}
#ifdef MENU_ITEMS_EXTERNAL_STATE
/* A mod has one save record (save_data_set replaces it): a mod that keeps other checks there
 * stores these with them, through its own menu_store and menu_restore. */
static void menu_store(void);
static void menu_restore(void* user);
#else
static void menu_store(void) {
    if (s_menu_save) {
        const uint32_t data[3] = {0x4D495431u, s_menu_checks, s_menu_consumed};
        s_menu_save->save_data_set(s_mod,data,sizeof data);
    }
}
static void menu_restore(void* user) {
    (void)user;
    uint32_t data[3] = {0};
    s_menu_checks = s_menu_consumed = 0;
    if (s_menu_save && s_menu_save->save_data_get(s_mod,data,sizeof data)==sizeof data
        && data[0]==0x4D495431u && !(data[1] & ~0x3Fu) && !(data[2] & ~1u)) {
        s_menu_checks=data[1]; s_menu_consumed=data[2];
    }
}
#endif
/* The host installs a loaded save's data before game updates, then notifies listeners
 * after that tick. Refresh before giving/checking rewards on the first update too. */
static void menu_refresh(void) {
    openrac_play_status status;
    memset(&status,0,sizeof status); status.size=sizeof status;
    if (s_api->play_status && s_api->play_status(&status)==OPENRAC_HOOK_OK
        && status.loads!=s_menu_load) {
        s_menu_load=status.loads; menu_restore(NULL);
    }
}
static int menu_init(void) {
    s_menu_save=menu_save_host(s_api);
    s_menu_running=-1; s_menu_shown=0; s_menu_load=0;
    menu_restore(NULL);
    return !s_menu_save || s_menu_save->on_save_data(s_mod,menu_restore,NULL)==OPENRAC_HOOK_OK;
}
static void menu_give_special(int special) {
    if (special<0 || special>=SPECIAL_COUNT) return;
    if (special==SPECIAL_RARITANIUM && menu_mode()==0 && *owned(ITEM_PERSUADER)
        && !*menu_owned(SPECIAL_RARITANIUM)) {
        /* In vanilla/off slots Persuader can only come from consuming this Raritanium.
         * This also handles an old save without a mod-data sidecar. */
        s_menu_consumed |= 1u; menu_store();
    }
    if (menu_special_held(special)) return;
    *menu_owned(special)=1;
    if (special==SPECIAL_PREMIUM_NANOTECH || special==SPECIAL_ULTRA_NANOTECH) {
        /* Ultra first remains eight HP; a later Premium never downgrades it. */
        const int maximum=*menu_owned(SPECIAL_ULTRA_NANOTECH) ? 8 : 5;
        set32(menu_max_health(),maximum);
        set32(menu_health(),maximum);
    }
}
static int menu_had(int index) { return (s_menu_checks >> index) & 1u; }
static void menu_suspend(void) {
    if (!s_menu_shown) return;
    s_menu_shown=0;
    uint32_t fresh=0;
    for (int i=0;i<s_menu_count;++i) {
        if (*s_menu_at[i] && !menu_had(s_menu_index[i])) fresh |= 1u << s_menu_index[i];
        *s_menu_at[i]=s_menu_real[i];
    }
    if (s_menu_running==3) {
        /* A randomized purchase must not retain retail's five/eight-HP side effect. */
        set32(menu_health(),s_menu_hp); set32(menu_max_health(),s_menu_max);
    }
    if (s_menu_running==5 && (fresh & (1u<<5))) {
        /* Retail consumes Raritanium. Do not replay it from a checked source on load. */
        s_menu_consumed |= 1u;
    }
    s_menu_checks |= fresh;
    if (fresh) menu_store();
    for (int i=0;i<MENU_ITEM_CHECKS;++i) if (fresh & (1u<<i)) menu_collected(i);
}
static void menu_resume(void) {
    if (s_menu_running<0 || s_menu_shown) return;
    for (int i=0;i<s_menu_count;++i) {
        s_menu_real[i]=*s_menu_at[i];
        *s_menu_at[i]=(uint8_t)menu_had(s_menu_index[i]);
    }
    s_menu_hp=get32(menu_health()); s_menu_max=get32(menu_max_health());
    s_menu_shown=1;
}
static void (*s_menu_updates[5])(uint32_t);
static void menu_run(int source,uint32_t moby) {
    menu_refresh();
    const int mode=menu_mode();
    if (mode<0) return; /* unjudged AP slot */
    if (!mode) {
        const int had_raritanium=*menu_owned(SPECIAL_RARITANIUM)!=0;
        s_menu_updates[source](moby);
        if (source==4 && had_raritanium && !*menu_owned(SPECIAL_RARITANIUM)) {
            s_menu_consumed |= 1u; menu_store();
        }
        return;
    }
    if (!s_menu_save) return; /* the option requires interface 1.16 save-scoped data */
    s_menu_running=source==4 ? 5 : source;
    s_menu_count=source==3 ? 2 : 1;
    for (int i=0;i<s_menu_count;++i) {
        const int index=s_menu_running+i;
        s_menu_index[i]=index;
        s_menu_at[i]=index==5 ? owned(ITEM_PERSUADER) : menu_owned(index);
    }
    menu_resume();
    s_menu_updates[source](moby);
    menu_suspend();
    s_menu_running=-1;
}
static void menu_zoom_update(uint32_t moby) { menu_run(0,moby); }
static void menu_raritanium_update(uint32_t moby) { menu_run(1,moby); }
static void menu_codebot_update(uint32_t moby) { menu_run(2,moby); }
static void menu_nanotech_update(uint32_t moby) { menu_run(3,moby); }
static void menu_persuader_update(uint32_t moby) { menu_run(4,moby); }
static int menu_hooks(void) {
    static const char* names[5]={"rac1.moby.hoverboard_girl.update","rac1.moby.miner.update",
        "rac1.moby.moby1428.update","rac1.moby.nanotech_vendor.update","rac1.moby.pokitaru_vendor.update"};
    static void (*const replacements[5])(uint32_t)={menu_zoom_update,menu_raritanium_update,
        menu_codebot_update,menu_nanotech_update,menu_persuader_update};
    for (int i=0;i<5;++i)
        if (s_api->hook(s_mod,names[i],"void(u32)",(void*)replacements[i],
            (void**)&s_menu_updates[i])!=OPENRAC_HOOK_OK) {
            s_api->fail(s_mod,names[i]); return 0;
        }
    return 1;
}
