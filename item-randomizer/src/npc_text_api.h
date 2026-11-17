/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NPC_TEXT_API_H
#define NPC_TEXT_API_H
#include <stddef.h>

#include "openrac/openrac_mod.h"
/* Only the optional text allocator is used. Preserve every released ABI entry;
 * building with the pinned SDK does not make old hosts expose newer entries. */
#if OPENRAC_MOD_API_MINOR < 19
typedef struct npc_text_api {
    openrac_api base;
#if OPENRAC_MOD_API_MINOR < 15
    void (*settings[5])(void);
#endif
#if OPENRAC_MOD_API_MINOR < 16
    void (*save[3])(void);
#endif
#if OPENRAC_MOD_API_MINOR < 17
    void (*collision)(void);
#endif
#if OPENRAC_MOD_API_MINOR < 18
    void (*props[2])(void);
#endif
    void (*visuals)(void);
    openrac_gaddr (*guest_text)(openrac_mod*, const char*);
} npc_text_api;
#else
typedef openrac_api npc_text_api;
#endif
typedef openrac_gaddr (*npc_text_copy)(openrac_mod*, const char*);

static npc_text_copy npc_text_allocator(const openrac_api* api) {
    if (api->api_minor < 19
        || api->size < offsetof(npc_text_api, guest_text) + sizeof(npc_text_copy)) {
        return NULL;
    }
    return ((const npc_text_api*)api)->guest_text;
}
#endif
