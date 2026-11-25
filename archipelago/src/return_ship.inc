#include "ship_shortcut.h"
static ship_shortcut s_ship_shortcut;
static int s_ship_return_pending, s_ship_return_level;
static uint32_t s_ship_return_load;

static void return_ship_pad(void* user, int port, uint8_t* report, int size) {
    (void)user;
    if (port != 0) return;
    uint32_t load = 0;
    const int phase = play_phase(&load);
    openrac_pose pose = {0}; pose.size = sizeof pose;
    const int allowed = phase == OPENRAC_PLAY_PLAYING && playing_level()
        && return_ship_active() && !*byte_at(s_disc->hero_clank)
        && s_api->hero_pose && s_api->hero_pose(&pose) == OPENRAC_HOOK_OK && pose.health > 0;
    if (ship_shortcut_report(&s_ship_shortcut, report, size, allowed)) {
        s_ship_return_pending = 1;
        s_ship_return_load = load;
        s_ship_return_level = current_level();
    }
}

static void return_ship_tick(int phase, uint32_t load) {
    if (!s_ship_return_pending) return;
    s_ship_return_pending = 0;
    if (phase != OPENRAC_PLAY_PLAYING || load != s_ship_return_load
        || current_level() != s_ship_return_level || !return_ship_active()) return;
    openrac_pose pose = {0}; pose.size = sizeof pose;
    if (*byte_at(s_disc->hero_clank) || !s_api->hero_pose
        || s_api->hero_pose(&pose) != OPENRAC_HOOK_OK || pose.health <= 0) return;
    /* Normal arrival puts Ratchet at his ship and clears rides/rail attachments,
     * retaining the current save's collected rewards and inventory. */
    if (s_api->play_travel && s_api->play_travel(s_mod, s_ship_return_level) == OPENRAC_HOOK_OK)
        log_line(OPENRAC_LOG_INFO, "Return to ship: replaying this planet's arrival");
}

static void return_ship_init(void) {
    const char* text = s_api->setting_string(s_mod, "return_to_ship_buttons");
    memset(&s_ship_shortcut, 0, sizeof s_ship_shortcut);
    s_ship_return_pending = 0;
    if (!ship_shortcut_parse(&s_ship_shortcut, text))
        log_line(OPENRAC_LOG_WARN, "Return to ship disabled: invalid button combination");
    if (s_api->on_pad) s_api->on_pad(s_mod, return_ship_pad, NULL);
}
