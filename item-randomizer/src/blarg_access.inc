/* Blarg's Clank-section beam gates normally wait for linked Gadgetbots.
 * Ratchet can enter with an early O2 Mask, but cannot issue those commands.
 * State 3 uses retail removal/grid/audio cleanup without awarding the puzzle skill point. */
static void (*s_blarg_beam_update)(openrac_gaddr);
static void blarg_beam_update(openrac_gaddr moby) {
    uint8_t* state = (uint8_t*)OPENRAC_G(s_api, moby) + 0x20u;
    if (blarg_o2_access_active() && current_level() == LEVEL_BLARG
        && *owned(ITEM_O2_MASK) && (*state == 0 || *state == 1)) {
        uint32_t no_collision = 0;
        memcpy((uint8_t*)OPENRAC_G(s_api, moby) + 0x94u, &no_collision, sizeof no_collision);
        *state = 3;
    }
    s_blarg_beam_update(moby);
}

/* Fred's purchase closes the door back to the shuttle (the hangar door in his pvars at +0x164)
 * and leaves him waiting for the hero to grind, which opens it again and removes him
 * (fred_update, func_L06_003024C8: state 5). When his reward was not the Grindboots and they
 * are not owned, the hero cannot grind there, so the door would stay shut. Open it the way the
 * grind does (set_moby_and_links_to_states_4_and_5, func_L06_002F6790): a closed door (state 2)
 * goes to 4, its angles to 0 and its seven pieces to 5. Fred stays, as before the purchase. */
enum {
    FRED_WAIT_FOR_GRIND = 5,
    FRED_SHUTTLE_DOOR = 0x164,
    HANGAR_DOOR_CLOSED = 2,
    HANGAR_DOOR_OPENING = 4,
    HANGAR_DOOR_PIECE_OPENING = 5,
    HANGAR_DOOR_PIECES = 7
};

static uint32_t blarg_u32(openrac_gaddr address) {
    uint32_t value;
    memcpy(&value, (uint8_t*)OPENRAC_G(s_api, address), sizeof value);
    return value;
}

static void blarg_set_u32(openrac_gaddr address, uint32_t value) {
    memcpy((uint8_t*)OPENRAC_G(s_api, address), &value, sizeof value);
}

static void blarg_open_shuttle_door(openrac_gaddr fred) {
    const openrac_gaddr vars = blarg_u32(fred + 0x78u);
    const openrac_gaddr pool = blarg_moby_pool();
    if (!vars || !pool) return;
    const int32_t slot = (int32_t)blarg_u32(vars + FRED_SHUTTLE_DOOR);
    if (slot < 0 || slot > 0xFFFF) return;
    const openrac_gaddr door = pool + (uint32_t)slot * 0x100u;
    uint8_t* state = (uint8_t*)OPENRAC_G(s_api, door) + 0x20u;
    const openrac_gaddr door_vars = blarg_u32(door + 0x78u);
    if (*state != HANGAR_DOOR_CLOSED || !door_vars) return;
    openrac_gaddr pieces[HANGAR_DOOR_PIECES];
    for (int i = 0; i < HANGAR_DOOR_PIECES; ++i) {
        pieces[i] = blarg_u32(door_vars + 0x24u + 4u * (uint32_t)i);
        if (!pieces[i]) return;
    }
    *((uint8_t*)OPENRAC_G(s_api, door) + 0xBCu) = 1;
    *state = HANGAR_DOOR_OPENING;
    blarg_set_u32(door_vars + 0x14u, 0); /* angle_goal */
    blarg_set_u32(door_vars + 0x18u, 0); /* angle */
    for (int i = 0; i < HANGAR_DOOR_PIECES; ++i) {
        *((uint8_t*)OPENRAC_G(s_api, pieces[i]) + 0x20u) = HANGAR_DOOR_PIECE_OPENING;
    }
}

static void (*s_blarg_fred_update)(openrac_gaddr);
static void blarg_fred_update(openrac_gaddr moby) {
    s_blarg_fred_update(moby);
    /* Owned Grindboots (his own reward or another's) keep the game's grind-to-open path. */
    if (blarg_o2_access_active() && current_level() == LEVEL_BLARG && !*owned(ITEM_GRINDBOOTS)
        && *((uint8_t*)OPENRAC_G(s_api, moby) + 0x20u) == FRED_WAIT_FOR_GRIND) {
        blarg_open_shuttle_door(moby);
    }
}

static int take(int result, const char* point);
static int blarg_access_hooks(void) {
    return take(s_api->hook(s_mod, "rac1.moby.class_1035.update", "void(u32)",
               (void*)blarg_beam_update, (void**)&s_blarg_beam_update), "Blarg beam gate")
        && take(s_api->hook(s_mod, "rac1.moby.moby1105.update", "void(u32)",
               (void*)blarg_fred_update, (void**)&s_blarg_fred_update), "Blarg Fred's door");
}
