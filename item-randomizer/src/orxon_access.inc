/* Orxon removes Ratchet's enemies and force fields while Clank is playing.
 * An early O2 Mask allows a switch back in the same load, so replay the normal
 * arrival before Ratchet explores that incomplete version of the planet. */
static uint32_t s_orxon_load;
static int s_orxon_clank_seen;

static void orxon_access_tick(int phase, uint32_t load, int active) {
    if (!active || current_level() != LEVEL_ORXON
        || phase == OPENRAC_PLAY_FRONT_END || phase == OPENRAC_PLAY_LOADING) {
        s_orxon_clank_seen = 0;
        s_orxon_load = load;
        return;
    }
    if (load != s_orxon_load) {
        s_orxon_load = load;
        s_orxon_clank_seen = 0;
    }
    const int complete = *byte_at(s_disc->triggers + LEVEL_ORXON * TRIGGER_SLOTS
                                 + ORXON_CLANK_MISSION) == TRIGGER_DONE;
    const int clank = *byte_at(s_disc->hero_clank) != 0;
    if (clank && !complete) s_orxon_clank_seen = 1;

    /* Finish the infobot scene, check reporting and item delivery first. A
     * completed save or an ordinary Ratchet visit must never trigger a loop. */
    if (s_orxon_clank_seen && complete && !clank && *owned(ITEM_O2_MASK)
        && phase == OPENRAC_PLAY_PLAYING && s_api->play_travel
        && s_api->play_travel(s_mod, LEVEL_ORXON) == OPENRAC_HOOK_OK) {
        s_orxon_clank_seen = 0;
        log_line(OPENRAC_LOG_INFO, "Orxon: Clank complete; reloading Ratchet's arrival");
    }
}
