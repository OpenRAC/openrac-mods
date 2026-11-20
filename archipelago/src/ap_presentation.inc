#include "gold_case_prompt.h"
#include "special_item_visual.h"

/* Render-only Archipelago reward displays. Included after the location/game helpers. */
static int take(int result, const char* point);

static int presentation_reward(int64_t location, int64_t* id, int* same, char* label, size_t size) {
    return ap_client_location_presentation(location, id, same, label, size);
}

static int s_reward_models;
static int s_reward_text;
static const ap_visual_api* s_visual_api;
static openrac_moby_visual s_visuals[OPENRAC_MOBY_VISUALS_MAX];
static int s_visual_count;
static uint32_t s_vendor_preview[3];
static uint32_t s_vendor_preview_load;
static uint32_t s_dialog_load;
static int64_t s_dialog_location = -1;
static uint32_t s_case_dialog_request;
static int s_draw_vendor_icons;
static int s_draw_vendor_details;
static int s_in_dialog_try;
static int s_presentation_text; /* this file owns rac1.npc.try_dialog and rac1.text.msg_string */
static void (*s_draw_icons)(void);
static void (*s_draw_details)(void);
static void (*s_vendor_initialize)(void);
static int32_t (*s_icon_frame)(int32_t, int32_t);
static void (*s_draw_sprite)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
static int32_t (*s_try_dialog)(uint32_t, uint32_t);
static uint32_t (*s_message_string)(int32_t);

static uint8_t* presentation_item(int item) {
    const uint32_t table = strcmp(s_disc->version, "pal") == 0 ? 0x001864D0u : 0x001863D0u;
    return byte_at(table) + item * 0x4Cu;
}

static int presentation_class(int item) {
    /* Backpack equipment is not a held weapon; use its native pickup model. */
    if (item == ITEM_HELI_PACK) return 607;
    const uint32_t table = strcmp(s_disc->version, "pal") == 0 ? 0x00179BC0u : 0x00179AC0u;
    return get32(level_byte_at(table) + item * 0x4Cu + 0x10);
}

/* Match the pause/items grid, including its shared special-item icon sheet. */
static void item_menu_icon(int item, int* icon, int* frame) {
    *frame = 0;
    if (item == ITEM_PERSUADER || item == ITEM_MAP_O_MATIC || item == ITEM_BOLT_GRABBER) {
        *icon = 0xEAC9;
        *frame = item == ITEM_PERSUADER ? 0 : item == ITEM_MAP_O_MATIC ? 1 : 2;
        return;
    }
    const uint32_t table = strcmp(s_disc->version, "pal") == 0 ? 0x00179BC0u : 0x00179AC0u;
    uint16_t value;
    memcpy(&value, level_byte_at(table) + item * 0x4Cu + 0x38, sizeof value);
    if (!value) {
        memcpy(&value, presentation_item(item) + 0x38, sizeof value);
    }
    *icon = value;
}

/* The owning player's game matters: other games may reuse these numeric item ids. */
static int reward_class(int64_t location, char* label, size_t size, int* icon, int* frame) {
    int64_t id = -1;
    int same = 0;
    if (!presentation_reward(location, &id, &same, label, size)) {
        return -1;
    }
    if (same) {
        const int special = special_item_visual(label, icon, frame);
        if (special >= 0) {
            return special;
        }
    }
    const rando_reward reward = ap_reward_of(id);
    const int gold = ap_gold_weapon_base(id);
    *icon = 0x754F;
    *frame = 0;
    if (same && gold >= 0) {
        item_menu_icon(gold, icon, frame);
        *frame = 4;
        return 0x5B0 + ap_gold_weapon_index(gold);
    }
    if (same && reward.kind == REWARD_ITEM) {
        item_menu_icon(reward.value, icon, frame);
        const int cls = presentation_class(reward.value);
        return cls > 0 && cls < 0x800 ? cls : 0x46E;
    }
    if (same && reward.kind == REWARD_PLANET) {
        *icon = 0xE99A;
        *frame = 4;
        return 0x2EE;
    }
    return 0x46E;
}

static int find_presentation_location(int kind, int level, int key) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        if (ap_is_location(l) && l->kind == kind && l->level == level && l->key == key) {
            return (int)i;
        }
    }
    return -1;
}

