/* Blarg's Clank-section beam gates normally wait for linked Gadgetbots.
 * Ratchet can enter with an early O2 Mask, but cannot issue those commands.
 * State 3 uses retail removal/grid/audio cleanup without awarding the puzzle skill point. */
static void (*s_blarg_beam_update)(openrac_gaddr);
static void blarg_beam_update(openrac_gaddr moby) {
    uint8_t* state = (uint8_t*)OPENRAC_G(s_api, moby) + 0x20u;
    if (blarg_o2_access_active() && current_level() == LEVEL_BLARG
        && *owned(ITEM_O2_MASK) && (*state == 0 || *state == 1)) {
        uint32_t no_collision = 0;
        memcpy((uint8_t*)OPENRAC_G(s_api, moby) + 0x94u, &no_collision, sizeof no_collision);
        *state = 3;
    }
    s_blarg_beam_update(moby);
}

static int take(int result, const char* point);
static int blarg_access_hooks(void) {
    return take(s_api->hook(s_mod, "rac1.moby.class_1035.update", "void(u32)",
        (void*)blarg_beam_update, (void**)&s_blarg_beam_update), "Blarg beam gate");
}
