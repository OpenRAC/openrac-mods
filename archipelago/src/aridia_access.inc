/* Aridia's exit follows the pickup check, independent of the reward's identity.
 * Retail initializes each door's home position in state 0 and slides it in state 2. */
static void (*s_aridia_door1_update)(openrac_gaddr);
static void (*s_aridia_door2_update)(openrac_gaddr);

static void aridia_door_update(openrac_gaddr moby, void (*original)(openrac_gaddr)) {
    original(moby);
    if (current_level() != LEVEL_ARIDIA || !aridia_reward_collected()) return;
    uint8_t* state = (uint8_t*)OPENRAC_G(s_api, moby) + 0x20u;
    if (*state == 1) *state = 2;
}

static void aridia_door1_update(openrac_gaddr moby) {
    aridia_door_update(moby, s_aridia_door1_update);
}

static void aridia_door2_update(openrac_gaddr moby) {
    aridia_door_update(moby, s_aridia_door2_update);
}

static int take(int result, const char* point);
static int aridia_access_hooks(void) {
    return take(s_api->hook(s_mod, "rac1.moby.moby743.update", "void(u32)",
        (void*)aridia_door1_update, (void**)&s_aridia_door1_update), "Aridia exit door 1")
        && take(s_api->hook(s_mod, "rac1.moby.moby744.update", "void(u32)",
        (void*)aridia_door2_update, (void**)&s_aridia_door2_update), "Aridia exit door 2");
}