/* The case and its display share a retail slot. The display class identifies
 * a mesh, not the case's item: resolve both through the same slot table. */
static int gold_case_base(uint32_t moby) {
    const uint32_t vars = OPENRAC_GREF(s_api, uint32_t, moby + MOBY_PVARS);
    const int slot = vars ? i32_at(vars + 0x40u) : -1;
    if (slot < 0 || slot >= 20) {
        return -1;
    }
    const uint32_t table = strcmp(s_disc->version, "pal") == 0 ? 0x001DEDE0u : 0x001DEB10u;
    const int base = get32((uint8_t*)OPENRAC_G(s_api, s_api->relocate_level_data(1, table)) + slot * 4);
    return ap_gold_weapon_index(base) >= 0 ? base : -1;
}

static int64_t npc_location(uint32_t moby) {
    const int level = current_level();
    const int cls = OPENRAC_GREF(s_api, int16_t, moby + 0xA6);
    if (cls == 0x130 && ap_client_shuffle_gold_weapons() == 1) {
        const uint32_t vars = OPENRAC_GREF(s_api, uint32_t, moby + MOBY_PVARS);
        const int slot = vars ? i32_at(vars + 0x40) : -1;
        if (slot >= 0 && slot < 20) {
            const uint32_t table = strcmp(s_disc->version, "pal") == 0 ? 0x1DEDE0u : 0x1DEB10u;
            const int base =
                get32((uint8_t*)OPENRAC_G(s_api, s_api->relocate_level_data(1, table)) + slot * 4);
            if (ap_gold_weapon_index(base) >= 0) {
                return ap_gold_case_id(base);
            }
        }
    }
    if (ap_client_npc_infobots() == 1) {
        for (int i = 0; i < AP_NPC_COUNT; ++i) {
            if (k_ap_npcs[i].level == level && k_ap_npcs[i].oclass == cls) {
                return ap_npc_id(i);
            }
        }
    }
    int item = -1, planet = -1;
    switch (cls) {
        case 0x38D:
            item = ITEM_HELI_PACK;
            break;
        case 0x37A:
            item = ITEM_SWINGSHOT;
            break;
        case 0x5A:
            item = ITEM_THRUSTER_PACK;
            break;
        case 0x148:
            item = ITEM_HYDRO_PACK;
            break;
        case 0x39D:
            item = ITEM_RYNO;
            break;
        case 0x306:
            if (level == LEVEL_NOVALIS) {
                planet = LEVEL_ARIDIA;
            }
            break;
        default:
            break;
    }
    if (item >= 0 && s_by_item[item] >= 0) {
        return ap_location_id(&rando_locations[s_by_item[item]]);
    }
    if (planet >= 0) {
        for (size_t i = 0; i < rando_location_count; ++i) {
            const rando_location* l = &rando_locations[i];
            if (ap_is_location(l) && l->level == level && l->vanilla.kind == REWARD_PLANET
                && l->vanilla.value == planet) {
                return ap_location_id(l);
            }
        }
    }
    return -1;
}

