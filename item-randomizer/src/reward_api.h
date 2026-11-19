/* Optional reward presentation API (interface 1.22: moby_visuals, guest_text, sprite_tint,
 * pickup_animation). An older SDK still builds the core mod: the extension follows the official
 * 1.15 settings, 1.16 save-data, 1.17 collision, 1.18 prop attachment, 1.19 developer menu and
 * 1.21 hook_entry slots; never interpret the earlier experimental 1.15/1.16 layout as this ABI. */
#ifndef ITEM_REWARD_API_H
#define ITEM_REWARD_API_H
#include <stddef.h>

#include "gold_save_api.h"
#include "openrac/openrac_mod.h"
#if OPENRAC_MOD_API_MINOR < 22
typedef struct openrac_moby_visual {
    openrac_gaddr moby;
    int32_t oclass;
    float height;
} openrac_moby_visual;

#define OPENRAC_MOBY_VISUALS_MAX 512

typedef struct reward_visual_api {
    openrac_api base;
#if OPENRAC_MOD_API_MINOR < 15
    void (*settings_slots[5])(void);
#endif
#if OPENRAC_MOD_API_MINOR < 16
    void (*save_slots[3])(void);
#endif
#if OPENRAC_MOD_API_MINOR < 17
    void (*collision_slot)(void);
#endif
#if OPENRAC_MOD_API_MINOR < 18
    void (*prop_slots[2])(void); /* released 1.18 prop attachment API */
#endif
#if OPENRAC_MOD_API_MINOR < 19
    void (*menu_slots[3])(void); /* released 1.19 developer menu switches and choices */
#endif
#if OPENRAC_MOD_API_MINOR < 21
    void (*hook_entry_slot)(void); /* released 1.21 hook_entry */
#endif
    int (*moby_visuals)(openrac_mod*, const openrac_moby_visual*, int);
    openrac_gaddr (*guest_text)(openrac_mod*, const char*);
    uint32_t (*sprite_tint)(openrac_mod*, uint32_t);
    int (*pickup_animation)(openrac_mod*, int, int);
} reward_visual_api;

static inline openrac_api* reward_visual_base(reward_visual_api* api) {
    return &api->base;
}
#else
typedef openrac_api reward_visual_api;

static inline openrac_api* reward_visual_base(reward_visual_api* api) {
    return api;
}
#endif
typedef reward_visual_api reward_pickup_api;

static inline const reward_visual_api* reward_visual_host(const openrac_api* api) {
    if (api->api_minor < 22 || api->size < sizeof(reward_visual_api)) {
        return NULL;
    }
    const reward_visual_api* result = (const reward_visual_api*)api;
    return result->moby_visuals && result->guest_text && result->sprite_tint ? result : NULL;
}

static inline const reward_pickup_api* reward_pickup_host(const openrac_api* api) {
    const reward_visual_api* result = reward_visual_host(api);
    return result && result->pickup_animation ? result : NULL;
}

#endif
