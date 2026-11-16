/* Optional reward presentation API. The released 1.14 SDK still builds the core mod.
 * The extension follows the official 1.15 settings, 1.16 save-data and 1.17 collision
 * slots; never interpret the earlier experimental 1.15/1.16 layout as this ABI. */
#ifndef ITEM_REWARD_API_H
#define ITEM_REWARD_API_H
#include <stddef.h>

#include "openrac/openrac_mod.h"
#if OPENRAC_MOD_API_MINOR < 18
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
    if (api->api_minor < 18 || api->size < sizeof(reward_visual_api)) {
        return NULL;
    }
    const reward_visual_api* result = (const reward_visual_api*)api;
    return result->moby_visuals && result->guest_text && result->sprite_tint ? result : NULL;
}

static inline const reward_pickup_api* reward_pickup_host(const openrac_api* api) {
    const reward_visual_api* result = reward_visual_host(api);
    return result && result->pickup_animation ? result : NULL;
}

/* The official 1.16 save-data prefix, usable with the pinned 1.14 headers. */
#if OPENRAC_MOD_API_MINOR < 16
typedef struct reward_save_api {
    openrac_api base;
#if OPENRAC_MOD_API_MINOR < 15
    void (*settings[5])(void);
#endif
    int (*save_data_set)(openrac_mod*, const void*, uint32_t);
    uint32_t (*save_data_get)(openrac_mod*, void*, uint32_t);
    int (*on_save_data)(openrac_mod*, openrac_frame_fn, void*);
} reward_save_api;
#else
typedef openrac_api reward_save_api;
#endif
static inline const reward_save_api* reward_save_host(const openrac_api* api) {
    if (api->api_minor < 16
        || api->size < offsetof(reward_save_api, on_save_data) + sizeof(void*)) {
        return NULL;
    }
    const reward_save_api* out = (const reward_save_api*)api;
    return out->save_data_set && out->save_data_get && out->on_save_data ? out : NULL;
}
#endif
