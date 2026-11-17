/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>

#include "npc_reward_text.h"
#include "npc_text_api.h"
#include "openrac/openrac_mod.h"
static uint8_t memory[0x10000];
static const openrac_api* s_api;
static openrac_mod* s_mod;
static int level = 3, enabled = 1, fail_copy, nested, hook_count;

static int current_level(void) {
    return level;
}

static int32_t get32(const uint8_t* p) {
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void log_line(int level_, const char* text) {
    (void)level_;
    (void)text;
}

static int npc_reward_label(const npc_reward_source* source, char* out, size_t size) {
    if (!enabled) {
        return 0;
    }
    snprintf(
        out, size, "%s", source->cls == 890 ? "Gold Bolt for Helga" : "Swingshot 100% for Player %s"
    );
    return 1;
}

static int32_t native_try(uint32_t, uint32_t);

static uint32_t native_message(int32_t id) {
    (void)id;
    return 0x100;
}

static uint32_t copy_text(openrac_mod* mod, const char* text) {
    (void)mod;
    if (fail_copy) {
        return 0;
    }
    strcpy((char*)memory + 0x1000, text);
    return 0x1000;
}

static int hook(openrac_mod* mod, const char* name, const char* sig, void* fn, void** out) {
    (void)mod;
    (void)sig;
    (void)fn;
    ++hook_count;
    *out = strcmp(name, "rac1.text.msg_string") == 0 ? (void*)native_message : (void*)native_try;
    return OPENRAC_HOOK_OK;
}

#include "npc_reward_text.inc"

static int32_t native_try(uint32_t moby, uint32_t request) {
    (void)moby;
    (void)request;
    uint32_t a = npc_reward_message(42);
    assert(a == (enabled && !fail_copy ? 0x1000u : 0x100u));
    assert(npc_reward_message(43) == 0x100); /* subtitles/unrelated UI */
    if (nested) {
        nested = 0;
        npc_reward_try(0x3100, 0x2000);
        assert(s_npc_text_source->cls == 909);
        assert(npc_reward_message(42) == 0x1000);
        assert(strstr((char*)memory + 0x1000, "Swingshot"));
    }
    return 7;
}

static void flatten(char* text) {
    for (; *text; ++text) {
        if (*text == 1) {
            *text = ' ';
        }
    }
}
int main(void) {
    npc_text_api host = {0};
#if OPENRAC_MOD_API_MINOR < 19
    openrac_api* base = &host.base;
#else
    openrac_api* base = &host;
#endif
    base->size = sizeof host;
    base->api_minor = 19;
    base->memory = memory;
    base->hook = hook;
    host.guest_text = copy_text;
    s_api = base;
    /* No read beyond an older host, including a claimed version with a short table. */
    base->api_minor = 18;
    assert(!npc_text_allocator(base));
    base->api_minor = 19;
    base->size = offsetof(npc_text_api, guest_text) - 1;
    assert(!npc_text_allocator(base));
    base->size = sizeof host;
    assert(npc_text_allocator(base) == copy_text);
    npc_reward_text_hooks();
    assert(hook_count == 2);
    const int32_t entries = 0x4000;
    const uint16_t msg = 42, al = 909, helga = 890;
    memcpy(memory + 0x203C, &entries, 4);
    memcpy(memory + 0x4000, &msg, 2);
    memcpy(memory + 0x30A6, &al, 2);
    strcpy((char*)memory + 0x100, "# Buy Heli-Pack for %d bolts");
    assert(npc_reward_try(0x3000, 0x2000) == 7);
    char formatted[256];
    snprintf(formatted, sizeof formatted, (char*)memory + 0x1000, 1000);
    flatten(formatted);
    assert(strstr(formatted, "Buy Swingshot 100% for Player %s for 1000 bolts"));
    assert(!s_npc_text_request && !s_npc_text_source);
    assert(npc_reward_message(42) == 0x100); /* outside interaction scope */
    enabled = 0;
    npc_reward_try(0x3000, 0x2000);
    enabled = 1;
    fail_copy = 1;
    npc_reward_try(0x3000, 0x2000);
    fail_copy = 0;
    /* Nested callbacks restore the outer NPC instead of leaking another reward. */
    memcpy(memory + 0x31A6, &helga, 2); /* nested host below uses another class */
    nested = 1;
    npc_reward_try(0x3000, 0x2000);
    level = LEVEL_RILGAR;
    const uint16_t race_starter = 918;
    memcpy(memory + 0x30A6, &race_starter, 2);
    strcpy((char*)memory + 0x100, "\x12 Enter race");
    assert(npc_reward_try(0x3000, 0x2000) == 7);
    snprintf(formatted, sizeof formatted, (char*)memory + 0x1000);
    flatten(formatted);
    assert(strcmp(formatted, "\x12 Enter race - Reward: Swingshot 100% for Player %s") == 0);
    enabled = 0;
    npc_reward_try(0x3000, 0x2000);
    enabled = 1;
    const uint16_t bouncer = 919;
    memcpy(memory + 0x30A6, &bouncer, 2);
    strcpy((char*)memory + 0x100, "\x12 Bribe Bouncer with 4,000 bolts");
    assert(npc_reward_try(0x3000, 0x2000) == 7);
    snprintf(formatted, sizeof formatted, (char*)memory + 0x1000);
    flatten(formatted);
    assert(
        strcmp(formatted, "\x12 Bribe Bouncer with 4,000 bolts to get Swingshot 100% for Player %s")
        == 0
    );
    level = 3;
    char prompt[120], huge[1000];
    memset(huge, 'a', sizeof huge);
    huge[999] = 0;
    assert(npc_reward_prompt(prompt, "# Talk", huge));
    assert(strlen(prompt) < 120);
    assert(!npc_reward_prompt(prompt, huge, "Gold Bolt"));
    assert(npc_race_prompt(prompt, "\x12 Enter race", "Gold Bolt"));
    assert(strcmp(prompt, "\x12 Enter race - Reward: Gold Bolt") == 0);
    assert(!npc_race_prompt(prompt, "You need a Hoverboard to race", "Gold Bolt"));
    assert(npc_bouncer_prompt(prompt, "\x12 Bribe Bouncer with 4,000 bolts", "Devastator"));
    assert(strcmp(prompt, "\x12 Bribe Bouncer with 4,000 bolts to get Devastator") == 0);
    assert(!npc_bouncer_prompt(prompt, "You need 4,000 bolts to see Captain Qwark", "Devastator"));
    assert(npc_reward_prompt(prompt, "\x12 Buy \fHeli-Pack\x08 for 1,000 bolts", "Devastator"));
    assert(strcmp(prompt, "\x12 Buy \fDevastator\x08 for 1,000 bolts") == 0);
    assert(!strstr(prompt, "Heli-Pack") && !strstr(prompt, "Reward:"));
    assert(
        npc_reward_prompt(prompt, "\x12 Pay 2,000 Bolts for \fThruster-Pack\x08", "Bolt Grabber")
    );
    assert(strcmp(prompt, "\x12 Pay 2,000 Bolts for \fBolt Grabber\x08") == 0);
    assert(npc_reward_prompt(prompt, "\x12 Trade Raritanium for Persuader", "Devastator"));
    assert(strcmp(prompt, "\x12 Trade Raritanium for Devastator") == 0);
    assert(npc_reward_prompt(prompt, "You need 1,000 bolts for the Heli-Pack", "Devastator"));
    assert(strcmp(prompt, "You need 1,000 bolts for Devastator") == 0);
    assert(npc_race_native_flag("pal") == 0x13D490u);
    assert(npc_race_native_flag("ntsc") == 0x13D388u);
    assert(!npc_reward_source_for(3, 925)); /* class alone is not a location */
    for (size_t i = 0; i < sizeof npc_reward_sources / sizeof *npc_reward_sources; ++i) {
        const npc_reward_source* source = &npc_reward_sources[i];
        assert(npc_reward_source_for(source->level, source->cls) == source);
        if (!source->fixed) {
            assert(npc_reward_row(source) >= 0);
        }
    }
    assert(npc_reward_source_for(8, 1283)->value == ITEM_METAL_DETECTOR);
    assert(npc_reward_source_for(14, 851)->value == ITEM_PDA);
    printf("guest_text offset: %zu\n", offsetof(npc_text_api, guest_text));
    puts("NPC interaction text, format safety, source coverage and optional ABI passed");
    return 0;
}
