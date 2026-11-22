// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The ammo-max library as the port loads it, against a host table made here (openrac_mod.h): it
// reports its id and interface, refuses another game and an unknown disc, takes no hook point but a
// tick callback; then, on a game memory laid out as each disc's vendor has it, the buy dialog's
// count starts at the most the bolts and the weapon's room allow, once each time the dialog opens,
// and only for ammo the game lets the count rise for.
//
// Run with the library's path: load_test <ammo_max.dylib|.so|.dll>.
//
// Expected values are worked out by hand from the rule in ammo_max.hpp, on a memory laid out at the
// discs' addresses in the same header.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ammo_max.hpp"
#include "openrac/openrac_mod.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Stops the test at the first failed condition and says where. It is a macro for the line number
// and the text of the condition, which a function cannot see.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

// How big the test's game memory is, in bytes: enough for both discs' addresses.
constexpr std::uint32_t kMemoryBytes = 0x00200000u;

// How far the test moves level 0's program from the executable's addresses, so that the mod is
// seen asking for level 0's words and not the executable's.
constexpr std::uint32_t kLevelShift = 0x100u;

// The vendor row the test lays out: row 2 of the menu.
constexpr std::uint32_t kRowChosen = 2u;

// The item id the test's row buys ammo for.
constexpr std::uint32_t kItemId = 0x0Bu;

// The offsets of the vendor's words, as the mod reads them (seen in game code).
constexpr std::uint32_t kDialogPriceSelect = 0x40u;
constexpr std::uint32_t kDialogRow = 0x58u;
constexpr std::uint32_t kDialogState = 0x5Cu;
constexpr std::uint32_t kDialogRows = 0xD0u;
constexpr std::uint32_t kRowStride = 0x14u;
constexpr std::uint32_t kRowIsAmmo = 4u;
constexpr std::uint32_t kItemStride = 0x18u;
constexpr std::uint32_t kItemPrice = 8u;
constexpr std::uint32_t kItemPriceSelected = 0xAu;
constexpr std::uint32_t kItemMaxAmmo = 0xEu;

// The dialog states the test steps through.
constexpr std::int32_t kStateClosed = 0;
constexpr std::int32_t kStateOpening = 1;
constexpr std::int32_t kStateAdjusting = 2;
constexpr std::int32_t kStateBuying = 3;

// The host the mod runs against: the game's memory, what the mod told the host, and the callbacks
// it registered. The test's host functions are C callbacks that carry no user pointer, so they
// reach it through g_host.
struct Host {
    // The game's memory, zeroed before each disc.
    std::vector<std::uint8_t> memory = std::vector<std::uint8_t>(kMemoryBytes);

    // The reason the mod gave for refusing to load, empty when it did not.
    std::string failed;

    // The level the host reports as loaded, -1 for none.
    int level = -1;

    // How many hook points the mod asked for.
    int hooks = 0;

    // How many log lines the mod wrote.
    int log_lines = 0;

    // Both the original dialog watcher and Buy All register game-frame callbacks.
    openrac_frame_fn tick = nullptr;
    void* tick_user = nullptr;
    openrac_frame_fn buy_all_tick = nullptr;
    void* buy_all_tick_user = nullptr;

    // The disc whose vendor the test has laid out.
    const ammo_max::Disc* disc = nullptr;
};

// The host the test's callbacks reach (see Host). Set once by main, before any callback runs; only
// the test's thread touches it.
Host* g_host = nullptr;

// ---- The host table ----

// Counts a log line and prints it.
void host_log(openrac_mod* /*mod*/, int level, const char* text) {
    ++g_host->log_lines;
    std::printf("[%d] ammo-max: %s\n", level, text);
}

// Keeps the reason the mod refused to load.
void host_fail(openrac_mod* /*mod*/, const char* reason) {
    g_host->failed = reason;
}

// Answers the one setting the mod reads: the trace is on.
int setting_bool(openrac_mod* /*mod*/, const char* key) {
    return std::strcmp(key, "trace") == 0 ? 1 : 0;
}