static int64_t moby_location(uint32_t moby) {
    const int cls = OPENRAC_GREF(s_api, int16_t, moby + 0xA6);
    const int level = current_level();
    const uint32_t vars = OPENRAC_GREF(s_api, uint32_t, moby + MOBY_PVARS);
    if (cls == 0x46E && vars) {
        const int key = i32_at(vars);
        if (key < 0 || key >= 4) {
            return -1;
        }
        const int row = find_presentation_location(LOC_GOLD, level, key);
        return row >= 0 ? ap_location_id(&rando_locations[row]) : -1;
    }
    if (cls == 0x2EE && vars) {
        const int mission = OPENRAC_GREF(s_api, uint8_t, moby + MOBY_MISSION);
        const int row = mission < TRIGGER_SLOTS && level >= 0 && level < LEVEL_COUNT
                            ? s_by_infobot[level][mission]
                            : -1;
        return row >= 0 && i32_at(vars + INFOBOT_PLANET) == rando_locations[row].vanilla.value
                   ? ap_location_id(&rando_locations[row])
                   : -1;
    }
    if (cls >= 0x5B0 && cls < 0x5B0 + AP_GOLD_WEAPON_COUNT
        && ap_client_shuffle_gold_weapons() == 1) {
        const int base = gold_case_base(moby);
        return base >= 0 ? ap_gold_case_id(base) : -1;
    }
    if (cls <= 0 || cls >= 0x800) {
        return -1;
    }
    /* Only world pickups, never the hero's equipment or a preview that shares its class. */
    if (OPENRAC_GREF(s_api, uint16_t, moby + 0x34) & 2) {
        return -1;
    }
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        /* NPC gifts have no preview before the transaction. Preserve their
         * original models, including props used by cutscenes and inventory. */
        const int pickup = l->key == ITEM_TRESPASSER || l->key == ITEM_SUCK_CANNON
                           || l->key == ITEM_HYDRODISPLACER || l->key == ITEM_PILOTS_HELMET
                           || l->key == ITEM_MAGNEBOOTS || l->key == ITEM_MORPH_O_RAY
                           || l->key == ITEM_BOLT_GRABBER;
        if (ap_is_location(l) && l->kind == LOC_GIVE && l->level == level && pickup
            && l->vanilla.kind == REWARD_ITEM
            && (presentation_class(l->vanilla.value) == cls
                || (level == LEVEL_EUDORA && l->key == ITEM_SUCK_CANNON && cls == 1120)
                || (level == LEVEL_ARIDIA && l->key == ITEM_TRESPASSER && cls == 1005)
                || (level == LEVEL_RILGAR && l->key == ITEM_HYDRODISPLACER && cls == 1016)
                || (level == LEVEL_ORXON && l->key == ITEM_MAGNEBOOTS && cls == 18)
                || (level == LEVEL_OLTANIS && l->key == ITEM_MORPH_O_RAY && cls == 1354)
                || (level == LEVEL_QUARTU && l->key == ITEM_BOLT_GRABBER && cls == 1388))
            && !had((int)i)) {
            return ap_location_id(l);
        }
    }
    return -1;
}

static void add_visual(uint32_t moby, int64_t location, float height) {
    if (!moby || s_visual_count >= OPENRAC_MOBY_VISUALS_MAX) {
        return;
    }
    char label[192];
    int icon, frame;
    const int cls = reward_class(location, label, sizeof label, &icon, &frame);
    if (cls < 0) {
        return; /* scouts not received: preserve the original display */
    }
    const int source = OPENRAC_GREF(s_api, int16_t, moby + 0xA6);
    if (cls == source && !(source >= 0x5B0 && source < 0x5BA)) {
        return;
    }
    s_visuals[s_visual_count++] = (openrac_moby_visual){moby, cls, height};
}

static int64_t selected_vendor_location(void) {
    uint8_t* menu = level_byte_at(s_disc->vendor_menu);
    const int cursor = get32(menu + VENDOR_CURSOR), count = get32(menu + VENDOR_COUNT);
    if (cursor < 0 || cursor >= count || cursor >= VENDOR_MAX_ENTRIES) {
        return -1;
    }
    uint8_t* entry = menu + VENDOR_ENTRIES + cursor * VENDOR_ENTRY_SIZE;
    if (get32(entry + VENDOR_ENTRY_AMMO)) {
        return -1;
    }
    const int item = get32(entry);
    const int row = item >= 0 && item < RANDO_ITEM_COUNT ? vendor_slot(item) : -1;
    return row >= 0 && !had(row) ? ap_location_id(&rando_locations[row]) : -1;
}

/* Retail vendor weapons have authored poses for both the inset and hologram. */
static int s_vendor_native_item = -1;
static int64_t s_vendor_initializing_location = -1;
static int s_vendor_template;
static int s_vendor_patched_item = -1;
static uint32_t s_vendor_patch_load;
static uint8_t s_vendor_saved_placement[3][0x40];
static uint8_t s_vendor_default_placement[3][RANDO_ITEM_COUNT][0x40];
static int s_vendor_defaults_ready;
static const uint32_t k_vendor_placement_offsets[3] = {0x1BC0, 0x14D0, 0xB90};
static const size_t k_vendor_placement_sizes[3] = {0x30, 0x40, 0x30};

