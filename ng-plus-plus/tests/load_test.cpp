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
// none of them.
//
// The test's game holds made-up rows and texts in the game's layout (src/discs.h); the expected
// values are the README's rules worked out for them.
//
// Run with the library's path: ng_plus_plus_load_test <ng_plus_plus.dylib|.so|.dll>.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "bolt_api.h"
#include "bolt_rules.h"
#include "check.h"
#include "discs.h"
#include "hooks.h"
#include "openrac/games/rac2.hpp"
#include "openrac/openrac_mod.h"

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

// The ports the test plays: 1.23 with the bolt hook points, 1.22 without, 1.23 with the setting
// off, and 1.24, whose moby_material draws obsidian too.
enum class Host {
    Bolts,
    Old,
    Off,
    Obsidian
};

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
constexpr std::int32_t kAmmoCodedText = 0x2004;    // "Got %d \x0cMega \x0cLancer\x08\x08 ammo".
// The collectors' dialogs (collectors.h): Tabora's crystals and Grelbin's moonstones.
constexpr std::int32_t kCrystalText = 10135;    // "Trade %d crystals for %d bolts?".
constexpr std::int32_t kMoonstoneText = 10209;  // "%d moonstones: %d bolts".
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

// Vendor_BuildList: two entries, the ammo of items 22 and 23.
void game_build_list(std::int32_t /*hide*/) {
    // The game's two entries.
    for (int n = 0; n < 2; ++n) {
        set<std::int32_t>(entry(n) + kEntryItem, 22 + n);
        set<std::int32_t>(entry(n) + kEntryKind, 1);
        set<std::int32_t>(entry(n) + kEntryUpgrade, 0);
    }

    set<std::int32_t>(g_disc->vendor + kVendorCount, 2);
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

// Hero_TakeDamage: the points taken, added up.
void game_take_damage(std::int32_t amount) {
    g_taken += amount;
}

// The game's text lookup: its strings by id.
std::int32_t game_text(std::int32_t id) {
    // The test's strings by their ids.
    switch (id) {
        case kUpgradeLabelText:
            return static_cast<std::int32_t>(kMegaAt);
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
        case kCrystalText:
            return static_cast<std::int32_t>(kTextAt + 0x540);
        case kMoonstoneText:
            return static_cast<std::int32_t>(kTextAt + 0x580);
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
            g_drawn_label = string_at(port_text(kUpgradeLabelText));
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
// doubled in challenge mode, split into the biggest coins within `maximum` under flags 8, each coin
// starting at x = its place in the burst.
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

// Bolt_Collect: the coin's class's worth added to the bolts.
void game_collect(std::uint32_t moby) {
    const int kind = get<std::uint16_t>(moby + kMobyClass) - kCoinClass;
    g_bolts += kCoinValues[static_cast<std::size_t>(kind)];
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

int setting_bool(openrac_mod* /*mod*/, const char* key) {
    return std::strcmp(key, "notices") == 0 || std::strcmp(key, "enemy_scaling") == 0;
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
        {hooks::Vendor_DrawList::id, reinterpret_cast<void*>(&game_draw_list)},
        {hooks::Item_VersionColor::id, reinterpret_cast<void*>(&game_version_color)},
        {hooks::Text_ById::id, reinterpret_cast<void*>(&game_text)},
        {hooks::Bolt_SpawnPickups::id, reinterpret_cast<void*>(&game_spawn_pickups)},
        {hooks::Bolt_SpawnPickup::id, reinterpret_cast<void*>(&game_spawn_pickup)},
        {hooks::Bolt_Collect::id, reinterpret_cast<void*>(&game_collect)},
    };

    // Each hook point the test's port has.
    for (const auto& point : points) {
        const bool bolt = std::strncmp(point.id, "rac2.bolt.", 10) == 0;

        // A port of 1.22 has no bolt hook points.
        if (bolt && g_host == Host::Old) {
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
    // What the port refuses: obsidian before 1.24.
    const std::uint32_t most = g_host == Host::Obsidian ? kMaterialObsidian : kMaterialPlatinum;
    if (moby == 0 || material > most || rgb > kNoTint) {
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

int host_on_rules(openrac_mod* /*mod*/, openrac_party_rules_fn /*fn*/, void* /*user*/) {
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

// The table the port hands the mod: this SDK's, then the entries up to 1.23's moby_material.
BoltApi g_table;
openrac_api& g_api = bolt_base(g_table);

// Fills the port's table for the disc the test runs on.
void make_api() {
    g_table = BoltApi{};
    g_api.api_major = OPENRAC_MOD_API_MAJOR;

    // A port of 1.22 ends its table before moby_material; the others have it.
    if (g_host == Host::Old) {
        g_api.size = static_cast<std::uint32_t>(offsetof(BoltApi, moby_material));
        g_api.api_minor = 22;
    } else {
        g_api.size = sizeof g_table;
        g_api.api_minor = g_host == Host::Obsidian ? kObsidianMinor : kMaterialMinor;
        g_table.moby_material = host_material;
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
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 30'000);  // five times the others'
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
    CHECK(port_text(kUpgradeLabelText) == static_cast<std::int32_t>(kMegaAt));
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
    CHECK(ammo_message(kLancer, 12) == "Got %d \x0c\x0c" "Beta Heavy Lancer\x08\x08 ammo");
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

    // Challenge mode's third game (cycle 2): enemies have 2.5 times the health, deal 1.375 times.
    set<std::int16_t>(g_disc->progress + kProgressCycle, 2);
    set<float>(kRequestAt + kHitDamage, 10.0f);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(std::fabs(g_damage_seen - 8.0f) < 0.001f);  // Beta's 2 over the health's 2.5
    g_taken = 0;

    // Four hits of one point: five (5.5, the fractions carried).
    for (int n = 0; n < 4; ++n) {
        hooked<rac2::Hero_TakeDamage>()(1);
    }

    CHECK(g_taken == 5);
    set<std::int16_t>(g_disc->progress + kProgressCycle, 1);
    g_taken = 0;
    hooked<rac2::Hero_TakeDamage>()(3);
    CHECK(g_taken == 3);
}

// Checks the RYNO II reaching Heavy by experience, and the levels kept by the save's clock.
void test_ryno_heavy_and_kept() {
    set_experience(kRyno, 29'999);
    tick(1);
    CHECK(g_notice.find("RYNO") == std::string::npos);
    set_experience(kRyno, 30'000);
    tick(1);
    CHECK(g_notice == "Heavy RYNO II");
    CHECK(get<std::uint16_t>(row(kRyno) + kRowAmmoMax) == 150);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 0);  // Ultima is for sale at once

    // Saved (a new clock): kept. A new game (zeros) has none; the save loaded again has them.
    set_clock(0x20);
    tick(1);
    set_clock(0);
    tick(1);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 500);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 30'000);  // toward Heavy again
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

// Checks the RYNO II at the vendor after the Lancer's Gamma: Ultima (named by its own row's text,
// having no version 2 row), then Alpha once Ultima is mastered.
void test_ryno_at_the_vendor() {
    set<std::int32_t>(row(kRyno) + kRowLabel, kRynoLabelText);
    hooked<rac2::Vendor_BuildList>()(0);
    CHECK(vendor_count() == 4);
    CHECK(ryno_place() == 3);
    set_cursor(ryno_place());
    CHECK(title(kRynoLabelText) == "Ultima RYNO II");
    std::int32_t money = 10'000'000;
    CHECK(purchase(money) > 0);
    CHECK(g_notice == "Ultima RYNO II");
    // Bought: no entry left until Ultima is mastered (five times the others' experience).
    CHECK(vendor_count() == 3);
    CHECK(get<std::int32_t>(g_disc->vendor + kVendorCursor) == 2);  // kept in the list
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 30'000);
    set_experience(kRyno, 29'999);
    hooked<rac2::Vendor_BuildList>()(0);
    CHECK(vendor_count() == 3);
    set_experience(kRyno, 30'000);
    hooked<rac2::Vendor_BuildList>()(0);
    CHECK(vendor_count() == 4);
    set_cursor(3);
    CHECK(title(kRynoLabelText) == "Alpha RYNO II");
    money = 10'000'000;
    CHECK(purchase(money) > 0);
    CHECK(g_notice == "Alpha RYNO II");
    CHECK(vendor_count() == 3);  // Beta is by experience: no entry
    CHECK(title(kRynoLabelText) == "Alpha RYNO II");
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 40'000);  // toward Beta
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
    CHECK(string_at(port_text(kLevelUpLineText)) == "\x0c" "Delta Heavy Lancer\x08 upgraded");
    CHECK(string_at(port_text(0)).empty());  // the popup was named: text 0 is the game's
    set<std::int32_t>(g_disc->hero + kHeroHeld, -1);
}

// Checks a restarted game (a new version of the mod): the steps bought since the last save were
// kept at once, so the save loaded has them, at the title where its own clock shows before its
// level starts, and after the game stamps a new one; then a new game has none.
void test_a_restart_keeps_the_steps(openrac_mod_load_fn load) {
    // Twice: the second restart loads the clock the first one stamped.
    for (int restart = 0; restart < 2; ++restart) {
        CHECK(reload(load));
        // A restarted game has the table as the disc has it.
        set<std::int32_t>(row(kRyno) + kRowExpGoal, 0);
        set<std::uint16_t>(row(kLancerV4) + kRowAmmoMax, 500);
        set<std::uint16_t>(row(kRyno) + kRowAmmoMax, 100);
        set_clock(0);
        tick(1);
        CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 30'000);  // nothing yet
        g_level_now = -1;
        tick(1);
        set_clock(restart == 0 ? 0x20 : 0x50);
        tick(2);
        g_level_now = 1;
        set_clock(static_cast<std::uint8_t>(0x50 + restart));
        tick(1);
        CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 40'000);
    }

    // A new game after it: nothing; its first clock (the counter 0, the weapons gone) nothing
    // either.
    set_clock(0);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 30'000);
    set<std::int16_t>(g_disc->progress + kProgressCycle, 0);
    set_experience(kLancer, 0);
    set_experience(kRyno, 0);
    g_memory[g_disc->slot + kLancer] = kLancer;
    tick(1);
    set_clock(0x58);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 30'000);
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
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 40'000);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(experience(kRyno) == 1234 * kExpUnit);  // started: kept

    // B at the title: its own data and a clock the book has never seen. No tiers, and its weapons'
    // experience counted from 0 as for any save new to the mod.
    g_level_now = -1;
    tick(1);
    set_experience(kRyno, 50'000);
    set_experience(kLancer, 9'000);
    set<std::int32_t>(g_disc->progress, 123);
    set_clock(0x60);
    tick(2);
    g_level_now = 1;
    set_clock(0x61);
    tick(2);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 30'000);  // toward Heavy: no tier
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
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 40'000);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(experience(kRyno) == 1234 * kExpUnit);

    // B again, from inside A's level: none.
    set_experience(kRyno, 0);
    set_experience(kLancer, 0);
    set<std::int32_t>(g_disc->progress, 123);
    set_clock(0x61);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 30'000);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 500);

    // A once more, then challenge mode's next game: the clock to zeros with the weapons kept and
    // the counter up, then a new clock: A's tiers go on, its experience too.
    load_save_a();
    set_clock(0x52);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 40'000);
    set<std::int16_t>(g_disc->progress + kProgressCycle, 2);
    set_clock(0);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 30'000);  // nothing while zeros
    CHECK(experience(kRyno) == 1234 * kExpUnit);                   // not zeroed while zeros
    set_clock(0x70);
    tick(1);
    CHECK(get<std::int32_t>(row(kRyno) + kRowExpGoal) == 40'000);
    CHECK(get<std::uint16_t>(row(kLancerV4) + kRowAmmoMax) == 600);
    CHECK(experience(kRyno) == 1234 * kExpUnit);
    set<std::int16_t>(g_disc->progress + kProgressCycle, 1);
}

// Sets the challenge mode counter and the kill streak.
void set_run(std::int16_t cycle, std::uint8_t streak) {
    set<std::int16_t>(g_disc->progress + kProgressCycle, cycle);
    set<std::uint8_t>(g_disc->progress + kProgressStreak, streak);
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
        const std::int32_t times = !rare ? 1
                                   : found->second == kMaterialObsidian ? kObsidianTimes
                                                                        : kPlatinumTimes;
        total += coin.value * times;
    }

    return total;
}

// Checks a drop in challenge mode's fourth run (cycle 3) at a x20 streak from a tough enemy: the
// game asked for five times the amount (x2.5 the run, x2 its toughness), its 15,000 made again as
// 9,750 in platinum and 5,250 in gold at the game's places; collecting pays five times a platinum
// coin's class and takes its material off.
void test_platinum_drop() {
    lay_out_enemy();
    set_run(3, 20);
    g_coins.clear();
    g_materials.clear();
    port_spawn_pickups(kMobyEnemy, 1500, 9);
    CHECK(g_burst_high == 7500);
    CHECK(g_coins.size() == 15);
    CHECK(g_materials.size() == 7);
    CHECK(coins_worth() == 15'000);
    CHECK(g_sp == kStackTop);

    // The biggest first: a platinum 5,000 (the 1,000 coin drawn as platinum), then gold 1,000s.
    CHECK(g_coins.size() > 1 && g_coins[0].value == 1000 && g_coins[1].value == 1000);
    CHECK(g_materials.count(g_coins[0].moby) == 1);
    CHECK(g_materials[g_coins[0].moby] == kMaterialPlatinum);
    CHECK(g_materials.count(g_coins[1].moby) == 0);

    // Each coin starts where the game's coin of its place did.
    for (std::size_t n = 0; n < g_coins.size(); ++n) {
        CHECK(g_coins[n].x == static_cast<float>(n));
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
// 3) at a x20 streak gives 15% as obsidian first (2,250: a 1,250 and two 500s, each class with its
// own glow), then platinum's 65% (9,750) and gold; an obsidian coin collected is worth 25 times its
// class. Without a streak there are only hints: 2% of a drop of 7,500.
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
    std::uint32_t glow_1250 = 0;
    std::uint32_t glow_500 = 0;

    // The coins drawn as obsidian, by class, and their glows.
    for (const Coin& coin : g_coins) {
        // Only the obsidian ones.
        if (g_materials.count(coin.moby) == 0 || g_materials[coin.moby] != kMaterialObsidian) {
            continue;
        }

        ++obsidian[coin.value];
        (coin.value == 50 ? glow_1250 : glow_500) = g_glows[coin.moby];
    }

    CHECK(obsidian.size() == 2 && obsidian[50] == 1 && obsidian[20] == 2);
    // A shade per size: the bigger coin redder, each a colour, not the default white.
    CHECK(glow_1250 != glow_500 && glow_1250 != kNoTint && glow_500 != kNoTint);
    CHECK((glow_1250 >> 16) >= (glow_500 >> 16));
    // The biggest first: obsidian before platinum and gold of a size.
    CHECK(!g_coins.empty() && g_coins[0].value == 1000);
    CHECK(g_materials[g_coins[0].moby] == kMaterialPlatinum);

    // Collected: an obsidian 1,250 (a 50 coin) first, then the rest, the whole drop.
    g_bolts = 0;
    std::uint32_t first = 0;

    // The one obsidian 50 coin.
    for (const Coin& coin : g_coins) {
        // Found.
        if (coin.value == 50 && g_materials[coin.moby] == kMaterialObsidian) {
            first = coin.moby;
            break;
        }
    }

    port_collect(first);
    CHECK(g_bolts == 1250);
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

    // No streak: the 2% of a 7,500 drop at x1 is 150, an obsidian 125 and an obsidian 25.
    set_run(3, 1);
    g_coins.clear();
    port_spawn_pickups(kMobyEnemy, 750, 9);
    int obsidian_coins = 0;

    // The obsidian coins of the drop.
    for (const Coin& coin : g_coins) {
        obsidian_coins += g_materials.count(coin.moby) != 0
                                  && g_materials[coin.moby] == kMaterialObsidian
                              ? 1
                              : 0;
    }

    CHECK(obsidian_coins == 2);
    CHECK(coins_worth() == 7'500);
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
// unscaled with a taste of platinum; and (from cycle 2) a crate's, which is scaled by the run only,
// at the least platinum share.
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
    CHECK(g_burst_high == 150);
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
    CHECK(string_at(port_text(kCrystalText))
          == (scaled ? "Trade %d crystals for 100000 bolts?" : "Trade %d crystals for %d bolts?"));
    g_bolts = 0;
    set<std::int32_t>(block + kProgressBolts, 41'000);
    set<std::int16_t>(block + kProgressCrystals, 0);
    tick(1);
    CHECK(g_bolts == (scaled ? 60'000 : 0));
    // Nothing more on the next tick, and no dialog of nothing.
    tick(1);
    CHECK(g_bolts == (scaled ? 60'000 : 0));
    CHECK(string_at(port_text(kCrystalText)) == "Trade %d crystals for %d bolts?");

    // Grelbin: the moonstones.
    g_level_now = 19;
    tick(1);
    CHECK(string_at(port_text(kMoonstoneText))
          == (scaled ? "%d moonstones: 225000 bolts" : "%d moonstones: %d bolts"));
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
    CHECK(string_at(port_text(kCrystalText)) == "Trade %d crystals for %d bolts?");
    g_bolts = 0;
    set<std::int32_t>(block + kProgressBolts, 151'000);
    set<std::int16_t>(block + kProgressCrystals, 0);
    tick(1);
    CHECK(g_bolts == 0);
    g_level_now = 1;
}

void run(const char* path, const Disc& disc, Host host) {
    g_disc = &disc;
    g_host = host;
    g_level_now = 1;
    g_log.clear();
    g_bolts = 0;
    g_burst_high = -1;
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
    // The nine of before, and the three bolt hook points on a port of 1.23 with the setting on.
    const bool bolts = host == Host::Bolts || host == Host::Obsidian;
    CHECK(g_hooks.size() == (bolts ? 12u : 9u));
    CHECK((g_log.find("interface 1.23") != std::string::npos) == (host == Host::Old));
    // A port of 1.23 drops platinum only, and says so.
    CHECK((g_log.find("interface 1.24") != std::string::npos) == (host == Host::Bolts));
    lay_out_game();
    test_a_new_save_counts_from_zero();
    test_the_vendor_offers_alpha();
    test_alpha_bought();
    test_beta_by_experience();
    test_tint_damage_and_enemies();
    test_ryno_heavy_and_kept();
    test_ryno_at_the_vendor();
    test_a_restart_keeps_the_steps(load);
    test_each_save_has_its_own();
    test_saves_from_inside_a_level();
    test_in_hand_level_up();

    test_collectors(host != Host::Off);

    // The bolts as the port offers them.
    if (host == Host::Obsidian) {
        test_obsidian_drop();
    } else if (host == Host::Bolts) {
        test_platinum_drop();
        test_level_left();
        test_drops_before_and_crates();
    } else {
        test_drops_untouched();
    }
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
        for (const Host host : {Host::Bolts, Host::Old, Host::Off, Host::Obsidian}) {
            run(argv[1], disc, host);
        }
    }

    return ng_plus_plus::test::result("ng_plus_plus_load");
} catch (...) {
    // A test that throws has failed.
    return 1;
}
