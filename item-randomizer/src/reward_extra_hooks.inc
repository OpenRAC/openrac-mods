static int32_t (*s_count_unspent)(void);
static void (*s_gold_case_update)(uint32_t);
static int s_has_gold_currency_hook;
static int64_t s_gold_hint_location = -1;
static float s_gold_hint_distance = 9;
static unsigned s_gold_hint_price;
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
    if (s_gold_case >= 0 && *gold_owned(k_rando_gold_bases[s_gold_case])) {
        save_checks(saved_checks() | (1u << s_gold_case));
    }
    /* Retail may save inside an NPC update while its planet flag shows collection state. */
    swaps_suspend();
    uint8_t shown = 0;
    uint8_t* upgrade = s_gold_case >= 0 ? gold_owned(k_rando_gold_bases[s_gold_case]) : NULL;
    if (upgrade) {
        shown = *upgrade;
        *upgrade = s_gold_real_upgrade;
    }
    s_save_serialize(buffer);
    if (upgrade) {
        *upgrade = shown;
    }
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

static void gold_case_update(openrac_gaddr moby) {
    const int mode = reward_gold_mode();
    if (mode < 0) {
        return; /* no purchases before the room settings/save are known */
    }
    if (!mode) {
        s_gold_case_update(moby);
        return;
    }
    if (!s_has_gold_currency_hook) {
        return;
    }
    uint8_t* m = (uint8_t*)OPENRAC_G(s_api, moby);
    int16_t oclass;
    memcpy(&oclass, m + 0xA6, sizeof oclass);
    const uint32_t pvars = (uint32_t)get32(m + MOBY_PVARS);
    if (oclass >= 0x5B0 && oclass < 0x5B0 + RANDO_GOLD_WEAPON_COUNT) {
        const int index = oclass - 0x5B0;
        const int base = k_rando_gold_bases[index];
        const uint8_t real = *gold_owned(base);
        *gold_owned(base) = (uint8_t)((gold_case_checks() >> index) & 1u);
        s_gold_case_update(moby);
        *gold_owned(base) = real;
        return;
    }
    if (oclass != 304 || !pvars) {
        s_gold_case_update(moby);
        return;
    }
    const int slot = i32_at(pvars + 0x40u);
    if (slot < 0 || slot >= 20) {
        s_gold_case_update(moby);
        return;
    }
    const uint32_t table = strcmp(s_disc->version, "pal") == 0 ? 0x001DEDE0u : 0x001DEB10u;
    uint8_t* entries = (uint8_t*)OPENRAC_G(s_api, s_api->relocate_level_data(1, table));
    const int base = get32(entries + slot * 4);
    const int index = rando_gold_index(base);
    if (index < 0) {
        s_gold_case_update(moby);
        return;
    }
    const uint32_t bit = 1u << index;
    const int had_case = (gold_case_checks() & bit) != 0;
    if (!had_case && s_api->hero_pose) {
        openrac_pose pose = {0};
        pose.size = sizeof pose;
        if (s_api->hero_pose(&pose) == OPENRAC_HOOK_OK) {
            float position[3];
            memcpy(position, m + 0x10, sizeof position);
            float distance = 0;
            for (int axis = 0; axis < 3; ++axis) {
                const float d = pose.position[axis] - position[axis];
                distance += d * d;
            }
            if (distance < s_gold_hint_distance) {
                const uint32_t prices =
                    strcmp(s_disc->version, "pal") == 0 ? 0x001C47B0u : 0x001C4530u;
                uint8_t* price_table =
                    (uint8_t*)OPENRAC_G(s_api, s_api->relocate_level_data(1, prices));
                uint16_t price;
                memcpy(&price, price_table + base * 0x18 + 0x14, sizeof price);
                s_gold_hint_location = rando_case_row(base);
                s_gold_hint_distance = distance;
                s_gold_hint_price = price;
            }
        }
    }
    const uint8_t upgrade = *gold_owned(base);
    const int previous = s_gold_case;
    const uint8_t previous_upgrade = s_gold_real_upgrade;
    s_gold_real_upgrade = upgrade;
    s_gold_case = index;
    *gold_owned(base) = (uint8_t)had_case;
    /* A shuffled case sells its reward, not an upgrade requiring the old base
     * weapon. Retail still checks both currencies; save serialization sees
     * the player's real inventory through swaps_suspend(). */
    swap_push(owned(base), -2);
    s_gold_case_update(moby);
    swap_pop();
    const int bought = *gold_owned(base) != 0 && !had_case;
    *gold_owned(base) = upgrade;
    s_gold_case = previous;
    s_gold_real_upgrade = previous_upgrade;
    if (bought) {
        set_had(rando_case_row(base));
        give_reward(rando_case_row(base), 0, -1);
        s_seen[rando_case_row(base)] = 1;
        if (acquisition_case(base, moby)) {
            /* An instant case dialogue must not be finished again when the
             * purchased Infobot's movie ends. This affects only this case. */
            const uint32_t context = s_api->relocate_level_data(
                0, strcmp(s_disc->version, "pal") == 0 ? 0x179200u : 0x179100u
            );
            if (OPENRAC_GREF(s_api, uint32_t, context + 8) == moby
                && OPENRAC_GREF(s_api, uint32_t, context + 12) == pvars) {
                OPENRAC_GREF(s_api, uint32_t, context + 8) = 0;
                OPENRAC_GREF(s_api, uint32_t, context + 12) = 0;
            }
        }
    }
}

static int32_t reward_count_unspent(void) {
    if (!reward_gold_mode()) {
        return s_count_unspent();
    }
    uint32_t checks = gold_case_checks();
    if (s_gold_case >= 0 && *gold_owned(k_rando_gold_bases[s_gold_case])) {
        checks |= 1u << s_gold_case;
    }
    int bought = 0;
    for (int i = 0; i < 10; ++i) {
        bought += (checks >> i) & 1u;
    }
    int remaining = count_gold_bolts_hook() - 4 * bought;
    return remaining > 0 ? remaining : 0;
}

static void reward_case_hint_tick(void) {
    if (s_gold_hint_location >= 0 && rando_on() && s_notice_count == 0) {
        char item[100], line[160];
        rando_reward_name(s_plan.reward[s_gold_hint_location], item, sizeof item);
        snprintf(line, sizeof line, "%s - %u bolts + 4 Gold Bolts", item, s_gold_hint_price);
        s_api->play_notice(s_mod, line);
    }
    s_gold_hint_location = -1;
    s_gold_hint_distance = 9;
}

static int reward_extra_hooks(void) {
    if (!reward_save_host(s_api)) {
        return 1;
    }
    s_has_gold_currency_hook = s_api->hook(
                                   s_mod,
                                   "rac1.gold_bolt.count_unspent",
                                   "i32()",
                                   (void*)reward_count_unspent,
                                   (void**)&s_count_unspent
                               )
                               == OPENRAC_HOOK_OK;
    if ((s_plan.settings.pools & POOL_GOLD_WEAPONS) && !s_has_gold_currency_hook) {
        s_api->fail(
            s_mod, "Shuffled gold cases need the GoldBolts_CountUnspent hook. Update OpenRAC."
        );
        return 0;
    }
    return npc_extra_hooks()
           && take(
               s_api->hook(
                   s_mod,
                   "Moby_GoldWeaponCase_Update",
                   "void(u32)",
                   (void*)gold_case_update,
                   (void**)&s_gold_case_update
               ),
               "Gold weapon cases"
           );
}
