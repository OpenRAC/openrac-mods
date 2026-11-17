/* Production integration against a fake host; no preview/editor controls. */
#define main legacy_load_test_main
#include "load_test.c"
#undef main
#include "reward_api.h"
#include "reward_pools.h"

static uint32_t data_words[3], data_size;
static openrac_frame_fn data_changed;
static int modern_gold = 0, animation_kind = -1, animation_item, animation_calls, animation_busy;
static int npc_purchases, case_purchases, source_demos, serialized, case_base = -1;
static uint8_t real_owned, real_gold;
static float hero_position[3] = {240, 130, 54.46f}, place_target[3], hero_yaw = 1.25f, place_yaw;
static int place_pending, places;
static openrac_moby_visual visual_rows[512];
static int visual_count;
static char detail_name[192], prompt[256];
static int selected_native;

static uint8_t* gold_flag(int base) {
    return s_memory + (strcmp(D->version, "pal") == 0 ? 0x13E620u : 0x13E520u) + base;
}

static uint32_t global_mode(void) {
    return strcmp(D->version, "pal") == 0 ? 0x15F6A8u : 0x15F5A8u;
}

static uint32_t pool_word(void) {
    return strcmp(D->version, "pal") == 0 ? 0x160098u : 0x15FFD8u;
}

static int modern_setting(openrac_mod* mod, const char* key) {
    if (strcmp(key, "reward_models") == 0 || strcmp(key, "reward_text") == 0) {
        return 1;
    }
    if (strcmp(key, "shuffle_gold_weapons") == 0) {
        return modern_gold;
    }
    if (strcmp(key, "spoiler_log") == 0 || strcmp(key, "vendor_hints") == 0) {
        return 0;
    }
    return setting_bool(mod, key);
}

static int data_set(openrac_mod* mod, const void* bytes, uint32_t n) {
    (void)mod;
    CHECK(n == 12);
    memcpy(data_words, bytes, n);
    data_size = n;
    return OPENRAC_HOOK_OK;
}

static uint32_t data_get(openrac_mod* mod, void* out, uint32_t n) {
    (void)mod;
    if (out && n >= data_size) {
        memcpy(out, data_words, data_size);
    }
    return data_size;
}

static int data_watch(openrac_mod* mod, openrac_frame_fn fn, void* user) {
    (void)mod;
    (void)user;
    data_changed = fn;
    return OPENRAC_HOOK_OK;
}

static int visuals(openrac_mod* mod, const openrac_moby_visual* rows, int n) {
    (void)mod;
    CHECK(n <= 512);
    memcpy(visual_rows, rows, n * sizeof *rows);
    visual_count = n;
    return OPENRAC_HOOK_OK;
}

static openrac_gaddr guest_text(openrac_mod* mod, const char* text) {
    (void)mod;
    snprintf((char*)s_memory + 0x1F8000, 1024, "%s", text);
    return 0x1F8000;
}

static uint32_t tint(openrac_mod* mod, uint32_t rgb) {
    (void)mod;
    (void)rgb;
    return UINT32_MAX;
}

static int pickup(openrac_mod* mod, int kind, int item) {
    (void)mod;
    if (kind < 0) {
        return animation_busy;
    }
    animation_kind = kind;
    animation_item = item;
    ++animation_calls;
    return kind ? 1 : 0;
}

static int pose(openrac_pose* p) {
    p->level = get32(s_memory + D->level);
    p->health = 3;
    memcpy(p->position, hero_position, 12);
    p->rotation[2] = hero_yaw;
    return OPENRAC_HOOK_OK;
}

static int place(openrac_mod* mod, const float* p, float yaw, uint32_t flags) {
    (void)mod;
    (void)flags;
    memcpy(place_target, p, 12);
    place_yaw = yaw;
    place_pending = 1;
    ++places;
    return OPENRAC_HOOK_OK;
}

static void apply_place(void) {
    if (place_pending) {
        memcpy(hero_position, place_target, 12);
        hero_yaw = place_yaw;
        place_pending = 0;
    }
}

static int no_mobys(openrac_moby* rows, int n) {
    (void)rows;
    (void)n;
    return 0;
}

static void original_initialize(void) {
    uint8_t* menu = s_memory + D->vendor_menu;
    selected_native =
        get32(menu + VENDOR_ENTRIES + get32(menu + VENDOR_CURSOR) * VENDOR_ENTRY_SIZE);
    set32(menu + 0x20, 0x1A0000);
    for (int i = 1; i < 4; ++i) {
        int16_t cls = (int16_t)(0x500 + selected_native);
        memcpy(s_memory + 0x1A0000 + i * 0x100 + 0xA6, &cls, 2);
    }
}

