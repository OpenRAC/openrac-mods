/* The native ship already exists on Novalis, but until Agnogg's ship quest is done its update
 * (agnogg_ship_front_update, state 1) calls deactivate_recorded_moby every frame: the recorded
 * ship gets flags |= 3 (hidden, inactive) and no collision. Show it again the way retail does
 * when the quest ends (show_tracked_moby): clear those two flags and restore the collision.
 * Nothing else is touched: initialize_moby_state_record would also copy the ship's pvars over
 * the level's launch values (record +0x30..+0x38), which the take-off scene uses, and restart
 * the ship's animation. Coordinates and the quest's randomized reward are left alone. */
enum {
    NOVALIS_SHIP_HIDDEN = 3,
    NOVALIS_SHIP_FLAGS = 0x34,
    NOVALIS_SHIP_COLLISION = 0x94
};

static int random_start_level(void);
/* With the return-to-ship shortcut on, or a random starting planet (a new game flies to Novalis
 * first and leaves from its ship). */
static int novalis_ship_live(void) {
    return current_level() == LEVEL_NOVALIS
        && (return_ship_active() || random_start_level() > LEVEL_VELDIN);
}

/* The ship the level recorded (level_render_state.f0), or 0. */
static openrac_gaddr novalis_ship_moby(void) {
    const uint32_t record = strcmp(s_disc->version, "pal") == 0 ? 0x13E130u : 0x13E030u;
    const openrac_gaddr ship = (openrac_gaddr)get32(byte_at(record));
    if (!ship || ship > 0x02000000u - 0x100u) return 0;
    const int cls = OPENRAC_GREF(s_api, int16_t, ship + 0xA6);
    if (cls < 531 || cls > 533 || OPENRAC_GREF(s_api, uint8_t, ship + 0x20) >= 0xFD) return 0;
    return ship;
}

/* The ship's collision: the one it had, else its class's (MobyClass +0x10). */
static void novalis_ship_restore_collision(openrac_gaddr ship, uint32_t collision) {
    if (!collision) {
        const openrac_gaddr cls = OPENRAC_GREF(s_api, uint32_t, ship + 0x24);
        if (!cls || cls > 0x02000000u - 0x14u) return;
        collision = OPENRAC_GREF(s_api, uint32_t, cls + 0x10);
    }
    OPENRAC_GREF(s_api, uint32_t, ship + NOVALIS_SHIP_COLLISION) = collision;
}

/* Each tick, while playing: a ship still hidden (from the last frame, or since the level
 * loaded) is shown before the mobys update. */
static void novalis_ship_tick(int phase, uint32_t load) {
    (void)load;
    if (phase != OPENRAC_PLAY_PLAYING || !novalis_ship_live()) return;
    const openrac_gaddr ship = novalis_ship_moby();
    uint16_t* flags = ship ? &OPENRAC_GREF(s_api, uint16_t, ship + NOVALIS_SHIP_FLAGS) : NULL;
    if (!flags || (*flags & NOVALIS_SHIP_HIDDEN) != NOVALIS_SHIP_HIDDEN) return;
    *flags &= (uint16_t)~NOVALIS_SHIP_HIDDEN;
    novalis_ship_restore_collision(ship, 0);
}

/* Around the Agnogg ship's own update, the one that hides the ship again: undo exactly what its
 * deactivate_recorded_moby did this frame, so the ship is drawn and solid, and a flag the game
 * set itself (the take-off sets flags |= 1) is kept. */
static void novalis_ship_around_agnogg(void (*update)(uint32_t), uint32_t moby) {
    const openrac_gaddr ship = novalis_ship_live() ? novalis_ship_moby() : 0;
    uint16_t flags = 0;
    uint32_t collision = 0;
    if (ship) {
        flags = OPENRAC_GREF(s_api, uint16_t, ship + NOVALIS_SHIP_FLAGS);
        collision = OPENRAC_GREF(s_api, uint32_t, ship + NOVALIS_SHIP_COLLISION);
    }
    update(moby);
    if (!ship || (flags & NOVALIS_SHIP_HIDDEN) == NOVALIS_SHIP_HIDDEN || novalis_ship_moby() != ship) return;
    uint16_t* now = &OPENRAC_GREF(s_api, uint16_t, ship + NOVALIS_SHIP_FLAGS);
    if ((*now & NOVALIS_SHIP_HIDDEN) != NOVALIS_SHIP_HIDDEN) return;
    *now = (uint16_t)((*now & ~NOVALIS_SHIP_HIDDEN) | (flags & NOVALIS_SHIP_HIDDEN));
    novalis_ship_restore_collision(ship, collision);
}