static uint8_t* vendor_placement(int table, int item) {
    return level_byte_at(s_disc->vendor_menu - k_vendor_placement_offsets[table])
           + item * k_vendor_placement_sizes[table];
}

static void vendor_capture_defaults(void) {
    if (s_vendor_defaults_ready) {
        return;
    }
    for (int table = 0; table < 3; ++table) {
        for (int item = 0; item < RANDO_ITEM_COUNT; ++item) {
            memcpy(
                s_vendor_default_placement[table][item],
                vendor_placement(table, item),
                k_vendor_placement_sizes[table]
            );
        }
    }
    s_vendor_defaults_ready = 1;
}

static void vendor_restore_placement(void) {
    uint32_t loads = 0;
    play_phase(&loads);
    if (s_vendor_patched_item >= 0 && loads == s_vendor_patch_load) {
        for (int i = 0; i < 3; ++i) {
            memcpy(
                vendor_placement(i, s_vendor_patched_item),
                s_vendor_saved_placement[i],
                k_vendor_placement_sizes[i]
            );
        }
    }
    s_vendor_patched_item = -1;
}

static int native_vendor_reward(void) {
    const int64_t location = selected_vendor_location();
    int64_t id;
    int same;
    char label[192];
    if (location < 0 || !presentation_reward(location, &id, &same, label, sizeof label) || !same) {
        return -1;
    }
    const int gold_base = ap_gold_weapon_base(id);
    if (gold_base >= 0) {
        return gold_base;
    }
    const rando_reward reward = ap_reward_of(id);
    /* All coordinate rewards use the same vendor presentation. The approved
     * Pokitaru reference was captured in the Mine Glove slot, so initialize
     * that slot's immutable placement/scale regardless of the reward's source. */
    if (reward.kind == REWARD_PLANET) {
        return ITEM_MINE_GLOVE;
    }
    /* Approved Kerwan Gold Bolt reference: Pyrocitor's retail display rows. */
    if (reward.kind == REWARD_GOLD_BOLT) {
        return ITEM_PYROCITOR;
    }
    /* Zoomerator stands on local Z. Borrow the upright gadget placement,
     * while reward_class continues to supply its own mesh and icon. */
    if (reward.kind == REWARD_SPECIAL && reward.value == SPECIAL_ZOOMERATOR) {
        return ITEM_HELI_PACK;
    }
    if (reward.kind != REWARD_ITEM) {
        return -1;
    }
    return reward.value >= ITEM_HELI_PACK && reward.value < RANDO_ITEM_COUNT
                   && (reward.value != ITEM_WRENCH)
               ? reward.value
               : -1;
}

static void vendor_initialize_presentation(void);

static void presentation_tick(int phase) {
    if (!s_visual_api) {
        return;
    }
    uint32_t loads = 0;
    play_phase(&loads);
    if (s_dialog_load != loads) {
        s_dialog_location = -1;
        s_case_dialog_request = 0;
    }
    if (!s_reward_models) {
        return;
    }
    s_visual_count = 0;
    if (phase != OPENRAC_PLAY_FRONT_END && (ap_client_new_game_plus() >= 0)) {
        const int mode =
            get32(level_byte_at(strcmp(s_disc->version, "pal") == 0 ? 0x15F6A8u : 0x15F5C4u));
        if (mode != 5) {
            vendor_restore_placement();
            s_vendor_native_item = -1;
        }
        static openrac_moby* mobys;
        static int capacity;
        const int total = s_api->moby_list(NULL, 0);
        if (total > capacity) {
            openrac_moby* larger = (openrac_moby*)realloc(mobys, (size_t)total * sizeof *mobys);
            if (larger) {
                mobys = larger;
                capacity = total;
            }
        }
        int n = s_api->moby_list(mobys, capacity);
        if (n > capacity) {
            n = capacity;
        }
        const uint32_t pool = (uint32_t)get32(
            level_byte_at(strcmp(s_disc->version, "pal") == 0 ? 0x00160098u : 0x0015FFD8u)
        );
        /* Inventory and cutscene models can reuse world classes and addresses. */
        for (int i = 0; phase == OPENRAC_PLAY_PLAYING && pool && i < n; ++i) {
            if (mobys[i].slot < 0 || (mobys[i].flags & OPENRAC_MOBY_DEAD)) {
                continue;
            }
            const uint64_t address = (uint64_t)pool + (uint64_t)mobys[i].slot * 0x100u;
            if (address + 0x100u > 0x02000000u) {
                continue;
            }
            const uint32_t moby = (uint32_t)address;
            const int64_t location = moby_location(moby);
            if (location >= 0) {
                add_visual(moby, location, 1.0f);
            }
        }
        const int64_t vendor = selected_vendor_location();
        if (mode == 5 && vendor >= 0 && s_vendor_preview_load == loads) {
            if (native_vendor_reward() != s_vendor_native_item) {
                vendor_initialize_presentation();
            }
            /* Placement belongs to retail; geometry always belongs to the reward.
             * In particular a gold reward requests its actual gold display class,
             * never the normal weapon that supplied its placement or animation. */
            add_visual(s_vendor_preview[0], vendor, 0);
            add_visual(s_vendor_preview[1], vendor, 0);
            if ((s_vendor_native_item < 0 || s_vendor_template) && s_vendor_preview[2]
                && s_visual_count < OPENRAC_MOBY_VISUALS_MAX) {
                s_visuals[s_visual_count++] = (openrac_moby_visual){s_vendor_preview[2], -1, 0};
            }
        }
    }
    s_visual_api->moby_visuals(s_mod, s_visuals, s_visual_count);
}

