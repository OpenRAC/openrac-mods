// SPDX-License-Identifier: MIT
// Ratchet & Clank 2 / OpenRAC: press Square (Xbox X) in the Megacorp vendor to refill owned ammo.
// Addresses: OpenRAC/openrac-mods ng-plus-plus/src/discs.h and difficulty-mod/src/rac2/discs.h.
// Vendor record: OpenRAC/rac2-decomp src/game/002F5220-purchase.c and 002F5220-menu.c.
//
// Gameplay and native vendor drawing run on the game thread. The tick publishes one HUD quote.
#include "openrac/mod.hpp"
#include "openrac/games/rac2.hpp"
#include "purchase_plan.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

void load_rac1(openrac::Mod& mod);

namespace {

struct Disc {
    const char* version;
    std::uint32_t game_state; // s32: 5 is the vendor
    std::uint32_t vendor;     // +0x84: purchase dialog state, +0x740: entry count
    std::uint32_t table;      // 0xE0-byte weapon records
    std::uint32_t slot;       // u8[56] active record per item
    std::uint32_t ammo;       // s32[56] current ammo
    std::uint32_t owned;      // u8[56] inventory flags
};

constexpr Disc kDiscs[] = {
    {"ntsc",  0x001A8BB0u, 0x0021F240u, 0x00239BA0u, 0x00139568u, 0x00139688u, 0x001A7AF8u},
    {"pal",   0x001A8C60u, 0x0021F1C0u, 0x00239BA0u, 0x001395E8u, 0x00139708u, 0x001A7B80u},
    {"ntsc2", 0x001A8BB0u, 0x0021F1C0u, 0x00239B20u, 0x00139568u, 0x00139688u, 0x001A7B00u},
};

constexpr std::uint32_t kVendorCursor = 0x80u;
constexpr std::uint32_t kVendorDialogState = 0x84u;
constexpr std::uint32_t kVendorEntries = 0x140u;
constexpr std::uint32_t kVendorCount = 0x740u;
constexpr std::uint32_t kVendorEntrySize = 0x18u;
constexpr std::uint32_t kRowSize = 0xE0u;
constexpr std::uint32_t kRowUnitPrice = 0x88u;
constexpr std::uint32_t kRowCapacity = 0x8Eu;
constexpr int kMaxRows = 119;
constexpr int kVendorMaxEntries = 64;

// The first pad's 16-bit PS2 report is little-endian, active LOW; bit 15 = Square.
// On an Xbox controller, OpenRAC maps its X button to this same button.
constexpr std::uint16_t kSquare = 0x8000u;
constexpr std::uint64_t kHudKindMask = 0xFFu;
constexpr unsigned kHudCostShift = 8;

// Published atomically so the renderer never accesses the game memory or torn HUD data.
std::atomic<std::uint64_t> s_hud{0};
std::string s_hud_position;
using Print = void (*)(float, float, std::uint64_t, openrac::gaddr, int, float, std::uint64_t, openrac::gaddr);
using Texture = std::uint64_t (*)(int);
using Scissor = void (*)(int, int, int, int);
using Sound = int (*)(int, int, openrac::gaddr);
using Rect = void (*)(int, int, int, int, std::uint64_t);
Print s_print = nullptr;
Texture s_texture = nullptr;
Scissor s_scissor = nullptr;
Sound s_sound = nullptr;
Rect s_rect = nullptr;
bool s_square_was_down = false;
bool s_square_just_pressed = false;
bool s_show_hud = true;
bool s_touch_buy_visible = false;
int s_notice_ticks = 0;
int s_notice_cost = 0;

void publish(buy_all_ammo::Hint hint, int cost = 0) {
    const auto bits = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cost)) << kHudCostShift) |
                      static_cast<std::uint64_t>(hint);
    s_hud.store(bits, std::memory_order_release);
}