static uint32_t original_text(int id) {
    (void)id;
    strcpy((char*)s_memory + 0x1F7000, "Buy Infobot for 500 bolts");
    return 0x1F7000;
}

static int original_dialog(uint32_t moby, uint32_t request) {
    (void)moby;
    (void)request;
    uint32_t p = ((uint32_t (*)(int))hook_named("rac1.text.msg_string"))(999);
    snprintf(prompt, sizeof prompt, "%s", s_memory + p);
    return 1;
}

static void original_details(void) {
    uint8_t* menu = s_memory + D->vendor_menu;
    int item = get32(menu + VENDOR_ENTRIES + get32(menu + VENDOR_CURSOR) * VENDOR_ENTRY_SIZE);
    int id = get32(s_memory + (strcmp(D->version, "pal") == 0 ? 0x1864D0 : 0x1863D0) + item * 0x4C);
    uint32_t p = ((uint32_t (*)(int))hook_named("rac1.text.msg_string"))(id);
    snprintf(detail_name, sizeof detail_name, "%s", s_memory + p);
}

static int original_icon(int icon, int frame) {
    return icon * 10 + frame;
}

static void no_args(void) {}

static void no_sprite(int a, int b, int c, int d, int e, int f) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
}

static void original_demo(uint32_t moby, int item) {
    (void)moby;
    (void)item;
    ++source_demos;
}

static void original_serialize(uint32_t buffer) {
    (void)buffer;
    ++serialized;
    if (case_base >= 0) {
        CHECK(s_memory[D->owned + case_base] == real_owned);
        CHECK(*gold_flag(case_base) == real_gold);
        CHECK(data_words[2] & (1u << rando_gold_index(case_base)));
    }
}

static void original_npc(uint32_t moby) {
    int16_t cls;
    memcpy(&cls, s_memory + moby + 0xA6, 2);
    int index = -1;
    for (int i = 0; i < 13; ++i) {
        if (k_rando_npcs[i].oclass == cls && k_rando_npcs[i].level == get32(s_memory + D->level)) {
            index = i;
        }
    }
    CHECK(index >= 0);
    int planet = k_rando_npcs[index].planet;
    if (!s_memory[D->planets + planet]) {
        ++npc_purchases;
        ((void (*)(int))hook_named(OPENRAC_RAC1_HOOK_Planet_Unlock))(planet);
        ((void (*)(uint32_t))hook_named("rac1.save.serialize"))(0x1F0000);
    }
}

static void original_case(uint32_t moby) {
    int16_t cls;
    memcpy(&cls, s_memory + moby + 0xA6, 2);
    if (cls != 304) {
        return;
    }
    uint32_t vars = (uint32_t)get32(s_memory + moby + 0x78);
    int slot = get32(s_memory + vars + 0x40);
    int base = get32(s_memory + (strcmp(D->version, "pal") == 0 ? 0x1DEDE0 : 0x1DEB10) + slot * 4);
    CHECK(s_memory[D->owned + base] == 1);
    if (*gold_flag(base)) {
        return;
    }
    CHECK(((int (*)(void))hook_named("rac1.gold_bolt.count_unspent"))() >= 4);
    ++case_purchases;
    *gold_flag(base) = 1;
    ((void (*)(uint32_t, int))hook_named("GoldCase_StartDemo"))(moby, base);
    ((void (*)(uint32_t))hook_named("rac1.save.serialize"))(0x1F0000);
}