// Answers no integer setting: the mod reads none.
std::int64_t setting_int(openrac_mod* /*mod*/, const char* /*key*/) {
    return 0;
}

// Answers no float setting: the mod reads none.
double setting_float(openrac_mod* /*mod*/, const char* /*key*/) {
    return 0.0;
}

// Answers no string setting: the mod reads none.
const char* setting_string(openrac_mod* /*mod*/, const char* /*key*/) {
    return "";
}

// Counts a hook request and refuses it; the mod must take no hook point.
int host_hook(
    openrac_mod* /*mod*/,
    const char* /*name*/,
    const char* /*signature*/,
    void* /*replacement*/,
    void** /*original*/
) {
    ++g_host->hooks;

    return OPENRAC_HOOK_UNKNOWN;
}

// Keeps the tick callback and its user pointer.
int host_on_frame(openrac_mod* /*mod*/, openrac_frame_fn callback, void* user) {
    if (!g_host->tick) {
        g_host->tick = callback;
        g_host->tick_user = user;
    } else {
        CHECK(g_host->buy_all_tick == nullptr);
        g_host->buy_all_tick = callback;
        g_host->buy_all_tick_user = user;
    }
    return OPENRAC_HOOK_OK;
}

// Accepts a pad callback and ignores it.
int host_on_pad(openrac_mod* /*mod*/, openrac_pad_fn /*callback*/, void* /*user*/) {
    return OPENRAC_HOOK_OK;
}

// The added RAC1 Buy All loader resolves the vendor sound function at load.
int test_vendor_sound(int, int, openrac_gaddr) { return 0; }
void* host_function(const char* name) {
    if (std::strcmp(name, "func_0022ED80") == 0 ||
        std::strcmp(name, "FUN_0022da68") == 0)
        return reinterpret_cast<void*>(&test_vendor_sound);
    return nullptr;
}

// Finds no game function by address.
void* host_function_at(openrac_gaddr /*address*/) {
    return nullptr;
}

// Returns the executable's address as it is: the test does not move the executable.
openrac_gaddr identity(openrac_gaddr address) {
    return address;
}

// Returns level 0's address as the level loaded has it, kLevelShift bytes on, so that the test sees
// the mod ask for level 0.
openrac_gaddr level_data(int level, openrac_gaddr address) {
    CHECK(level == 0);

    return address + kLevelShift;
}

// Returns the level the test says is loaded.
int loaded_level() {
    return g_host->level;
}

// Gives the mod a stack frame it does not use.
openrac_gaddr stack_push(std::uint32_t /*size*/, openrac_gaddr* saved) {
    *saved = 0;

    return 0x001F0000u;
}

// Takes back the stack frame the mod does not use.
void stack_pop(openrac_gaddr /*saved*/) {}

// An SDK 1.20 host must provide keyboard/mouse input if it advertises
// the corresponding API slots. The Buy All frame callback checks input
// activity even while the vendor is closed, so provide an idle snapshot.
const openrac_input* host_input() {
    static const openrac_input idle{};
    return &idle;
}

// Returns the host table for a game and a disc, over the test's memory.
openrac_api make_api(const char* game, const char* version) {
    openrac_api api;

    std::memset(&api, 0, sizeof api);
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = game;
    api.version = version;
    api.openrac_version = "test";
    api.platform = "test";
    api.memory = g_host->memory.data();
    api.log = host_log;
    api.fail = host_fail;
    api.setting_bool = setting_bool;
    api.setting_int = setting_int;
    api.setting_float = setting_float;
    api.setting_string = setting_string;
    api.hook = host_hook;
    api.on_frame = host_on_frame;
    api.on_pad = host_on_pad;
    api.input = host_input;
    api.function = host_function;
    api.function_at = host_function_at;
    api.relocate_data = identity;
    api.relocate_level_data = level_data;
    api.loaded_level = loaded_level;
    api.stack_push = stack_push;
    api.stack_pop = stack_pop;

    return api;
}

// ---- The library ----

// Opens the library at `path`, or returns null.
void* open_library(const char* path) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(path));
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