// Draw through the vendor's game-thread draw hook. The host overlay is camera-gated,
// and a menu frame can have no world camera. All text uses guest stack storage.
void draw_hud(openrac::Mod& mod) {
    const auto bits = s_hud.load(std::memory_order_acquire);
    const auto hint = static_cast<buy_all_ammo::Hint>(bits & kHudKindMask);
    if (!s_show_hud || hint == buy_all_ammo::Hint::Hidden) return;
    const auto cost = static_cast<std::uint32_t>(bits >> kHudCostShift);
    char description[96];
    switch (hint) {
        case buy_all_ammo::Hint::Buy:
            std::snprintf(description, sizeof description, "%u bolts", cost); break;
        case buy_all_ammo::Hint::Partial:
            std::snprintf(description, sizeof description, "%u bolts (partial)", cost); break;
        case buy_all_ammo::Hint::Full:
            std::snprintf(description, sizeof description, "Ammo full"); break;
        case buy_all_ammo::Hint::Insufficient:
            std::snprintf(description, sizeof description, "Not enough bolts"); break;
        case buy_all_ammo::Hint::Purchased:
            std::snprintf(description, sizeof description, "Bought: %u bolts", cost); break;
        default: return;
    }
    const bool left = s_hud_position == "vendor_left";
    // A separate box below the normal Buy control, in the centred game UI.
    const float centre_x = left ? 76.0f : 410.0f;
    const float y = left ? 215.0f : s_hud_position == "top_right" ? 35.0f : 391.0f;
    // PAL's vendor stretches its vertical coordinates by 1.2 and offsets them by four.
    const bool pal = mod.version() == "pal";
    const float scale_y = pal ? 1.2f : 1.0f;
    const float offset_y = pal ? 4.0f : 0.0f;
    const auto glyphs = mod.relocate(pal ? 0x00263C70u :
                                    mod.version() == "ntsc2" ? 0x00263EB0u : 0x00263EF0u);
    if (!glyphs) return;
    openrac::StackFrame frame(mod, 192);
    const auto line = [&](const char* text, float at_y, float scale) {
        float width = 0.0f;
        for (std::size_t i = 0; i <= std::char_traits<char>::length(text); ++i) {
            const auto character = static_cast<std::uint8_t>(text[i]);
            mod.memory().write<char>(frame.address() + static_cast<openrac::gaddr>(i), text[i]);
            if (character) width += mod.memory().read<std::int8_t>(glyphs + character * 4u + 3u) * scale;
        }
        // Keep long prices/statuses within the panel too, using the actual font metrics.
        const float max_width = left ? 96.0f : 145.0f;
        if (width > max_width) {
            scale *= max_width / width;
            width = max_width;
        }
        s_print(centre_x - width * 0.5f, at_y * scale_y + offset_y, 0x80F0F0F0u, frame.address(), -1,
                scale, s_texture(2), glyphs);
    };
    s_scissor(0, 511, 0, pal ? 511 : 447);
    const auto screen_y = [&](float at) { return static_cast<int>(at * scale_y + offset_y); };
    // Native GS colours are AABBGGRR. A border and inset tie both lines together.
    const int box_left = static_cast<int>(centre_x - 58.0f);
    const int box_right = static_cast<int>(centre_x + 58.0f);
    s_rect(screen_y(y - 5.0f), screen_y(y + 30.0f), box_left, box_right, 0x807CADBFu);
    s_rect(screen_y(y - 4.0f), screen_y(y + 29.0f), box_left + 1, box_right - 1, 0x7032240Bu);
    // RAC2's Square glyph, not a literal controller name. OpenRAC replaces this font
    // cell with the current input's prompt, just as it does for the vendor's Exit icon.
    line("\x13 Buy all", y, 0.85f);
    line(description, y + 16.0f, 0.60f);
}

void touch_buy_button(openrac::Mod& mod, bool visible) {
    if (s_touch_buy_visible == visible) return;
    s_touch_buy_visible = visible;
    // The "vendor" touch layout normally has no Square button. The mod adds it while buying
    // ammunition is possible, rather than replacing the game's normal vendor controls.
    mod.touch_hint("buy_all_ammo", visible);
}

