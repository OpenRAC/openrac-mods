/* Starting routes exclude optional gold/skill challenges and the final boss.
 * Only ship-side vendors can supply the one missing route item. */
static int start_ship_vendor(int level) {
    return level == LEVEL_EUDORA || level == LEVEL_GASPAR || level == LEVEL_POKITARU
        || level == LEVEL_HOVEN;
}
static int start_route(const rando_location* l) {
    return l->kind != LOC_VENDOR && l->kind != LOC_START && l->kind != LOC_GOLD
        && l->kind != LOC_GOLD_CASE && !(l->flags & (LOCF_GOAL | LOCF_COSTLY))
        && l->level > LEVEL_VELDIN && l->level < LEVEL_VELDIN2;
}
/* Does this planet have a route with exactly these available items? */
static int start_route_open(int level, int difficulty, uint64_t items) {
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];
        if (l->level == level && start_route(l) && !l->planets
            && !(rando_requirement(difficulty, i) & ~items)
            && (!l->any || (l->any & items))) return 1;
    }
    return 0;
}