static void vendor_initialize_presentation(void) {
    /* Retail indexes the selected item without bounds checks. An empty rebuild or
     * consumed final row must never reach that initializer. */
    uint8_t* selected_menu = level_byte_at(s_disc->vendor_menu);
    const int count = get32(selected_menu + VENDOR_COUNT);
    const int cursor = get32(selected_menu + VENDOR_CURSOR);
    if (count <= 0 || count > VENDOR_MAX_ENTRIES || cursor < 0 || cursor >= count
        || get32(selected_menu + VENDOR_ENTRIES + cursor * VENDOR_ENTRY_SIZE) < 0
        || get32(selected_menu + VENDOR_ENTRIES + cursor * VENDOR_ENTRY_SIZE) >= RANDO_ITEM_COUNT) {
        vendor_restore_placement();
        s_vendor_native_item = -1;
        return;
    }
    const int64_t location = selected_vendor_location();
    vendor_restore_placement();
    s_vendor_native_item = native_vendor_reward();
    uint8_t* entry = NULL;
    int source = -1, display = s_vendor_native_item;
    s_vendor_template = 0;
    uint8_t saved_display[3][0x40];
    int restore_display = 0;
    if (s_vendor_native_item >= 0) {
        vendor_capture_defaults();
        /* Non-vendor items have empty placement records. Give them the normal
         * Devastator display machinery without creating an unsupported game moby. */
        /* Nonzero bytes do not establish a usable retail vendor pose: gadget
         * rows may contain unrelated data. Only retail vendor weapons own one. */
        switch (s_vendor_native_item) {
            case ITEM_BOMB_GLOVE: case ITEM_PYROCITOR: case ITEM_BLASTER:
            case ITEM_GLOVE_OF_DOOM: case ITEM_TAUNTER: case ITEM_MINE_GLOVE:
            case ITEM_DEVASTATOR: case ITEM_WALLOPER: case ITEM_VISIBOMB:
            case ITEM_DECOY_GLOVE: case ITEM_DRONE_DEVICE: case ITEM_TESLA_CLAW:
                s_vendor_template = 0;
                break;
            default:
                s_vendor_template = 1;
                break;
        }
        if (s_vendor_template) {
            display = ITEM_DEVASTATOR;
        }
        uint8_t* menu = level_byte_at(s_disc->vendor_menu);
        entry = menu + VENDOR_ENTRIES + get32(menu + VENDOR_CURSOR) * VENDOR_ENTRY_SIZE;
        source = get32(entry);
        /* A reward item may itself be another randomized vendor's source slot.
         * Always initialize from the immutable retail records, never that slot's
         * currently borrowed placement (e.g. Devastator's row displaying R.Y.N.O.). */
        for (int i = 0; i < 3; ++i) {
            memcpy(
                vendor_placement(i, display),
                s_vendor_default_placement[i][display],
                k_vendor_placement_sizes[i]
            );
        }
        const int gadget =
            s_vendor_template
            && !(
                s_vendor_native_item >= ITEM_SUCK_CANNON && s_vendor_native_item <= ITEM_DECOY_GLOVE
                && s_vendor_native_item != ITEM_SWINGSHOT
                && s_vendor_native_item != ITEM_HYDRODISPLACER
            );
        if (gadget) {
            for (int i = 0; i < 3; ++i) {
                memcpy(
                    saved_display[i],
                    s_vendor_default_placement[i][display],
                    k_vendor_placement_sizes[i]
                );
            }
            /* Gadgets stand on local Z; the native display still supplies its spin. */
            set32(vendor_placement(0, display) + 0x14, 0);
            set32(vendor_placement(2, display) + 0x14, 0);
            restore_display = 1;
        }
        if (source != display || gadget) {
            play_phase(&s_vendor_patch_load);
            s_vendor_patched_item = source;
            for (int i = 0; i < 3; ++i) {
                memcpy(
                    s_vendor_saved_placement[i],
                    s_vendor_default_placement[i][source],
                    k_vendor_placement_sizes[i]
                );
                if (source != display) {
                    memcpy(
                        vendor_placement(i, source),
                        vendor_placement(i, display),
                        k_vendor_placement_sizes[i]
                    );
                }
            }
        }
        /* Only model initialization sees this id. Actual prices and checks keep the source row. */
        set32(entry, display);
    }
    /* The banner is initialized while entry carries the render-only display id. */
    s_vendor_initializing_location = location;
    s_vendor_initialize();
    s_vendor_initializing_location = -1;
    if (entry) {
        set32(entry, source);
    }
    if (restore_display && source != display) {
        for (int i = 0; i < 3; ++i) {
            memcpy(vendor_placement(i, display), saved_display[i], k_vendor_placement_sizes[i]);
        }
    }
    play_phase(&s_vendor_preview_load);
    const uint32_t objects = (uint32_t)get32(level_byte_at(s_disc->vendor_menu) + 0x20);
    s_vendor_preview[0] = objects ? objects + 0x100u : 0;
    s_vendor_preview[1] = objects ? objects + 0x300u : 0;
    s_vendor_preview[2] = objects ? objects + 0x200u : 0;
}