// Read only the vendor's own ammo entries. Return zero outside its main list, including dialogs.
int candidates_at_vendor(const openrac::Mod& mod,
                         const Disc& disc,
                         std::array<buy_all_ammo::Candidate, buy_all_ammo::kItems>& out,
                         openrac::gaddr& ammo_addr) {
    const auto& memory = mod.memory();
    const auto mode = mod.relocate(disc.game_state);
    if (!mode || memory.read<std::int32_t>(mode) != 5) return 0;

    const auto vendor = mod.relocate(disc.vendor);
    if (!vendor || memory.read<std::int32_t>(vendor + kVendorDialogState) != 0) return 0;
    const int count = memory.read<std::int32_t>(vendor + kVendorCount);
    const int cursor = memory.read<std::int32_t>(vendor + kVendorCursor);
    if (count < 1 || count > kVendorMaxEntries || cursor < 0 || cursor >= count) return 0;

    const auto owned = mod.relocate(disc.owned);
    const auto slots = mod.relocate(disc.slot);
    const auto weapon_table = mod.relocate(disc.table);
    ammo_addr = mod.relocate(disc.ammo);
    if (!owned || !slots || !weapon_table || !ammo_addr) return 0;

    int out_count = 0;
    std::array<bool, buy_all_ammo::kItems> seen{};
    for (int row = 0; row < count && out_count < buy_all_ammo::kItems; ++row) {
        const auto entry = vendor + kVendorEntries + static_cast<std::uint32_t>(row) * kVendorEntrySize;
        const int item = memory.read<std::int32_t>(entry);
        const int kind = memory.read<std::int32_t>(entry + 4u);
        if (kind != 1 || item < 0 || item >= buy_all_ammo::kItems) continue;
        const auto index = static_cast<std::size_t>(item);
        if (seen[index]) continue;
        if (!memory.read<std::uint8_t>(owned + static_cast<std::uint32_t>(item))) continue;
        const int slot = memory.read<std::uint8_t>(slots + static_cast<std::uint32_t>(item));
        if (slot < 0 || slot >= kMaxRows) continue;
        const auto record = weapon_table + static_cast<std::uint32_t>(slot) * kRowSize;
        const int unit_price = memory.read<std::uint16_t>(record + kRowUnitPrice);
        const int capacity = memory.read<std::uint16_t>(record + kRowCapacity);
        const int current = memory.read<std::int32_t>(ammo_addr + static_cast<std::uint32_t>(item) * 4u);
        seen[index] = true;
        out[static_cast<std::size_t>(out_count++)] = {item, current, capacity, unit_price};
    }
    return out_count;
}

void vendor_sound(openrac::Mod& mod, int sound) {
    const auto vendor = mod.relocate(mod.version() == "ntsc" ? 0x0021F240u : 0x0021F1C0u);
    const auto owner = vendor ? mod.memory().read<openrac::gaddr>(vendor + 0x28u) : 0;
    if (owner) s_sound(sound, 17, owner);
}

void buy(openrac::Mod& mod,
         openrac::gaddr ammo_addr,
         const buy_all_ammo::Plan& plan) {
    const auto& memory = mod.memory();
    // One transaction on the game thread; all costs come from the active weapon's vendor record.
    // The SDK's bolts method keeps the game's own balance state in sync.
    mod.bolts(-plan.cost);
    for (int i = 0; i < plan.count; ++i) {
        const auto& purchase = plan.entries[static_cast<std::size_t>(i)];
        const auto addr = ammo_addr + static_cast<std::uint32_t>(purchase.item) * 4u;
        memory.write<std::int32_t>(addr, memory.read<std::int32_t>(addr) + purchase.amount);
    }
    // The same sound, mode and vendor owner as VendorPurchaseDialogUpdate.
    vendor_sound(mod, 7);
    mod.info("Buy All Ammo: purchased " + std::to_string(plan.count) +
             " ammo types for " + std::to_string(plan.cost) + " bolts");
}

