#define main legacy_load_test_main
#include "load_test.c"
#undef main
#include "gold_save_api.h"
#include "reward_pools.h"
static uint32_t words[3], bytes;
static int purchases, serializations, active_base = -1;
static openrac_frame_fn changed;

static uint8_t* upgrade(int base) {
    return s_memory + (strcmp(D->version, "pal") == 0 ? 0x13E620 : 0x13E520) + base;
}

static int save_set(openrac_mod* m, const void* p, uint32_t n) {
    (void)m;
    CHECK(n == 12);
    memcpy(words, p, n);
    bytes = n;
    return 0;
}

static uint32_t save_get(openrac_mod* m, void* p, uint32_t n) {
    (void)m;
    if (p && n >= bytes) {
        memcpy(p, words, bytes);
    }
    return bytes;
}

static int save_watch(openrac_mod* m, openrac_frame_fn fn, void* u) {
    (void)m;
    (void)u;
    changed = fn;
    return 0;
}

static int settings(openrac_mod* m, const char* k) {
    if (!strcmp(k, "shuffle_gold_weapons")) {
        return 1;
    }
    if (!strcmp(k, "spoiler_log")) {
        return 0;
    }
    return setting_bool(m, k);
}

static void serialize(uint32_t p) {
    (void)p;
    ++serializations;
    CHECK(active_base >= 0);
    CHECK(s_memory[D->owned + active_base] == 0);
    CHECK(*upgrade(active_base) == 1);
}

static void original_case(uint32_t moby) {
    uint32_t v = get32(s_memory + moby + 0x78);
    int slot = get32(s_memory + v + 0x40);
    int base = k_rando_gold_bases[slot];
    CHECK(s_memory[D->owned + base] == 1);
    if (*upgrade(base)) {
        return;
    }
    CHECK(((int (*)(void))hook_named("rac1.gold_bolt.count_unspent"))() >= 4);
    ++purchases;
    *upgrade(base) = 1;
    ((void (*)(uint32_t))hook_named("rac1.save.serialize"))(0);
}

static int hook(openrac_mod* m, const char* n, const char* sig, void* r, void** o) {
    int result = host_hook(m, n, sig, r, o);
    if (!strcmp(n, "rac1.save.serialize")) {
        *o = (void*)serialize;
    }
    if (!strcmp(n, "Moby_GoldWeaponCase_Update")) {
        *o = (void*)original_case;
    }
    return result;
}

int main(int argc, char** argv) {
    CHECK(argc > 1);
    s_memory = calloc(1, MEMORY_SIZE);
    CHECK(s_memory);
    D = &k_rando_discs[argc > 2 && !strcmp(argv[2], "ntsc") ? 1 : 0];
    s_seed = "gold-cases";
    s_keep_start = 1;
    char dir[80];
    snprintf(dir, sizeof dir, "gold-case-test-%s", D->version);
    set_env("OPENRAC_MOD_DATA_DIR", dir);
    void* lib = open_library(argv[1]);
    CHECK(lib);
    openrac_mod_load_fn load = (openrac_mod_load_fn)symbol(lib, OPENRAC_MOD_LOAD);
    CHECK(load);
    reward_save_api ext = {0};
#if OPENRAC_MOD_API_MINOR < 16
    openrac_api* api = &ext.base;
#else
    openrac_api* api = &ext;
#endif
    *api = make_api("rac1", D->version);
    api->size = sizeof ext;
    api->api_minor = 16;
    api->hook = hook;
    api->setting_bool = settings;
    ext.save_data_set = save_set;
    ext.save_data_get = save_get;
    ext.on_save_data = save_watch;
    set32(s_memory + D->level, LEVEL_NOVALIS);
    memset(s_memory + D->vendor_stock, 0xFF, VENDOR_STOCK_SIZE);
    CHECK(load(api, (openrac_mod*)1) == 0 && changed);
    rando_options options = {s_seed, 1, 1, 1, 1, 1, 1, "standard", 1};
    rando_settings settings_;
    rando_plan plan;
    rando_settings_of(&options, NULL, 1, &settings_);
    CHECK(rando_solve(&settings_, &plan) == 0);
    char code[RANDO_CODE_SIZE];
    rando_encode(&settings_, code);
    set_tag_word(rando_seed_tag(code));
    for (size_t i = 0; i < rando_location_count; ++i) {
        if (plan.reward[i].kind == REWARD_GOLD_BOLT) {
            const rando_location* l = &rando_locations[i];
            if (l->kind == LOC_GOLD) {
                s_memory[D->gold_bolts + l->level * 4 + l->key] = 1;
            } else if (l->kind == LOC_INFOBOT) {
                s_memory[D->triggers + l->level * 16 + l->key] = 0xFF;
            } else {
                s_memory[D->obtained + l->key] = 1;
            }
        }
    }
    for (int i = 0; i < 10; ++i) {
        *upgrade(k_rando_gold_bases[i]) = 1;
    }
    CHECK(((int (*)(void))hook_named("rac1.gold_bolt.count_unspent"))() == 40);
    for (int i = 0; i < 10; ++i) {
        uint32_t moby = 0x180000, vars = 0x190000;
        int16_t cls = 304;
        memcpy(s_memory + moby + 0xA6, &cls, 2);
        set32(s_memory + moby + 0x78, vars);
        set32(s_memory + vars + 0x40, i);
        set32(
            s_memory + (strcmp(D->version, "pal") == 0 ? 0x1DEDE0 : 0x1DEB10) + i * 4,
            k_rando_gold_bases[i]
        );
        active_base = k_rando_gold_bases[i];
        s_memory[D->owned + active_base] = 0;
        ((void (*)(uint32_t))hook_named("Moby_GoldWeaponCase_Update"))(moby);
        CHECK(purchases == i + 1 && serializations == i + 1);
        CHECK(((int (*)(void))hook_named("rac1.gold_bolt.count_unspent"))() == 40 - 4 * (i + 1));
        ((void (*)(uint32_t))hook_named("Moby_GoldWeaponCase_Update"))(moby);
        CHECK(purchases == i + 1);
    }
    CHECK((words[2] & 0x3FF) == 0x3FF);
    s_frame(NULL);
    CHECK(get32(s_memory + D->level + 0x9C) == 1 && get32(s_memory + D->level + 0x11C) == 0);
    uint32_t checks = words[2];
    changed(NULL);
    CHECK(words[2] == checks);
    ((void (*)(int))hook_named(ITEM_RANDOMIZER_HOOK_Save_NewGamePlus))(0);
    CHECK(words[2] == checks);
    ((void (*)(void))hook_named(ITEM_RANDOMIZER_HOOK_Save_NewGame))();
    CHECK(words[2] == 0);
    free(s_memory);
    printf("gold-case gameplay test passed\n");
    return 0;
}
