/* SPDX-License-Identifier: GPL-3.0-or-later
 * Retail skips the first-prize handoff when the original prize is already owned.
 * Observe a genuine first-place finish instead of hiding inventory during player
 * updates: the Zoomerator must still enable its normal tricks and boost.
 * PAL/NTSC share these Hero offsets (update_hero_hoverboard_race_progress).
 */
static int hoverboard_race_row(void) {
    if (!kerwan_randomizer_active()) {
        return -1;
    }
    if (current_level() == LEVEL_RILGAR && menu_mode() == 1 && s_menu_save) {
        return menu_location_row(SPECIAL_ZOOMERATOR);
    }
    if (current_level() == LEVEL_KALEBO) {
        return s_by_item[ITEM_HOLOGUISE];
    }
    return -1;
}

static int hoverboard_race_lap(void) {
    int16_t lap;
    memcpy(&lap, byte_at(s_disc->hero + 0x89A), sizeof lap);
    return lap;
}

static void hoverboard_race_player(void (*original)(void)) {
    menu_refresh();
    const int row = hoverboard_race_row();
    const int race_level = current_level();
    const int original_prize =
        race_level == LEVEL_RILGAR
            ? *menu_owned(SPECIAL_ZOOMERATOR)
            : *owned(ITEM_HOLOGUISE) || *byte_at(s_disc->obtained + ITEM_HOLOGUISE);
    const int pending = row >= 0 && !had(row) && original_prize
                        && get32(byte_at(s_disc->hero + 0x208C)) == 0x16
                        && get32(byte_at(s_disc->hero + 0x86C)) != 0 && hoverboard_race_lap() < 3;
    original();
    if (!pending || current_level() != race_level || had(row) || hoverboard_race_lap() < 3
        || get32(byte_at(s_disc->hero + 0x8C0)) != 1) {
        return;
    }
    if (race_level == LEVEL_RILGAR) {
        s_menu_checks |= 1u << SPECIAL_ZOOMERATOR;
        menu_store();
        menu_collected(SPECIAL_ZOOMERATOR);
    } else {
        /* Both mods' normal give hook marks this location and sends its reward. */
        item_give(ITEM_HOLOGUISE, 0);
    }
}
