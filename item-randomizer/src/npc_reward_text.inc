/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "npc_reward_text.h"
#include "npc_text_api.h"
static int32_t (*s_npc_text_try)(uint32_t, uint32_t);
static uint32_t (*s_npc_text_message)(int32_t);
static npc_text_copy s_npc_text_copy;
static uint32_t s_npc_text_request;
static const npc_reward_source* s_npc_text_source;

static uint32_t npc_reward_message(int32_t id) {
    const uint32_t original = s_npc_text_message(id);
    if (!original || !s_npc_text_request || !s_npc_text_source) {
        return original;
    }
    const uint8_t* request = (const uint8_t*)OPENRAC_G(s_api, s_npc_text_request);
    const int16_t index = OPENRAC_GREF(s_api, int16_t, s_npc_text_request + 0x36);
    const uint32_t entries = (uint32_t)get32(request + 0x3C);
    if (!entries || index < 0 || index >= 64) {
        return original;
    }
    /* Only the selected interaction's prompt, never its speech, subtitles,
     * unrelated UI or another NPC's text, including nested dialog requests. */
    if (OPENRAC_GREF(s_api, uint16_t, entries + (uint32_t)index * 0x1C) != (uint16_t)id) {
        return original;
    }
    char reward[192], prompt[120];
    if (!npc_reward_label(s_npc_text_source, reward, sizeof reward)) {
        return original;
    }
    const char* text = (const char*)OPENRAC_G(s_api, original);
    int changed;
    if ((s_npc_text_source->level == LEVEL_RILGAR && s_npc_text_source->cls == 918)
        || (s_npc_text_source->level == LEVEL_KALEBO && s_npc_text_source->cls == 1455)) {
        changed = npc_race_prompt(prompt, text, reward);
    } else if (s_npc_text_source->level == LEVEL_RILGAR && s_npc_text_source->cls == 919) {
        changed = npc_bouncer_prompt(prompt, text, reward);
    } else {
        changed = npc_reward_prompt(prompt, text, reward);
    }
    if (!changed) {
        return original;
    }
    npc_reward_wrap(prompt);
    const uint32_t replacement = s_npc_text_copy(s_mod, prompt);
    return replacement ? replacement : original;
}

static int32_t npc_reward_try(uint32_t moby, uint32_t request) {
    const uint32_t previous_request = s_npc_text_request;
    const npc_reward_source* previous_source = s_npc_text_source;
    s_npc_text_request = request;
    s_npc_text_source =
        moby ? npc_reward_source_for(current_level(), OPENRAC_GREF(s_api, int16_t, moby + 0xA6))
             : NULL;
    const int result = s_npc_text_try(moby, request);
    s_npc_text_request = previous_request;
    s_npc_text_source = previous_source;
    return result;
}

/* Whether an NPC interaction this text owns is being tried: its prompt is rewritten here, and
 * a mod's other text owner must leave it alone. */
static inline int npc_reward_text_owns(void) {
    return s_npc_text_copy && s_npc_text_request && s_npc_text_source;
}

/* For a mod that hooks rac1.npc.try_dialog and rac1.text.msg_string itself (the Archipelago
 * reward presentation): each hook point is taken once, and that mod's hooks call
 * npc_reward_try and npc_reward_message, which go on to `try_dialog` and `message`. */
static inline int npc_reward_text_attach(
    int32_t (*try_dialog)(uint32_t, uint32_t), uint32_t (*message)(int32_t)
) {
    s_npc_text_source = NULL;
    s_npc_text_request = 0;
    s_npc_text_try = try_dialog;
    s_npc_text_message = message;
    s_npc_text_copy = npc_text_allocator(s_api);
    return s_npc_text_copy != NULL;
}

static void npc_reward_text_hooks(void) {
    if (s_api->setting_bool && !s_api->setting_bool(s_mod, "reward_text")) {
        return;
    }
    s_npc_text_source = NULL;
    s_npc_text_request = 0;
    s_npc_text_copy = npc_text_allocator(s_api);
    if (!s_npc_text_copy) {
        log_line(
            OPENRAC_LOG_INFO,
            "NPC reward text needs OpenRAC interface 1.22 and its dialog/text hooks; native "
            "prompts retained"
        );
        return;
    }
    /* These hooks are optional on older hosts, not new required manifest IDs. */
    if (s_api->hook(
            s_mod,
            "rac1.text.msg_string",
            "i32(i32)",
            (void*)npc_reward_message,
            (void**)&s_npc_text_message
        ) != OPENRAC_HOOK_OK
        || s_api->hook(
               s_mod,
               "rac1.npc.try_dialog",
               "i32(u32,u32)",
               (void*)npc_reward_try,
               (void**)&s_npc_text_try
           ) != OPENRAC_HOOK_OK) {
        log_line(OPENRAC_LOG_WARN, "NPC reward text hooks unavailable; native prompts retained");
    }
}
