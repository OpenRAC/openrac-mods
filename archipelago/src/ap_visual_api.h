/* Optional reward presentation API. The released 1.14 SDK still builds the core mod.
 * The extension follows the official 1.15 settings, 1.16 save-data and 1.17 collision
 * slots; never interpret the earlier experimental 1.15/1.16 layout as this ABI. */
#ifndef AP_VISUAL_API_H
#define AP_VISUAL_API_H
#include "openrac/openrac_mod.h"
#if OPENRAC_MOD_API_MINOR < 18
typedef struct openrac_moby_visual {
    openrac_gaddr moby;
    int32_t oclass;
    float height;
} openrac_moby_visual;

#define OPENRAC_MOBY_VISUALS_MAX 512

typedef struct ap_visual_api {
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
} ap_visual_api;

static inline openrac_api* ap_visual_base(ap_visual_api* api) {
    return &api->base;
}
#else
typedef openrac_api ap_visual_api;

static inline openrac_api* ap_visual_base(ap_visual_api* api) {
    return api;
}
#endif
typedef ap_visual_api ap_pickup_api;

static inline const ap_visual_api* ap_visual_host(const openrac_api* api) {
    if (api->api_minor < 18 || api->size < sizeof(ap_visual_api)) {
        return NULL;
    }
    const ap_visual_api* result = (const ap_visual_api*)api;
    return result->moby_visuals && result->guest_text && result->sprite_tint ? result : NULL;
}

static inline const ap_pickup_api* ap_pickup_host(const openrac_api* api) {
    const ap_visual_api* result = ap_visual_host(api);
    return result && result->pickup_animation ? result : NULL;
}
#endif
