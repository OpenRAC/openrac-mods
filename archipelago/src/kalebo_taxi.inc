/* Kalebo's taxi normally unlocks from Map-o-Matic ownership. Its return trip
 * belongs to the help-desk check, even when the gadget came from elsewhere. */
static void (*s_kalebo_taxi_update)(openrac_gaddr);
static void kalebo_taxi_update(openrac_gaddr moby) {
    const int row = s_by_item[ITEM_MAP_O_MATIC];
    const int mode = kalebo_taxi_mode();
    if (current_level() == LEVEL_KALEBO && row >= 0 && mode < 0) return;
    if (mode != 1 || current_level() != LEVEL_KALEBO || row < 0) {
        s_kalebo_taxi_update(moby);
        return;
    }
    /* Cover both initialization and the native wait-for-enable state. Keep
     * authored stops, travel paths, animation and unrelated stop availability. */
    swap_push(owned(ITEM_MAP_O_MATIC), row);
    s_kalebo_taxi_update(moby);
    swap_pop();
}

static int kalebo_taxi_hooks(void) {
    return take(s_api->hook(s_mod, "rac1.moby.path_pickup.update", "void(u32)",
        (void*)kalebo_taxi_update, (void**)&s_kalebo_taxi_update), "Kalebo taxi");
}
