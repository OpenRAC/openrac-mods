// SPDX-License-Identifier: MIT
#pragma once
#include "purchase_plan.hpp"

namespace vendor_buy_all::rac1 {
struct Disc {
    const char* version;
    std::uint32_t mode, vendor, table, ammo, owned;
    const char* sound;
};
// Executable globals: ammo and owned. Level-0 globals: mode, menu and item table.
// Sources: rac1-decomp vendor_menu.c and game/ui/vendor.c; OpenRAC ammo-max disc tables.
inline constexpr Disc discs[] = {
    {"pal", 0x0015F6A8u, 0x001CA7C0u, 0x001C43B0u, 0x0013D530u, 0x0013D5C8u,
     "func_0022ED80"},
    {"ntsc", 0x0015F5C4u, 0x001CA4C0u, 0x001C40B0u, 0x0013D428u, 0x0013D4C0u,
     "FUN_0022da68"},
};
inline constexpr int items = 37;
inline constexpr int max_rows = 16;
inline constexpr std::uint32_t row_size = 0x14u;

// Called only in vendor game mode. The menu's state 1 is its settled main list;
// +0x5C excludes the normal purchase dialog. A snapshot invalidated by a menu
// transition never becomes a transaction.
template<class Memory>
int candidates(const Memory& memory, std::uint32_t vendor, std::uint32_t table,
               std::uint32_t ammo, std::uint32_t owned,
               std::array<buy_all_ammo::Candidate, buy_all_ammo::kItems>& out) {
    if (!vendor || !table || !ammo || !owned) return 0;
    if (memory.template read<std::int32_t>(vendor) != 1 ||
        memory.template read<std::int32_t>(vendor + 0x5Cu) != 0) return 0;
    const int count = memory.template read<std::int32_t>(vendor + 0x210u);
    const int cursor = memory.template read<std::int32_t>(vendor + 0x58u);
    if (count < 1 || count > max_rows || cursor < 0 || cursor >= count) return 0;
    // The PDA selects the second ammo price, exactly as the regular buy dialog does.
    const auto price_offset = memory.template read<std::int32_t>(vendor + 0x40u) ? 0xAu : 0x8u;
    std::array<bool, items> seen{};
    int found = 0;
    for (int row = 0; row < count; ++row) {
        const auto entry = vendor + 0xD0u + static_cast<std::uint32_t>(row) * row_size;
        const int item = memory.template read<std::int32_t>(entry);
        if (item < 0 || item >= items || memory.template read<std::int32_t>(entry + 4u) != 1) continue;
        const auto index = static_cast<std::size_t>(item);
        if (seen[index] || !memory.template read<std::uint8_t>(owned + item)) continue;
        seen[index] = true;
        const auto record = table + static_cast<std::uint32_t>(item) * 0x18u;
        out[static_cast<std::size_t>(found++)] = {
            item, memory.template read<std::int32_t>(ammo + item * 4u),
            memory.template read<std::uint16_t>(record + 0xEu),
            memory.template read<std::uint16_t>(record + price_offset)};
    }
    return found;
}
} // namespace vendor_buy_all::rac1
