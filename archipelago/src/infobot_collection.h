#ifndef INFOBOT_COLLECTION_H
#define INFOBOT_COLLECTION_H
/* queue_planet_unlock_message uses these message IDs on PAL and NTSC.
 * Resolve by the original destination, not the current level: the shuttle can
 * display its queued message after travelling to another planet. */
static int infobot_collection_row(int message) {
    static const int messages[] = {0, 6, 0x3F1, 0x3F2, 0xBC6, 0x177B, 0xFA6,
        0x1394, 0x1B58, 0x1F47, 0x1F48, 0x2719, 0x271A, 0x2EE5, 0x32C9,
        0x36B1, 0x3AA1, 0x3AA2, 0x4269};
    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location *l = &rando_locations[i];
        if ((l->kind == LOC_INFOBOT || l->kind == LOC_FIXED)
            && l->vanilla.kind == REWARD_PLANET && l->vanilla.value > 0
            && l->vanilla.value < sizeof messages / sizeof messages[0]
            && messages[l->vanilla.value] == message) return (int)i;
    }
    return -1;
}
#endif
