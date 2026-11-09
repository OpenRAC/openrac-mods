/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Going Commando: the item randomizer in play (README.md, Going Commando). At load it reads the
 * settings, makes the seed's placement (solver2.c) and writes the spoiler log; in play it gives each
 * location's reward in place of the game's. The three discs (NTSC-U v1.01, PAL, USA v2.00) alike:
 * the port gives each disc's givers the same hook points, and discs2.h has where each keeps the
 * globals below.
 *
 * How the game is changed, only where a location is shuffled:
 *
 *   - Item_Give (GiveInventoryItem): the game gives item X at X's own place (an NPC, a pickup, an
 *     arena prize). The mod marks that location had and gives its reward instead, through the
 *     game's own GiveInventoryItem (owned, new, ammo, the vendor's list, the quick select). A
 *     location had already gives nothing again. Its reward is said in a line over the game
 *     (play_notice); the game's own words name X.
 *   - Item_Equip (EquipItem): most givers put X in the hero's hand (or on him) right after they
 *     give it. Where the mod gave another item in X's place, X is not the player's: he would hold
 *     an item he does not own (it is in no list, and leaves his hand for good once put away). The
 *     mod hands the giver's equip the reward instead, or drops it when the reward is not the
 *     player's to hold.
 *   - The givers that test X's owned byte before they give it (the Dynamo, the Tractor Beam's,
 *     Thermanator's, Levitator's and Hypnomatic's sellers, the Box Breaker, the Glider, the Charge
 *     Boots' race, the Mapper) and the two level starts that place the hero by it (Endako's
 *     Swingshot area, Barlow's Thermanator area): while their update runs, the mod shows them X's
 *     location's had state in place of the owned byte, and puts the player's own back after. A
 *     player who has the Dynamo from elsewhere still finds Oozla's.
 *   - Endako sets the Grindboots and Clank's three packs itself (no GiveInventoryItem): each tick on
 *     Endako an owned byte of those four that the player does not hold is that location had now; the
 *     mod takes it back and gives the reward.
 *   - Each level's start: every location had holds its reward (a save from before a reward, a
 *     death): what is missing is given again. Not in Aranos Prison, whose cell takes the packs away.
 *   - The vendor: with its pool shuffled, the table of the weapons it stocks (0x2403C0 on NTSC-U v1.01) gets the
 *     seed's first level for each, checked against the game's own table first.
 *
 * In a party (multiplayer's README, Mods in a party) the host's seed counts, as in Ratchet & Clank:
 * this game publishes its seed's code (which carries its settings), and a member's game plays the
 * host's placement until the party goes, then its own again. What a party passes on is given as it
 * is (party_replaying): the give hook lets it through.
 *
 * What the game keeps: which locations are had, in its progress flags (chunk 5 of the save,
 * 0x1395B8 on NTSC-U v1.01), in bytes no code of any of the three discs reads (+0xC0..+0xC7: the
 * seed's tag, then a bit per location), so a save carries its own progress and a new game starts
 * with none.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "openrac/openrac_mod.h"
#include "paths.h"
#include "mod.h"
#include "discs2.h"
#include "rando2.h"

/* ---- the game's places (discs2.h; rac2-decomp, docs/port/RAC2_IDENTIFIED.tsv) ---- */

static const r2_disc* s_disc;

#define OWNED (s_disc->owned)             /* g_inventoryOwned: a byte per item */
#define NEW_FLAG (s_disc->new_flag)       /* g_inventoryNewFlag */
#define STOCK_TABLE (s_disc->stock_table) /* {i32 item, i32 first level} pairs, -2 ends */
#define STOCK_END (-2)
#define PROGRESS (s_disc->progress)       /* chunk 5 of the save: the story's bytes */
#define MARK_TAG (PROGRESS + 0xC0u)       /* the seed's tag, u32 */
#define MARK_BITS (PROGRESS + 0xC4u)      /* a bit per location, u32 */

#define DIR_SIZE 512
#define PATH_SIZE 640
#define NOTICE_SIZE 160
#define SPOILER_SIZE 8192

/* ---- state ---- */

static const openrac_api* s_api;
static openrac_mod* s_mod;
static r2_plan s_plan;
static char s_code[RANDO_CODE_SIZE];
static int s_location_of[R2_ITEM_COUNT]; /* the shuffled location whose item is this, -1 none */
static rando_settings s_own_settings;    /* this game's own, for when a party's host's is over */
static int s_following;                  /* playing a party host's seed */
static void (*s_give)(int32_t id);        /* the game's GiveInventoryItem (the hook's original) */
static int32_t (*s_equip)(int32_t id);    /* the game's EquipItem (the hook's original) */
static int s_giving;                      /* inside the mod's own give */
static uint32_t s_loads = 0xFFFFFFFFu;    /* the level load the start's check ran for */
static uint32_t s_stock_loads = 0xFFFFFFFFu; /* the program load the vendor's table was stocked in */
static int s_stock_written;                  /* the table holds a seed's levels */

/* The owned bytes shown otherwise while a giver's update runs: the player's own value. */
typedef struct standin {
    int item;
    uint8_t real;
} standin;
#define MAX_STANDINS 4
static standin s_standins[MAX_STANDINS];
static int s_standin_count;

/* ---- memory ---- */

static uint8_t* byte_at(uint32_t address) {
    return (uint8_t*)OPENRAC_G(s_api, s_api->relocate_data(address));
}

static uint32_t get32(uint32_t address) {
    uint32_t v;
    memcpy(&v, byte_at(address), 4);
    return v;
}

static void set32(uint32_t address, uint32_t v) {
    memcpy(byte_at(address), &v, 4);
}

static uint8_t* owned(int item) {
    return byte_at(OWNED + (uint32_t)item);
}

static void log_line(int level, const char* text) {
    s_api->log(s_mod, level, text);
}

/* ---- what is had ---- */

/* The seed's tag: the code's 60 bits folded, never 0 (0 is a save that has none yet). */
static uint32_t seed_tag(void) {
    uint64_t h = 1469598103934665603ull;

    for (const char* p = s_code; *p != 0; ++p) {
        h = (h ^ (uint8_t)*p) * 1099511628211ull;
    }

    const uint32_t tag = (uint32_t)(h ^ (h >> 32));
    return tag != 0 ? tag : 1u;
}

static int had(int location) {
    return (get32(MARK_BITS) >> location) & 1u;
}

static void mark_had(int location) {
    // The progress of a save made with another seed (or none) is this seed's from now on.
    if (get32(MARK_TAG) != seed_tag()) {
        if (get32(MARK_TAG) != 0) {
            log_line(OPENRAC_LOG_WARN, "item randomizer: this game was played with another seed");
        }
        set32(MARK_TAG, seed_tag());
    }

    set32(MARK_BITS, get32(MARK_BITS) | (1u << location));
}

/* Whether the player holds item from a location had (the game's own owned byte aside). */
static int reward_had(int item) {
    for (size_t i = 0; i < r2_location_count; ++i) {
        if (s_plan.shuffled[i] && s_plan.reward[i] == item && had((int)i)) {
            return 1;
        }
    }
    return 0;
}

/* ---- stand-ins ---- */

static void standins_show(int real) {
    for (int i = 0; i < s_standin_count; ++i) {
        const standin* st = &s_standins[i];
        *owned(st->item) = real ? st->real : (uint8_t)had(s_location_of[st->item]);
    }
}

static void standins_read(void) {
    for (int i = 0; i < s_standin_count; ++i) {
        s_standins[i].real = *owned(s_standins[i].item);
    }
}

/* ---- giving ---- */

static void notice(const char* text) {
    char line[NOTICE_SIZE];
    snprintf(line, sizeof line, "Item randomizer: %s", text);
    log_line(OPENRAC_LOG_INFO, line);
    if (s_api->play_notice != NULL) {
        s_api->play_notice(s_mod, line);
    }
}

/* Gives item through the game's routine, with the player's own owned bytes in place. */
static void give_item(int item) {
    if (s_give == NULL || item <= 0 || item >= R2_ITEM_COUNT) {
        return;
    }
    standins_show(1);
    s_giving = 1;
    s_give(item);
    s_giving = 0;
    standins_read();
    standins_show(0);
}

/* Location i had now: mark it and give its reward. */
static void check(int i) {
    char text[NOTICE_SIZE];
    mark_had(i);
    give_item(s_plan.reward[i]);
    snprintf(text, sizeof text, "%s (%s)", r2_item_name(s_plan.reward[i]), r2_locations[i].name);
    notice(text);
}

static void give_hook(int32_t id) {
    const int replaying = s_api->party_replaying != NULL && s_api->party_replaying();

    if (s_giving || replaying || id <= 0 || id >= R2_ITEM_COUNT || s_location_of[id] < 0) {
        s_give(id);
        return;
    }

    const int i = s_location_of[id];

    if (!had(i)) {
        check(i);
    }
}

/* The player's own owned byte: under a giver's stand-in, the value put aside. */
static int real_owned(int item) {
    for (int k = s_standin_count - 1; k >= 0; --k) {
        if (s_standins[k].item == item) {
            return s_standins[k].real;
        }
    }
    return *owned(item);
}

/* A giver puts its item X in the hero's hand after its give. Where the mod gave the location's
 * reward in X's place, the reward is what he takes; the game never puts an item not owned in his
 * hand, so nothing is put there when the reward is not the player's either. */
static int32_t equip_hook(int32_t id) {
    const int replaying = s_api->party_replaying != NULL && s_api->party_replaying();

    if (s_giving || replaying || id <= 0 || id >= R2_ITEM_COUNT || s_location_of[id] < 0
        || real_owned(id)) {
        return s_equip(id);
    }

    const int reward = s_plan.reward[s_location_of[id]];

    if (reward > 0 && reward != id && real_owned(reward)) {
        return s_equip(reward);
    }
    return 0;
}

/* ---- the givers' updates ---- */

static void giver(void (*original)(uint32_t), uint32_t moby, const int* items, int count) {
    const int base = s_standin_count;

    for (int k = 0; k < count && s_standin_count < MAX_STANDINS; ++k) {
        if (s_location_of[items[k]] < 0) {
            continue;
        }
        s_standins[s_standin_count].item = items[k];
        s_standins[s_standin_count].real = *owned(items[k]);
        *owned(items[k]) = (uint8_t)had(s_location_of[items[k]]);
        ++s_standin_count;
    }

    original(moby);

    // What the update wrote to a byte shown otherwise is the location's (a give went through the
    // hook); the player's own comes back.
    for (int k = s_standin_count - 1; k >= base; --k) {
        *owned(s_standins[k].item) = s_standins[k].real;
    }
    s_standin_count = base;
}

#define GIVER(name, ...)                                                                           \
    static void (*orig_##name)(uint32_t);                                                          \
    static void hook_##name(uint32_t moby) {                                                       \
        static const int items[] = {__VA_ARGS__};                                                  \
        giver(orig_##name, moby, items, (int)(sizeof items / sizeof items[0]));                    \
    }

GIVER(Moby_DynamoPickup_Update, R2_DYNAMO)
GIVER(Moby_TractorBeamSeller_Update, R2_TRACTOR_BEAM)
GIVER(Moby_BoxBreakerPickup_Update, R2_BOX_BREAKER)
GIVER(Moby_ThermanatorSeller_Update, R2_THERMANATOR)
GIVER(Moby_ThermanatorArea_Update, R2_THERMANATOR)
GIVER(Moby_SwingshotArea_Update, R2_SWINGSHOT)
GIVER(Moby_GliderPickup_Update, R2_GLIDER)
GIVER(Moby_LevitatorSeller_Update, R2_LEVITATOR)
GIVER(Moby_ChargeBootsRider_Update, R2_CHARGE_BOOTS)
GIVER(Moby_RaceController_Update, R2_CHARGE_BOOTS)
GIVER(Moby_MapperReturn_Update, R2_MAPPER)
GIVER(Moby_HypnomaticMaker_Update, R2_HYPNOMATIC)

typedef struct giver_hook {
    const char* name;
    void* replacement;
    void** original;
} giver_hook;

#define G(name, id) {"rac2.moby." id ".update", (void*)hook_##name, (void**)&orig_##name}
static const giver_hook k_givers[] = {
    G(Moby_DynamoPickup_Update, "dynamo_pickup"),      G(Moby_TractorBeamSeller_Update, "tractor_beam_seller"),
    G(Moby_BoxBreakerPickup_Update, "box_breaker_pickup"),  G(Moby_ThermanatorSeller_Update, "thermanator_seller"),
    G(Moby_ThermanatorArea_Update, "thermanator_area"),   G(Moby_SwingshotArea_Update, "swingshot_area"),
    G(Moby_GliderPickup_Update, "glider_pickup"),      G(Moby_LevitatorSeller_Update, "levitator_seller"),
    G(Moby_ChargeBootsRider_Update, "charge_boots_rider"),  G(Moby_RaceController_Update, "race_controller"),
    G(Moby_MapperReturn_Update, "mapper_return"),      G(Moby_HypnomaticMaker_Update, "hypnomatic_maker"),
};

/* ---- each tick ---- */

/* Whether the vendor's table holds the game's own weapons (and its end mark). */
static int stock_is_games(void) {
    for (int i = 0; i <= R2_VENDOR_COUNT; ++i) {
        const int32_t item = (int32_t)get32(STOCK_TABLE + 8u * (uint32_t)i);

        if (i == R2_VENDOR_COUNT ? item != STOCK_END : item != r2_vendor_stock[i].item) {
            return 0;
        }
    }

    return 1;
}

/* The vendor's table, once each program load (it is level data: each level's program brings the
 * game's own again), when its weapons are the game's own (never written over anything else): the
 * seed's levels, or the game's own back when a placement that shuffled it has gone (a party's). */
static void stock_vendor(uint32_t loads) {
    if (s_stock_loads == loads) {
        return;
    }
    s_stock_loads = loads;
    const int shuffled = (s_plan.settings.pools & POOL_VENDOR) != 0;

    if (!shuffled && !s_stock_written) {
        return;
    }

    if (!stock_is_games()) {
        if (shuffled) {
            log_line(OPENRAC_LOG_WARN, "item randomizer: the vendor's table is not the game's: "
                                       "its stock is left as it is");
        }
        return;
    }

    for (int i = 0; i < R2_VENDOR_COUNT; ++i) {
        set32(STOCK_TABLE + 8u * (uint32_t)i + 4u,
              (uint32_t)(shuffled ? s_plan.stock[i] : r2_vendor_stock[i].level));
    }
    s_stock_written = shuffled;
}

/* The four Endako sets itself: one the player does not hold is its location had now. */
static void endako_sets(void) {
    static const int items[] = {R2_GRINDBOOTS, R2_HELI_PACK, R2_THRUSTER_PACK, R2_HYDRO_PACK};

    for (size_t k = 0; k < sizeof items / sizeof items[0]; ++k) {
        const int item = items[k];
        const int i = s_location_of[item];

        if (i < 0 || *owned(item) == 0 || reward_had(item)) {
            continue;
        }

        *owned(item) = 0;
        *byte_at(NEW_FLAG + (uint32_t)item) = 0;

        if (!had(i)) {
            check(i);
        }
    }
}

static void each_tick(void* user) {
    (void)user;
    openrac_play_status st;
    memset(&st, 0, sizeof st);
    st.size = sizeof st;

    if (s_api->play_status(&st) != OPENRAC_HOOK_OK || st.level < 0) {
        return;
    }

    stock_vendor(st.loads);

    if (st.phase != OPENRAC_PLAY_PLAYING && st.phase != OPENRAC_PLAY_SCENE) {
        return;
    }

    if (st.level == R2_ENDAKO) {
        endako_sets();
    }

    // Each level's start: every reward had is held.
    if (st.phase == OPENRAC_PLAY_PLAYING && st.loads != s_loads) {
        s_loads = st.loads;

        for (size_t i = 0; st.level != R2_ARANOS_PRISON && i < r2_location_count; ++i) {
            const int item = s_plan.reward[i];

            if (s_plan.shuffled[i] && had((int)i) && item > 0 && *owned(item) == 0) {
                give_item(item);
            }
        }
    }
}

/* ---- the placement, and a party's ---- */

/* Plays the placement of `settings` (Going Commando's pools of them, its starting gear kept): the
 * solver finds one for every setting, the same on every computer. 1 when it did. */
static int play_settings(const rando_settings* settings) {
    rando_settings s = *settings;
    s.pools &= (uint8_t)(POOL_GADGETS | POOL_WEAPONS | POOL_VENDOR);
    s.keep_start = 1;

    if (r2_solve(&s, &s_plan) != 0) {
        return 0;
    }

    rando_encode(&s_plan.settings, s_code);

    for (int id = 0; id < R2_ITEM_COUNT; ++id) {
        s_location_of[id] = -1;
    }
    for (size_t i = 0; i < r2_location_count; ++i) {
        if (s_plan.shuffled[i]) {
            s_location_of[r2_locations[i].item] = (int)i;
        }
    }

    // The vendor's table again by this placement, at the next tick.
    s_stock_loads = 0xFFFFFFFFu;
    return 1;
}

/* What this game plays by, for a party it hosts: the seed code, which carries the settings, and the
 * words for a player. */
static void publish_rules(void) {
    static const struct {
        int pool;
        const char* name;
    } k_pools[] = {
        {POOL_GADGETS, "gadgets"},
        {POOL_WEAPONS, "weapons"},
        {POOL_VENDOR, "the vendor"},
    };

    char text[160];
    int at =
        snprintf(text, sizeof text, "Logic: %s", rando_difficulty_name(s_plan.settings.difficulty));

    // Only what this seed shuffles.
    for (size_t i = 0; i < sizeof k_pools / sizeof k_pools[0]; ++i) {
        if ((s_plan.settings.pools & k_pools[i].pool) != 0) {
            at += snprintf(text + at, sizeof text - (size_t)at, "\nShuffles %s", k_pools[i].name);
        }
    }

    s_api->party_rules_publish(s_mod, s_code, (uint32_t)strlen(s_code), text);
}

/* The host's rules reached this game: its seed's placement in place of this game's; none, the party
 * went, and this game's own is back. */
static void on_party_rules(void* user, const void* data, uint32_t size) {
    (void)user;
    char line[96];

    if (data == NULL) {
        if (s_following) {
            s_following = 0;
            play_settings(&s_own_settings);
            log_line(OPENRAC_LOG_INFO, "out of the party: this game's own seed counts again");
        }
        return;
    }

    char code[RANDO_CODE_SIZE];
    rando_settings host;

    if (size >= sizeof code
        || (memcpy(code, data, size), code[size] = 0, !rando_decode(code, &host))) {
        log_line(OPENRAC_LOG_WARN, "the host's seed cannot be read here");
        return;
    }

    if (!play_settings(&host)) {
        log_line(OPENRAC_LOG_ERROR, "no placement for the host's seed: this game's own counts");
        s_following = 0;
        play_settings(&s_own_settings);
        return;
    }

    s_following = 1;
    snprintf(line, sizeof line, "in a party: playing the host's seed %s", s_code);
    log_line(OPENRAC_LOG_INFO, line);
}

/* ---- start-up ---- */

static uint64_t fresh_seed(void) {
    rando_rng r = {(uint64_t)time(NULL) ^ ((uint64_t)clock() << 32) ^ (uint64_t)(uintptr_t)&r};
    return rando_next(&r);
}

static void write_files(int source) {
    char dir[DIR_SIZE], path[PATH_SIZE];
    static char spoiler[SPOILER_SIZE];

    if (rando_data_dir(dir, sizeof dir) != 0) {
        log_line(OPENRAC_LOG_WARN, "item randomizer: no data folder for the spoiler log");
        return;
    }

    if (source == SEED_FROM_NEW) {
        rando_join(path, sizeof path, dir, "seed-rac2.txt");
        rando_write_file(path, s_code, strlen(s_code));
    }

    if (s_api->setting_bool(s_mod, "spoiler_log")) {
        char name[64];
        snprintf(name, sizeof name, "spoiler-rac2-%s.txt", s_code);
        rando_join(path, sizeof path, dir, name);
        const size_t n = r2_spoiler(&s_plan, ITEM_RANDOMIZER_VERSION,
                                    spoiler, sizeof spoiler);
        rando_write_file(path, spoiler, n < sizeof spoiler ? n : sizeof spoiler - 1);
    }
}

int rac2_mod_load(const openrac_api* api, openrac_mod* mod) {
    s_api = api;
    s_mod = mod;
    s_give = NULL;
    s_equip = NULL;
    s_giving = 0;
    s_standin_count = 0;
    s_loads = 0xFFFFFFFFu;
    s_stock_loads = 0xFFFFFFFFu;
    s_stock_written = 0;

    s_disc = r2_disc_of(api->version);
    if (s_disc == NULL) {
        api->fail(mod, "the item randomizer does not know this Going Commando disc");
        return 1;
    }

    char dir[DIR_SIZE], path[PATH_SIZE], remembered[64] = "";
    int have_remembered = 0;

    if (rando_data_dir(dir, sizeof dir) == 0) {
        rando_join(path, sizeof path, dir, "seed-rac2.txt");
        have_remembered = rando_read_file(path, remembered, sizeof remembered) == 0;
    }

    // Going Commando has no gold bolts or infobots in the pools, and keeps the starting gear.
    const rando_options options = {
        api->setting_string(mod, "seed"),
        api->setting_bool(mod, "shuffle_weapons"),
        api->setting_bool(mod, "shuffle_gadgets"),
        0,
        0,
        api->setting_bool(mod, "shuffle_vendor"),
        1,
        api->setting_string(mod, "difficulty"),
    };
    rando_settings settings;
    const int source =
        rando_settings_of(&options, have_remembered ? remembered : NULL, fresh_seed(), &settings);

    if (!play_settings(&settings)) {
        api->fail(mod, "no placement for these settings (please report them)");
        return 1;
    }

    s_own_settings = s_plan.settings;
    s_following = 0;
    // A port with party rules may yet bring a host's seed that shuffles something.
    const int rules = api->api_minor >= 14 && api->party_rules_publish != NULL
                      && api->on_party_rules != NULL;
    const int shuffles = (s_plan.settings.pools & (POOL_GADGETS | POOL_WEAPONS)) != 0;

    if (shuffles || rules) {
        if (api->hook(mod, "rac2.item.give", "void(i32)", (void*)give_hook, (void**)&s_give)
            != OPENRAC_HOOK_OK) {
            return 1;
        }

        if (api->hook(mod, "rac2.item.equip", "i32(i32)", (void*)equip_hook, (void**)&s_equip)
            != OPENRAC_HOOK_OK) {
            return 1;
        }

        for (size_t k = 0; k < sizeof k_givers / sizeof k_givers[0]; ++k) {
            if (api->hook(mod, k_givers[k].name, "void(u32)", k_givers[k].replacement,
                          k_givers[k].original)
                != OPENRAC_HOOK_OK) {
                return 1;
            }
        }
    }

    if (shuffles || rules || (s_plan.settings.pools & POOL_VENDOR)) {
        api->on_frame(mod, each_tick, NULL);
    }

    if (rules) {
        publish_rules();
        api->on_party_rules(mod, on_party_rules, NULL);
    }

    char line[160];
    snprintf(line, sizeof line, "item randomizer: Going Commando, seed code %s, logic %s", s_code,
             rando_difficulty_name(s_plan.settings.difficulty));
    log_line(OPENRAC_LOG_INFO, line);
    write_files(source);
    return 0;
}