// Returns the library's exported `name`, or null.
void* symbol(void* library, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

// ---- Game memory ----

// Writes an s32 into the game's memory.
void put_s32(openrac_gaddr address, std::int32_t value) {
    std::memcpy(g_host->memory.data() + address, &value, sizeof value);
}

// Writes a u16 into the game's memory.
void put_u16(openrac_gaddr address, std::uint16_t value) {
    std::memcpy(g_host->memory.data() + address, &value, sizeof value);
}

// Reads an s32 from the game's memory.
std::int32_t get_s32(openrac_gaddr address) {
    std::int32_t value;

    std::memcpy(&value, g_host->memory.data() + address, sizeof value);

    return value;
}

// Returns the address of a word of the vendor's dialog, in level 0's program as loaded.
openrac_gaddr dialog_word(std::uint32_t offset) {
    return g_host->disc->dialog + kLevelShift + offset;
}

// Returns the dialog's state.
std::int32_t state() {
    return get_s32(dialog_word(kDialogState));
}

// Returns the count the dialog asks for.
std::int32_t count() {
    return get_s32(g_host->disc->count + kLevelShift);
}

// Sets the count the dialog asks for.
void set_count(std::int32_t value) {
    put_s32(g_host->disc->count + kLevelShift, value);
}

// Sets the dialog's state.
void set_state(std::int32_t value) {
    put_s32(dialog_word(kDialogState), value);
}

// Runs the mod's tick callback once.
void run_tick() {
    g_host->tick(g_host->tick_user);
    if (g_host->buy_all_tick) g_host->buy_all_tick(g_host->buy_all_tick_user);
}

// Lays out the vendor of the running disc: row 2 chosen, item 0x0B's ammo (10 bolts a round, 15
// with the dialog's other price, 160 at most), the player with `ammo` of it and `bolts`.
void lay_vendor(std::int32_t ammo, std::int32_t bolts) {
    const ammo_max::Disc& disc = *g_host->disc;
    const openrac_gaddr dialog = dialog_word(0);
    const openrac_gaddr items = disc.item_table + kLevelShift;
    const openrac_gaddr row = dialog + kDialogRows + kRowChosen * kRowStride;
    const openrac_gaddr record = items + kItemId * kItemStride;

    std::memset(g_host->memory.data() + dialog, 0, 0x200);
    put_s32(dialog + kDialogRow, static_cast<std::int32_t>(kRowChosen));
    put_s32(row, static_cast<std::int32_t>(kItemId));
    put_s32(row + kRowIsAmmo, 1);
    put_u16(record + kItemPrice, 10);
    put_u16(record + kItemPriceSelected, 15);
    put_u16(record + kItemMaxAmmo, 160);
    put_s32(disc.ammo + 4u * kItemId, ammo);
    put_s32(disc.bolts, bolts);
}

// ---- The game's dialog ----

// Plays the game's state 1 as VendorBuyDialogUpdate runs it (the count 1, Up allowed when a round
// can be bought), then state 2, with the mod's tick after each.
void open_dialog(int can_raise) {
    set_state(kStateOpening);
    run_tick();
    set_count(1);
    put_s32(g_host->disc->can_raise + kLevelShift, can_raise);
    set_state(kStateAdjusting);
    run_tick();
}

// Plays the dialog closing: the buy state, then closed, with the mod's tick after each.
void close_dialog() {
    set_state(kStateBuying);
    run_tick();
    set_state(kStateClosed);
    run_tick();
}

// ---- Tests ----

// Loads the library on `version`, then checks that the count starts at the most for each way the
// dialog can open, and is left alone where the game allows no more.
void test_the_count_starts_at_the_most_on_a_disc(
    const char* version, openrac_mod* mod, openrac_mod_load_fn load
) {
    g_host->disc = std::strcmp(version, "pal") == 0 ? &ammo_max::kDiscs[0] : &ammo_max::kDiscs[1];
    std::fill(g_host->memory.begin(), g_host->memory.end(), std::uint8_t{0});

    openrac_api api = make_api("rac1", version);

    g_host->failed.clear();
    g_host->hooks = 0;
    g_host->tick = nullptr;
    g_host->buy_all_tick = nullptr;
    g_host->level = 3;
    CHECK(
        load(&api, mod) == 0 && g_host->failed.empty() && g_host->hooks == 0
        && g_host->tick != nullptr && g_host->buy_all_tick != nullptr
    );

    // 0 of 160 and bolts for all of it: 160.
    lay_vendor(0, 5000);
    run_tick();
    open_dialog(1);
    CHECK(state() == kStateAdjusting && count() == 160);

    // The player lowers it: kept, tick after tick.
    set_count(7);
    run_tick();
    run_tick();
    CHECK(count() == 7);
    close_dialog();

    // Bolts for 40 rounds only: 40.
    lay_vendor(0, 409);
    open_dialog(1);
    CHECK(count() == 40);
    close_dialog();

    // 150 of 160 and rich: the 10 it has room for.
    lay_vendor(150, 9999999);
    open_dialog(1);
    CHECK(count() == 10);
    close_dialog();

    // The dialog's other price (15 a round): 400 bolts buy 26.
    lay_vendor(0, 400);
    put_s32(dialog_word(kDialogPriceSelect), 1);
    open_dialog(1);
    CHECK(count() == 26);
    close_dialog();

    // The game keeps Up off (too few bolts, full, a weapon not ammo): its 1 stays.
    lay_vendor(0, 5000);
    open_dialog(0);
    CHECK(count() == 1);
    close_dialog();
    lay_vendor(0, 5000);
    put_s32(dialog_word(kDialogRows + kRowChosen * kRowStride + kRowIsAmmo), 0);
    open_dialog(1);
    CHECK(count() == 1);
    close_dialog();

    // A level loaded with the dialog's state already 2 (what the level's file holds): untouched.
    lay_vendor(0, 5000);
    set_count(1);
    put_s32(g_host->disc->can_raise + kLevelShift, 1);
    set_state(kStateAdjusting);
    g_host->level = 4;
    run_tick();
    CHECK(count() == 1);

    // No level loaded: nothing read.
    g_host->level = -1;
    run_tick();
    CHECK(count() == 1);
}

}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);

    Host host;

    g_host = &host;

    void* library = open_library(argv[1]);

    CHECK(library != nullptr);

    auto query = reinterpret_cast<openrac_mod_query_fn>(symbol(library, OPENRAC_MOD_QUERY));
    auto load = reinterpret_cast<openrac_mod_load_fn>(symbol(library, OPENRAC_MOD_LOAD));

    CHECK(query != nullptr && load != nullptr);

    openrac_mod_info info;

    std::memset(&info, 0, sizeof info);
    info.size = sizeof info;
    query(&info);

    // The interface its manifest names (1.1), whatever the SDK it was built with.
    CHECK(info.api_major == OPENRAC_MOD_API_MAJOR && info.api_minor == 20);
    CHECK(std::strcmp(info.id, "ammo-max") == 0 && std::strcmp(info.version, "1.1.0") == 0);

    // Any handle will do: the mod only passes it back.
    auto* mod = reinterpret_cast<openrac_mod*>(&info);

    // Another game, an unknown disc: refused, with a reason, before any callback.
    openrac_api api = make_api("rac3", "ntsc");

    CHECK(
        load(&api, mod) != 0 && host.failed.find("RAC1") != std::string::npos
        && host.tick == nullptr
    );
    api = make_api("rac1", "jp");
    host.failed.clear();
    CHECK(
        load(&api, mod) != 0 && host.failed.find("jp") != std::string::npos && host.tick == nullptr
    );

    // Each disc's vendor at its own addresses.
    host.log_lines = 0;
    test_the_count_starts_at_the_most_on_a_disc("pal", mod, load);
    CHECK(host.log_lines > 0);
    test_the_count_starts_at_the_most_on_a_disc("ntsc", mod, load);
    std::printf("ammo_max_load_test: ok\n");

    return 0;
}
