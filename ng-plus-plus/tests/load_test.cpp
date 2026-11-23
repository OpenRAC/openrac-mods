// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ as the port loads it (the built library), against a host table and game memory of the
// test's own, on each of the three discs: a Lancer at V4 offered Alpha at the vendor and bought,
// levelled to Beta by experience, its tint, damage, most ammo and texts (its name, the vendor's
// label and title, the ammo pickup's message, the level-up line); the RYNO II reaching Heavy; the
// levels kept by the save's clock in the mod's data folder, each save with its own (a save loaded
// at the title or in a level, stamped again, challenge mode's next game); then the bolts: on a port
// of 1.23 a drop in challenge mode's fourth run at a x20 streak made again as gold and platinum
// coins (the platinum ones drawn so), a platinum coin collected for five times its class, the
// coins of a level left forgotten, and the drops before cycle 2 left alone; on a port of 1.22 (no
// bolt hook points, no moby_material) and with the setting off, the mod loads as before and takes
// none of them. Then the weapons' looks (weapon_visuals.h): on a port of 1.25 the Lancer's guns
// at Beta drawn with energy in Beta's colour and strength, nothing on a dead slot or another
// class; on 1.23 and 1.24 the guns left as the game draws them; on 1.22 nothing at all.
//
// The test's game holds made-up rows and texts in the game's layout (src/discs.h); the expected
// values are the README's rules worked out for them.
//
// Run with the library's path: ng_plus_plus_load_test <ng_plus_plus.dylib|.so|.dll>.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "armour_api.h"
#include "bolt_api.h"
#include "bolt_rules.h"
#include "check.h"
#include "culling.h"
#include "discs.h"
#include "extra_enemies.h"
#include "hooks.h"
#include "moby_pool.h"
#include "openrac/games/rac2.hpp"
#include "openrac/openrac_mod.h"
#include "perk_rules.h"
#include "rewards.h"
#include "weapon_visuals.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

using namespace ng_plus_plus;
namespace rac2 = openrac::rac2;

// The disc the test runs on now, for the failures' lines.
const Disc* g_disc = nullptr;

}  // namespace

// Checks `condition` on the disc the test runs now; a macro for the file and line.
#undef CHECK
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            const std::string context = std::string("[") + g_disc->version + "] ";                 \
            ::ng_plus_plus::test::fail(__FILE__, __LINE__, context.c_str(), #condition);           \
        }                                                                                          \
    } while (0)