void tick(openrac::Mod& mod, const Disc& disc) {
    const bool square = s_square_just_pressed;
    s_square_just_pressed = false;

    std::array<buy_all_ammo::Candidate, buy_all_ammo::kItems> candidates{};
    openrac::gaddr ammo_addr = 0;
    const int count = candidates_at_vendor(mod, disc, candidates, ammo_addr);
    if (count <= 0) {
        s_notice_ticks = 0;
        publish(buy_all_ammo::Hint::Hidden);
        touch_buy_button(mod, false);
        return;
    }

    const auto quote = buy_all_ammo::make_quote(candidates, count, mod.bolts());
    const auto hint = buy_all_ammo::hint_for(quote);
    // Keep the action available for native rejection feedback on Android as well.
    touch_buy_button(mod, true);

    if (square && quote.affordable.cost > 0 && quote.affordable.count > 0) {
        buy(mod, ammo_addr, quote.affordable);
        s_notice_ticks = (std::string(disc.version) == "pal") ? 100 : 120;
        s_notice_cost = quote.affordable.cost;
    } else if (square) {
        // The normal vendor's rejection sound: once per press, never on an idle quote.
        vendor_sound(mod, 2);
        s_notice_ticks = 0;
    }
    if (s_notice_ticks > 0) {
        --s_notice_ticks;
        publish(buy_all_ammo::Hint::Purchased, s_notice_cost);
    } else {
        publish(hint, quote.affordable.cost);
    }
}

void load_rac2(openrac::Mod& mod) {
    if (mod.game() != "rac2") throw openrac::Error("R&C2 only");
    const Disc* disc = nullptr;
    for (const Disc& d : kDiscs) {
        if (mod.version() == d.version) { disc = &d; break; }
    }
    if (!disc) throw openrac::Error("Unknown RAC2 disc version");

    s_square_was_down = false;
    s_square_just_pressed = false;
    s_notice_ticks = 0;
    s_notice_cost = 0;
    s_touch_buy_visible = false;
    publish(buy_all_ammo::Hint::Hidden);
    s_show_hud = mod.setting<bool>("show_hud");
    s_hud_position = mod.setting<std::string>("prompt_position");
    const bool pal = mod.version() == "pal";
    const bool us2 = mod.version() == "ntsc2";
    s_print = mod.required_function<Print>(pal ? "func_0027FB48" : us2 ? "func_0027FCB0" : "func_0027F640");
    s_texture = mod.required_function<Texture>(pal ? "func_0027CD60" : us2 ? "func_0027CED8" : "func_0027C878");
    s_scissor = mod.required_function<Scissor>(pal ? "func_002FD678" : us2 ? "func_002FD4B0" : "func_002FC620");
    s_sound = mod.required_function<Sound>("func_002E5CF8");
    s_rect = mod.required_function<Rect>(pal ? "func_0027E338" : us2 ? "func_0027E4D0" : "func_0027DE50");
    mod.hook<openrac::rac2::Vendor_DrawList>([&mod](auto original) {
        // The list establishes its own scissor after our text, so its clipping is preserved.
        draw_hud(mod);
        original();
    });

    mod.on_pad([](int port, std::uint8_t* report, int size) {
        if (port != 0 || report == nullptr || size < 2) return;
        const auto bits = static_cast<std::uint16_t>(report[0]) |
                          (static_cast<std::uint16_t>(report[1]) << 8);
        const bool down = (bits & kSquare) == 0;
        if (down && !s_square_was_down) s_square_just_pressed = true;
        s_square_was_down = down;
    });

    mod.on_frame([&mod, disc] { tick(mod, *disc); });
    mod.info("Vendor Buy All 0.2.1: Square / Xbox X, vendor HUD and Android touch button");
}
} // namespace

// Export a loader rather than a second OpenRAC mod entry point.
void load_buy_all(openrac::Mod& mod) {
    if (mod.game() == "rac1") load_rac1(mod);
    else if (mod.game() == "rac2") load_rac2(mod);
    else throw openrac::Error("Buy All supports RAC1 and RAC2");
}
