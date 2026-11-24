#ifndef ITEM_GOLD_SAVE_API_H
#define ITEM_GOLD_SAVE_API_H
#include <stddef.h>

#include "openrac/openrac_mod.h"
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
