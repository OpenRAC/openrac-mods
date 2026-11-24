/* The native ship already exists on Novalis, but retail hides/deactivates it
 * before the ship quest. Enable that ship without granting coordinates or
 * spending the quest's randomized reward. */
static void (*s_novalis_ship_initialize)(openrac_gaddr);
static uint32_t s_novalis_ship_load;
static openrac_gaddr s_novalis_ship;

static void novalis_ship_tick(int phase, uint32_t load) {
    if (load != s_novalis_ship_load) {
        s_novalis_ship_load = load;
        s_novalis_ship = 0;
    }
    if (phase != OPENRAC_PLAY_PLAYING || current_level() != LEVEL_NOVALIS
        || !return_ship_active() || !s_novalis_ship_initialize) return;
    const uint32_t record = strcmp(s_disc->version, "pal") == 0 ? 0x13E130u : 0x13E030u;
    const openrac_gaddr ship = (openrac_gaddr)get32(byte_at(record));
    if (!ship || ship > 0x02000000u - 0x100u) return;
    const int cls = OPENRAC_GREF(s_api, int16_t, ship + 0xA6);
    if (cls < 531 || cls > 533 || OPENRAC_GREF(s_api, uint8_t, ship + 0x20) >= 0xFD) return;
    if (ship != s_novalis_ship || (OPENRAC_GREF(s_api, uint16_t, ship + 0x34) & 3)) {
        s_novalis_ship_initialize(ship);
        s_novalis_ship = ship;
    }
}

static void novalis_ship_init(void) {
    s_novalis_ship_load = UINT32_MAX;
    s_novalis_ship = 0;
    s_novalis_ship_initialize = (void (*)(openrac_gaddr))s_api->function(
        strcmp(s_disc->version, "pal") == 0 ? "func_L00_0028FB78" : "FUN_L00_0028e8a0");
}
