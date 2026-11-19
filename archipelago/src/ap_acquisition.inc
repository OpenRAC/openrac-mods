/* Reward-based world pickup sequences. Collection and network delivery stay independent. */
#include "ap_acquisition_rules.h"
static const ap_pickup_api* s_acquisition_api;
static openrac_gaddr s_acquisition_source;
static int s_acquisition_source_row = -1;
static void (*s_acquisition_gold)(uint32_t);
static void (*s_acquisition_scene)(int32_t);
static void (*s_acquisition_case_demo)(uint32_t, int32_t);
static void (*s_acquisition_updates[5])(uint32_t);

typedef struct acquisition_entry {
    int kind, item, center;
    float position[3], origin[3], yaw;
} acquisition_entry;

static acquisition_entry s_acquisition_queue[16];
static acquisition_entry s_acquisition_return;
static int s_acquisition_return_pending;
static int s_acquisition_count;
static uint32_t s_acquisition_load;
static int s_acquisition_vendor_row = -1;

static int acquisition_reward_location(int64_t location, int* kind, int* item) {
    int64_t id;
    int same;
    char name[192];
    if (location < 0 || !presentation_reward(location, &id, &same, name, sizeof name)) {
        return 0;
    }
    const int special = same ? special_item_acquisition(name) : -1;
    if (special >= 0) {
        *kind = 4;
        *item = special;
        return 1;
    }
    *kind = ap_acquisition_kind(id, same, item);
    return 1;
}

static int acquisition_reward(int row, int* kind, int* item) {
    return row >= 0
           && acquisition_reward_location(ap_location_id(&rando_locations[row]), kind, item);
}

static void acquisition_enqueue(int kind, int item) {
    uint32_t load;
    play_phase(&load);
    if (load != s_acquisition_load) {
        s_acquisition_load = load;
        s_acquisition_count = 0;
        s_acquisition_return_pending = 0;
    }
    if (s_acquisition_count < 16) {
        memset(&s_acquisition_queue[s_acquisition_count], 0, sizeof s_acquisition_queue[0]);
        s_acquisition_queue[s_acquisition_count].kind = kind;
        s_acquisition_queue[s_acquisition_count++].item = item;
    }
}

static void acquisition_queue(int row) {
    int kind, item;
    if (s_acquisition_api && acquisition_reward(row, &kind, &item)) {
        acquisition_enqueue(kind, item);
    }
}

static int acquisition_case(int base, uint32_t moby) {
    int kind, item;
    if (s_acquisition_api && acquisition_reward_location(ap_gold_case_id(base), &kind, &item)) {
        if (s_acquisition_count >= 16) {
            return 0;
        }
        acquisition_enqueue(kind, item);
        acquisition_entry* entry = &s_acquisition_queue[s_acquisition_count - 1];
        openrac_pose hero = {0};
        hero.size = sizeof hero;
        if (kind && s_api->hero_place && s_api->hero_pose
            && s_api->hero_pose(&hero) == OPENRAC_HOOK_OK && hero.health > 0) {
            const uint32_t pool = (uint32_t)get32(
                level_byte_at(strcmp(s_disc->version, "pal") == 0 ? 0x160098u : 0x15FFD8u)
            );
            const uint32_t end = (uint32_t)get32(
                level_byte_at(strcmp(s_disc->version, "pal") == 0 ? 0x1600A0u : 0x15FFE0u)
            );
            float source[3], sum[3] = {0};
            int count = 0;
            memcpy(source, OPENRAC_G(s_api, moby + 0x10), sizeof source);
            if (pool && end > pool && end <= 0x02000000u && (end - pool) % 0x100u == 0) {
                for (uint32_t m = pool; m < end; m += 0x100u) {
                    if (OPENRAC_GREF(s_api, int16_t, m + 0xA6) != 304
                        || OPENRAC_GREF(s_api, uint8_t, m + 0x20) >= 0xFD) {
                        continue;
                    }
                    float p[3];
                    memcpy(p, OPENRAC_G(s_api, m + 0x10), sizeof p);
                    const float x = p[0] - source[0], y = p[1] - source[1], z = p[2] - source[2];
                    if (x * x + y * y > 900 || z * z > 25) {
                        continue;
                    }
                    for (int axis = 0; axis < 3; ++axis) {
                        sum[axis] += p[axis];
                    }
                    ++count;
                }
            }
            if (count >= 3) {
                entry->center = 1;
                entry->yaw = hero.rotation[2];
                memcpy(entry->origin, hero.position, sizeof entry->origin);
                for (int axis = 0; axis < 3; ++axis) {
                    entry->position[axis] = sum[axis] / count;
                }
                entry->position[2] += .25f;
            }
        }
        return 1;
    }
    return 0;
}

