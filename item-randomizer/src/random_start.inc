/* Skip the tutorial using the native departure/arrival path. Clank is part of
 * Ratchet's normal off-Veldin setup; no movement gadgets are granted. */
static uint32_t s_random_start_load = UINT32_MAX;
static void random_start_tick(int phase, uint32_t load) {
    const int target = random_start_level();
    if (target > LEVEL_VELDIN && phase == OPENRAC_PLAY_PLAYING
        && current_level() == LEVEL_NOVALIS) {
        const int pal = strcmp(s_disc->version, "pal") == 0;
        const uint32_t record = pal ? 0x13E130u : 0x13E030u;
        const openrac_gaddr ship = (openrac_gaddr)get32(byte_at(record));
        if (ship && ship <= 0x02000000u - 0x100u) {
            const int cls = OPENRAC_GREF(s_api, int16_t, ship + 0xA6);
            if (cls >= 531 && cls <= 533 && OPENRAC_GREF(s_api, uint8_t, ship + 0x20) < 0xFD
                && (OPENRAC_GREF(s_api, uint16_t, ship + 0x34) & 3)) {
                void (*enable)(openrac_gaddr) = (void (*)(openrac_gaddr))s_api->function(
                    pal ? "func_L00_0028FB78" : "FUN_L00_0028e8a0");
                if (enable) enable(ship);
            }
        }
    }
    if (target <= LEVEL_VELDIN || target >= LEVEL_VELDIN2
        || current_level() != LEVEL_VELDIN || load == s_random_start_load
        || (phase != OPENRAC_PLAY_PLAYING && phase != OPENRAC_PLAY_SCENE)) return;
    s_planet_unlock(target);
    if (s_api->play_travel && s_api->play_travel(s_mod, target) == OPENRAC_HOOK_OK) {
        s_random_start_load = load;
        log_line(OPENRAC_LOG_INFO, "Random starting planet: skipping Veldin tutorial");
    }
}
