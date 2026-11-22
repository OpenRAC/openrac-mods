// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The setup for hidden test runs (test_setup.h).

#include "test_setup.h"

#ifdef NGPP_TEST_SETUP

#include <cstdlib>
#include <string>

#include "discs.h"
#include "moby_scan.h"
#include "perk_rules.h"
#include "ship_upgrades.h"

namespace ng_plus_plus {
namespace {

// NTSC-U's: the item owned bytes, the item rows by item, the hero's position (g_heroPos, the hero
// +0x80), the save block, and Item_Equip.
constexpr std::uint32_t kOwned = 0x001A7AF8u;
constexpr std::uint32_t kSlot = 0x00139568u;
constexpr std::uint32_t kHeroPos = 0x00189EA0u;
constexpr std::uint32_t kProgress = 0x001A79F8u;
constexpr const char* kEquip = "func_002AD860";

// Ticks into a level before the setup, and between two moves near an enemy.
constexpr int kSetupTicks = 90;
constexpr int kNearTicks = 1200;

}  // namespace

void TestSetup::tick() {
    const int level = m_view.loaded_level();

    // Another level: the setup again.
    if (level != m_level) {
        m_level = level;
        m_ticks = 0;
    }

    // Outside a level, NTSC-U only.
    if (level < 0 || std::string(m_mod.version()) != "ntsc") {
        return;
    }

    ++m_ticks;

    if (m_ticks == kSetupTicks) {
        apply();
    }

    // Near an enemy every while, held for ten ticks.
    if (m_near && m_ticks >= kSetupTicks && (m_ticks - kSetupTicks) % kNearTicks == 0) {
        bring_near();
    }

    if (m_hold_ticks > 0) {
        --m_hold_ticks;
        const openrac::Memory memory = m_view.memory();

        for (int c = 0; c < 3; ++c) {
            memory.write(m_mod.relocate(kHeroPos) + 4u * static_cast<std::uint32_t>(c), m_spot[c]);
        }
    }
}

void TestSetup::apply() {
    const char* env = std::getenv("NGPP_TEST");

    // Not a test run.
    if (env == nullptr) {
        return;
    }

    const std::string spec(env);
    const openrac::Memory memory = m_view.memory();
    int hold = -1;
    std::size_t at = 0;
    const std::size_t options = spec.find(';');
    const std::string items = spec.substr(0, options);

    // Each ITEM:LEVEL.
    while (at < items.size()) {
        const std::size_t comma = items.find(',', at);
        const std::string one =
            items.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        at = comma == std::string::npos ? items.size() : comma + 1;
        const std::size_t colon = one.find(':');

        if (colon == std::string::npos) {
            continue;
        }

        const int item = std::atoi(one.substr(0, colon).c_str());
        const int level = std::atoi(one.substr(colon + 1).c_str());

        // The armour: the game's best, and the tier.
        if (item == kArmourItem) {
            memory.write(m_mod.relocate(kProgress) + kProgressArmour, std::int16_t{kBestArmour});
            m_tiers.set_level_for_test(item, level);
            m_mod.info(
                "test: armour " + std::to_string(kBestArmour) + ", tier " + std::to_string(level)
            );
            continue;
        }

        // The ship's weapon tier (ship_upgrades.h): the tier alone.
        if (item == kShipItem) {
            m_tiers.set_level_for_test(item, level);
            m_mod.info("test: ship tier " + std::to_string(level));
            continue;
        }

        // Owned, at its last version, its ammo full, at the tier.
        memory.write(m_mod.relocate(kOwned) + static_cast<std::uint32_t>(item), std::uint8_t{1});
        Row row = m_view.row(item);

        for (int n = 0; n < 16 && row.parent() > 0; ++n) {
            row = m_view.row(row.parent());
        }

        for (int n = 0; n < 16 && row.next() > 0; ++n) {
            row = m_view.row(row.next());
        }

        memory.write(
            m_mod.relocate(kSlot) + static_cast<std::uint32_t>(item),
            static_cast<std::uint8_t>(row.index())
        );
        m_tiers.set_level_for_test(item, level);
        m_view.set_ammo(item, row.ammo_max() > 0 ? row.ammo_max() : 999);
        m_mod.info(
            "test: item " + std::to_string(item) + " at row " + std::to_string(row.index())
            + " (last " + std::to_string(row.is_last() ? 1 : 0) + "), tier " + std::to_string(level)
            + ", has a tier " + std::to_string(m_weapons.weapon(item).has_tier() ? 1 : 0)
        );
    }

    // The options.
    if (options != std::string::npos) {
        const std::string rest = spec.substr(options + 1);
        const std::size_t h = rest.find("hold=");
        hold = h == std::string::npos ? -1 : std::atoi(rest.c_str() + h + 5);
        m_near = rest.find("near=1") != std::string::npos;
        const std::size_t c = rest.find("cycle=");

        // A challenge mode cycle (the save block's counter, +0x12).
        if (c != std::string::npos) {
            const auto cycle = static_cast<std::int16_t>(std::atoi(rest.c_str() + c + 6));
            memory.write(m_mod.relocate(kProgress) + kProgressCycle, cycle);
            m_mod.info("test: cycle " + std::to_string(cycle));
        }
    }

    // The weapon in hand.
    if (hold >= 0) {
        using EquipFn = std::int32_t (*)(std::int32_t);
        const auto equip = reinterpret_cast<EquipFn>(m_mod.api().function(kEquip));

        if (equip != nullptr) {
            equip(hold);
        }

        m_mod.info(
            "test: equipped " + std::to_string(hold) + ", in hand " + std::to_string(m_view.held())
        );
    }

    // The enemies nearest the hero, for a spot.
    MobyScan scan(m_view);
    scan.read(0xFFFFFFFFu);
    const std::array<float, 3> hero = scan.position(m_view.hero_moby());
    int logged = 0;
    float best = 1e30f;

    for (const ScannedMoby& moby : scan.mobys()) {
        if (moby.enemy && distance2(moby.position, hero) < best) {
            best = distance2(moby.position, hero);
        }
    }

    for (const ScannedMoby& moby : scan.mobys()) {
        // The enemies within twice the nearest's distance, a few.
        if (!moby.enemy || logged >= 6 || distance2(moby.position, hero) > best * 4.0f + 100.0f) {
            continue;
        }

        ++logged;
        m_mod.info(
            "test: enemy class " + std::to_string(moby.moby_class) + " at "
            + std::to_string(moby.position[0]) + "," + std::to_string(moby.position[1]) + ","
            + std::to_string(moby.position[2])
        );
    }

    if (m_near) {
        bring_near();
    }
}

void TestSetup::bring_near() {
    MobyScan scan(m_view);
    scan.read(0xFFFFFFF0u + static_cast<std::uint32_t>(m_ticks % 15));
    const std::array<float, 3> hero = scan.position(m_view.hero_moby());
    const ScannedMoby* nearest = nullptr;
    float best = 1e30f;

    // The nearest enemy that is not a boss.
    for (const ScannedMoby& moby : scan.mobys()) {
        if (moby.enemy && !is_boss(moby.moby_class) && distance2(moby.position, hero) < best) {
            best = distance2(moby.position, hero);
            nearest = &moby;
        }
    }

    // None left.
    if (nearest == nullptr) {
        return;
    }

    m_spot[0] = nearest->position[0] - 6.0f;
    m_spot[1] = nearest->position[1];
    m_spot[2] = nearest->position[2] + 1.0f;
    m_hold_ticks = 10;
    m_mod.info(
        "test: hero put near class " + std::to_string(nearest->moby_class) + " at "
        + std::to_string(m_spot[0]) + "," + std::to_string(m_spot[1]) + ","
        + std::to_string(m_spot[2])
    );
}

}  // namespace ng_plus_plus

#endif