namespace {

// The test's game memory and where it keeps its own texts and records.
constexpr std::uint32_t kMemoryBytes = 0x00400000u;
constexpr std::uint32_t kTextAt = 0x00300000u;     // The test's strings.
constexpr std::uint32_t kTintAt = 0x00300100u;     // Item_Colors' three u32.
constexpr std::uint32_t kRequestAt = 0x00300200u;  // A hit request.
constexpr std::uint32_t kSlotsAt = 0x00301000u;    // The port's text slots.
constexpr std::uint32_t kMegaAt = 0x00302000u;     // The game's "Mega".
constexpr std::uint32_t kMobyHero = 0x00310000u;
constexpr std::uint32_t kMobyEnemy = 0x00310100u;
constexpr std::uint32_t kMobyShot = 0x00310200u;   // A Lancer shot still in flight.
constexpr std::uint32_t kMobyOther = 0x00310300u;  // A moby of a class no weapon owns.

// The bolts' records.
constexpr std::uint32_t kEnemyClassAt = 0x00311000u;  // The enemy's class record.
constexpr std::uint32_t kBurstAt = 0x00312000u;       // The game's burst's start and speed.
constexpr std::uint32_t kCoinsAt = 0x00320000u;       // The coins' mobys.
constexpr std::uint32_t kCoinBytes = 0x100u;          // One coin's moby.
constexpr std::uint32_t kStackTop = 0x003F0000u;      // The game's stack, growing down.
constexpr std::uint32_t kVisualMobys = 0x00340000u;   // The weapons' mobys, as the level's table.

// The ports the test plays: 1.23 with the bolt hook points, 1.22 without, 1.23 with the setting
// off, 1.24, whose moby_material draws obsidian too, 1.25, which draws energy as well and makes
// copies that never wake with their squad, 1.26, whose copies do, 1.27 with the pool, and 1.34
// with the armour vendor's hook points and paint on a class.
enum class Host {
    Bolts,
    Old,
    Off,
    Obsidian,
    Energy,
    Squads,
    Pool,
    Paint
};

// Whether the test's port has the pool (1.27 on).
bool pool_host(Host host) {
    return host == Host::Pool || host == Host::Paint;
}

// Whether the test's port draws energy (1.25 on).
bool energy_host(Host host) {
    return host == Host::Energy || host == Host::Squads || pool_host(host);
}

// Whether the test's port makes copies that wake (1.26 on).
bool squads_host(Host host) {
    return host == Host::Squads || pool_host(host);
}

// One coin the test's game made: its moby and worth, and where the mod said it starts.
struct Coin {
    std::uint32_t moby;
    std::int32_t value;
    float x;
};

// The Lancer's rows: its first version, Heavy Lancer (by experience), the Mega (bought) and V4.
constexpr int kLancer = 30;
constexpr int kLancerV2 = 60;
constexpr int kLancerV3 = 79;
constexpr int kLancerV4 = 80;
// The Lancer's most ammo at Gamma (500 at V4, 50 more a tier).
constexpr std::uint16_t kLancerGammaAmmo = 650;
constexpr int kRyno = 44;

// The game keeps experience times this (<< 5; seen in game code: g_weaponXp).
constexpr std::int32_t kExpUnit = 32;

// The test's text ids and where their strings are.
constexpr std::int32_t kMegaLancerText = 0x1234;   // "Mega Lancer": the Mega's and V4's name.
constexpr std::int32_t kLancerText = 0x1235;       // "Lancer".
constexpr std::int32_t kRynoText = 0x1236;         // "RYNO II".
constexpr std::int32_t kHeavyLancerText = 0x1237;  // "Heavy Lancer".
constexpr std::int32_t kMegaLabelText = 0x4321;    // "Mega Lancer": the Mega row's vendor label.
constexpr std::int32_t kRynoLabelText = 0x5555;    // "RYNO II": the RYNO II's vendor label.
constexpr std::int32_t kAmmoManyText = 0x2001;     // "%d Mega Lancer rounds".
constexpr std::int32_t kAmmoOneText = 0x2002;      // "1 Mega Lancer round".
constexpr std::int32_t kAmmoFullText = 0x2003;     // "Ammo full": names no weapon.
// The game's own shape of the message: the name in colour codes, between its words too.
constexpr std::int32_t kAmmoCodedText = 0x2004;  // "Got %d \x0cMega \x0cLancer\x08\x08 ammo".
// The collectors' dialogs (collectors.h): Tabora's crystals and Grelbin's moonstones.
// "Trade %d crystals for %d bolts?" and "%d moonstones: %d bolts": the disc's own ids
// (kDiscs' crystal_trade and moonstone_trade).
// The game's level-up table's line for the Lancer at its last version (help_show_message_for_id).
constexpr std::int32_t kLevelUpLineText = 0x2005;  // "\x0cMega Lancer\x08 upgraded".

// The ammo pickup's fields of a row (src/discs.h, kRowTextFields).
constexpr std::uint32_t kRowAmmoMany = 0x38u;
constexpr std::uint32_t kRowAmmoOne = 0x3Au;

std::vector<std::uint8_t> g_memory;
std::string g_failed;
std::string g_notice;
std::filesystem::path g_dir;

// A hook point the mod took: its id and the mod's replacement.
struct Hooked {
    std::string id;
    void* replacement;
};

std::vector<Hooked> g_hooks;
openrac_frame_fn g_frame = nullptr;
void* g_frame_user = nullptr;
int g_upgraded = -1;
float g_damage_seen = 0.0f;
std::int32_t g_taken = 0;
std::uint32_t g_drawing_vendor = 0;
std::string g_drawn_label;
std::uint32_t g_drawn_tint = 0;
std::uint32_t g_row_tint[16];
int g_banners = 0;
int g_fx_item = -1;  // The item the in-hand level-up was last started for.
std::int32_t g_banner_key = 0;
int g_level_now = 1;
Host g_host = Host::Bolts;
std::string g_log;
std::int32_t g_bolts = 0;
std::int32_t g_burst_high = -1;
std::vector<Coin> g_coins;
std::map<std::uint32_t, std::uint32_t> g_materials;  // Moby to material, as the port draws them.
std::map<std::uint32_t, std::uint32_t> g_glows;      // Moby to the rgb it was drawn with.
std::uint32_t g_sp = kStackTop;

// ---- Game memory ----

// Returns the `T` at game address `address`.
template <class T>
T get(std::uint32_t address) {
    T value;
    std::memcpy(&value, g_memory.data() + address, sizeof value);
    return value;
}

// Writes `value` at game address `address`.
template <class T>
void set(std::uint32_t address, T value) {
    std::memcpy(g_memory.data() + address, &value, sizeof value);
}

// Returns the string at game address `address`.
std::string string_at(std::int32_t address) {
    return reinterpret_cast<const char*>(g_memory.data() + static_cast<std::uint32_t>(address));
}

// Writes the string `text` at game address `address`.
void put_string(std::uint32_t address, const char* text) {
    std::memcpy(g_memory.data() + address, text, std::strlen(text) + 1);
}

// Returns the game address of row `index` on the disc.
std::uint32_t row(int index) {
    return g_disc->weapon_table + static_cast<std::uint32_t>(index) * kRowBytes;
}

// Returns the game address of vendor entry `place`.
std::uint32_t entry(int place) {
    return g_disc->vendor + kVendorEntries + static_cast<std::uint32_t>(place) * kVendorEntryBytes;
}

// Returns the mod's replacement of the hook point `HookPoint`, or null.
template <class HookPoint>
typename HookPoint::fn hooked() {
    // The hooks the mod took, in order.
    for (const Hooked& hook : g_hooks) {
        // The mod asks by the permanent id.
        if (hook.id == HookPoint::id) {
            return reinterpret_cast<typename HookPoint::fn>(hook.replacement);
        }
    }

    return nullptr;
}

// ---- The test's game ----

// The entries the test's game makes (a full list's test sets more).
int g_game_entries = 2;

// Vendor_BuildList: g_game_entries entries, the ammo of items 22 and 23 (and of 22 after them).
void game_build_list(std::int32_t /*hide*/) {
    // The game's entries.
    for (int n = 0; n < g_game_entries; ++n) {
        set<std::int32_t>(entry(n) + kEntryItem, n < 2 ? 22 + n : 22);
        set<std::int32_t>(entry(n) + kEntryKind, 1);
        set<std::int32_t>(entry(n) + kEntryUpgrade, 0);
    }

    set<std::int32_t>(g_disc->vendor + kVendorCount, g_game_entries);
}

// Vendor_Price: the game's price of whatever is under the cursor.
std::int32_t game_price() {
    return 77;
}

// Item_Upgrade: the game's Mega step, recorded.
std::int32_t game_upgrade(std::int32_t id) {
    g_upgraded = id;
    return 0;
}

// Item_Colors: the game's tint.
void game_colors(
    std::int32_t /*id*/, std::uint32_t /*first*/, std::uint32_t /*second*/, std::uint32_t tint
) {
    set<std::uint32_t>(tint, 0x60F08040u);
}

// Moby_PostHit: the damage the game saw.
void game_post_hit(std::uint32_t /*victim*/, std::uint32_t request) {
    g_damage_seen = get<float>(request + kHitDamage);
}

// Moby_RecordDeath as the game's: g_kill_xp of experience (<< 5) to the item g_kill_item.
// With g_death_bolts set, it also drops them as SetDeathBits does: 2% of the game's word (rounded
// up) taken out of it and added to the drop.
int g_kill_item = 0;
std::int32_t g_kill_xp = 0;
std::int32_t g_death_bolts = 0;
void port_kill_drop(std::uint32_t moby, std::int32_t amount);

void game_record_death(std::uint32_t moby, std::int32_t /*flags*/, std::int32_t /*arg*/) {
    const std::uint32_t at = g_disc->exp + 4u * static_cast<std::uint32_t>(g_kill_item);
    set<std::int32_t>(at, get<std::int32_t>(at) + g_kill_xp);

    // A drop too.
    if (g_death_bolts > 0) {
        const auto word = g_disc->progress + kProgressBoltCredit;
        const std::int32_t extra = (get<std::int32_t>(word) + 49) / 50;
        set<std::int32_t>(word, get<std::int32_t>(word) - extra);
        port_kill_drop(moby, g_death_bolts + extra);
    }
}

// Hero_TakeDamage: the points taken, added up.
void game_take_damage(std::int32_t amount) {
    g_taken += amount;
}

// The game's text lookup: its strings by id.
std::int32_t game_text(std::int32_t id) {
    // The disc's own ids: the "Mega" label and the two trades.
    if (id == g_disc->upgrade_label) {
        return static_cast<std::int32_t>(kMegaAt);
    }

    if (id == g_disc->crystal_trade) {
        return static_cast<std::int32_t>(kTextAt + 0x540);
    }

    if (id == g_disc->moonstone_trade) {
        return static_cast<std::int32_t>(kTextAt + 0x580);
    }

    // The test's strings by their ids.
    switch (id) {
        case kMegaLancerText:
        case kMegaLabelText:
            return static_cast<std::int32_t>(kTextAt);
        case kLancerText:
            return static_cast<std::int32_t>(kTextAt + 0x40);
        case kRynoText:
        case kRynoLabelText:
            return static_cast<std::int32_t>(kTextAt + 0x80);
        case kHeavyLancerText:
            return static_cast<std::int32_t>(kTextAt + 0xC0);
        case kAmmoManyText:
            return static_cast<std::int32_t>(kTextAt + 0x400);
        case kAmmoOneText:
            return static_cast<std::int32_t>(kTextAt + 0x440);
        case kAmmoFullText:
            return static_cast<std::int32_t>(kTextAt + 0x480);
        case kAmmoCodedText:
            return static_cast<std::int32_t>(kTextAt + 0x4C0);
        case kLevelUpLineText:
            return static_cast<std::int32_t>(kTextAt + 0x500);
        // Text 0: the game's empty string.
        case 0:
            return static_cast<std::int32_t>(kTextAt + 0x5F0);
        // No such text.
        default:
            return 0;
    }
}

// The port's lookup: its slots, else the hook point (the mod's replacement, else the game's).
std::int32_t port_text(std::int32_t id) {
    // The port's own slots for a mod's texts.
    if (id >= hooks::kTextSlot && id < hooks::kTextSlot + hooks::kTextSlots) {
        const auto slot = static_cast<std::uint32_t>(id - hooks::kTextSlot);
        return static_cast<std::int32_t>(kSlotsAt + slot * hooks::kTextSlotBytes);
    }

    const auto hook = hooked<hooks::Text_ById>();
    return hook != nullptr ? hook(id) : game_text(id);
}

// The vendor's list drawn as func_002F9C80 does: each row's icon colour, then an upgrade's label.
void game_draw_list() {
    const int cursor = get<std::int32_t>(g_drawing_vendor + kVendorCursor);
    const int count = get<std::int32_t>(g_drawing_vendor + kVendorCount);
    const auto color = hooked<hooks::Item_VersionColor>();

    // The rows -5 to +6 around the cursor, as the game draws them.
    for (int n = kVendorDrawFirst; n < kVendorDrawEnd; ++n) {
        const int place = cursor + n;

        // Rows past the list's ends are not drawn.
        if (place < 0 || place >= count) {
            continue;
        }

        const bool upgrade = get<std::int32_t>(entry(place) + kEntryUpgrade) != 0;
        const std::uint32_t rgb = color(upgrade ? 2 : 1);

        // The first rows' colours, for the checks.
        if (place < 16) {
            g_row_tint[place] = rgb;
        }

        // An upgrade row draws its label too.
        if (upgrade) {
            g_drawn_tint = rgb;
            g_drawn_label = string_at(port_text(g_disc->upgrade_label));
        }
    }
}

// Item_VersionColor: the gold of an upgrade, the grey of the rest.
std::uint32_t game_version_color(std::int32_t kind) {
    return kind == 2 ? 0x8000C0FFu : 0x80808080u;
}

// The game's level-up popup (func_00288E18): what it was given.
std::int32_t game_banner(std::int32_t key, std::int32_t selection) {
    ++g_banners;
    g_banner_key = selection == 1 ? key : -1;
    return 1;
}

// The game's in-hand level-up (func_L00_0037E6A8): the hero's animation starts; the test plays
// its end, the game's popup, itself.
void game_upgrade_fx(std::int32_t item, std::int32_t /*row*/) {
    g_fx_item = item;
}

// The game's message line (func_002B0A18): it looks its text up and shows it.
void game_message(std::int32_t text, std::int32_t /*seconds*/) {
    g_notice = string_at(port_text(text));
}

// The ammo pickup's message (func_0031F210): the row's text for the count, as the game prints it.
std::string ammo_message(int item, int count) {
    const std::uint32_t at =
        row(get<std::uint8_t>(g_disc->slot + static_cast<std::uint32_t>(item)));
    const std::int32_t id = get<std::int16_t>(at + (count == 1 ? kRowAmmoOne : kRowAmmoMany));
    return string_at(port_text(id));
}

// ---- The bolts ----

// Bolt_SpawnPickup: one coin of the class worth `value` (else the 1-bolt one), a moby of its own.
std::uint32_t game_spawn_pickup(
    std::uint32_t /*source*/,
    std::uint32_t position,
    std::uint32_t /*velocity*/,
    std::uint32_t /*flags*/,
    std::int32_t value,
    std::int32_t /*counted*/
) {
    int kind = 0;

    // The class worth `value`.
    for (int n = 0; n < kCoinKinds; ++n) {
        // Found.
        if (kCoinValues[static_cast<std::size_t>(n)] == value) {
            kind = n;
        }
    }

    const std::uint32_t moby = kCoinsAt + static_cast<std::uint32_t>(g_coins.size()) * kCoinBytes;
    set<std::uint16_t>(moby + kMobyClass, static_cast<std::uint16_t>(kCoinClass + kind));
    g_coins.push_back({moby, kCoinValues[static_cast<std::size_t>(kind)], get<float>(position)});
    return moby;
}

// The port's coin maker: the mod's replacement, else the game's.
std::uint32_t port_spawn_pickup(
    std::uint32_t source,
    std::uint32_t position,
    std::uint32_t velocity,
    std::uint32_t flags,
    std::int32_t value,
    std::int32_t counted
) {
    const auto hook = hooked<hooks::Bolt_SpawnPickup>();
    return hook != nullptr ? hook(source, position, velocity, flags, value, counted)
                           : game_spawn_pickup(source, position, velocity, flags, value, counted);
}

// Bolt_SpawnPickups as the game's (moby_spawn_bolt_pickups), with no roll (the amount is `high`):
// doubled in challenge mode, split into the biggest coins within `maximum` under flags 8 (the rest
// to the game's word, divided by the kill streak), each coin starting at x = its place in the
// burst.
void game_spawn_pickups(
    std::uint32_t source,
    std::int32_t low,
    std::int32_t high,
    std::int32_t flags,
    std::int32_t /*surface*/,
    std::int32_t maximum,
    float /*height*/
) {
    g_burst_high = high;

    // Nothing asked for.
    if (flags == 0 || (low == 0 && high == 0)) {
        return;
    }

    const bool doubled = get<std::int16_t>(g_disc->progress + kProgressCycle) != 0;
    std::int32_t value = doubled ? high * 2 : high;
    int room = (flags & 8) != 0 ? maximum : 1'000'000;
    int place = 0;

    // The biggest coin first, while there is room.
    for (int kind = kCoinKinds - 1; kind >= 0 && room > 0; --kind) {
        const std::int32_t worth = kCoinValues[static_cast<std::size_t>(kind)];

        // Each coin of the class.
        while (value >= worth && room > 0) {
            set<float>(kBurstAt, static_cast<float>(place));
            set<float>(kBurstAt + 16, 0.5f);
            port_spawn_pickup(
                source, kBurstAt, kBurstAt + 16, static_cast<std::uint32_t>(flags), worth, 1
            );
            value -= worth;
            --room;
            ++place;
        }
    }

    // Under the limit, the rest to the word over the streak.
    if ((flags & 8) != 0 && value > 0) {
        const int streak = get<std::uint8_t>(g_disc->progress + kProgressStreak);
        const auto word = g_disc->progress + kProgressBoltCredit;
        set<std::int32_t>(word, get<std::int32_t>(word) + (streak > 0 ? value / streak : value));
    }
}

// The port's burst: the mod's replacement, else the game's.
void port_spawn_pickups(std::uint32_t moby, std::int32_t amount, std::int32_t flags) {
    const auto hook = hooked<hooks::Bolt_SpawnPickups>();

    // The game's death handler's call: no path, no lift, at most 64 coins.
    if (hook != nullptr) {
        hook(moby, amount, amount, flags, -1, 64, 0.0f);
    } else {
        game_spawn_pickups(moby, amount, amount, flags, -1, 64, 0.0f);
    }
}

// The port's burst as the game's death handler (SetDeathBits) calls it for a kill: flags 9, at most
// three coins.
void port_kill_drop(std::uint32_t moby, std::int32_t amount) {
    const auto hook = hooked<hooks::Bolt_SpawnPickups>();

    // The mod's replacement, else the game's.
    if (hook != nullptr) {
        hook(moby, amount, amount, 9, -1, 3, 0.0f);
    } else {
        game_spawn_pickups(moby, amount, amount, 9, -1, 3, 0.0f);
    }
}

// Bolt_Collect: the coin's class's worth added to the bolts.
void game_collect(std::uint32_t moby) {
    const int kind = get<std::uint16_t>(moby + kMobyClass) - kCoinClass;
    g_bolts += kCoinValues[static_cast<std::size_t>(kind)];
}

// Arena_Prize: the game's prize of a challenge, as challenge mode pays it (the record's 5,000 x10).
constexpr std::int32_t kArenaPrize = 50'000;

std::int32_t game_arena_prize(std::int32_t /*challenge*/) {
    return kArenaPrize;
}

// The port's collect: the mod's replacement, else the game's.
void port_collect(std::uint32_t moby) {
    const auto hook = hooked<hooks::Bolt_Collect>();

    // The mod's, which goes on to the game's.
    if (hook != nullptr) {
        hook(moby);
    } else {
        game_collect(moby);
    }
}

// ---- The host ----

void host_log(openrac_mod* /*mod*/, int /*level*/, const char* text) {
    g_log += text;
    g_log += '\n';
}

void host_fail(openrac_mod* /*mod*/, const char* reason) {
    g_failed = reason;
}

// The "extra_enemies" setting as the test plays it.
bool g_extra_enemies = true;

int setting_bool(openrac_mod* /*mod*/, const char* key) {
    return std::strcmp(key, "notices") == 0 || std::strcmp(key, "enemy_scaling") == 0
           || std::strcmp(key, "gentle_clank") == 0
           || (std::strcmp(key, "extra_enemies") == 0 && g_extra_enemies);
}

const char* setting_string(openrac_mod* /*mod*/, const char* key) {
    // The bolts' setting as the host plays it.
    if (std::strcmp(key, "bolt_scaling") == 0) {
        return g_host == Host::Off ? "off" : "normal";
    }

    return nullptr;
}

double setting_float(openrac_mod* /*mod*/, const char* key) {
    return std::strcmp(key, "enemy_strength") == 0 ? 1.0 : 0.0;
}

// ---- The level's enemies and their copies (interface 1.25) ----

// The level's mobys the test's port lists: slot 0 the enemy (kMobyEnemy) and slot 1 another, both
// free to copy and of a class of Barlow's in NG++'s table (its hounds); slot 2 a wave's, slot 3 a
// crystal creature of Tabora's (the port's table), slot 4 no enemy; slot 5 one the game made (key
// -1), never asked about; slot 6 free to copy but of a class the table has not.
constexpr int kListed = 7;
constexpr int kTableClass = 4022;                 // Barlow's Gadgetron Hounds of Cuddly Death.
constexpr int kOtherClass = 2000;                 // In no level's table.
constexpr std::uint32_t kCopiesAt = 0x00320000u;  // Where the test's port puts copies.
constexpr int kCopySlots = 100;                   // The copies' slots, from 100.
int g_loads = 0;                                  // The game's level loads.
std::vector<std::pair<int, int>> g_copies_asked;  // Each copy asked: slot, index.
int g_copies_made = 0;

int answer_of(int slot) {
    static const int answers[kListed] = {
        kCopyOk,
        kCopyOk,
        kCopyScripted,
        kCopyListed,
        kCopyNotEnemy,
        kCopyNotPlaced,
        kCopyOk,
    };
    return slot >= 0 && slot < kListed ? answers[slot] : OPENRAC_HOOK_UNKNOWN;
}

std::uint32_t moby_at(int slot) {
    return slot == 0 ? kMobyEnemy : 0x00330000u + static_cast<std::uint32_t>(slot) * 0x100u;
}

int g_fake_coins = 0;   // Bolt pickups the test's level has alive besides the listed mobys.
int g_pool_asked = 0;   // The spare slots the mod asked the port for.
int g_pool_free = 500;  // The free spare slots the test's pool reports.

int host_moby_list(openrac_moby* out, int max) {
    // Each listed moby, as far as there is room.
    for (int n = 0; n < kListed && n < max; ++n) {
        out[n] = openrac_moby{};
        out[n].size = sizeof(openrac_moby);
        out[n].slot = n;
        out[n].key = n == 5 ? -1 : 40 + n;
        out[n].oclass = n == 3 ? 4098 : (n == 6 ? kOtherClass : kTableClass);
    }

    // The bolt pickups alive (classes 13 to 19), none the level placed.
    for (int n = 0; n < g_fake_coins && kListed + n < max; ++n) {
        openrac_moby& coin = out[kListed + n];
        coin = openrac_moby{};
        coin.size = sizeof(openrac_moby);
        coin.slot = 200 + n;
        coin.key = -1;
        coin.oclass = 13 + n % 7;
        coin.state = 1;
    }

    return kListed + g_fake_coins;
}

// 1.27's pool: a raise is granted as asked (the mod's request is written down), the status has 600
// spare slots of which g_pool_free are free.
int host_pool_raise(openrac_mod* /*mod*/, int extra) {
    g_pool_asked = extra;
    return extra;
}

int host_pool_status(openrac_mod* /*mod*/, PoolStatus* out) {
    out->capacity = 600;
    out->free = g_pool_free;
    out->live = 600 - g_pool_free;
    out->extra = g_pool_asked;
    out->requested = g_pool_asked;
    return OPENRAC_HOOK_OK;
}

int host_copy_check(openrac_mod* /*mod*/, int slot) {
    return answer_of(slot);
}

// A copy: the original's moby in the next copy slot. Slot 1 has no clear spot for a second copy.
int host_copy(openrac_mod* /*mod*/, int slot, int index) {
    g_copies_asked.emplace_back(slot, index);

    // Only what the check allowed; slot 1's second copy finds no spot.
    if (answer_of(slot) != kCopyOk || (slot == 1 && index == 1)) {
        return OPENRAC_HOOK_UNKNOWN;
    }

    const std::uint32_t copy = kCopiesAt + static_cast<std::uint32_t>(g_copies_made) * 0x100u;
    std::memcpy(g_memory.data() + copy, g_memory.data() + moby_at(slot), 0x100u);
    return kCopySlots + g_copies_made++;
}

// Level_Load: the game's own loads the level.
std::int32_t game_level_load(std::int32_t /*load*/, std::int32_t /*resume*/) {
    ++g_loads;
    return 0;
}

// The port's level load: the mod's replacement, else the game's.
void port_level_load() {
    const auto hook = hooked<rac2::Level_Load>();
    CHECK((hook != nullptr ? hook(1, 0) : game_level_load(1, 0)) == 0);
}

// A call of the port's that refuses: OPENRAC_HOOK_UNKNOWN as its return type has it.
template <class R, class... A>
R refuse(A...) {
    return static_cast<R>(OPENRAC_HOOK_UNKNOWN);
}

// ---- The armour vendor (1.34) ----

// The game's armours' prices (g_shop_slot_value_table) and the first name's text id.
constexpr std::int32_t kArmourPrices[5] = {0, 25'000, 100'000, 250'000, 1'000'000};
constexpr std::int32_t kArmourNameText = 0xEA92;

// The materials the mod put on classes, by class and scope: material and rgb.
std::map<std::pair<int, std::uint32_t>, std::pair<std::uint32_t, std::uint32_t>> g_class_looks;
std::int32_t g_dialog_answer = -1;  // The answer the game's dialog read last.

// ArmorVendor_BuildList: the game's rows, each armour above the one worn.
void game_armour_list() {
    const int worn = get<std::int16_t>(g_disc->progress + kProgressArmour);
    int count = 0;

    // Each armour above the one worn, all unlocked in the test's game.
    for (int armour = worn + 1; armour <= kBestArmour; ++armour, ++count) {
        const std::uint32_t at = g_disc->vendor + kVendorEntries + count * kArmourRowBytes;
        set<std::int32_t>(at, kArmourNameText + armour);
        set<std::int32_t>(at + 4u, kArmourPrices[armour]);
        set<std::int32_t>(at + 8u, armour);
    }

    set<std::int32_t>(g_disc->vendor + kVendorCount, count);
}

// ArmorVendor_Buy: the game's dialog at its decision reads the answer (it refuses every armour
// with Carbonox worn, so a yes here would be the game's refusal) and closes.
void game_armour_buy(std::uint32_t answer, std::uint32_t /*unused*/) {
    g_dialog_answer = get<std::int32_t>(answer);
    set<std::int32_t>(g_disc->vendor + kVendorDialog, 0);
}

int host_material_class(
    openrac_mod* /*mod*/, int oclass, std::uint32_t scope, std::uint32_t material, std::uint32_t rgb
) {
    // What the port refuses.
    if (oclass < 0 || scope == 0 || scope > (kScopeWorld | kScopeMenu) || (material & 0xFFu) > 5u
        || rgb > kNoTint) {
        return OPENRAC_HOOK_UNKNOWN;
    }

    // Each scope named: set, or taken off.
    for (const std::uint32_t bit : {kScopeWorld, kScopeMenu}) {
        if ((scope & bit) == 0) {
            continue;
        }

        if (material == kMaterialNone) {
            g_class_looks.erase({oclass, bit});
        } else {
            g_class_looks[{oclass, bit}] = {material, rgb};
        }
    }

    return OPENRAC_HOOK_OK;
}

openrac_party_rules_fn g_rules = nullptr;
void* g_rules_user = nullptr;

int host_hook(
    openrac_mod* /*mod*/, const char* id, const char* /*sig*/, void* replacement, void** original
) {
    // The game's functions behind each hook point the test's port has.
    const struct {
        const char* id;
        void* game;
    } points[] = {
        {rac2::Vendor_BuildList::id, reinterpret_cast<void*>(&game_build_list)},
        {rac2::Vendor_Price::id, reinterpret_cast<void*>(&game_price)},
        {rac2::Item_Upgrade::id, reinterpret_cast<void*>(&game_upgrade)},
        {rac2::Item_Colors::id, reinterpret_cast<void*>(&game_colors)},
        {rac2::Moby_PostHit::id, reinterpret_cast<void*>(&game_post_hit)},
        {rac2::Hero_TakeDamage::id, reinterpret_cast<void*>(&game_take_damage)},
        {rac2::Moby_RecordDeath::id, reinterpret_cast<void*>(&game_record_death)},
        {hooks::Vendor_DrawList::id, reinterpret_cast<void*>(&game_draw_list)},
        {hooks::Item_VersionColor::id, reinterpret_cast<void*>(&game_version_color)},
        {hooks::Text_ById::id, reinterpret_cast<void*>(&game_text)},
        {hooks::Bolt_SpawnPickups::id, reinterpret_cast<void*>(&game_spawn_pickups)},
        {hooks::Bolt_SpawnPickup::id, reinterpret_cast<void*>(&game_spawn_pickup)},
        {hooks::Bolt_Collect::id, reinterpret_cast<void*>(&game_collect)},
        {hooks::Arena_Prize::id, reinterpret_cast<void*>(&game_arena_prize)},
        {rac2::Level_Load::id, reinterpret_cast<void*>(&game_level_load)},
        {hooks::ArmorVendor_BuildList::id, reinterpret_cast<void*>(&game_armour_list)},
        {hooks::ArmorVendor_Buy::id, reinterpret_cast<void*>(&game_armour_buy)},
    };

    // Each hook point the test's port has.
    for (const auto& point : points) {
        const bool bolt = std::strncmp(point.id, "rac2.bolt.", 10) == 0
                          || std::strcmp(point.id, hooks::Arena_Prize::id) == 0;

        // A port of 1.22 has no bolt hook points, nor the arena's.
        if (bolt && g_host == Host::Old) {
            continue;
        }

        // Only a port of 1.34 has the armour vendor's.
        if (std::strncmp(point.id, "rac2.armor_vendor.", 18) == 0 && g_host != Host::Paint) {
            continue;
        }

        // Found: the mod goes on to the game's.
        if (std::strcmp(id, point.id) == 0) {
            *original = point.game;
            g_hooks.push_back({id, replacement});
            return OPENRAC_HOOK_OK;
        }
    }

    return OPENRAC_HOOK_UNKNOWN;
}

int host_on_frame(openrac_mod* /*mod*/, openrac_frame_fn fn, void* user) {
    g_frame = fn;
    g_frame_user = user;
    return 0;
}

void* host_function(const char* name) {
    // The disc's names of the popup, the line, the in-hand level-up and the lookup.
    if (std::strcmp(name, g_disc->banner) == 0) {
        return reinterpret_cast<void*>(&game_banner);
    }

    // The in-hand level-up by its name.
    if (std::strcmp(name, g_disc->upgrade_fx) == 0) {
        return reinterpret_cast<void*>(&game_upgrade_fx);
    }

    // The message line by its name.
    if (std::strcmp(name, g_disc->message) == 0) {
        return reinterpret_cast<void*>(&game_message);
    }

    return std::strcmp(name, g_disc->text) == 0 ? reinterpret_cast<void*>(&port_text) : nullptr;
}

openrac_gaddr identity(openrac_gaddr address) {
    return address;
}

// The test's levels keep their globals where the disc has them.
openrac_gaddr level_identity(int /*level*/, openrac_gaddr address) {
    return address;
}

int host_level() {
    return g_level_now;
}

int host_notice(openrac_mod* /*mod*/, const char* text) {
    g_notice = text;
    return OPENRAC_HOOK_OK;
}

const char* host_data_dir(openrac_mod* /*mod*/) {
    static std::string dir;
    dir = g_dir.string();
    return dir.c_str();
}

int host_publish(
    openrac_mod* /*mod*/, const void* /*data*/, std::uint32_t /*size*/, const char* /*text*/
) {
    return OPENRAC_HOOK_OK;
}

int host_bolts(int add) {
    g_bolts += add;
    return g_bolts;
}

openrac_gaddr host_stack_push(std::uint32_t size, openrac_gaddr* saved) {
    *saved = g_sp;
    g_sp -= (size + 15u) & ~15u;
    return g_sp;
}

void host_stack_pop(openrac_gaddr saved) {
    g_sp = saved;
}

int host_material(
    openrac_mod* /*mod*/, openrac_gaddr moby, std::uint32_t material, std::uint32_t rgb
) {
    // What the port refuses: obsidian before 1.24, energy (with its level) before 1.25.
    const bool obsidian = g_host == Host::Obsidian || energy_host(g_host);
    const std::uint32_t most = energy_host(g_host) ? kMaterialEnergy
                               : obsidian          ? kMaterialObsidian
                                                   : kMaterialPlatinum;
    const std::uint32_t kind = material & 0xFFu;
    const bool level_ok = material == kind || (kind == kMaterialEnergy && material <= 0xFFFu);
    if (moby == 0 || kind > most || !level_ok || rgb > kNoTint) {
        return OPENRAC_HOOK_UNKNOWN;
    }

    // None takes the mod's material off.
    if (material == kMaterialNone) {
        g_materials.erase(moby);
        g_glows.erase(moby);
    } else {
        g_materials[moby] = material;
        g_glows[moby] = rgb;
    }

    return OPENRAC_HOOK_OK;
}

int host_on_rules(openrac_mod* /*mod*/, openrac_party_rules_fn fn, void* user) {
    g_rules = fn;
    g_rules_user = user;
    return 0;
}

// ---- Driving the game ----

// The vendor's purchase of the entry under the cursor, in the game's order (func_002F8918): the
// price, the upgrade for an upgrade entry, the money, the list built again, the cursor kept in it.
// Returns the price.
std::int32_t purchase(std::int32_t& money) {
    const int cursor = get<std::int32_t>(g_disc->vendor + kVendorCursor);
    const int item = get<std::int32_t>(entry(cursor) + kEntryItem);
    const std::int32_t price = hooked<rac2::Vendor_Price>()();

    // Only an upgrade entry steps the item.
    if (get<std::int32_t>(entry(cursor) + kEntryUpgrade) != 0) {
        hooked<rac2::Item_Upgrade>()(item);
    }

    money -= price;
    hooked<rac2::Vendor_BuildList>()(0);
    const int count = get<std::int32_t>(g_disc->vendor + kVendorCount);

    // The cursor stays in the shorter list.
    if (cursor > count - 1) {
        set<std::int32_t>(g_disc->vendor + kVendorCursor, count - 1);
    }

    return price;
}

// Returns the vendor's title for text `id`, as its dialog names the entry under the cursor.
std::string title(std::int32_t id) {
    return string_at(port_text(id));
}

// Runs `n` game ticks.
void tick(int n) {
    // One frame callback a tick.
    for (int k = 0; k < n; ++k) {
        g_frame(g_frame_user);
    }
}

// Sets the save's clock: `first` and the seven bytes after it, or all zeros for 0.
void set_clock(std::uint8_t first) {
    // Each of the clock's eight bytes.
    for (std::uint32_t n = 0; n < 8; ++n) {
        const auto byte = static_cast<std::uint8_t>(first != 0 ? first + n : 0);
        g_memory[g_disc->clock + n] = byte;
    }
}

// Returns the vendor's count.
int vendor_count() {
    return get<std::int32_t>(g_disc->vendor + kVendorCount);
}

// Sets the vendor's cursor.
void set_cursor(int place) {
    set<std::int32_t>(g_disc->vendor + kVendorCursor, place);
}

// Lays out the test's game: a Lancer at V4 (rows 30, 60, 79 and 80 linked, its Mega and V4 named
// as the Mega), the RYNO II at its one row, the hero, and the texts.
void lay_out_game() {
    g_memory[g_disc->owned + kLancer] = 1;
    g_memory[g_disc->slot + kLancer] = kLancerV4;
    const int chain[4] = {kLancer, kLancerV2, kLancerV3, kLancerV4};
    const std::int16_t names[4] = {kLancerText, kHeavyLancerText, kMegaLancerText, kMegaLancerText};

    // Each version linked to the next and the one before.
    for (int v = 0; v < 4; ++v) {
        g_memory[row(chain[v]) + kRowVersion] = static_cast<std::uint8_t>(v);
        set<std::int16_t>(row(chain[v]) + kRowName, names[v]);
        set<std::int16_t>(
            row(chain[v]) + kRowNext, static_cast<std::int16_t>(v < 3 ? chain[v + 1] : 0)
        );
        set<std::int16_t>(
            row(chain[v]) + kRowParent, static_cast<std::int16_t>(v > 0 ? chain[v - 1] : 0)
        );
    }

    set<std::uint16_t>(row(kLancerV4) + kRowAmmoMax, 500);
    set<std::int32_t>(row(kLancerV4) + kRowClass, 999);
    set<std::int16_t>(row(kLancerV4) + kRowAmmoMany, kAmmoManyText);
    set<std::int16_t>(row(kLancerV4) + kRowAmmoOne, kAmmoOneText);
    put_string(kTextAt, "Mega Lancer");
    put_string(kTextAt + 0x40, "Lancer");
    put_string(kTextAt + 0x80, "RYNO II");
    put_string(kTextAt + 0xC0, "Heavy Lancer");
    put_string(kTextAt + 0x400, "%d Mega Lancer rounds");
    put_string(kTextAt + 0x440, "1 Mega Lancer round");
    put_string(kTextAt + 0x480, "Ammo full");
    put_string(kTextAt + 0x4C0, "Got %d \x0cMega \x0cLancer\x08\x08 ammo");
    put_string(kTextAt + 0x500, "\x0cMega Lancer\x08 upgraded");
    put_string(kTextAt + 0x5F0, "");
    put_string(kTextAt + 0x540, "Trade %d crystals for %d bolts?");
    put_string(kTextAt + 0x580, "%d moonstones: %d bolts");
    put_string(kMegaAt, "Mega");
    set<std::int16_t>(row(kRyno) + kRowName, kRynoText);
    g_memory[g_disc->owned + kRyno] = 1;
    g_memory[g_disc->slot + kRyno] = kRyno;
    set<std::uint16_t>(row(kRyno) + kRowAmmoMax, 100);
    // The game's full bars at the last versions: no experience to a next one.
    set<std::int32_t>(row(kLancerV4) + kRowExpGoal, 0);
    set<std::int32_t>(row(kRyno) + kRowExpGoal, 0);
    set<std::uint32_t>(g_disc->hero + kHeroMoby, kMobyHero);
    set<std::int32_t>(g_disc->hero + kHeroHeld, -1);
}

// Returns item `item`'s experience as the game keeps it (<< 5).
std::int32_t experience(int item) {
    return get<std::int32_t>(g_disc->exp + 4u * static_cast<std::uint32_t>(item));
}

// Sets item `item`'s experience, as the game compares it (>> 5).
void set_experience(int item, std::int32_t points) {
    set<std::int32_t>(g_disc->exp + 4u * static_cast<std::uint32_t>(item), points * kExpUnit);
}

// The table the port hands the mod: this SDK's, then the entries up to 1.25's copies.
// The table, with room past this SDK's for the entries of 1.34 the test's port of that minor puts in
// (armour_api.h).
struct WholeTable {
    PoolApi table;
    unsigned char later[2048];
};

WholeTable g_whole;
PoolApi& g_table = g_whole.table;
#if OPENRAC_MOD_API_MINOR < 27
CopyApi& g_copy_table = g_table.upto_copy;
#else
openrac_api& g_copy_table = g_table;
#endif
#if OPENRAC_MOD_API_MINOR < 25
openrac_api& g_api = bolt_base(g_copy_table.upto_material);
#else
openrac_api& g_api = g_copy_table;
#endif

// Fills the port's table for the disc the test runs on.
void make_api() {
    g_whole = WholeTable{};
    g_class_looks.clear();
    g_api.api_major = OPENRAC_MOD_API_MAJOR;

    // A port of 1.22 ends its table before moby_material; the others have it.
    if (g_host == Host::Old) {
        g_api.size = static_cast<std::uint32_t>(offsetof(BoltApi, moby_material));
        g_api.api_minor = 22;
    } else {
        // A port before 1.27 ends its table at the copies.
        g_api.size =
            static_cast<std::uint32_t>(pool_host(g_host) ? sizeof g_table : sizeof g_copy_table);
        g_api.api_minor = g_host == Host::Paint      ? kPaintMinor
                          : g_host == Host::Pool     ? kPoolMinor
                          : g_host == Host::Squads   ? kSquadCopyMinor
                          : g_host == Host::Energy   ? kEnergyMinor
                          : g_host == Host::Obsidian ? kObsidianMinor
                                                     : kMaterialMinor;
#if OPENRAC_MOD_API_MINOR < 25
        g_copy_table.upto_material.moby_material = host_material;
#else
        g_copy_table.moby_material = host_material;
#endif
    }

    // A port of 1.25 (energy's) makes copies; an older one ends its table before them.
    if (energy_host(g_host)) {
        g_copy_table.moby_copy_check = host_copy_check;
        g_copy_table.moby_copy = host_copy;
    } else if (g_host != Host::Old) {
        g_api.size = static_cast<std::uint32_t>(offsetof(CopyApi, moby_copy_check));
    }

    // A port of 1.27 has the pool.
    if (pool_host(g_host)) {
        g_table.moby_pool_raise = host_pool_raise;
        g_table.moby_pool_status =
            reinterpret_cast<decltype(g_table.moby_pool_status)>(host_pool_status);
    }
    g_api.moby_list = host_moby_list;

    // A port of 1.34: the calls of 1.28 to 1.33 refuse (their own tests are in game runs), then
    // moby_material_class, the table ending there.
    if (g_host == Host::Paint) {
        WeaponCallsTable calls{};
        calls.on_weapon_fire = refuse;
        calls.weapon_refire = refuse;
        calls.on_shot_end = refuse;
        calls.moby_spawn_live = refuse;
        calls.weapon_look = refuse;
        calls.moby_status = refuse;
        calls.weapon_record = refuse;
        std::memcpy(
            reinterpret_cast<unsigned char*>(&g_api) + weapon_calls_offset(), &calls, sizeof calls
        );
        const IgnoreCullingFn culling = refuse;
        std::memcpy(
            reinterpret_cast<unsigned char*>(&g_api) + weapon_calls_offset() + sizeof calls,
            &culling,
            sizeof culling
        );
        const std::size_t at = weapon_calls_offset() + sizeof(WeaponCallsTable)
                               + kMaterialClassAfterCulling * sizeof(void*);
        const MaterialClassFn fn = host_material_class;
        CHECK(at + sizeof fn <= sizeof g_whole);
        std::memcpy(reinterpret_cast<unsigned char*>(&g_api) + at, &fn, sizeof fn);
        g_api.size = static_cast<std::uint32_t>(at + sizeof fn);
    }

    g_api.game = "rac2";
    g_api.version = g_disc->version;
    g_api.openrac_version = "test";
    g_api.platform = "test";
    g_api.memory = g_memory.data();
    g_api.log = host_log;
    g_api.fail = host_fail;
    g_api.setting_bool = setting_bool;
    g_api.setting_float = setting_float;
    g_api.setting_string = setting_string;
    g_api.bolts = host_bolts;
    g_api.stack_push = host_stack_push;
    g_api.stack_pop = host_stack_pop;
    g_api.hook = host_hook;
    g_api.on_frame = host_on_frame;
    g_api.function = host_function;
    g_api.relocate_data = identity;
    g_api.relocate_level_data = level_identity;
    g_api.loaded_level = host_level;
    g_api.play_notice = host_notice;
    g_api.data_dir = host_data_dir;
    g_api.party_rules_publish = host_publish;
    g_api.on_party_rules = host_on_rules;
}

// Loads the library's entry point into `load`, or reports why not.
bool open_library(const char* path, openrac_mod_load_fn& load) {
#if defined(_WIN32)
    HMODULE library = LoadLibraryA(path);
    void* symbol = library != nullptr
                       ? reinterpret_cast<void*>(GetProcAddress(library, "openrac_mod_load"))
                       : nullptr;
#else
    void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    void* symbol = library != nullptr ? dlsym(library, "openrac_mod_load") : nullptr;
#endif

    // A library that does not open, or has no entry point.
    if (symbol == nullptr) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }

    load = reinterpret_cast<openrac_mod_load_fn>(symbol);
    return true;
}

// Loads the mod again into the table, as a restarted game does.
bool reload(openrac_mod_load_fn load) {
    g_hooks.clear();
    return load(&g_api, reinterpret_cast<openrac_mod*>(&g_api)) == 0;
}

// Returns the text of the file at `path`, "" for none.
std::string file_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// Checks a book of the layout before the RYNO II's path changed (Heavy, Ultima, Alpha to Omega,
// Omega Prime) moved over once as the mod loads: old Ultima (2) to Alpha, old Omega (10) to Omega
// (9), Omega Prime (11) to Ultima (10), the started bit and the other items kept; loaded again, the
// book stays as it is. The book before is put back after.
void test_old_book_moved(openrac_mod_load_fn load) {
    const std::filesystem::path book = g_dir / "tiers.txt";
    const std::string before = file_text(book);
    // Three saves: the RYNO II at old Ultima (counting), old Omega, old Omega Prime; the Lancer 5.
    const std::string zeros(112, '0');
    std::string first = zeros;
    first.replace(kRyno * 2, 2, "82");
    first.replace(kLancer * 2, 2, "05");
    std::string second = zeros;
    second.replace(kRyno * 2, 2, "0a");
    std::string third = zeros;
    third.replace(kRyno * 2, 2, "0b");
    {
        std::ofstream file(book, std::ios::binary | std::ios::trunc);
        file << "0100000000000011 " << first << "\n0200000000000011 " << second
             << "\n0300000000000011 " << third << "\n";
    }

    CHECK(reload(load));
    const std::string moved = file_text(book);
    std::string first_now = zeros;
    first_now.replace(kRyno * 2, 2, "82");
    first_now.replace(kLancer * 2, 2, "05");
    std::string second_now = zeros;
    second_now.replace(kRyno * 2, 2, "09");
    std::string third_now = zeros;
    third_now.replace(kRyno * 2, 2, "0a");
    CHECK(
        moved
        == "ngpp-tiers 2\n0100000000000011 " + first_now + "\n0200000000000011 " + second_now
               + "\n0300000000000011 " + third_now + "\n"
    );

    // Loaded again: never moved twice.
    CHECK(reload(load));
    CHECK(file_text(book) == moved);

    // The book before back, as the tests after it had it.
    {
        std::ofstream file(book, std::ios::binary | std::ios::trunc);
        file << before;
    }

    CHECK(reload(load));
}

// The armour tiers' first two prices: perk_rules.h's ladder from its base, x1.5 and x2.
constexpr std::int32_t kAlphaArmorPrice = 900'000;
constexpr std::int32_t kBetaArmorPrice = 1'200'000;

// Returns the armour vendor's row at `place`: text, price, armour.
std::array<std::int32_t, 3> armour_row(int place) {
    const std::uint32_t at = g_disc->vendor + kVendorEntries + place * kArmourRowBytes;
    return {get<std::int32_t>(at), get<std::int32_t>(at + 4u), get<std::int32_t>(at + 8u)};
}

// Returns the material and rgb the mod put on the hero's class in `scope` (0, 0 for none).
std::pair<std::uint32_t, std::uint32_t> hero_look(std::uint32_t scope) {
    const auto found = g_class_looks.find(std::make_pair(kHeroClass, scope));
    return found != g_class_looks.end() ? found->second : std::make_pair(0u, 0u);
}

// The game's armour purchase dialog at its decision on the row under the cursor, the answer yes.
void armour_dialog_yes() {
    constexpr std::uint32_t kAnswerAt = 0x00300300u;
    set<std::int32_t>(kAnswerAt, 1);
    set<std::int32_t>(g_disc->vendor + kVendorDialog, kDialogDecide);
    hooked<hooks::ArmorVendor_Buy>()(kAnswerAt, kAnswerAt + 4u);
}

// Checks the armour tiers: never at the weapon vendor; at the armour vendor (a port of 1.34, `sold`)
// none below Carbonox, Alpha Armor after it as the game's next armour (its name, price, Carbonox's
// preview and description), its purchase paid by the mod (too few bolts: none), Beta next, a hit at
// Alpha's share, the hero and the preview repainted, and the row gone at Omega.
void test_armour_at_the_vendor(bool sold) {
    const std::uint32_t worn = g_disc->progress + kProgressArmour;
    const std::uint32_t bolts = g_disc->progress + kProgressBolts;
    set<std::int16_t>(g_disc->progress + kProgressCycle, 1);
    set<std::int16_t>(worn, kBestArmour);

    // The weapon vendor has no armour entry.
    hooked<rac2::Vendor_BuildList>()(0);
    CHECK(g_log.find("for sale at the end of the list") == std::string::npos);
    set_cursor(vendor_count() - 1);
    CHECK(hooked<rac2::Vendor_Price>()() != kAlphaArmorPrice);

    // An older port: no armour vendor's hook points, said once.
    if (!sold) {
        CHECK(hooked<hooks::ArmorVendor_BuildList>() == nullptr);
        CHECK(g_log.find("mod interface 1.34") != std::string::npos);
        set<std::int16_t>(worn, 0);
        return;
    }

    // The armour vendor's menu up.
    set<std::int32_t>(g_disc->game_mode, kVendorGameMode);
    set<std::int32_t>(g_disc->vendor + kVendorMode, kArmourMode);
    set<std::int32_t>(g_disc->vendor + kVendorState, 0);
    set_cursor(0);

    // Electrosteel worn: the game's Carbonox row alone.
    set<std::int16_t>(worn, 3);
    hooked<hooks::ArmorVendor_BuildList>()();
    CHECK(vendor_count() == 1);
    CHECK(armour_row(0)[0] == kArmourNameText + 4);

    // Carbonox worn: the game has none, and Alpha Armor is the row, shown in Carbonox.
    set<std::int16_t>(worn, kBestArmour);
    hooked<hooks::ArmorVendor_BuildList>()();
    CHECK(vendor_count() == 1);
    const std::array<std::int32_t, 3> alpha = armour_row(0);
    CHECK(alpha[1] == kAlphaArmorPrice && alpha[2] == kBestArmour);
    CHECK(alpha[0] == kArmourNameText + 4);  // Carbonox's icon
    CHECK(title(g_disc->armour_name) == "Alpha Armor");
    CHECK(title(g_disc->armour_about) == "Alpha Armor: 10% less damage, on top of Carbonox.");

    // The preview repainted in Alpha's steel, the hero (no tier yet) as the game draws him.
    tick(2);
    CHECK(g_class_looks.count(std::make_pair(0, kScopeWorld)) == 0);
    CHECK(hero_look(kScopeMenu) == std::make_pair(paint_at(0), 0xB8C8E0u));

    // Too few bolts: not bought, and the game's dialog closes as for a no.
    set<std::int32_t>(bolts, 1'000);
    armour_dialog_yes();
    CHECK(g_dialog_answer == 0);
    CHECK(get<std::int32_t>(bolts) == 1'000);
    CHECK(title(g_disc->armour_name) == "Alpha Armor");

    // Bought: paid by the mod, the dialog closed as for a no, Beta Armor next at its price.
    set<std::int32_t>(bolts, 1'000'000);
    armour_dialog_yes();
    CHECK(g_dialog_answer == 0);
    CHECK(get<std::int32_t>(bolts) == 1'000'000 - kAlphaArmorPrice);
    CHECK(g_log.find("purchase: armour tier 0 -> 1") != std::string::npos);
    CHECK(g_notice == "Alpha Armor");
    CHECK(vendor_count() == 1);
    CHECK(title(g_disc->armour_name) == "Beta Armor");
    CHECK(armour_row(0)[1] == kBetaArmorPrice);

    // The hero in Alpha's steel, the preview in Beta's azure with its rim.
    tick(2);
    CHECK(hero_look(kScopeWorld) == std::make_pair(paint_at(0), 0xB8C8E0u));
    CHECK(hero_look(kScopeMenu) == std::make_pair(paint_at(2), 0x4090FFu));

    // A hit of ten at Alpha: nine.
    g_taken = 0;
    hooked<rac2::Hero_TakeDamage>()(10);
    CHECK(g_taken == 9);

    // Every tier: the row goes after Omega, which the hero then wears.
    for (int tier = 2; tier <= kArmourTiers; ++tier) {
        set<std::int32_t>(bolts, 100'000'000);
        armour_dialog_yes();
    }
    CHECK(g_log.find("purchase: armour tier 7 -> 8") != std::string::npos);
    CHECK(vendor_count() == 0);
    hooked<hooks::ArmorVendor_BuildList>()();
    CHECK(vendor_count() == 0);

    // The menu closed: the preview's scope shows the tier worn.
    set<std::int32_t>(g_disc->game_mode, 0);
    tick(2);
    CHECK(hero_look(kScopeMenu).first == paint_at(15));
    CHECK(hero_look(kScopeWorld).first == paint_at(15));

    set<std::int16_t>(worn, 0);
}

// Checks a save's RYNO II with all the experience it ever had: not Heavy at once, counted from 0;
// the bars' goals.
void test_a_new_save_counts_from_zero() {
    set_experience(kRyno, 50'000);
    set_clock(0x10);  // a save loaded the book does not have: kept as it is (no levels)
    tick(1);
    tick(1);
    CHECK(experience(kRyno) == 0);
    CHECK(g_notice.find("Heavy") == std::string::npos);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 500);
    // The RYNO II's bar fills toward Heavy; the Lancer's Alpha is bought, so its bar stays full.
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 37'500);  // five times the others'
    CHECK(get<std::int32_t>(row(kLancerV4) + kRowExpGoal) == 0);
}

// Checks the vendor's Alpha entry for the Lancer: its place, price, label and tint.
void test_the_vendor_offers_alpha() {
    hooked<rac2::Vendor_BuildList>()(0);
    CHECK(vendor_count() == 3);
    CHECK(get<std::int32_t>(entry(2) + kEntryItem) == kLancer);
    CHECK(get<std::int32_t>(entry(2) + kEntryUpgrade) == 1);
    CHECK(get<std::int32_t>(entry(2) + kEntryClass) == 999);
    set_cursor(0);
    CHECK(hooked<rac2::Vendor_Price>()() == 77);
    set_cursor(2);
    CHECK(hooked<rac2::Vendor_Price>()() == 2'250'000);  // no Mega row price here

    // The cursor on the game's first entry: rows 0, 1 and the mod's 2 drawn.
    set_cursor(0);
    g_drawn_label.clear();
    g_drawing_vendor = g_disc->vendor;
    hooked<hooks::Vendor_DrawList>()();
    CHECK(g_drawn_label == "Alpha");
    CHECK(g_drawn_tint == 0x8020FFB0u);  // Alpha's lime
    // Outside the draw the game's own.
    CHECK(port_text(g_disc->upgrade_label) == static_cast<std::int32_t>(kMegaAt));
    set_cursor(2);
}

// Checks Alpha bought at the vendor: named in the preview after the version before the Mega
// ("Alpha Heavy Lancer", not "Alpha Lancer" nor "Alpha Mega Lancer"), the list without the
// Lancer's entry after it (Beta is by experience), the tints, ammo and bar.
void test_alpha_bought() {
    set<std::int32_t>(row(kLancerV3) + kRowLabel, kMegaLabelText);
    CHECK(title(kMegaLabelText) == "Alpha Heavy Lancer");
    std::int32_t money = 3'000'000;
    const int banners = g_banners;
    CHECK(purchase(money) == 2'250'000);
    CHECK(g_banners == banners);  // bought: the vendor's own feedback, no level-up
    CHECK(money == 750'000);
    CHECK(g_upgraded == -1);
    CHECK(vendor_count() == 2);
    CHECK(get<std::int32_t>(g_disc->vendor + kVendorCursor) == 1);

    // No upgrade entry left.
    for (int place = 0; place < 2; ++place) {
        CHECK(get<std::int32_t>(entry(place) + kEntryUpgrade) == 0);
    }

    CHECK(title(kMegaLabelText) == "Alpha Heavy Lancer");  // no entry of the mod's: at its tier
    CHECK(g_notice == "Alpha Heavy Lancer");

    // The Lancer's own entry (its ammo) in the list: Alpha's lime now; another's as the game has it.
    set<std::int32_t>(entry(0) + kEntryItem, kLancer);
    set_cursor(0);
    hooked<hooks::Vendor_DrawList>()();
    CHECK(g_row_tint[0] == 0x8020FFB0u);
    CHECK(g_row_tint[1] == 0x80808080u);
    hooked<rac2::Vendor_BuildList>()(0);
    set_cursor(1);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 550);
    CHECK(experience(kLancer) == 0);
    CHECK(get<std::int32_t>(g_disc->ammo + 4u * kLancer) == 550);
    CHECK(get<std::int32_t>(row(kLancerV4) + kRowExpGoal) == 8'000);  // toward Beta
    // Not the mod's entry: the game's.
    set_cursor(0);
    CHECK(hooked<rac2::Item_Upgrade>()(22) == 0);
    CHECK(g_upgraded == 22);
}

// Checks Beta by experience: the game's level-up popup and line naming it, its name and ammo
// message everywhere at the tier, then Gamma for sale.
void test_beta_by_experience() {
    set_experience(kLancer, 7'999);
    tick(1);
    CHECK(g_notice == "Alpha Heavy Lancer");
    const int banners = g_banners;
    set_experience(kLancer, 8'000);
    tick(1);
    CHECK(g_notice == "Beta Heavy Lancer");
    // A step by experience as the game's own level-up: its popup, then the line.
    CHECK(g_banners == banners + 1);
    CHECK(g_banner_key == kLevelUpKey);
    // Its name everywhere the game prints it (the menus): the tier's.
    CHECK(string_at(port_text(kMegaLancerText)) == "Beta Heavy Lancer");
    CHECK(string_at(port_text(kMegaLancerText)) == "Beta Heavy Lancer");
    CHECK(port_text(kRynoText) == static_cast<std::int32_t>(kTextAt + 0x80));  // no tier yet
    // The ammo pickup's messages: the tier's name in the game's text, the count's placeholder kept.
    CHECK(ammo_message(kLancer, 12) == "%d Beta Heavy Lancer rounds");
    CHECK(ammo_message(kLancer, 1) == "1 Beta Heavy Lancer round");
    // A row text that does not name the weapon stays the game's.
    set<std::int16_t>(row(kLancerV4) + kRowAmmoOne, kAmmoFullText);
    CHECK(ammo_message(kLancer, 1) == "Ammo full");
    set<std::int16_t>(row(kLancerV4) + kRowAmmoOne, kAmmoOneText);
    // The game's messages wrap the name in colour codes, between its words too ("Got %d Ultra
    // \x0cHeavy \x0cLancer\x08\x08 ammo" for "Ultra Heavy Lancer"); 0.2.0 looked for the name
    // as one run of letters, found none and left the game's "Ultra". The tier's name takes its
    // place, the codes kept before it so each closing code still has its opener.
    set<std::int16_t>(row(kLancerV4) + kRowAmmoMany, kAmmoCodedText);
    CHECK(
        ammo_message(kLancer, 12)
        == "Got %d \x0c\x0c"
           "Beta Heavy Lancer\x08\x08 ammo"
    );
    set<std::int16_t>(row(kLancerV4) + kRowAmmoMany, kAmmoManyText);
    CHECK(experience(kLancer) == 0);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(get<std::int32_t>(row(kLancerV4) + kRowExpGoal) == 0);  // Gamma is for sale at once
    hooked<rac2::Vendor_BuildList>()(0);
    CHECK(vendor_count() == 3);
    set_cursor(2);
    CHECK(hooked<rac2::Vendor_Price>()() == 3'750'000);
}

// Checks Beta's tint, its damage with the Lancer in hand, and the enemies of the third game.
void test_tint_damage_and_enemies() {
    hooked<rac2::Item_Colors>()(kLancer, kTintAt + 8, kTintAt + 4, kTintAt);
    CHECK(get<std::uint32_t>(kTintAt) == 0x60FFE000u);  // cyan, in the game's alpha
    hooked<rac2::Item_Colors>()(22, kTintAt + 8, kTintAt + 4, kTintAt);
    CHECK(get<std::uint32_t>(kTintAt) == 0x60F08040u);

    // Beta's damage, with the Lancer in hand: twice for the call, put back after.
    set<std::int32_t>(g_disc->hero + kHeroHeld, kLancer);
    set<float>(kRequestAt + kHitDamage, 10.0f);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(g_damage_seen == 20.0f);
    CHECK(get<float>(kRequestAt + kHitDamage) == 10.0f);
    hooked<rac2::Moby_PostHit>()(kMobyHero, kRequestAt);
    CHECK(g_damage_seen == 10.0f);

    // A hit from the Lancer's own moby (class 1490, the request's source) after the player took
    // the RYNO II in hand: the Lancer's tier, not the hand's.
    set<std::uint16_t>(kMobyShot + kMobyClass, 1490);
    set<std::uint16_t>(kMobyOther + kMobyClass, 9999);
    set<std::uint32_t>(kRequestAt + kHitSource, kMobyShot);
    set<std::int32_t>(g_disc->hero + kHeroHeld, kRyno);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(g_damage_seen == 20.0f);
    set<std::int32_t>(g_disc->hero + kHeroHeld, -1);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(g_damage_seen == 20.0f);
    // A source no weapon owns, and none: the weapon in hand.
    set<std::uint32_t>(kRequestAt + kHitSource, kMobyOther);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(g_damage_seen == 10.0f);
    set<std::int32_t>(g_disc->hero + kHeroHeld, kLancer);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(g_damage_seen == 20.0f);
    set<std::uint32_t>(kRequestAt + kHitSource, 0);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(g_damage_seen == 20.0f);

    // Challenge mode's third game (cycle 2): enemies have 3.75 times the health, deal 1.375 times.
    set<std::int16_t>(g_disc->progress + kProgressCycle, 2);
    set<float>(kRequestAt + kHitDamage, 10.0f);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(std::fabs(g_damage_seen - 20.0f / 3.75f) < 0.001f);  // Beta's 2 over the health's 3.75

    // The final boss (the mutated Protopet's class): a boss's twice that health, half again.
    const auto enemy_class = get<std::uint16_t>(kMobyEnemy + kMobyClass);
    set<std::uint16_t>(kMobyEnemy + kMobyClass, 4576);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(std::fabs(g_damage_seen - 20.0f / 11.25f) < 0.001f);

    // The RYNO II in hand: on the final boss 4.5 times the game's own RYNO II's hits at cycle 2;
    // on an ordinary enemy none of the curve, the game's own hit.
    set<std::int32_t>(g_disc->hero + kHeroHeld, kRyno);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(std::fabs(g_damage_seen - 10.0f / 4.5f) < 0.001f);
    set<std::uint16_t>(kMobyEnemy + kMobyClass, enemy_class);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(g_damage_seen == 10.0f);
    set<std::int32_t>(g_disc->hero + kHeroHeld, kLancer);
    g_taken = 0;

    // Four hits of one point: five (5.5, the fractions carried).
    for (int n = 0; n < 4; ++n) {
        hooked<rac2::Hero_TakeDamage>()(1);
    }

    CHECK(g_taken == 5);

    // Clank as the hero (class 87): every hit at most one point, scaled or not; a heal as it is.
    // Ratchet again: the scaled hits.
    set<std::uint16_t>(kMobyHero + kMobyClass, static_cast<std::uint16_t>(kClankHeroClass));
    g_taken = 0;
    hooked<rac2::Hero_TakeDamage>()(3);
    CHECK(g_taken == 1);
    hooked<rac2::Hero_TakeDamage>()(1);
    CHECK(g_taken == 2);
    hooked<rac2::Hero_TakeDamage>()(-2);
    CHECK(g_taken == 0);
    set<std::uint16_t>(kMobyHero + kMobyClass, 0);
    g_taken = 0;
    hooked<rac2::Hero_TakeDamage>()(3);
    CHECK(g_taken > 3);
    set<std::int16_t>(g_disc->progress + kProgressCycle, 1);
    g_taken = 0;
    hooked<rac2::Hero_TakeDamage>()(3);
    CHECK(g_taken == 3);
    set<std::uint16_t>(kMobyHero + kMobyClass, static_cast<std::uint16_t>(kClankHeroClass));
    g_taken = 0;
    hooked<rac2::Hero_TakeDamage>()(3);
    CHECK(g_taken == 1);
    set<std::uint16_t>(kMobyHero + kMobyClass, 0);
}

// Lays the level's moby table out over `classes` (0 for a dead slot), one moby each from
// kVisualMobys; an empty list takes the table away.
void lay_out_mobys(const std::vector<int>& classes) {
    const std::uint32_t table = std::string(g_disc->version) == "ntsc2" ? 0x001B1ADCu : 0x001B1B5Cu;
    const auto count = static_cast<std::uint32_t>(classes.size());
    set<std::uint32_t>(table, count == 0 ? 0u : kVisualMobys);
    set<std::uint32_t>(table + 8u, count == 0 ? 0u : kVisualMobys + count * 0x100u);

    // Each moby: its class, and its state (0x80 and up for a dead slot).
    for (std::uint32_t n = 0; n < count; ++n) {
        const std::uint32_t moby = kVisualMobys + n * 0x100u;
        const int cls = classes[n];
        set<std::uint16_t>(moby + kMobyClass, static_cast<std::uint16_t>(cls == 0 ? 1490 : cls));
        g_memory[moby + 0x20u] = cls == 0 ? 0xFFu : 0x01u;
    }
}

// Returns the material the port draws the weapon moby in slot `n` with (0 for none), and its rgb.
std::uint32_t visual_material(std::uint32_t n) {
    const auto found = g_materials.find(kVisualMobys + n * 0x100u);
    return found != g_materials.end() ? found->second : 0u;
}

std::uint32_t visual_rgb(std::uint32_t n) {
    const auto found = g_glows.find(kVisualMobys + n * 0x100u);
    return found != g_glows.end() ? found->second : 0u;
}

// Checks the weapons' looks with the Lancer at Beta: its gun and Heavy Lancer's in Beta's cyan at
// Beta's strength on a port with energy, a dead slot, another class and an unowned weapon's bomb
// left alone; on an older port the guns as the game draws them, and on 1.22 nothing.
void test_weapon_visuals() {
    // The Lancer, Heavy Lancer, a dead slot, a class no weapon owns, a Gravity Bomb (not owned).
    lay_out_mobys({1490, 3077, 0, 9999, 3299});
    tick(1);
    const bool energy = energy_host(g_host);
    // Beta: the second of eight steps (effect_strength), from the first's strength.
    const std::uint32_t beta = energy_at(kEffectFirst + (kEffectTop - kEffectFirst + 3) / 7);

    // Energy: the Lancer's two guns in Beta's cyan, raised to full strength (0x00E0FF).
    CHECK(visual_material(0) == (energy ? beta : 0u));
    CHECK(visual_material(1) == (energy ? beta : 0u));
    CHECK(!energy || (visual_rgb(0) == 0x00E0FFu && visual_rgb(1) == 0x00E0FFu));
    CHECK(visual_material(2) == 0u && visual_material(3) == 0u && visual_material(4) == 0u);

    // Its slot taken by another class: the new moby gets its own look, the Gravity Bomb's none.
    lay_out_mobys({3299, 3077, 0, 9999, 3299});
    g_materials.erase(kVisualMobys);
    tick(1);
    CHECK(visual_material(0) == 0u);
    CHECK(visual_material(1) == (energy ? beta : 0u));

    // A level left: the port forgets the looks; back in a level, they are drawn again.
    g_materials.clear();
    g_glows.clear();
    g_level_now = -1;
    tick(1);
    g_level_now = 1;
    tick(1);
    CHECK(visual_material(1) == (energy ? beta : 0u));

    // The table taken away for the tests after.
    lay_out_mobys({});
    tick(1);
    g_materials.clear();
    g_glows.clear();
}

// Checks the RYNO II reaching Heavy by experience, and the levels kept by the save's clock.
void test_ryno_heavy_and_kept() {
    set_experience(kRyno, 37'499);
    tick(1);
    CHECK(g_notice.find("RYNO") == std::string::npos);
    set_experience(kRyno, 37'500);
    tick(1);
    CHECK(g_notice == "Heavy RYNO II");
    CHECK(get<std::uint16_t>(row(kRyno) + kRowAmmoMax) == 150);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 0);  // Alpha is for sale at once

    // Saved (a new clock): kept. A new game (zeros) has none; the save loaded again has them.
    set_clock(0x20);
    tick(1);
    set_clock(0);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 500);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 37'500);  // toward Heavy again
    set_clock(0x20);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 0);
}

// Returns the vendor list place of the RYNO II's entry, or -1.
int ryno_place() {
    int place = -1;

    // Each entry of the list.
    for (int n = 0; n < vendor_count(); ++n) {
        // The RYNO II's entry.
        if (get<std::int32_t>(entry(n) + kEntryItem) == kRyno) {
            place = n;
        }
    }

    return place;
}

// Buys the RYNO II's tier for sale at the vendor's last entry, named `name`, and checks it.
void buy_ryno(const std::string& name) {
    hooked<rac2::Vendor_BuildList>()(0);
    CHECK(vendor_count() == 4);
    CHECK(ryno_place() == 3);
    set_cursor(3);
    CHECK(title(kRynoLabelText) == name);
    std::int32_t money = 30'000'000;
    CHECK(purchase(money) > 0);
    CHECK(g_notice == name);
    CHECK(vendor_count() == 3);  // the next is by experience: no entry
    tick(1);
}

// Levels the RYNO II up to `name` by `exp` of experience, checking the bar's goal before.
void level_ryno(std::int32_t exp, const std::string& name) {
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == exp);
    set_experience(kRyno, exp - 1);
    tick(1);
    CHECK(g_notice != name);
    set_experience(kRyno, exp);
    tick(1);
    CHECK(g_notice == name);
}

// Checks the RYNO II at the vendor after the Lancer's Gamma, from Heavy: Alpha for sale at once
// (named by its own row's text, having no version 2 row), then the bar toward Beta.
void test_ryno_at_the_vendor() {
    set<std::int32_t>(row(kRyno) + kRowLabel, kRynoLabelText);
    buy_ryno("Alpha RYNO II");
    CHECK(title(kRynoLabelText) == "Alpha RYNO II");
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 50'000);  // toward Beta
}

// Checks the rest of the RYNO II's path, from Beta (reached in hand): Gamma for sale at once,
// Delta, Epsilon, Zeta and Eta in turn, Omega by experience, and Ultima, its last, by experience
// after Omega, with a full bar and nothing for sale or to level after it.
void test_ryno_to_ultima() {
    set<std::int32_t>(g_disc->hero + kHeroHeld, -1);
    set_experience(kRyno, 0);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 0);  // Gamma for sale at once
    buy_ryno("Gamma RYNO II");
    level_ryno(75'000, "Delta RYNO II");
    buy_ryno("Epsilon RYNO II");
    level_ryno(100'000, "Zeta RYNO II");
    buy_ryno("Eta RYNO II");
    level_ryno(150'000, "Omega RYNO II");
    tick(1);
    level_ryno(187'500, "Ultima RYNO II");
    tick(1);
    // The last step: a full bar, no goal, nothing at the vendor, no step for more experience.
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 0);
    hooked<rac2::Vendor_BuildList>()(0);
    CHECK(vendor_count() == 3);
    g_notice.clear();
    set_experience(kRyno, 10'000'000);
    tick(1);
    CHECK(g_notice.empty());
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 0);
}

// Returns the vendor list place of item `item`'s upgrade entry, or -1.
int upgrade_place(int item) {
    int place = -1;

    // Each entry of the list.
    for (int n = 0; n < vendor_count(); ++n) {
        // The item's upgrade entry.
        if (get<std::int32_t>(entry(n) + kEntryItem) == item
            && get<std::int32_t>(entry(n) + kEntryUpgrade) != 0) {
            place = n;
        }
    }

    return place;
}

// Checks a tier earned by a weapon in hand: the game's in-hand level-up, then at its end the
// game's popup from its own level-up table named at the tier. The RYNO II has no line there, so
// the game shows text 0, an empty box (0.3.5): the tier's name fills it. The Lancer's line names
// its last version ("Ultra", the test's "Mega"): the tier's name takes its place. Each popup is
// named once; after it text 0 is the game's again.
void test_in_hand_level_up() {
    set<std::int32_t>(g_disc->hero + kHeroReady, 1);

    // The RYNO II in hand, at a tier by experience: no popup of the mod's, the animation instead.
    set<std::int32_t>(g_disc->hero + kHeroHeld, kRyno);
    g_fx_item = -1;
    const int banners = g_banners;
    set_experience(kRyno, 1'000'000);
    tick(1);
    CHECK(g_fx_item == kRyno);
    CHECK(g_banners == banners);
    const std::string ryno = string_at(port_text(0));
    CHECK(ryno.size() > std::string(" RYNO II").size());
    CHECK(ryno.find(" RYNO II") == ryno.size() - std::string(" RYNO II").size());
    CHECK(string_at(port_text(0)).empty());  // shown once

    // The Lancer: Gamma bought, then Delta by experience in hand.
    hooked<rac2::Vendor_BuildList>()(0);
    set_cursor(upgrade_place(kLancer));
    std::int32_t money = 100'000'000;
    CHECK(purchase(money) > 0);
    set<std::int32_t>(g_disc->hero + kHeroHeld, kLancer);
    g_fx_item = -1;
    set_experience(kLancer, 1'000'000);
    tick(1);
    CHECK(g_fx_item == kLancer);
    // Its own name in the HUD on the way, then the popup: the tier's name for the version's.
    CHECK(string_at(port_text(kMegaLancerText)) == "Delta Heavy Lancer");
    CHECK(
        string_at(port_text(kLevelUpLineText))
        == "\x0c"
           "Delta Heavy Lancer\x08 upgraded"
    );
    CHECK(string_at(port_text(0)).empty());  // the popup was named: text 0 is the game's
    set<std::int32_t>(g_disc->hero + kHeroHeld, -1);
}

// Buys the Lancer's Gamma at the vendor, taking its price from the bolts as the game does, and
// checks its most ammo.
void buy_lancer_gamma() {
    hooked<rac2::Vendor_BuildList>()(0);
    set_cursor(upgrade_place(kLancer));
    std::int32_t money = 100'000'000;
    const std::int32_t price = purchase(money);
    CHECK(price > 0);
    set<std::int32_t>(g_disc->progress, get<std::int32_t>(g_disc->progress) - price);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == kLancerGammaAmmo);
}

// Checks a restarted game (a new version of the mod): the steps saved are kept, so the save loaded
// has them, at the title where its own clock shows before its level starts, and after the game
// stamps a new one; a step bought after the last save is gone, as the bolts it cost come back with
// the save; then a new game has none.
void test_a_restart_keeps_the_saved_steps(openrac_mod_load_fn load) {
    // Saved after the RYNO II's Alpha (a new clock), then the Lancer's Gamma bought and not saved.
    set<std::int32_t>(g_disc->progress, 9'000'000);
    set_clock(0x30);
    tick(1);
    buy_lancer_gamma();

    // Twice: the second restart loads the clock the first one stamped.
    for (int restart = 0; restart < 2; ++restart) {
        CHECK(reload(load));
        // A restarted game has the table as the disc has it.
        set<std::int32_t>(row(kRyno) + kRowExpGoal, 0);
        set<std::uint16_t>(row(kLancerV4) + kRowAmmoMax, 500);
        set<std::uint16_t>(row(kRyno) + kRowAmmoMax, 100);
        set_clock(0);
        tick(1);
        CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 37'500);  // nothing yet
        g_level_now = -1;
        tick(1);
        set<std::int32_t>(g_disc->progress, 9'000'000);  // the save's bolts
        set_clock(restart == 0 ? 0x30 : 0x50);
        tick(2);
        g_level_now = 1;
        set_clock(static_cast<std::uint8_t>(0x50 + restart));
        tick(1);
        CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 50'000);
        CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);  // Beta: Gamma unsaved
    }

    // A new game after it: nothing; its first clock (the counter 0, the weapons gone) nothing
    // either.
    set_clock(0);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 37'500);
    set<std::int16_t>(g_disc->progress + kProgressCycle, 0);
    set_experience(kLancer, 0);
    set_experience(kRyno, 0);
    g_memory[g_disc->slot + kLancer] = kLancer;
    tick(1);
    set_clock(0x58);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 37'500);
    g_memory[g_disc->slot + kLancer] = kLancerV4;
}

// Puts save A's data in the game (the one played above, its last clock 0x51).
void load_save_a() {
    set_experience(kRyno, 1234);
    set_experience(kLancer, 77);
    set<std::int32_t>(g_disc->progress, 5000);
}

// Checks save A (tiered) and save B (never tiered) loaded at the title: each with its own tiers.
void test_each_save_has_its_own() {
    // A at the title: its tiers.
    g_level_now = -1;
    set_clock(0);
    tick(1);
    load_save_a();
    set_clock(0x51);
    tick(2);
    g_level_now = 1;
    set_clock(0x52);  // stamped again as its level starts
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 50'000);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(experience(kRyno) == 1234 * kExpUnit);  // started: kept

    // B at the title: its own data and a clock the book has never seen. No tiers, and its weapons'
    // experience counted from 0 as for any save new to the mod.
    g_level_now = -1;
    tick(1);
    // Another slot's save: its tiered weapons hold no experience, so A's tiers do not carry to it.
    set_experience(kRyno, 0);
    set_experience(kLancer, 0);
    set<std::int32_t>(g_disc->progress, 123);
    set_clock(0x60);
    tick(2);
    g_level_now = 1;
    set_clock(0x61);
    tick(2);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 37'500);  // toward Heavy: no tier
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 500);
    CHECK(experience(kRyno) == 0);
    CHECK(experience(kLancer) == 0);
    CHECK(string_at(port_text(kMegaLancerText)) == "Mega Lancer");  // the game's name
}

// Checks saves A and B loaded from inside each other's levels, and challenge mode's next game after
// A: each save with its own tiers, A's carried on.
void test_saves_from_inside_a_level() {
    // A again, loaded from inside B's level (its data with its clock): its tiers back.
    load_save_a();
    set_clock(0x52);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 50'000);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(experience(kRyno) == 1234 * kExpUnit);

    // B again, from inside A's level: none.
    set_experience(kRyno, 0);
    set_experience(kLancer, 0);
    set<std::int32_t>(g_disc->progress, 123);
    set_clock(0x61);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 37'500);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 500);

    // A once more, then challenge mode's next game: the clock to zeros with the weapons kept and
    // the counter up, then a new clock: A's tiers go on, its experience too.
    load_save_a();
    set_clock(0x52);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 50'000);
    set<std::int16_t>(g_disc->progress + kProgressCycle, 2);
    set_clock(0);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 37'500);  // nothing while zeros
    CHECK(experience(kRyno) == 1234 * kExpUnit);                   // not zeroed while zeros
    set_clock(0x70);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 50'000);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(experience(kRyno) == 1234 * kExpUnit);
    set<std::int16_t>(g_disc->progress + kProgressCycle, 1);
}

// Puts save S's data in the game: A's, with its own bolts.
void load_save_s(std::int32_t bolts) {
    load_save_a();
    set<std::int32_t>(g_disc->progress, bolts);
}

// Returns the Lancer's level byte in the book's record of the clock `first` (set_clock's), or -1.
int booked_lancer(std::uint8_t first) {
    const std::string text = file_text(g_dir / "tiers.txt");
    char clock[17];
    std::snprintf(
        clock,
        sizeof clock,
        "%02x%02x%02x%02x%02x%02x%02x%02x",
        first,
        first + 1,
        first + 2,
        first + 3,
        first + 4,
        first + 5,
        first + 6,
        first + 7
    );
    const std::size_t at = text.rfind(std::string(clock) + " ");

    // No record of that clock.
    if (at == std::string::npos) {
        return -1;
    }

    const std::string hex = text.substr(at + 17 + 2 * static_cast<std::size_t>(kLancer), 2);
    return static_cast<int>(std::strtol(hex.c_str(), nullptr, 16)) & 0x7F;
}

// Checks a save whose clock moved while the mod was not running: the closest earlier record's
// levels carry to it and are booked under the new clock, and the book is backed up for the day.
void test_clock_moved_unseen() {
    g_level_now = -1;
    set_clock(0);
    tick(1);
    load_save_s(4'000'000);
    const int before = booked_lancer(0x90);
    CHECK(before >= 1);
    CHECK(booked_lancer(0xB0) == -1);
    set_clock(0xB0);
    tick(2);
    CHECK(g_log.find("carried the levels of the book's clock") != std::string::npos);
    CHECK(booked_lancer(0xB0) == before);
    CHECK(booked_lancer(0x90) == before);

    // One dated backup next to the book.
    int backups = 0;
    for (const auto& entry : std::filesystem::directory_iterator(g_dir)) {
        const std::string name = entry.path().filename().string();
        backups += name.rfind("tiers-", 0) == 0 && name.size() == 18 ? 1 : 0;
    }
    CHECK(backups == 1);
}

// Checks that the tiers follow the saves as the bolts do (0.5.2): a tier bought after the last save
// is the game in progress's only, so another save loaded and this one loaded back (or this one
// loaded again at once, its clock unchanged) has the tier gone with the bolts back; a tier bought
// and saved stays with that save; two saves keep their own; a restart keeps what was saved.
void test_tiers_follow_the_saves(openrac_mod_load_fn load) {
    // Save S: A's tiers (the Lancer at Beta), saved with 4,000,000 bolts.
    load_save_s(4'000'000);
    set_clock(0x80);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(booked_lancer(0x80) == 2);

    // Bought, not saved: the book's record of S is as S was saved.
    buy_lancer_gamma();
    CHECK(booked_lancer(0x80) == 2);

    // B loaded, then S loaded back: the Gamma gone, as the bolts it cost come back.
    set_experience(kRyno, 0);
    set_experience(kLancer, 0);
    set<std::int32_t>(g_disc->progress, 123);
    set_clock(0x61);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 500);
    load_save_s(4'000'000);
    set_clock(0x80);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 50'000);  // the RYNO II's Alpha kept

    // Bought again, then S loaded again at once: the clock stays S's, its data comes back.
    buy_lancer_gamma();
    load_save_s(4'000'000);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(g_log.find("the save in play loaded again") != std::string::npos);
    // Its level starting stamps a new clock: S's tiers go on under it.
    set_clock(0x81);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(booked_lancer(0x81) == 2);

    // Bought and saved as T: kept with T, through B and back.
    buy_lancer_gamma();
    const std::int32_t t_bolts = get<std::int32_t>(g_disc->progress);
    set_clock(0x90);
    tick(1);
    CHECK(booked_lancer(0x90) == 3);
    CHECK(booked_lancer(0x80) == 2);
    set_experience(kRyno, 0);
    set_experience(kLancer, 0);
    set<std::int32_t>(g_disc->progress, 123);
    set_clock(0x61);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 500);
    load_save_s(t_bolts);
    set_experience(kLancer, 0);  // the Gamma's purchase started its count again
    set_clock(0x90);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == kLancerGammaAmmo);

    // Two saves, each with its own: S at Beta, T at Gamma, either way round.
    load_save_s(4'000'000);
    set_clock(0x80);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    load_save_s(t_bolts);
    set_experience(kLancer, 0);
    set_clock(0x90);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == kLancerGammaAmmo);

    // Saved and quit, the game started again: T loaded at the title has its Gamma.
    CHECK(reload(load));
    // A restarted game has the table as the disc has it.
    set<std::int32_t>(row(kRyno) + kRowExpGoal, 0);
    set<std::uint16_t>(row(kLancerV4) + kRowAmmoMax, 500);
    set<std::uint16_t>(row(kRyno) + kRowAmmoMax, 100);
    set_clock(0);
    tick(1);
    g_level_now = -1;
    tick(1);
    set_clock(0x90);
    tick(2);
    g_level_now = 1;
    set_clock(0x91);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == kLancerGammaAmmo);

    // S again for the tests after: the Lancer at Beta.
    load_save_s(4'000'000);
    set_clock(0x80);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
}

