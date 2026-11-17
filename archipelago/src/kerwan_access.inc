/* SPDX-License-Identifier: GPL-3.0-or-later
 * Kerwan access uses real inventory, independently of the NPC collection view.
 * PAL/NTSC share these Moby and NPC pvar layouts (taxi_and_mine_layer.c).
 */
static uint32_t s_kerwan_load;
static openrac_gaddr s_helga_taxi, s_al_door;
static int32_t s_helga_exit_volume;
static int s_helga_taxi_disabled;

static int kerwan_access_ready(void) {
    uint32_t loads = 0;
    const int phase = play_phase(&loads);
    if (loads != s_kerwan_load || current_level() != LEVEL_KERWAN) {
        s_kerwan_load = loads;
        s_helga_taxi = s_al_door = 0;
        s_helga_taxi_disabled = 0;
    }
    return (phase == OPENRAC_PLAY_PLAYING || phase == OPENRAC_PLAY_SCENE
            || phase == OPENRAC_PLAY_MENU) && current_level() == LEVEL_KERWAN
        && kerwan_randomizer_active();
}

static uint8_t* kerwan_vars(openrac_gaddr moby) {
    const uint32_t vars = moby ? (uint32_t)i32_at(moby + 0x78u) : 0;
    return vars ? (uint8_t*)OPENRAC_G(s_api, vars) : NULL;
}

static openrac_gaddr kerwan_link(const uint8_t* vars, unsigned offset, uint16_t cls) {
    if (!vars) return 0;
    const int32_t slot = get32(vars + offset);
    const uint32_t pool = (uint32_t)get32(level_byte_at(s_disc->moby_base));
    if (slot < 0 || slot > 0xFFFF || !pool) return 0;
    const openrac_gaddr moby = pool + (uint32_t)slot * 0x100u;
    uint16_t actual;
    memcpy(&actual, (uint8_t*)OPENRAC_G(s_api, moby) + 0xA6u, sizeof actual);
    return actual == cls ? moby : 0;
}

static void kerwan_access_tick(void) {
    if (!kerwan_access_ready() || play_phase(NULL) != OPENRAC_PLAY_PLAYING) return;
    if (s_al_door && *owned(ITEM_HELI_PACK)) {
        uint8_t* state = (uint8_t*)OPENRAC_G(s_api, s_al_door) + 0x20u;
        /* Opening by inventory does not mark Al's purchase collected. */
        if (*state == 0) *state = 1;
    }
    if (!*owned(ITEM_SWINGSHOT)) {
        s_helga_taxi_disabled = 0;
    } else if (s_helga_taxi && !s_helga_taxi_disabled) {
        uint8_t* vars = kerwan_vars(s_helga_taxi);
        if (vars) {
            /* The same reset Helga performs after a retail Swingshot purchase. */
            set32(vars + 0x7C, 0);
            set32(vars + 0x8C, 0);
            set32(vars + 0xA0, s_helga_exit_volume);
            s_helga_taxi_disabled = 1;
        }
    }
}

static void helga_update(openrac_gaddr moby) {
    uint8_t* taxi = NULL;
    int32_t started = 0, autostart = 0, volume = 0;
    if (kerwan_access_ready()) {
        const uint8_t* vars = kerwan_vars(moby);
        const openrac_gaddr link = kerwan_link(vars, 0xD4, 1012);
        if (link) {
            s_helga_taxi = link;
            s_helga_exit_volume = get32(vars + 0xD8);
            taxi = kerwan_vars(link);
            if (taxi) {
                started = get32(taxi + 0x7C);
                autostart = get32(taxi + 0x8C);
                volume = get32(taxi + 0xA0);
            }
        }
    }
    run_shown(s_helga_update, moby, ITEM_SWINGSHOT);
    /* The real ownership byte has been restored, including any reward just received.
     * Keep normal course activation; undo only Helga's purchase/dialogue taxi reset. */
    if (taxi && !*owned(ITEM_SWINGSHOT) && get32(taxi + 0x7C) == 0
        && get32(taxi + 0x8C) == 0 && get32(taxi + 0xA0) == s_helga_exit_volume) {
        set32(taxi + 0x7C, started);
        set32(taxi + 0x8C, autostart);
        set32(taxi + 0xA0, volume);
    }
    /* An uncollected Helga can activate the taxi again during dialogue even if
     * the Swingshot came from elsewhere. Reapply ownership after her update. */
    if (taxi && *owned(ITEM_SWINGSHOT)) {
        set32(taxi + 0x7C, 0);
        set32(taxi + 0x8C, 0);
        set32(taxi + 0xA0, s_helga_exit_volume);
        s_helga_taxi_disabled = 1;
    }
    kerwan_access_tick();
}

static void big_al_update(openrac_gaddr moby) {
    if (kerwan_access_ready()) {
        s_al_door = kerwan_link(kerwan_vars(moby), 0x144, 0x3E5);
    }
    run_shown(s_big_al_update, moby, ITEM_HELI_PACK);
    kerwan_access_tick();
}