static int modern_hook(
    openrac_mod* mod, const char* name, const char* sig, void* replacement, void** original
) {
    int result = host_hook(mod, name, sig, replacement, original);
    if (result != OPENRAC_HOOK_OK) {
        return result;
    }
    for (int i = 0; i < 13; ++i) {
        if (strcmp(name, k_rando_npcs[i].hook) == 0) {
            *original = (void*)original_npc;
        }
    }
    if (strcmp(name, "Moby_GoldWeaponCase_Update") == 0) {
        *original = (void*)original_case;
    }
    if (strcmp(name, "GoldCase_StartDemo") == 0) {
        *original = (void*)original_demo;
    }
    if (strcmp(name, "rac1.save.serialize") == 0) {
        *original = (void*)original_serialize;
    }
    if (strcmp(name, "rac1.gold_bolt.count_unspent") == 0) {
        *original = (void*)game_count;
    }
    if (strcmp(name, "rac1.vendor.initialize_selected") == 0) {
        *original = (void*)original_initialize;
    }
    if (strcmp(name, "rac1.vendor.draw_icons") == 0) {
        *original = (void*)no_args;
    }
    if (strcmp(name, "rac1.vendor.draw_details") == 0) {
        *original = (void*)original_details;
    }
    if (strcmp(name, "rac1.ui.sprite.draw") == 0) {
        *original = (void*)no_sprite;
    }
    if (strcmp(name, "rac1.ui.icon_frame") == 0) {
        *original = (void*)original_icon;
    }
    if (strcmp(name, "rac1.npc.try_dialog") == 0) {
        *original = (void*)original_dialog;
    }
    if (strcmp(name, "rac1.text.msg_string") == 0) {
        *original = (void*)original_text;
    }
    return result;
}

static int reward_kind(rando_reward r, int* item) {
    *item = 0;
    if (r.kind == REWARD_GOLD_BOLT) {
        return 1;
    }
    if (r.kind == REWARD_PLANET) {
        *item = r.value;
        return 3;
    }
    if (r.kind == REWARD_GOLD_WEAPON) {
        *item = r.value;
        return 2;
    }
    if (r.kind == REWARD_ITEM && r.value >= ITEM_SUCK_CANNON && r.value <= ITEM_DECOY_GLOVE
        && r.value != ITEM_SWINGSHOT && r.value != ITEM_HYDRODISPLACER) {
        *item = r.value;
        return 2;
    }
    return 0;
}

static void finish_sequence(int kind, int item, int before, int centered) {
    for (int i = 0; i < 8 && animation_calls == before; ++i) {
        s_frame(NULL);
        apply_place();
    }
    CHECK(animation_calls == before + 1 && animation_kind == kind && animation_item == item);
    if (kind && centered) {
        int at = places;
        animation_busy = 1;
        s_frame(NULL);
        CHECK(places == at);
        animation_busy = 0;
    }
    for (int i = 0; i < 3; ++i) {
        s_frame(NULL);
        apply_place();
    }
}

