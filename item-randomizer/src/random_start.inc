/* Skip the tutorial using the native departure/arrival path. Clank is part of
 * Ratchet's normal off-Veldin setup; no movement gadgets are granted. */
static uint32_t s_random_start_load = UINT32_MAX;
static void random_start_tick(int phase, uint32_t load) {
    const int target = random_start_level();
    /* Novalis's ship is shown early by novalis_ship.inc. */
    if (target <= LEVEL_VELDIN || target >= LEVEL_VELDIN2
        || current_level() != LEVEL_VELDIN || load == s_random_start_load
        || (phase != OPENRAC_PLAY_PLAYING && phase != OPENRAC_PLAY_SCENE)) return;
    s_planet_unlock(target);
    if (s_api->play_travel && s_api->play_travel(s_mod, target) == OPENRAC_HOOK_OK) {
        s_random_start_load = load;
        log_line(OPENRAC_LOG_INFO, "Random starting planet: skipping Veldin tutorial");
    }
}
