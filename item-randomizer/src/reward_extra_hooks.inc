/* Direct coordinate rewards retain their story flags but give the seed reward. */
static int take(int result, const char* point);
static void (*s_npc_update[RANDO_NPC_COUNT])(uint32_t);
static void (*s_save_serialize)(uint32_t);

static void run_npc_infobot(int index, uint32_t moby) {
    const int mode = reward_npc_mode();
    if (mode < 0) {
        return; /* do not spend or mark a new room's NPC before the save is judged */
    }
    if (!mode) {
        s_npc_update[index](moby);
        return;
    }
    const rando_npc_infobot* npc = &k_rando_npcs[index];
    int row = -1;
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        if (l->kind == LOC_FIXED && l->level == npc->level && l->vanilla.kind == REWARD_PLANET
            && l->vanilla.value == npc->planet) {
            row = (int)i;
            break;
        }
    }
    if (row < 0 || current_level() != npc->level) {
        s_npc_update[index](moby);
        return;
    }
    const int previous = s_npc_running;
    const uint32_t before = saved_checks();
    kept_banner banner;
    banner_keep(&banner);
    swap_push(planet_flag(npc->planet), row);
    s_npc_running = index;
    s_npc_update[index](moby);
    s_npc_running = previous;
    swap_pop();
    if (saved_checks() != before) {
        banner_put_back(&banner);
    }
}

static void npc_update_0(uint32_t moby) {
    run_npc_infobot(0, moby);
}

static void npc_update_1(uint32_t moby) {
    run_npc_infobot(1, moby);
}

static void npc_update_2(uint32_t moby) {
    run_npc_infobot(2, moby);
}

static void npc_update_3(uint32_t moby) {
    run_npc_infobot(3, moby);
}

static void npc_update_4(uint32_t moby) {
    run_npc_infobot(4, moby);
}

static void npc_update_5(uint32_t moby) {
    run_npc_infobot(5, moby);
}

static void npc_update_6(uint32_t moby) {
    run_npc_infobot(6, moby);
}

static void npc_update_7(uint32_t moby) {
    run_npc_infobot(7, moby);
}

static void npc_update_8(uint32_t moby) {
    run_npc_infobot(8, moby);
}

static void npc_update_9(uint32_t moby) {
    run_npc_infobot(9, moby);
}

static void npc_update_10(uint32_t moby) {
    run_npc_infobot(10, moby);
}

static void npc_update_11(uint32_t moby) {
    run_npc_infobot(11, moby);
}

static void npc_update_12(uint32_t moby) {
    run_npc_infobot(12, moby);
}

static void serialize_real_ownership(uint32_t buffer) {
    swaps_suspend();
    s_save_serialize(buffer);
    swaps_resume();
}

static int npc_extra_hooks(void) {
    static void (*const wrappers[RANDO_NPC_COUNT])(uint32_t) =
        {npc_update_0,
         npc_update_1,
         npc_update_2,
         npc_update_3,
         npc_update_4,
         npc_update_5,
         npc_update_6,
         npc_update_7,
         npc_update_8,
         npc_update_9,
         npc_update_10,
         npc_update_11,
         npc_update_12};
    for (int i = 0; i < RANDO_NPC_COUNT; ++i) {
        if (!take(
                s_api->hook(
                    s_mod,
                    k_rando_npcs[i].hook,
                    "void(u32)",
                    (void*)wrappers[i],
                    (void**)&s_npc_update[i]
                ),
                k_rando_npcs[i].hook
            )) {
            return 0;
        }
    }
    return take(
        s_api->hook(
            s_mod,
            "rac1.save.serialize",
            "void(u32)",
            (void*)serialize_real_ownership,
            (void**)&s_save_serialize
        ),
        "rac1.save.serialize"
    );
}

static int reward_extra_hooks(void) {
    return !reward_save_host(s_api) || npc_extra_hooks();
}
