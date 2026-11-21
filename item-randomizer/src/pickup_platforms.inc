/* Retail progression platforms test gadget ownership. In a randomized game,
 * only collecting the gadget's own location should activate its platform. */
static void (*s_blarg_bridge_update)(openrac_gaddr);
static void (*s_aridia_elevator_update)(openrac_gaddr);

static int platform_pickup_row(int item, int level) {
    const int row = s_by_item[item];
    return row >= 0 && rando_locations[row].level == level ? row : -1;
}

static void blarg_bridge_update(openrac_gaddr moby) {
    const int row = platform_pickup_row(ITEM_HYDRODISPLACER, LEVEL_BLARG);
    if (!kerwan_randomizer_active() || current_level() != LEVEL_BLARG || row < 0) {
        s_blarg_bridge_update(moby);
        return;
    }
    swap_push(owned(ITEM_HYDRODISPLACER), row);
    /* Ratchet can reach this pickup with an early O2 Mask too. Only borrow
     * Clank's activation test in the waiting state; keep the native animation. */
    const int borrow_clank = had(row)
        && OPENRAC_GREF(s_api, uint8_t, moby + MOBY_STATE) == 1;
    if (borrow_clank) swap_push(byte_at(s_disc->hero_clank), -2 /* force owned in both mods */);
    s_blarg_bridge_update(moby);
    if (borrow_clank) swap_pop();
    swap_pop();
}

static void aridia_elevator_update(openrac_gaddr moby) {
    const int row = platform_pickup_row(ITEM_TRESPASSER, LEVEL_ARIDIA);
    if (!kerwan_randomizer_active() || current_level() != LEVEL_ARIDIA || row < 0) {
        s_aridia_elevator_update(moby);
        return;
    }
    /* The rider test remains native; only its automatic gadget test changes. */
    swap_push(owned(ITEM_TRESPASSER), row);
    s_aridia_elevator_update(moby);
    swap_pop();
}

static int pickup_platform_hooks(void) {
    return take(s_api->hook(s_mod, "rac1.moby.drawbridge.update", "void(u32)",
        (void*)blarg_bridge_update, (void**)&s_blarg_bridge_update), "Blarg drawbridge")
        && take(s_api->hook(s_mod, "rac1.moby.elevator_651.update", "void(u32)",
        (void*)aridia_elevator_update, (void**)&s_aridia_elevator_update), "Aridia elevator");
}
