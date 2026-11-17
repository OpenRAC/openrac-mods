/* Extra checks belong to the game save, alongside its existing native seed tag. */
#define REWARD_CHECK_MAGIC 0x52465231u

static uint32_t saved_checks(void) {
    const reward_save_api* api = reward_save_host(s_api);
    uint32_t state[3] = {0};
    if (!api || api->save_data_get(s_mod, state, sizeof state) != sizeof state
        || state[0] != REWARD_CHECK_MAGIC || state[1] != rando_seed_tag(s_code)) {
        return 0;
    }
    return state[2] & 0x7FFFFFu;
}

static void save_checks(uint32_t checks) {
    const reward_save_api* api = reward_save_host(s_api);
    uint32_t state[3] = {REWARD_CHECK_MAGIC, rando_seed_tag(s_code), checks & 0x7FFFFFu};
    if (api) {
        api->save_data_set(s_mod, state, sizeof state);
    }
}

static void reward_save_changed(void* user) {
    (void)user; /* read lazily: never re-tag a foreign save */
}

static uint32_t gold_case_checks(void) {
    return saved_checks() & 0x3FFu;
}

static uint8_t* gold_owned(int base) {
    return byte_at(strcmp(s_disc->version, "pal") == 0 ? 0x13E620u + base : 0x13E520u + base);
}

static int extra_row(int row) {
    if (row < 0 || (size_t)row >= rando_location_count || !s_plan.shuffled[row]) {
        return -1;
    }
    const rando_location* l = &rando_locations[row];
    if (l->kind == LOC_GOLD_CASE) {
        int i = rando_gold_index(l->key);
        return i >= 0 ? i : -1;
    }
    return -1;
}
