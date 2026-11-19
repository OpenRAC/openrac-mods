// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ as the port loads it (the built library), against a host table and game memory of the
// test's own, on each of the three discs: a Lancer at V4 offered Alpha at the vendor and bought,
// levelled to Beta by experience, its tint, damage, most ammo and texts (its name, the vendor's
// label and title, the ammo pickup's message, the level-up line); the RYNO II reaching Heavy; the
// levels kept by the save's clock in the mod's data folder, each save with its own (a save loaded
// at the title or in a level, stamped again, challenge mode's next game).
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
#include <string>
#include <vector>

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
std::int32_t g_banner_key = 0;
int g_level_now = 1;

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

// ---- The host ----

void host_log(openrac_mod* /*mod*/, int /*level*/, const char* /*text*/) {}

void host_fail(openrac_mod* /*mod*/, const char* reason) {
    g_failed = reason;
}

int setting_bool(openrac_mod* /*mod*/, const char* key) {
    return std::strcmp(key, "notices") == 0 || std::strcmp(key, "enemy_scaling") == 0;
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
    };

    // Each hook point the test's port has.
    for (const auto& point : points) {
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
    // The disc's names of the popup, the line and the lookup; no in-hand level-up.
    if (std::strcmp(name, g_disc->banner) == 0) {
        return reinterpret_cast<void*>(&game_banner);
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

// The table the port hands the mod.
openrac_api g_api;

// Fills the port's table for the disc the test runs on.
void make_api() {
    g_api = openrac_api{};
    g_api.size = sizeof g_api;
    g_api.api_major = OPENRAC_MOD_API_MAJOR;
    g_api.api_minor = OPENRAC_MOD_API_MINOR;
    g_api.game = "rac2";
    g_api.version = g_disc->version;
    g_api.openrac_version = "test";
    g_api.platform = "test";
    g_api.memory = g_memory.data();
    g_api.log = host_log;
    g_api.fail = host_fail;
    g_api.setting_bool = setting_bool;
    g_api.setting_float = setting_float;
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

    // Beta's damage, with the Lancer in hand: 1.5 times for the call, put back after.
    set<std::int32_t>(g_disc->hero + kHeroHeld, kLancer);
    set<float>(kRequestAt + kHitDamage, 10.0f);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(g_damage_seen == 15.0f);
    CHECK(get<float>(kRequestAt + kHitDamage) == 10.0f);
    hooked<rac2::Moby_PostHit>()(kMobyHero, kRequestAt);
    CHECK(g_damage_seen == 10.0f);

    // Challenge mode's third game (cycle 2): enemies have 2.5 times the health, deal 1.25 times.
    set<std::int16_t>(g_disc->progress + kProgressCycle, 2);
    set<float>(kRequestAt + kHitDamage, 10.0f);
    hooked<rac2::Moby_PostHit>()(kMobyEnemy, kRequestAt);
    CHECK(std::fabs(g_damage_seen - 6.0f) < 0.001f);  // Beta's 1.5 over the health's 2.5
    g_taken = 0;

    // Four hits of one point: five, the quarters carried.
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

// Runs every check on `disc`, with the library at `path`.
void run(const char* path, const Disc& disc) {
    g_disc = &disc;
    g_level_now = 1;
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
    CHECK(g_hooks.size() == 9);
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
        run(argv[1], disc);
    }

    return ng_plus_plus::test::result("ng_plus_plus_load");
} catch (...) {
    // A test that throws has failed.
    return 1;
}
