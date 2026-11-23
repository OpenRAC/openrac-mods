// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The level's live mobys (moby_scan.h).

#include "moby_scan.h"

#include <algorithm>
#include <string_view>

#include "discs.h"

namespace ng_plus_plus {
namespace {

// The level's moby table: the first moby's, the first spawned one's and past the last's words
// (g_mobyTableBase, +4 and g_mobyTableEnd; as weapon_visuals.cpp's).
constexpr std::uint32_t kMobyTable = 0x001B1B5Cu;
constexpr std::uint32_t kMobyTableV2 = 0x001B1ADCu;
constexpr std::uint32_t kMobyTableDynamic = 0x04u;
constexpr std::uint32_t kMobyTableEnd = 0x08u;

// A moby's flags (+0x34: bit 0x20 it has a health record), its alpha, its variables, and its
// health record's health and aim height (moby_get_private_value_0, func_002AB1F0).
constexpr std::uint32_t kMobyFlags = 0x34u;
constexpr std::uint16_t kMobyHasHealth = 0x20u;
constexpr std::uint32_t kMobyAlpha = 0x23u;
constexpr std::uint32_t kMobyVars = 0x68u;
constexpr std::uint32_t kHealthRecordBytes = 0x20u;
constexpr std::uint32_t kAimHeight = 0x10u;

// The most mobys a level holds that the mod looks through: more is a broken table.
constexpr std::uint32_t kMostMobys = 8192u;

// Returns the moby table's word on the disc `version`.
std::uint32_t moby_table(std::string_view version) {
    return version == "ntsc2" ? kMobyTableV2 : kMobyTable;
}

}  // namespace

bool MobyScan::read(std::uint32_t tick) {
    // Read in this tick already.
    if (m_read && m_tick == tick) {
        return !m_mobys.empty() || m_base != 0;
    }

    m_read = true;
    m_tick = tick;
    m_mobys.clear();
    m_base = 0;

    // Outside a level there are no mobys.
    if (m_view.loaded_level() < 0) {
        return false;
    }

    const openrac::Mod& mod = m_view.mod();
    const openrac::Memory memory = m_view.memory();
    const std::uint32_t table = moby_table(mod.version());
    const auto first = memory.read<std::uint32_t>(m_view.address_of(table));
    const auto dynamic = memory.read<std::uint32_t>(m_view.address_of(table + kMobyTableDynamic));
    const auto end = memory.read<std::uint32_t>(m_view.address_of(table + kMobyTableEnd));

    // A table not yet laid out, or broken.
    if (first == 0 || end <= first || (end - first) / kMobyRecordBytes > kMostMobys
        || !GameView::is_memory(first, end - first)) {
        return false;
    }

    m_base = first;
    m_mobys.reserve((end - first) / kMobyRecordBytes);

    // Each live moby.
    for (std::uint32_t moby = first; moby + kMobyRecordBytes <= end; moby += kMobyRecordBytes) {
        // A dead or free slot.
        if (memory.read<std::uint8_t>(moby + kMobyStateByte) >= 0x80u) {
            continue;
        }

        ScannedMoby out;
        out.address = moby;
        out.slot = static_cast<int>((moby - first) / kMobyRecordBytes);
        out.moby_class = memory.read<std::uint16_t>(moby + kMobyClass);
        out.position = {
            memory.read<float>(moby + kMobyPosition),
            memory.read<float>(moby + kMobyPosition + 4u),
            memory.read<float>(moby + kMobyPosition + 8u),
        };
        out.enemy = m_view.enemy_bolts(moby).has_value();
        out.spawned = moby >= dynamic;
        m_mobys.push_back(out);
    }

    return true;
}

const ScannedMoby* MobyScan::find(std::uint32_t address) const {
    // In slot order: a binary search by address.
    const auto it = std::lower_bound(
        m_mobys.begin(), m_mobys.end(), address, [](const ScannedMoby& moby, std::uint32_t at) {
            return moby.address < at;
        }
    );
    return it != m_mobys.end() && it->address == address ? &*it : nullptr;
}

std::uint32_t MobyScan::slot_address(const GameView& view, int slot) {
    // Outside a level, or no slot.
    if (slot < 0 || view.loaded_level() < 0) {
        return 0;
    }

    const std::uint32_t table = moby_table(view.mod().version());
    const auto first = view.memory().read<std::uint32_t>(view.address_of(table));
    const auto end = view.memory().read<std::uint32_t>(view.address_of(table + kMobyTableEnd));
    const std::uint32_t moby = first + static_cast<std::uint32_t>(slot) * kMobyRecordBytes;
    return first != 0 && moby + kMobyRecordBytes <= end ? moby : 0u;
}

int MobyScan::slot_of(const GameView& view, std::uint32_t address) {
    // Outside a level.
    if (view.loaded_level() < 0) {
        return -1;
    }

    const std::uint32_t table = moby_table(view.mod().version());
    const auto first = view.memory().read<std::uint32_t>(view.address_of(table));
    const auto end = view.memory().read<std::uint32_t>(view.address_of(table + kMobyTableEnd));
    return first != 0 && address >= first && address + kMobyRecordBytes <= end
                   && (address - first) % kMobyRecordBytes == 0
               ? static_cast<int>((address - first) / kMobyRecordBytes)
               : -1;
}

std::uint32_t MobyScan::address_of(int slot) const {
    return m_base != 0 && slot >= 0 ? m_base + static_cast<std::uint32_t>(slot) * kMobyRecordBytes
                                    : 0u;
}

bool MobyScan::alive(std::uint32_t address, int moby_class) const {
    // No moby there, or one the game's memory does not hold.
    if (!GameView::is_memory(address, kMobyRecordBytes)) {
        return false;
    }

    const openrac::Memory memory = m_view.memory();
    return memory.read<std::uint8_t>(address + kMobyStateByte) < 0x80u
           && memory.read<std::uint16_t>(address + kMobyClass) == moby_class;
}

std::array<float, 3> MobyScan::position(std::uint32_t address) const {
    const openrac::Memory memory = m_view.memory();
    return {
        memory.read<float>(address + kMobyPosition),
        memory.read<float>(address + kMobyPosition + 4u),
        memory.read<float>(address + kMobyPosition + 8u),
    };
}

TargetFacts MobyScan::facts(std::uint32_t address) const {
    TargetFacts out;

    // No moby there.
    if (!GameView::is_memory(address, kMobyRecordBytes)) {
        return out;
    }

    const openrac::Memory memory = m_view.memory();
    out.state = memory.read<std::uint8_t>(address + kMobyStateByte);
    out.enemy = m_view.enemy_bolts(address).has_value();
    out.mode = memory.read<std::uint16_t>(address + kMobyFlags);
    out.alpha = memory.read<std::uint8_t>(address + kMobyAlpha);

    if ((out.mode & kMobyHasHealth) != 0) {
        const auto vars = memory.read<std::uint32_t>(address + kMobyVars);
        const auto record =
            GameView::is_memory(vars, 4) ? memory.read<std::uint32_t>(vars) : std::uint32_t{0};
        if (GameView::is_memory(record, kHealthRecordBytes)) {
            out.has_health = true;
            out.health = memory.read<float>(record);
        }
    }

    return out;
}

std::uint32_t MobyScan::health_record(std::uint32_t address) const {
    // No moby there.
    if (!GameView::is_memory(address, kMobyRecordBytes)) {
        return 0;
    }

    const openrac::Memory memory = m_view.memory();

    // No health record.
    if ((memory.read<std::uint16_t>(address + kMobyFlags) & kMobyHasHealth) == 0) {
        return 0;
    }

    const auto vars = memory.read<std::uint32_t>(address + kMobyVars);
    const auto record =
        GameView::is_memory(vars, 4) ? memory.read<std::uint32_t>(vars) : std::uint32_t{0};
    return GameView::is_memory(record, kHealthRecordBytes) ? record : 0;
}

float MobyScan::aim_height(std::uint32_t address) const {
    // No moby there.
    if (!GameView::is_memory(address, kMobyRecordBytes)) {
        return 0.0f;
    }

    const openrac::Memory memory = m_view.memory();

    // No health record: no height of its own.
    if ((memory.read<std::uint16_t>(address + kMobyFlags) & kMobyHasHealth) == 0) {
        return 0.0f;
    }

    const auto vars = memory.read<std::uint32_t>(address + kMobyVars);
    const auto record =
        GameView::is_memory(vars, 4) ? memory.read<std::uint32_t>(vars) : std::uint32_t{0};
    const float height =
        GameView::is_memory(record, kHealthRecordBytes) ? memory.read<float>(record + kAimHeight) : 0.0f;
    return height > 0.0f && height < 20.0f ? height : 0.0f;
}

}  // namespace ng_plus_plus