static void draw_icons_presentation(void) {
    ++s_draw_vendor_icons;
    s_draw_icons();
    --s_draw_vendor_icons;
}

static void draw_details_presentation(void) {
    ++s_draw_vendor_details;
    s_draw_details();
    --s_draw_vendor_details;
}

static int32_t icon_frame_presentation(int32_t icon, int32_t frame) {
    if (s_draw_vendor_icons && frame == 0) {
        for (int item = 0; item < RANDO_ITEM_COUNT; ++item) {
            uint16_t original;
            memcpy(&original, presentation_item(item) + 0x38, sizeof original);
            const int row = vendor_slot(item);
            const int64_t location =
                row >= 0 && !had(row) ? ap_location_id(&rando_locations[row]) : -1;
            if (original != icon || location < 0) {
                continue;
            }
            char label[192];
            int target, target_frame;
            if (reward_class(location, label, sizeof label, &target, &target_frame) >= 0
                && target) {
                return s_icon_frame(target, target_frame);
            }
        }
    }
    return s_icon_frame(icon, frame);
}

static void draw_sprite_presentation(
    int32_t tex, int32_t x, int32_t y, int32_t w, int32_t h, int32_t alpha
) {
    const uint32_t address = strcmp(s_disc->version, "pal") == 0 ? 0x161000u : 0x160F00u;
    uint8_t* cursor = byte_at(address);
    const uint32_t before = (uint32_t)get32(cursor);
    const int gold = s_draw_vendor_icons && tex == s_icon_frame(0x754F, 0);
    const uint32_t previous = gold ? s_visual_api->sprite_tint(s_mod, 0x20647Fu) : UINT32_MAX;
    s_draw_sprite(tex, x, y, w, h, alpha);
    if (gold) {
        s_visual_api->sprite_tint(s_mod, previous);
    }
    if (s_draw_vendor_icons && before && (uint32_t)get32(cursor) == before + 0x60u
        && tex == s_icon_frame(0x754F, 0)) {
        /* Gold Bolt icon uses the retail bolt sprite, tinted gold rather than grey. */
        put_i32(before + 0x30u, (alpha << 24) | 0x20647F);
    }
}