static void acquisition_case_demo(uint32_t moby, int32_t item) {
    int kind, reward;
    if (s_acquisition_api && s_gold_case >= 0
        && acquisition_reward_location(
            ap_gold_case_id(k_ap_gold_bases[s_gold_case]), &kind, &reward
        )) {
        return;
    }
    s_acquisition_case_demo(moby, item);
}

static void acquisition_item(int row) {
    if (s_acquisition_source && s_acquisition_source_row == row) {
        acquisition_queue(row);
    }
}

static int acquisition_vendor(int row) {
    if (!s_acquisition_api) {
        return 0;
    }
    uint32_t load;
    play_phase(&load);
    if (load != s_acquisition_load) {
        s_acquisition_load = load;
        s_acquisition_count = 0;
        s_acquisition_return_pending = 0;
    }
    s_acquisition_vendor_row = row; /* replace, rather than queue, earlier purchases */
    return 1;
}

static void acquisition_tick(int phase, uint32_t load) {
    if (!s_acquisition_api) {
        return;
    }
    const int active = s_acquisition_api->pickup_animation(s_mod, -1, 0);
    if (load != s_acquisition_load) {
        s_acquisition_load = load;
        s_acquisition_count = 0;
        s_acquisition_vendor_row = -1;
        s_acquisition_return_pending = 0;
    }
    if (!active && s_acquisition_return_pending && phase == OPENRAC_PLAY_PLAYING) {
        if (s_api->hero_place(
                s_mod,
                s_acquisition_return.origin,
                s_acquisition_return.yaw,
                OPENRAC_PLACE_INVULNERABLE
            )
            == OPENRAC_HOOK_OK) {
            s_acquisition_return_pending = 0;
        }
        return;
    }
    if (!active && phase == OPENRAC_PLAY_PLAYING && s_acquisition_vendor_row >= 0) {
        const int row = s_acquisition_vendor_row;
        s_acquisition_vendor_row = -1;
        acquisition_queue(row);
    }
    if (active || phase != OPENRAC_PLAY_PLAYING || !s_acquisition_count) {
        return;
    }
    const int kind = s_acquisition_queue[0].kind, item = s_acquisition_queue[0].item;
    if (s_acquisition_queue[0].center) {
        openrac_pose hero = {0};
        hero.size = sizeof hero;
        if (s_api->hero_pose(&hero) != OPENRAC_HOOK_OK || hero.health <= 0) {
            return;
        }
        const float* p = s_acquisition_queue[0].position;
        const float x = hero.position[0] - p[0], y = hero.position[1] - p[1],
                    z = hero.position[2] - p[2];
        if (x * x + y * y > .04f || z * z > 1.0f) {
            s_api->hero_place(
                s_mod,
                p,
                s_acquisition_queue[0].yaw,
                OPENRAC_PLACE_GROUND | OPENRAC_PLACE_INVULNERABLE
            );
            return; /* placement is queued: wait until the hero is actually there */
        }
    }
    if (kind == AP_ACQUIRE_WEAPON && !*owned(item)) {
        return; /* wait for server delivery */
    }
    const int result = s_acquisition_api->pickup_animation(s_mod, kind, item);
    if (result < 0) {
        return;
    }
    if (s_acquisition_queue[0].center) {
        s_acquisition_return = s_acquisition_queue[0];
        s_acquisition_return_pending = 1;
    }
    if (!result && kind) {
        s_acquisition_api->pickup_animation(s_mod, 0, 0);
    }
    memmove(
        s_acquisition_queue,
        s_acquisition_queue + 1,
        (size_t)--s_acquisition_count * sizeof s_acquisition_queue[0]
    );
}

static void acquisition_remove(uint32_t moby) {
    const int slot = s_api->moby_slot_of ? s_api->moby_slot_of(moby) : -1;
    if (slot >= 0 && s_api->moby_kill && s_api->moby_kill(s_mod, slot) == OPENRAC_HOOK_OK) {
        return;
    }
    OPENRAC_GREF(s_api, uint8_t, moby + 0x20) = 0xFD;
    OPENRAC_GREF(s_api, uint16_t, moby + 0x34) |= 0x81;
}

static void acquisition_collect(int row, uint32_t moby) {
    const rando_location* l = &rando_locations[row];
    if (l->kind == LOC_GOLD) {
        *byte_at(s_disc->gold_bolts + l->level * 4 + l->key) = 1;
    } else if (l->kind == LOC_INFOBOT) {
        *byte_at(s_disc->triggers + l->level * TRIGGER_SLOTS + l->key) = TRIGGER_DONE;
    }
    acquisition_queue(row);
    found(row);
    acquisition_remove(moby);
}