// Sets the challenge mode counter and the kill streak.
void set_run(std::int16_t cycle, std::uint8_t streak) {
    set<std::int16_t>(g_disc->progress + kProgressCycle, cycle);
    set<std::uint8_t>(g_disc->progress + kProgressStreak, streak);
}

// Makes the enemy a tough one (defined below).
void lay_out_enemy();

// Checks the experience of a kill: the game's in the first challenge mode; from the second on
// times the run's x1.5 (an enemy of 20 bolts or none), times the fourth root of a bigger enemy's
// bolts over 20 (320: x2), a boss's x5 and the final boss's x10 instead; x4.5 from cycle 5.
void test_kill_experience() {
    const auto enemy_class = get<std::uint16_t>(kMobyEnemy + kMobyClass);
    const auto bolts = get<std::int16_t>(kMobyEnemy + kMobyBolts);
    const auto info = get<std::uint32_t>(kMobyEnemy + kMobyClassInfo);
    const auto cycle = get<std::int16_t>(g_disc->progress + kProgressCycle);
    g_kill_item = kLancer;
    g_kill_xp = 1000;

    // The first challenge mode: the game's.
    set<std::int32_t>(g_disc->exp + 4u * kLancer, 0);
    set<std::int16_t>(g_disc->progress + kProgressCycle, 1);
    hooked<rac2::Moby_RecordDeath>()(kMobyOther, 0, 3);
    CHECK(experience(kLancer) == 1000);

    // Cycle 2, a moby with no bolts of its own: x1.5.
    set<std::int32_t>(g_disc->exp + 4u * kLancer, 0);
    set<std::int16_t>(g_disc->progress + kProgressCycle, 2);
    hooked<rac2::Moby_RecordDeath>()(kMobyOther, 0, 3);
    CHECK(experience(kLancer) == 1500);

    // A big enemy (320 bolts): x1.5 x 2.
    set<std::int32_t>(g_disc->exp + 4u * kLancer, 0);
    lay_out_enemy();
    set<std::int16_t>(kMobyEnemy + kMobyBolts, 320);
    hooked<rac2::Moby_RecordDeath>()(kMobyEnemy, 0, 3);
    CHECK(experience(kLancer) == 3000);

    // Cycle 5: the final boss x4.5 x 10, a boss x4.5 x 5.
    set<std::int16_t>(g_disc->progress + kProgressCycle, 5);
    set<std::int32_t>(g_disc->exp + 4u * kLancer, 0);
    set<std::uint16_t>(kMobyEnemy + kMobyClass, 4576);
    hooked<rac2::Moby_RecordDeath>()(kMobyEnemy, 0, 3);
    CHECK(experience(kLancer) == 45'000);
    set<std::int32_t>(g_disc->exp + 4u * kLancer, 0);
    set<std::uint16_t>(kMobyEnemy + kMobyClass, 2644);
    hooked<rac2::Moby_RecordDeath>()(kMobyEnemy, 0, 3);
    CHECK(experience(kLancer) == 22'500);

    set<std::uint16_t>(kMobyEnemy + kMobyClass, enemy_class);
    set<std::int16_t>(kMobyEnemy + kMobyBolts, bolts);
    set<std::uint32_t>(kMobyEnemy + kMobyClassInfo, info);
    set<std::int16_t>(g_disc->progress + kProgressCycle, cycle);
    set<std::int32_t>(g_disc->exp + 4u * kLancer, 0);
}

// Makes the enemy a tough one: its class record says enemy, its own bolts are 80 (x2 toughness).
void lay_out_enemy() {
    set<std::uint32_t>(kMobyEnemy + kMobyClassInfo, kEnemyClassAt);
    set<std::int16_t>(kEnemyClassAt + kClassKind, kKindEnemy);
    set<std::int16_t>(kMobyEnemy + kMobyBolts, 80);
}

// Returns what the coins made are worth, a platinum one (drawn so) five times its class.
std::int32_t coins_worth() {
    std::int32_t total = 0;

    // Each coin, as the player will collect it.
    for (const Coin& coin : g_coins) {
        const auto found = g_materials.find(coin.moby);
        const bool rare = found != g_materials.end();
        const std::int32_t times = !rare                                ? 1
                                   : found->second == kMaterialObsidian ? kObsidianTimes
                                                                        : kPlatinumTimes;
        total += coin.value * times;
    }

    return total;
}

// Checks a drop in challenge mode's fourth run (cycle 3) at a x20 streak from a tough enemy: the
// game's own burst (1,500 doubled: three 1,000s), its 3,000 times five (x2.5 the run, x2 its
// toughness), 15,000 made again as 9,750 in platinum and 5,250 in gold at the game's places;
// collecting pays five times a platinum coin's class and takes its material off.
void test_platinum_drop() {
    lay_out_enemy();
    set_run(3, 20);
    g_coins.clear();
    g_materials.clear();
    port_spawn_pickups(kMobyEnemy, 1500, 9);
    CHECK(g_burst_high == 1500);
    CHECK(g_coins.size() == 15);
    CHECK(g_materials.size() == 7);
    CHECK(coins_worth() == 15'000);
    CHECK(g_sp == kStackTop);

    // The biggest first: a platinum 5,000 (the 1,000 coin drawn as platinum), then gold 1,000s.
    CHECK(g_coins.size() > 1 && g_coins[0].value == 1000 && g_coins[1].value == 1000);
    CHECK(g_materials.count(g_coins[0].moby) == 1);
    CHECK(g_materials[g_coins[0].moby] == kMaterialPlatinum);
    CHECK(g_materials.count(g_coins[1].moby) == 0);

    // Each coin starts where one of the game's three did, round in turn.
    for (std::size_t n = 0; n < g_coins.size(); ++n) {
        CHECK(g_coins[n].x == static_cast<float>(n % 3));
    }

    // The platinum 5,000, then a gold 1,000.
    g_bolts = 0;
    port_collect(g_coins[0].moby);
    CHECK(g_bolts == 5000);
    CHECK(g_materials.count(g_coins[0].moby) == 0);
    port_collect(g_coins[1].moby);
    CHECK(g_bolts == 6000);

    // The rest: the whole drop.
    for (std::size_t n = 2; n < g_coins.size(); ++n) {
        port_collect(g_coins[n].moby);
    }

    CHECK(g_bolts == 15'000);
    CHECK(g_materials.empty());
}

// Checks obsidian on a port of 1.24: the same drop of 15,000 in challenge mode's fourth run (cycle
// 3) at a x20 streak gives 8% as obsidian first (1,200: two 500s, a 125 and three 25s, each class
// with its own glow), then platinum's 65% (9,750) and gold; an obsidian coin collected is worth 25
// times its class. Without a streak there are only hints: 1% of a drop of 7,500.
void test_obsidian_drop() {
    lay_out_enemy();
    set_run(3, 20);
    g_coins.clear();
    g_materials.clear();
    g_glows.clear();
    port_spawn_pickups(kMobyEnemy, 1500, 9);
    CHECK(coins_worth() == 15'000);
    CHECK(g_sp == kStackTop);
    CHECK(g_log.find("interface 1.24") == std::string::npos);

    std::map<std::int32_t, int> obsidian;  // Obsidian coins by their gold class's worth.
    std::uint32_t glow_500 = 0;
    std::uint32_t glow_25 = 0;

    // The coins drawn as obsidian, by class, and their glows.
    for (const Coin& coin : g_coins) {
        // Only the obsidian ones.
        if (g_materials.count(coin.moby) == 0 || g_materials[coin.moby] != kMaterialObsidian) {
            continue;
        }

        ++obsidian[coin.value];

        // The biggest and the smallest.
        if (coin.value == 20) {
            glow_500 = g_glows[coin.moby];
        } else if (coin.value == 1) {
            glow_25 = g_glows[coin.moby];
        }
    }

    CHECK(obsidian.size() == 3 && obsidian[20] == 2 && obsidian[5] == 1 && obsidian[1] == 3);
    // A shade per size: the bigger coin redder, each a colour, not the default white.
    CHECK(glow_500 != glow_25 && glow_500 != kNoTint && glow_25 != kNoTint);
    CHECK((glow_500 >> 16) >= (glow_25 >> 16));
    // The biggest first: obsidian before platinum and gold of a size.
    CHECK(!g_coins.empty() && g_coins[0].value == 1000);
    CHECK(g_materials[g_coins[0].moby] == kMaterialPlatinum);

    // Collected: an obsidian 500 (a 20 coin) first, then the rest, the whole drop.
    g_bolts = 0;
    std::uint32_t first = 0;

    // The first obsidian 20 coin.
    for (const Coin& coin : g_coins) {
        // Found.
        if (coin.value == 20 && g_materials[coin.moby] == kMaterialObsidian) {
            first = coin.moby;
            break;
        }
    }

    port_collect(first);
    CHECK(g_bolts == 500);
    CHECK(g_materials.count(first) == 0);

    // Every other coin.
    for (const Coin& coin : g_coins) {
        // Not the one collected.
        if (coin.moby != first) {
            port_collect(coin.moby);
        }
    }

    CHECK(g_bolts == 15'000);
    CHECK(g_materials.empty());

    // No streak: the 1% of a 7,500 drop at x1 is 75, three obsidian 25s.
    set_run(3, 1);
    g_coins.clear();
    port_spawn_pickups(kMobyEnemy, 750, 9);
    int obsidian_coins = 0;

    // The obsidian coins of the drop.
    for (const Coin& coin : g_coins) {
        obsidian_coins +=
            g_materials.count(coin.moby) != 0 && g_materials[coin.moby] == kMaterialObsidian ? 1
                                                                                             : 0;
    }

    CHECK(obsidian_coins == 3);
    CHECK(coins_worth() == 7'500);
}

// Checks kills under the game's own coin limit (three coins, as SetDeathBits asks) in challenge
// mode's fourth run (cycle 3) at a x20 streak: the game's burst keeps its limit and its word, and
// the mod scales what its three coins hold. A small enemy (10 bolts of its own: the game asks for
// 200, doubled 400) leaves three 100s and 100 over x20 (5) in the word: 300 times the run's x2.5,
// 750 in at most five coins. A big one (320 bolts: 6,400, doubled 12,800) leaves three 1,000s and
// 9,800 over x20 (490) in the word: 3,000 times x2.5 and its toughness x2 (0.4.1 paid 128,000),
// 15,000 in at most seven coins, among them an obsidian 500 and a platinum 5,000, what the coins
// can't hold added to the bolts at once.
void test_kill_drops() {
    lay_out_enemy();
    set_run(3, 20);
    const auto word = g_disc->progress + kProgressBoltCredit;
    const std::int32_t credit = get<std::int32_t>(word);

    // The small enemy.
    set<std::int16_t>(kMobyEnemy + kMobyBolts, 10);
    g_coins.clear();
    g_materials.clear();
    g_bolts = 0;
    port_kill_drop(kMobyEnemy, 200);
    CHECK(g_burst_high == 200);
    CHECK(g_coins.size() <= 5);
    CHECK(coins_worth() + g_bolts == 750);
    CHECK(get<std::int32_t>(word) == credit + 5);

    // The big one.
    set<std::int16_t>(kMobyEnemy + kMobyBolts, 320);
    g_coins.clear();
    g_materials.clear();
    g_bolts = 0;
    port_kill_drop(kMobyEnemy, 6400);
    CHECK(g_burst_high == 6400);
    CHECK(g_coins.size() > 3 && g_coins.size() <= 7);
    CHECK(coins_worth() + g_bolts == 15'000);
    CHECK(coins_worth() == 12'000);  // a platinum 5,000 and 2,500, four 1,000s, an obsidian 500
    CHECK(get<std::int32_t>(word) == credit + 5 + 490);
    bool obsidian_500 = false;
    bool platinum_5000 = false;

    // The rare coins.
    for (const Coin& coin : g_coins) {
        const auto found = g_materials.find(coin.moby);

        // Only the rare ones.
        if (found == g_materials.end()) {
            continue;
        }

        obsidian_500 |= found->second == kMaterialObsidian && coin.value == 20;
        platinum_5000 |= found->second == kMaterialPlatinum && coin.value == 1000;
    }

    CHECK(obsidian_500);
    CHECK(platinum_5000);

    // Collected, the coins pay what they showed: with what was added at once, the whole drop.
    for (const Coin& coin : g_coins) {
        port_collect(coin.moby);
    }

    CHECK(g_bolts == 15'000);
    CHECK(g_materials.empty());
    set<std::int16_t>(kMobyEnemy + kMobyBolts, 80);
    set<std::int32_t>(word, credit);
    set_run(1, 0);
}

// Checks that what a kill takes back out of the game's word is paid as it is, never scaled again
// (0.4.1's runaway: coins it could not make went to the word, whose 2% a kill was scaled again). A
// word of 1,000,000 in challenge mode's fourth run (x2.5) at x1: a small enemy's 10 bolts and
// 20,000 of the word, doubled, fill the game's three 1,000s; all 3,000 are the word's, so the drop
// is 3,000, not 7,500.
void test_word_not_scaled() {
    lay_out_enemy();
    set_run(3, 1);
    const auto word = g_disc->progress + kProgressBoltCredit;
    const std::int32_t credit = get<std::int32_t>(word);
    set<std::int32_t>(word, 1'000'000);
    set<std::int16_t>(kMobyEnemy + kMobyBolts, 10);
    g_coins.clear();
    g_materials.clear();
    g_bolts = 0;
    g_death_bolts = 10;
    hooked<rac2::Moby_RecordDeath>()(kMobyEnemy, 0, 3);
    g_death_bolts = 0;
    CHECK(g_burst_high == 20'010);
    CHECK(coins_worth() + g_bolts == 3'000);

    // A kill with no word to take from: its own drop is scaled (10 bolts, doubled 20, x2.5).
    set<std::int32_t>(word, 0);
    g_coins.clear();
    g_bolts = 0;
    g_death_bolts = 10;
    hooked<rac2::Moby_RecordDeath>()(kMobyEnemy, 0, 3);
    g_death_bolts = 0;
    CHECK(coins_worth() + g_bolts == 50);

    // A word 0.4.1 filled is cut back to 100,000 at the next tick, and said.
    set<std::int32_t>(word, 1'500'000);
    tick(1);
    CHECK(get<std::int32_t>(word) == 100'000);
    CHECK(g_log.find("cut back to 100000") != std::string::npos);
    set<std::int16_t>(kMobyEnemy + kMobyBolts, 80);
    set<std::int32_t>(word, credit);
    set_run(1, 0);
}

// Checks that a level left forgets its platinum coins: a moby of the next level is worth its class.
void test_level_left() {
    set_run(3, 20);
    g_coins.clear();
    port_spawn_pickups(kMobyEnemy, 1500, 9);
    g_level_now = 2;
    tick(1);
    g_bolts = 0;
    port_collect(g_coins[0].moby);
    CHECK(g_bolts == 1000);
    g_level_now = 1;
    tick(1);
    g_materials.clear();
}

// Checks the drops before cycle 2: the first game's the game's own, the first challenge mode's
// unscaled with a taste of platinum; and (from cycle 2) a crate's: the game's coins scaled by the
// run only, at the least platinum share.
void test_drops_before_and_crates() {
    set_run(0, 20);
    g_coins.clear();
    port_spawn_pickups(kMobyEnemy, 100, 9);
    CHECK(g_burst_high == 100);
    CHECK(g_coins.size() == 1);  // 100 (no doubling in the first game)
    CHECK(g_materials.empty());

    // The first challenge mode at x20: the game's 200 (doubled), unscaled, 40% of it platinum.
    set_run(1, 20);
    g_coins.clear();
    port_spawn_pickups(kMobyEnemy, 100, 9);
    CHECK(g_burst_high == 100);
    CHECK(coins_worth() == 200);
    CHECK(g_materials.size() == 4);  // 80 as three platinum 25s and a platinum 5, 120 in gold
    g_materials.clear();

    // A crate in challenge mode's third run (cycle 2), no streak: x1.5 the run, 10% platinum.
    set_run(2, 1);
    g_coins.clear();
    port_spawn_pickups(kMobyOther, 100, 9);
    CHECK(g_burst_high == 100);
    CHECK(coins_worth() == 300);
    CHECK(g_materials.size() == 2);  // 30 as a platinum 25 and a platinum 5
    g_materials.clear();
    set_run(1, 0);
}

// Checks a port without the bolts (1.22), or the setting off: no bolt hook point taken, the drop
// as the game makes it.
void test_drops_untouched() {
    lay_out_enemy();
    set_run(3, 20);
    g_coins.clear();
    port_spawn_pickups(kMobyEnemy, 1500, 9);
    CHECK(g_burst_high == 1500);
    CHECK(g_coins.size() == 3);
    CHECK(g_materials.empty());
    CHECK(coins_worth() == 3000);
    set_run(1, 0);
}

// Runs every check on `disc`, with the library at `path`, against a port of `host`.
// Checks the collectors in challenge mode's fourth run (cycle 3, x2.5): Tabora's dialog shows the
// scaled total of the crystals held (4 at 10,000: 100,000), and the trade pays it, the game's
// 40,000 and the mod's 60,000; Grelbin's moonstones the same (3 at 30,000: 225,000). In the first
// challenge mode, and with bolt scaling off, the game's own dialog and pay.
void test_collectors(bool scaled) {
    const std::uint32_t block = g_disc->progress;
    set_run(3, 1);
    set<std::int32_t>(block + kProgressBolts, 1'000);
    set<std::int16_t>(block + kProgressCrystals, 4);
    set<std::int32_t>(block + kProgressMoonstones, 3);

    // Tabora: the dialog, then the trade as the game makes it.
    g_level_now = 8;
    tick(1);
    CHECK(
        string_at(port_text(g_disc->crystal_trade))
        == (scaled ? "Trade %d crystals for 100000 bolts?" : "Trade %d crystals for %d bolts?")
    );
    g_bolts = 0;
    set<std::int32_t>(block + kProgressBolts, 41'000);
    set<std::int16_t>(block + kProgressCrystals, 0);
    tick(1);
    CHECK(g_bolts == (scaled ? 60'000 : 0));
    // Nothing more on the next tick, and no dialog of nothing.
    tick(1);
    CHECK(g_bolts == (scaled ? 60'000 : 0));
    CHECK(string_at(port_text(g_disc->crystal_trade)) == "Trade %d crystals for %d bolts?");

    // Grelbin: the moonstones.
    g_level_now = 19;
    tick(1);
    CHECK(
        string_at(port_text(g_disc->moonstone_trade))
        == (scaled ? "%d moonstones: 225000 bolts" : "%d moonstones: %d bolts")
    );
    g_bolts = 0;
    set<std::int32_t>(block + kProgressBolts, 131'000);
    set<std::int32_t>(block + kProgressMoonstones, 0);
    tick(1);
    CHECK(g_bolts == (scaled ? 135'000 : 0));

    // A count that drops with no pay (a save loaded): nothing.
    set<std::int32_t>(block + kProgressMoonstones, 5);
    tick(1);
    g_bolts = 0;
    set<std::int32_t>(block + kProgressMoonstones, 0);
    tick(1);
    CHECK(g_bolts == 0);

    // The first challenge mode: the game's.
    set_run(1, 1);
    set<std::int16_t>(block + kProgressCrystals, 2);
    g_level_now = 8;
    tick(1);
    CHECK(string_at(port_text(g_disc->crystal_trade)) == "Trade %d crystals for %d bolts?");
    g_bolts = 0;
    set<std::int32_t>(block + kProgressBolts, 151'000);
    set<std::int16_t>(block + kProgressCrystals, 0);
    tick(1);
    CHECK(g_bolts == 0);
    g_level_now = 1;
}

// Returns the game address of the word `word` of disc table `table`.
std::uint32_t table_word(const LevelTable& table, std::uint32_t offset) {
    return table.address + offset;
}

// Returns the price word `word` of `table`.
std::int32_t price_word(const LevelTable& table, int word) {
    return get<std::int32_t>(table_word(table, static_cast<std::uint32_t>(word) * 4u));
}

// Returns race `race`'s prize `prize` (0: the first win's) of `table`.
std::int32_t race_prize(const LevelTable& table, int race, int prize) {
    const auto offset = static_cast<std::uint32_t>(race) * kRaceBytes + kRacePrize
                        + static_cast<std::uint32_t>(prize) * 4u;
    return get<std::int32_t>(table_word(table, offset));
}

// Lays out the game's prizes of `table` (Barlow's: race 0 pays 0, 250 and 250, race 1 2,000, 750
// and 250, the others 3,000, 1,000 and 350).
void lay_out_races(const LevelTable& table) {
    const std::int32_t prizes[3][3] = {{0, 250, 250}, {2'000, 750, 250}, {3'000, 1'000, 350}};

    // Each race, each prize.
    for (int race = 0; race < kRaces; ++race) {
        // The first, second and later wins.
        for (int prize = 0; prize < kRacePrizes; ++prize) {
            const auto offset = static_cast<std::uint32_t>(race) * kRaceBytes + kRacePrize
                                + static_cast<std::uint32_t>(prize) * 4u;
            set<std::int32_t>(table_word(table, offset), prizes[race < 2 ? race : 2][prize]);
        }
    }
}

// Lays out a level's price table as the discs have it: Notak's door 6,000, Tabora's 10 crystals,
// Dobbo's 10,000, 6,000, 25,000, the prison's 20,000, Smolg's 40,000 and Grelbin's 9 moonstones.
void lay_out_prices(const LevelTable& table) {
    const std::int32_t pairs[] = {6'000, 10, 10'000, 6'000, 25'000, 20'000, 40'000, 9, 9};

    // Each pair: the game's price and challenge mode's, the same on the discs.
    for (std::uint32_t pair = 0; pair < 9; ++pair) {
        set<std::int32_t>(table_word(table, pair * 8u), pairs[pair]);
        set<std::int32_t>(table_word(table, pair * 8u + 4u), pairs[pair]);
    }
}

// Checks the prizes and prices in challenge mode's fourth run (cycle 3, x2.5; prices x1.75): the
// race tables of Barlow and Joba paid at x2.5 while each is loaded (race 0's first win still 0),
// never scaled twice and scaled again after a level loaded anew; the arena's prize (the game's
// 50,000) at 125,000 when the port has the hook point; the sellers' prices at x1.75 (Notak's door
// 10,500, Dobbo's 17,500, the prison's 35,000, Smolg's 70,000), the crystals and moonstones left
// alone; the gadget sellers' rows (Joba's Levitator 20,000: 35,000); and in the first challenge
// mode the game's values written back. With bolt scaling off, the game's everywhere.
void test_prizes_and_prices(bool scaled_run, bool arena) {
    const RewardDisc* disc = reward_disc_of(g_disc->version);
    CHECK(disc != nullptr);

    // A disc without tables cannot be tested.
    if (disc == nullptr) {
        return;
    }

    set_run(3, 1);

    // Barlow's races.
    const LevelTable& barlow = disc->races[0];
    CHECK(barlow.level == kBarlow);
    lay_out_races(barlow);
    g_log.clear();
    g_level_now = kBarlow;
    tick(1);
    CHECK(race_prize(barlow, 0, 0) == 0);
    CHECK(race_prize(barlow, 0, 1) == (scaled_run ? 625 : 250));
    CHECK(race_prize(barlow, 1, 0) == (scaled_run ? 5'000 : 2'000));
    CHECK(race_prize(barlow, 1, 1) == (scaled_run ? 1'875 : 750));
    CHECK(race_prize(barlow, 4, 2) == (scaled_run ? 875 : 350));
    CHECK(
        (g_log.find("the races pay x2.50 (the second race 2000 -> 5000)") != std::string::npos)
        == scaled_run
    );
    // Not scaled twice.
    tick(3);
    CHECK(race_prize(barlow, 1, 0) == (scaled_run ? 5'000 : 2'000));
    // The level loaded anew: the game's prize again, scaled again.
    set<std::int32_t>(table_word(barlow, kRaceBytes + kRacePrize), 2'000);
    tick(1);
    CHECK(race_prize(barlow, 1, 0) == (scaled_run ? 5'000 : 2'000));

    // The first challenge mode: the game's prizes back.
    set_run(1, 1);
    tick(1);
    CHECK(race_prize(barlow, 1, 0) == 2'000);
    CHECK(race_prize(barlow, 0, 1) == 250);
    set_run(3, 1);

    // The space missions of Feltzin, Hrugis and Gorn: each mission's rewards at x2.5, logged once.
    for (const LevelTable& table : disc->missions) {
        // Each mission's 1,000, 500 and 250.
        for (int word = 0; word < kMissions * kMissionRewards; ++word) {
            const std::int32_t rewards[kMissionRewards] = {1'000, 500, 250};
            set<std::int32_t>(
                table_word(table, static_cast<std::uint32_t>(word) * 4u),
                rewards[word % kMissionRewards]
            );
        }

        g_log.clear();
        g_level_now = table.level;
        tick(2);
        CHECK(get<std::int32_t>(table_word(table, 0)) == (scaled_run ? 2'500 : 1'000));
        CHECK(get<std::int32_t>(table_word(table, 44)) == (scaled_run ? 625 : 250));
        CHECK(
            (g_log.find("the space missions pay x2.50 (the second mission 1000 -> 2500)")
             != std::string::npos)
            == scaled_run
        );
    }

    // Joba's races, and the arena.
    const LevelTable& joba = disc->races[1];
    CHECK(joba.level == kJoba);
    lay_out_races(joba);
    g_memory[g_disc->slot + 8] = 8;
    set<std::int32_t>(row(8) + kRowPrice, 20'000);
    g_level_now = kJoba;
    tick(1);
    CHECK(race_prize(joba, 2, 0) == (scaled_run ? 7'500 : 3'000));
    CHECK(get<std::int32_t>(row(8) + kRowPrice) == (scaled_run ? 35'000 : 20'000));
    const auto prize = hooked<hooks::Arena_Prize>();
    CHECK((prize != nullptr) == arena);

    // The arena's prize, at the run's scale; the game's in the first challenge mode.
    if (prize != nullptr) {
        CHECK(prize(3) == 125'000);
        set_run(1, 1);
        CHECK(prize(3) == kArenaPrize);
        set_run(3, 1);
    }

    // The sellers' price tables, each in its level.
    for (const LevelTable& table : disc->prices) {
        lay_out_prices(table);
        g_level_now = table.level;
        tick(1);
        CHECK(price_word(table, 0) == (scaled_run ? 10'500 : 6'000));
        CHECK(price_word(table, 1) == (scaled_run ? 10'500 : 6'000));
        CHECK(price_word(table, 2) == 10);
        CHECK(price_word(table, 5) == (scaled_run ? 17'500 : 10'000));
        CHECK(price_word(table, 6) == 6'000);
        CHECK(price_word(table, 8) == 25'000);
        CHECK(price_word(table, 11) == (scaled_run ? 35'000 : 20'000));
        CHECK(price_word(table, 12) == (scaled_run ? 70'000 : 40'000));
        CHECK(price_word(table, 14) == 9);
        CHECK(price_word(table, 16) == 9);
    }

    // Another level's tables are left alone: Smolg's at Dobbo.
    lay_out_prices(disc->prices[3]);
    g_level_now = kDobbo;
    tick(1);
    CHECK(price_word(disc->prices[3], 12) == 40'000);

    set_run(0, 1);
    g_memory[g_disc->slot + 8] = 0;
    g_level_now = 1;
    tick(1);
}

// Returns the game address of the test's port's copy `n` (from 0, in the order made).
std::uint32_t copy_made(int n) {
    return kCopiesAt + static_cast<std::uint32_t>(n) * 0x100u;
}

// Checks the copies at each level load on a port of 1.25: none in the first challenge mode, one
// of each enemy the port allows from the second (the wave's, the crystal creature, the moby of
// another kind and the game's own never asked for), two from the fourth (where the port finds no
// spot for one: a copy fewer); none in a party guest's game; a copy's drop is its original's.
void test_extra_enemies() {
    lay_out_enemy();
    g_level_now = 4;
    const auto copies_asked = [] {
        std::vector<std::pair<int, int>> asked;
        asked.swap(g_copies_asked);
        return asked;
    };

    // The first challenge mode: the game's enemies.
    set_run(1, 0);
    g_loads = 0;
    g_copies_asked.clear();
    g_copies_made = 0;
    port_level_load();
    CHECK(g_loads == 1);
    CHECK(copies_asked().empty());

    // The second: one copy of each of the two free enemies.
    set_run(2, 0);
    g_log.clear();
    port_level_load();
    CHECK((copies_asked() == std::vector<std::pair<int, int>>{{0, 0}, {1, 0}}));
    CHECK(g_copies_made == 2);
    CHECK(g_log.find("level 4: 2 extra enemies beside 2 (new game 2)") != std::string::npos);

    // The fourth: two each, slot 1's second with no spot.
    set_run(4, 0);
    g_log.clear();
    port_level_load();
    CHECK((copies_asked() == std::vector<std::pair<int, int>>{{0, 0}, {1, 0}, {0, 1}, {1, 1}}));
    CHECK(g_copies_made == 5);
    CHECK(g_log.find("level 4: 3 extra enemies beside 2 (new game 4)") != std::string::npos);
    CHECK(g_loads == 3);

    // A copy drops its enemy's bolts, the run's scale and rare coins as its enemy's.
    set_run(3, 20);
    g_coins.clear();
    g_materials.clear();
    g_bolts = 0;
    port_kill_drop(kMobyEnemy, 200);
    const std::int32_t enemy = coins_worth() + g_bolts;
    g_coins.clear();
    g_materials.clear();
    g_bolts = 0;
    port_kill_drop(copy_made(2), 200);
    CHECK(
        get<std::int16_t>(copy_made(2) + kMobyBolts) == get<std::int16_t>(kMobyEnemy + kMobyBolts)
    );
    CHECK(enemy > 200 && coins_worth() + g_bolts == enemy);

    // Oozla: its table has no Barlow hounds, so none of these is copied.
    set_run(2, 0);
    g_level_now = 1;
    port_level_load();
    CHECK(copies_asked().empty());
    g_level_now = 4;

    // A party guest's game: the host's copies, none of its own.
    CHECK(g_rules != nullptr);
    std::vector<std::uint8_t> levels(kItems, 0);
    g_rules(g_rules_user, levels.data(), static_cast<std::uint32_t>(levels.size()));
    set_run(4, 0);
    port_level_load();
    CHECK(copies_asked().empty());
    g_rules(g_rules_user, nullptr, 0);
    set_run(1, 0);
    g_level_now = 1;
}

// Checks the pool's room (a port of 1.27): a big drop in a level whose pool is nearly full folds into
// the few coins that fit, bolts all kept; copies stop short of the pool's end and come back when
// room is freed.
void test_pool_room() {
    lay_out_enemy();
    set_run(3, 20);
    g_level_now = 4;
    int total_expected = 15'000;
    const auto drop = [&](int fake_coins, int free_slots, std::size_t most_coins) {
        g_fake_coins = fake_coins;
        g_pool_free = free_slots;
        g_coins.clear();
        g_materials.clear();
        g_bolts = 0;
        g_log.clear();
        port_spawn_pickups(kMobyEnemy, 1500, 9);
        const std::size_t made = g_coins.size();
        CHECK(made <= most_coins);

        // Every coin collected: the whole drop, the coins' and what was paid at once.
        for (const Coin& coin : std::vector<Coin>(g_coins)) {
            port_collect(coin.moby);
        }

        CHECK(g_bolts == total_expected);
        return made;
    };

    // Room: the whole drop's coins, however many pickups are alive (the pool counts, not a cap).
    CHECK(drop(0, 500, 1000) > 6);
    CHECK(g_log.find("folded") == std::string::npos);
    CHECK(drop(200, 500, 1000) > 6);
    CHECK(g_log.find("folded") == std::string::npos);
    // The pool's last slots (26 free, 24 kept): two coins.
    CHECK(drop(0, 26, 2) > 0);
    // Nothing free: no coin, the whole drop paid at once.
    CHECK(drop(0, 10, 0) == 0);
    g_fake_coins = 0;

    // Copies: the pool near its end gets none, and says so.
    g_pool_free = 100;
    set_run(2, 0);
    g_copies_asked.clear();
    g_copies_made = 0;
    g_log.clear();
    port_level_load();
    CHECK(g_copies_asked.empty() && g_copies_made == 0);
    CHECK(g_log.find("2 copies left out to keep the pool's room") != std::string::npos);

    // Room comes back: the missed copies are made a second later.
    g_pool_free = 500;
    g_log.clear();
    tick(ng_plus_plus::kRetryTicks + 2);
    CHECK(g_copies_made == 2);
    CHECK(g_log.find("2 more extra enemies on try 1, 0 still missing") != std::string::npos);

    // A new load of another level forgets what was missed.
    g_pool_free = 100;
    port_level_load();
    g_level_now = 1;
    g_pool_free = 500;
    g_copies_made = 0;
    tick(ng_plus_plus::kRetryTicks + 2);
    CHECK(g_copies_made == 0);
    g_level_now = 4;
    g_pool_free = 500;

    // The cycle set within a level whose load made none (a test run's setup): the cycle's copies
    // then, as many as a load in that cycle makes, and once.
    set_run(2, 0);
    g_copies_made = 0;
    port_level_load();
    const int at_load = g_copies_made;
    CHECK(at_load > 0);
    set_run(0, 0);
    port_level_load();
    g_copies_made = 0;
    tick(2);
    CHECK(g_copies_made == 0);
    set_run(2, 0);
    tick(2);
    CHECK(g_copies_made == at_load);
    tick(ng_plus_plus::kRetryTicks + 2);
    CHECK(g_copies_made == at_load);
    set_run(1, 0);
}

// Checks the fallback caps on a port that cannot count its pool (before 1.27): with 90 pickups alive
// of the 96 allowed a big drop folds into six coins, every bolt kept.
void test_fallback_caps() {
    lay_out_enemy();
    set_run(3, 20);
    g_fake_coins = 90;
    g_coins.clear();
    g_materials.clear();
    g_bolts = 0;
    g_log.clear();
    port_spawn_pickups(kMobyEnemy, 1500, 9);
    CHECK(!g_coins.empty() && g_coins.size() <= 6);
    CHECK(g_log.find("folded into at most 6 coins") != std::string::npos);

    // Every coin collected: the whole drop.
    for (const Coin& coin : std::vector<Coin>(g_coins)) {
        port_collect(coin.moby);
    }

    CHECK(g_bolts == 15'000);
    g_fake_coins = 0;
    set_run(1, 0);
}

void run(const char* path, const Disc& disc, Host host) {
    g_disc = &disc;
    g_host = host;
    g_level_now = 1;
    g_log.clear();
    g_bolts = 0;
    g_burst_high = -1;
    g_pool_asked = 0;
    g_fake_coins = 0;
    g_pool_free = 500;
    g_coins.clear();
    g_materials.clear();
    g_sp = kStackTop;
    g_memory.assign(kMemoryBytes, 0);
    g_hooks.clear();
    g_upgraded = -1;
    g_failed.clear();
    g_notice.clear();
    std::error_code error;
    std::filesystem::remove(g_dir / "tiers.txt", error);

    openrac_mod_load_fn load = nullptr;

    // Without the library nothing can run.
    if (!open_library(path, load)) {
        ++ng_plus_plus::test::failures();
        return;
    }

    make_api();
    CHECK(reload(load));
    CHECK(g_failed.empty());
    // The pool: asked for on a port of 1.27, a line in the log of an older one.
    CHECK((g_pool_asked == kPoolExtra) == pool_host(host));
    CHECK(
        (g_log.find("asked for 1024 more spare moby slots") != std::string::npos) == pool_host(host)
    );
    CHECK((g_log.find("no more moby slots to give") != std::string::npos) == !pool_host(host));
    // The ten of before (the kills' experience among them), and the three bolt hook points and
    // the arena's on a port of 1.23 with the setting on.
    const bool bolts = host == Host::Bolts || host == Host::Obsidian || energy_host(host);
    // The level's load too on a port of 1.26, for the copies.
    // The armour vendor's two on a port of 1.34.
    CHECK(
        g_hooks.size()
        == (bolts ? 14u : 10u) + (squads_host(host) ? 1u : 0u) + (host == Host::Paint ? 2u : 0u)
    );
    // A port before 1.26 makes no copies (1.25's never woke), and says so once.
    CHECK(
        (g_log.find("no extra enemies in this OpenRAC: they need mod interface 1.26")
         != std::string::npos)
        == !squads_host(host)
    );
    CHECK((g_log.find("interface 1.23") != std::string::npos) == (host == Host::Old));
    // A port of 1.23 drops platinum only, and says so.
    CHECK((g_log.find("interface 1.24") != std::string::npos) == (host == Host::Bolts));
    lay_out_game();
    test_a_new_save_counts_from_zero();
    test_the_vendor_offers_alpha();
    test_alpha_bought();
    test_beta_by_experience();
    test_tint_damage_and_enemies();
    test_weapon_visuals();
    test_ryno_heavy_and_kept();
    test_ryno_at_the_vendor();
    test_a_restart_keeps_the_saved_steps(load);
    test_each_save_has_its_own();
    test_saves_from_inside_a_level();
    test_tiers_follow_the_saves(load);
    test_in_hand_level_up();
    test_ryno_to_ultima();
    test_kill_experience();
    test_clock_moved_unseen();

    test_collectors(host != Host::Off);
    test_prizes_and_prices(host != Host::Off, bolts);

    // The bolts as the port offers them.
    if (host == Host::Obsidian || energy_host(host)) {
        test_obsidian_drop();

        // A port that cannot count its pool keeps to the fixed caps.
        if (!pool_host(host)) {
            test_fallback_caps();
        }

        test_kill_drops();
        test_word_not_scaled();
    } else if (host == Host::Bolts) {
        test_platinum_drop();
        test_level_left();
        test_drops_before_and_crates();
    } else {
        test_drops_untouched();
    }

    // The copies on a port of 1.26; with the setting off, none, and no hook for them.
    if (squads_host(host)) {
        test_extra_enemies();

        // A port of 1.27: the pool's room.
        if (pool_host(host)) {
            test_pool_room();
        }

        g_extra_enemies = false;
        CHECK(reload(load));
        CHECK(hooked<rac2::Level_Load>() == nullptr);
        g_extra_enemies = true;
    } else {
        CHECK(hooked<rac2::Level_Load>() == nullptr);
    }

    test_old_book_moved(load);
    test_armour_at_the_vendor(host == Host::Paint);
}

}  // namespace

int main(int argc, char** argv) try {
    // The library to load is the one argument.
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <ng_plus_plus library>\n", argv[0]);
        return 2;
    }

    // The mod's data folder: the system's temporary one, as the test has no other.
    std::error_code error;
    g_dir = std::filesystem::temp_directory_path(error);

    // No temporary folder: the working one.
    if (error) {
        g_dir = ".";
    }

    // Every disc NG++ knows.
    for (const Disc& disc : kDiscs) {
        // Each port: 1.23 with the bolts, 1.22 without, the setting off.
        for (const Host host :
             {Host::Bolts,
              Host::Old,
              Host::Off,
              Host::Obsidian,
              Host::Energy,
              Host::Squads,
              Host::Pool,
              Host::Paint}) {
            run(argv[1], disc, host);
        }
    }

    return ng_plus_plus::test::result("ng_plus_plus_load");
} catch (...) {
    // A test that throws has failed.
    return 1;
}