static int32_t try_dialog_presentation(uint32_t moby, uint32_t request) {
    const int64_t location = npc_location(moby);
    const int64_t previous = s_dialog_location;
    const uint32_t previous_case = s_case_dialog_request;
    s_case_dialog_request = moby && OPENRAC_GREF(s_api, int16_t, moby + 0xA6) == 304 ? request : 0;
    s_dialog_location = location;
    play_phase(&s_dialog_load);
    ++s_in_dialog_try;
    /* Through the NPC reward text, which marks the interactions whose prompts it owns. */
    const int result = npc_reward_try(moby, request);
    --s_in_dialog_try;
    if (!result) {
        s_dialog_location = previous;
        s_case_dialog_request = previous_case;
    }
    return result;
}

static uint32_t message_presentation(int32_t id) {
    /* One owner per prompt: an NPC interaction the NPC reward text knows is named there (its
     * collected and hint rules), never rewritten again here. Gold cases, the vendor's details
     * and an NPC dialog already open stay this file's. */
    if (!s_draw_vendor_details && s_in_dialog_try && npc_reward_text_owns()) {
        return npc_reward_message(id);
    }
    const uint32_t original = s_message_string(id);
    if (!original || !s_visual_api || (ap_client_new_game_plus() < 0)) {
        return original;
    }
    const char* text = (const char*)OPENRAC_G(s_api, original);
    const uint32_t game_mode = strcmp(s_disc->version, "pal") == 0 ? 0x15F6A8u : 0x15F5C4u;
    const int mode = get32(level_byte_at(game_mode));
    const int vendor_text = s_draw_vendor_details || (mode == 5 && !s_in_dialog_try);
    const int64_t location = s_vendor_initializing_location >= 0 ? s_vendor_initializing_location
        : vendor_text ? selected_vendor_location() : s_dialog_location;
    char label[192];
    int icon, frame;
    if (location < 0 || reward_class(location, label, sizeof label, &icon, &frame) < 0) {
        return original;
    }
    /* Cases use menu kind 6, with a single retail price placeholder. Their
     * localized text need not contain the vendor/NPC "Buy ... for ..." pattern. */
    if (!s_draw_vendor_details && s_case_dialog_request) {
        const uint32_t entries = OPENRAC_GREF(s_api, uint32_t, s_case_dialog_request + 0x3C);
        const int index = OPENRAC_GREF(s_api, int16_t, s_case_dialog_request + 0x36);
        if (entries && index >= 0 && index < 64) {
            const uint32_t entry = entries + (uint32_t)index * 0x1C;
            const int kind = OPENRAC_GREF(s_api, int16_t, entry + 8);
            if (id == OPENRAC_GREF(s_api, uint16_t, entry) && (kind == 6 || strstr(text, "Buy "))) {
                char prompt[120], body[110], price[24] = "%d";
                if (kind != 6) {
                    const int slot = i32_at(s_case_dialog_request + 0x40);
                    if (slot < 0 || slot >= 20) {
                        return original;
                    }
                    const uint32_t table =
                        strcmp(s_disc->version, "pal") == 0 ? 0x1DEDE0u : 0x1DEB10u;
                    const int base = get32(
                        (uint8_t*)OPENRAC_G(s_api, s_api->relocate_level_data(1, table)) + slot * 4
                    );
                    if (base < 0 || base >= RANDO_ITEM_COUNT) {
                        return original;
                    }
                    const uint32_t prices =
                        strcmp(s_disc->version, "pal") == 0 ? 0x1C47B0u : 0x1C4530u;
                    const unsigned bolts = OPENRAC_GREF(
                        s_api,
                        uint16_t,
                        s_api->relocate_level_data(1, prices) + (uint32_t)base * 0x18 + 0x14
                    );
                    if (bolts >= 1000) {
                        snprintf(price, sizeof price, "%u,%03u", bolts / 1000, bolts % 1000);
                    } else {
                        snprintf(price, sizeof price, "%u", bolts);
                    }
                }
                gold_case_prompt(body, sizeof body, label, price);
                const char* buy = strstr(text, "Buy ");
                const size_t prefix = buy && buy - text < 16 ? (size_t)(buy - text) : 0;
                snprintf(prompt, sizeof prompt, "%.*s%s", (int)prefix, text, body);
                const uint32_t address = s_visual_api->guest_text(s_mod, prompt);
                return address ? address : original;
            }
        }
    }
    if (vendor_text) {
        uint8_t* menu = level_byte_at(s_disc->vendor_menu);
        const int cursor = get32(menu + VENDOR_CURSOR);
        const int item = get32(menu + VENDOR_ENTRIES + cursor * VENDOR_ENTRY_SIZE);
        const uint32_t banner_table = strcmp(s_disc->version, "pal") == 0 ? 0x1C2488u : 0x1C2108u;
        const int banner = get32(level_byte_at(banner_table) + item * 0x14 + 0x10);
        if (id == get32(presentation_item(item)) || id == banner) {
            const uint32_t replacement = s_visual_api->guest_text(s_mod, label);
            return replacement ? replacement : original;
        }
    }
    /* Retain the retail cost and any format arguments/buttons, replacing only the reward name. */
    if (!s_draw_vendor_details && !s_in_dialog_try && mode != 2 && mode != 4) {
        return original;
    }
    const char* buy = strstr(text, "Buy ");
    const char* cost = buy ? strstr(buy, " for ") : NULL;
    if (cost && cost - text < 256) {
        char replacement[512], escaped[384];
        const size_t prefix = (size_t)(buy - text);
        const size_t suffix = strlen(cost);
        /* process_menu_entry formats into the retail 128-byte HUD panel. */
        if (prefix + suffix + 5 >= 120) {
            return original;
        }
        const size_t budget = 120 - prefix - suffix - 5;
        size_t at = 0;
        const int format = strchr(text, '%') != NULL;
        for (size_t i = 0; label[i] && at + 2 < sizeof escaped && at + 2 < budget; ++i) {
            if (format && label[i] == '%') {
                escaped[at++] = '%';
            }
            escaped[at++] = label[i];
        }
        escaped[at] = 0;
        snprintf(
            replacement, sizeof replacement, "%.*sBuy %s%s", (int)(buy - text), text, escaped, cost
        );
        const uint32_t address = s_visual_api->guest_text(s_mod, replacement);
        return address ? address : original;
    }
    return original;
}