static int acquisition_infobot(uint32_t moby, int row) {
    if (had(row)) {
        acquisition_remove(moby);
        return 1;
    }
    int kind, item;
    if (!s_acquisition_api || OPENRAC_GREF(s_api, uint8_t, moby + 0x20) != 8
        || !acquisition_reward(row, &kind, &item)) {
        return 0;
    }
    acquisition_collect(row, moby);
    return 1;
}

static void acquisition_gold(uint32_t moby) {
    const int64_t id = moby_location(moby);
    int row = -1, kind, item;
    if (id >= 0) {
        for (size_t i = 0; i < rando_location_count; ++i) {
            if (ap_location_id(&rando_locations[i]) == id) {
                row = (int)i;
                break;
            }
        }
    }
    if (row >= 0 && OPENRAC_GREF(s_api, uint8_t, moby + 0x20) == 1
        && acquisition_reward(row, &kind, &item) && kind != AP_ACQUIRE_GOLD) {
        openrac_pose hero = {0};
        hero.size = sizeof hero;
        if (s_api->hero_pose(&hero) == OPENRAC_HOOK_OK && hero.health > 0) {
            float p[3];
            memcpy(p, OPENRAC_G(s_api, moby + 0x10), sizeof p);
            const float dx = p[0] - hero.position[0], dy = p[1] - hero.position[1],
                        dz = p[2] - hero.position[2];
            if (dx * dx + dy * dy < 9 && dz * dz < 4) {
                acquisition_collect(row, moby);
                return;
            }
        }
    }
    s_acquisition_gold(moby);
}

static void acquisition_run(uint32_t moby, void (*original)(uint32_t)) {
    const uint32_t previous = s_acquisition_source;
    const int previous_row = s_acquisition_source_row;
    s_acquisition_source = moby;
    s_acquisition_source_row = -1;
    if (s_acquisition_api) {
        const int64_t id = moby_location(moby);
        if (id >= 0) {
            for (size_t i = 0; i < rando_location_count; ++i) {
                if (ap_location_id(&rando_locations[i]) == id) {
                    s_acquisition_source_row = (int)i;
                    break;
                }
            }
        }
    }
    original(moby);
    s_acquisition_source = previous;
    s_acquisition_source_row = previous_row;
}

static void acquisition_scene(int32_t scene) {
    if (s_acquisition_source && s_acquisition_source_row >= 0) {
        return;
    }
    s_acquisition_scene(scene);
}

static void acquisition_update0(uint32_t m) {
    acquisition_run(m, s_acquisition_updates[0]);
}

static void acquisition_update1(uint32_t m) {
    acquisition_run(m, s_acquisition_updates[1]);
}

static void acquisition_update2(uint32_t m) {
    acquisition_run(m, s_acquisition_updates[2]);
}

static void acquisition_update3(uint32_t m) {
    acquisition_run(m, s_acquisition_updates[3]);
}

static void acquisition_update4(uint32_t m) {
    acquisition_run(m, s_acquisition_updates[4]);
}

static int acquisition_hooks(void) {
    s_acquisition_api = s_reward_models ? ap_pickup_host(s_api) : NULL;
    s_acquisition_count = 0;
    s_acquisition_source = 0;
    s_acquisition_vendor_row = -1;
    s_acquisition_return_pending = 0;
    if (!s_acquisition_api) {
        return 1;
    }
    const char* points[] =
        {"rac1.moby.warp_portal.update",
         "Moby_Tresspasser_Update",
         "Moby_Magnetboots_Update",
         "Moby_Morphoray1354_Update",
         "Moby_BoltGrabber_Update"};
    void (*wrappers[])(uint32_t) =
        {acquisition_update0,
         acquisition_update1,
         acquisition_update2,
         acquisition_update3,
         acquisition_update4};
    for (int i = 0; i < 5; ++i) {
        if (!take(
                s_api->hook(
                    s_mod,
                    points[i],
                    "void(u32)",
                    (void*)wrappers[i],
                    (void**)&s_acquisition_updates[i]
                ),
                points[i]
            )) {
            return 0;
        }
    }
    return take(
               s_api->hook(
                   s_mod,
                   "rac1.moby.gold_bolt.update",
                   "void(u32)",
                   (void*)acquisition_gold,
                   (void**)&s_acquisition_gold
               ),
               "Gold Bolt collection"
           )
           && take(
               s_api->hook(
                   s_mod,
                   "Pickup_StartScene",
                   "void(i32)",
                   (void*)acquisition_scene,
                   (void**)&s_acquisition_scene
               ),
               "Pickup scene routing"
           )
           && take(
               s_api->hook(
                   s_mod,
                   "GoldCase_StartDemo",
                   "void(u32,i32)",
                   (void*)acquisition_case_demo,
                   (void**)&s_acquisition_case_demo
               ),
               "Gold case reward animation"
           );
}