int main(int argc, char** argv) {
    CHECK(argc > 1);
    s_memory = (uint8_t*)calloc(1, MEMORY_SIZE);
    CHECK(s_memory);
    D = &k_rando_discs[argc > 2 && strcmp(argv[2], "ntsc") == 0 ? 1 : 0];
    s_seed = "reward-integration";
    s_keep_start = 1;
    char dir[100];
    snprintf(dir, sizeof dir, "item-reward-integration-%s", D->version);
    set_env("OPENRAC_MOD_DATA_DIR", dir);
    void* lib = open_library(argv[1]);
    CHECK(lib);
    openrac_mod_load_fn load = (openrac_mod_load_fn)symbol(lib, OPENRAC_MOD_LOAD);
    CHECK(load);
    reward_visual_api ext;
    memset(&ext, 0, sizeof ext);
    openrac_api* api = reward_visual_base(&ext);
    *api = make_api("rac1", D->version);
    api->size = sizeof ext;
    api->api_minor = 19;
    api->setting_bool = modern_setting;
    api->hook = modern_hook;
    api->hero_pose = pose;
    api->hero_place = place;
    api->moby_list = no_mobys;
    reward_save_api* save = (reward_save_api*)api;
    save->save_data_set = data_set;
    save->save_data_get = data_get;
    save->on_save_data = data_watch;
    ext.moby_visuals = visuals;
    ext.guest_text = guest_text;
    ext.sprite_tint = tint;
    ext.pickup_animation = pickup;
    set32(s_memory + D->level, LEVEL_NOVALIS);
    memset(s_memory + D->vendor_stock, 0xFF, VENDOR_STOCK_SIZE);
    CHECK(load(api, (openrac_mod*)1) == 0);
    CHECK(s_failed[0] == 0 && data_changed);
    rando_options options = {s_seed, 1, 1, 1, 1, 1, 1, "standard", 1};
    rando_settings settings;
    rando_plan plan;
    rando_settings_of(&options, NULL, 1, &settings);
    CHECK(rando_solve(&settings, &plan) == 0);
    char code[RANDO_CODE_SIZE];
    rando_encode(&settings, code);
    set_tag_word(rando_seed_tag(code));
    /* Every coordinate can already be owned without spending its NPC check. */
    memset(s_memory + D->planets, 1, LEVEL_COUNT);
    for (int i = 0; i < 13; ++i) {
        uint32_t moby = 0x180000;
        int16_t cls = (int16_t)k_rando_npcs[i].oclass;
        memcpy(s_memory + moby + 0xA6, &cls, 2);
        set32(s_memory + D->level, k_rando_npcs[i].level);
        ((int (*)(uint32_t, uint32_t))hook_named("rac1.npc.try_dialog"))(moby, 0);
        CHECK(strstr(prompt, "Buy ") && strstr(prompt, " for 500 bolts"));
        int before = npc_purchases;
        ((void (*)(uint32_t))hook_named(k_rando_npcs[i].hook))(moby);
        CHECK(npc_purchases == before + 1 && (data_words[2] & (1u << (10 + i))));
        ((void (*)(uint32_t))hook_named(k_rando_npcs[i].hook))(moby);
        CHECK(npc_purchases == before + 1);
    }
    CHECK((data_words[2] & 0x3FFu) == 0 && animation_calls == 0);
    /* Vendor monitor text/models, original purchase rows, and last purchase on exit. */
    set32(s_memory + D->level, LEVEL_NOVALIS);
    for (int item = 0; item < RANDO_ITEM_COUNT; ++item) {
        uint32_t info = (strcmp(D->version, "pal") == 0 ? 0x1864D0 : 0x1863D0) + item * 0x4C;
        set32(s_memory + info, 1000 + item);
        uint32_t cls = (strcmp(D->version, "pal") == 0 ? 0x179BC0 : 0x179AC0) + item * 0x4C;
        set32(s_memory + cls + 0x10, 0x500 + item);
        uint16_t icon = (uint16_t)(0xEA60 + item);
        memcpy(s_memory + cls + 0x38, &icon, 2);
    }
    int vendor_before = animation_calls, vendor_count = 0, pending_kind = 0, pending_item = 0;
    for (size_t row = 0; row < rando_location_count; ++row) {
        if (rando_locations[row].kind == LOC_VENDOR && plan.shuffled[row]) {
            int source = rando_locations[row].key;
            uint8_t* menu = s_memory + D->vendor_menu;
            s_phase = OPENRAC_PLAY_MENU;
            set32(s_memory + global_mode(), 5);
            set32(menu + VENDOR_CURSOR, 0);
            set32(menu + VENDOR_COUNT, 1);
            set32(menu + VENDOR_ENTRIES, source);
            set32(menu + VENDOR_ENTRIES + 4, 0);
            ((void (*)(void))hook_named("rac1.vendor.initialize_selected"))();
            CHECK(get32(menu + VENDOR_ENTRIES) == source);
            s_frame(NULL);
            CHECK(visual_count >= 2);
            ((void (*)(void))hook_named("rac1.vendor.draw_details"))();
            CHECK(strcmp(detail_name, "Buy Infobot for 500 bolts") != 0);
            game_buy(
                (openrac_rac1_Item_Give)hook_named(OPENRAC_RAC1_HOOK_Item_Give),
                (openrac_rac1_Vendor_BuildItemList)
                    hook_named(OPENRAC_RAC1_HOOK_Vendor_BuildItemList),
                0
            );
            CHECK(get32(menu + VENDOR_LAST_BOUGHT) == -1);
            pending_kind = reward_kind(plan.reward[row], &pending_item);
            ++vendor_count;
            if (vendor_count == 1) {
                continue;
            }
            CHECK(animation_calls == vendor_before);
            s_phase = OPENRAC_PLAY_PLAYING;
            set32(s_memory + global_mode(), 0);
            finish_sequence(pending_kind, pending_item, vendor_before, 0);
            vendor_before = animation_calls;
        }
    }
    CHECK(vendor_count == 11);
    CHECK((data_words[2] >> 10) == 0x1FFFu && serialized >= 13);
    uint32_t checks = data_words[2];
    data_changed(NULL);
    CHECK(data_words[2] == checks);
    ((void (*)(int))hook_named(ITEM_RANDOMIZER_HOOK_Save_NewGamePlus))(0);
    CHECK(data_words[2] == checks);
    ((void (*)(void))hook_named(ITEM_RANDOMIZER_HOOK_Save_NewGame))();
    CHECK(data_words[2] == 0);
    free(s_memory);
#if defined(_WIN32)
    _rmdir(dir);
#else
    rmdir(dir);
#endif
    printf(
        "reward integration %s: NPC checks, vendor presentation, cases, currency and return "
        "placement passed\\n",
        D->version
    );
    return 0;
}