static int presentation_hooks(void) {
    s_presentation_text = 0;
    s_reward_models = s_api->setting_bool(s_mod, "reward_models");
    s_reward_text = s_api->setting_bool(s_mod, "reward_text");
    s_visual_api = ap_visual_host(s_api);
    if (!s_visual_api || !s_visual_api->moby_visuals || !s_visual_api->guest_text
        || !s_visual_api->sprite_tint) {
        return 1;
    }
#define PRESENT_HOOK(id, sig, fn, next)                                                            \
    if (!take(s_api->hook(s_mod, id, sig, (void*)fn, (void**)&next), id))                          \
    return 0
    if (s_reward_models) {
        PRESENT_HOOK(
            "rac1.vendor.initialize_selected",
            "void()",
            vendor_initialize_presentation,
            s_vendor_initialize
        );
        PRESENT_HOOK("rac1.vendor.draw_icons", "void()", draw_icons_presentation, s_draw_icons);
        PRESENT_HOOK(
            "rac1.ui.sprite.draw",
            "void(i32,i32,i32,i32,i32,i32)",
            draw_sprite_presentation,
            s_draw_sprite
        );
        PRESENT_HOOK("rac1.ui.icon_frame", "i32(i32,i32)", icon_frame_presentation, s_icon_frame);
    }
    if (s_reward_text) {
        PRESENT_HOOK(
            "rac1.vendor.draw_details", "void()", draw_details_presentation, s_draw_details
        );
        PRESENT_HOOK("rac1.npc.try_dialog", "i32(u32,u32)", try_dialog_presentation, s_try_dialog);
        PRESENT_HOOK("rac1.text.msg_string", "i32(i32)", message_presentation, s_message_string);
        npc_reward_text_attach(s_try_dialog, s_message_string);
        s_presentation_text = 1;
    }
#undef PRESENT_HOOK
    return 1;
}
