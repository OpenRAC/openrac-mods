// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The setup for hidden test runs (test_setup.h).

#include "test_setup.h"

#ifdef NGPP_TEST_SETUP

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

#include "discs.h"
#include "moby_scan.h"
#include "perk_rules.h"
#include "ship_upgrades.h"

namespace ng_plus_plus {
namespace {

// The hero's position (g_heroPos) and his heading (g_heroOrientVec's z) from the hero's record.
constexpr std::uint32_t kHeroPosition = 0x80u;
constexpr std::uint32_t kHeroFacing = 0x98u;

// Ticks into a level before the setup, and between two moves near an enemy.
constexpr int kSetupTicks = 90;
constexpr int kNearTicks = 1200;

// How far from the weapon vendor (Megacorp Vendor, class 11) the hero stands.
constexpr float kVendorStand = 2.5f;

// The vendor's price of the entry under its cursor (rac2.vendor.price), per disc.
const char* price_function(std::string_view version) {
    return version == "pal" ? "func_002F7480" : version == "ntsc2" ? "func_002F7390" : "func_002F6510";
}

}  // namespace

void TestSetup::tick() {
    const int level = m_view.loaded_level();

    // Another level: the setup again.
    if (level != m_level) {
        m_level = level;
        m_ticks = 0;
    }

    // Outside a level.
    if (level < 0) {
        return;
    }

    ++m_ticks;
    const int setup = m_view.ticks(kSetupTicks);

    if (m_ticks == setup) {
        apply();
    }

    // Near an enemy every while, held for ten ticks.
    if (m_near && m_ticks >= setup && (m_ticks - setup) % m_view.ticks(kNearTicks) == 0) {
        bring_near();
    }

    // At the vendor every two seconds until its list opens.
    if (m_vendor && m_ticks >= setup && (m_ticks - setup) % m_view.ticks(120) == 0
        && m_view.vendor_count() <= 0) {
        bring_to_vendor();
    }

    // The experience, a second after the setup.
    if (m_xp_item >= 0 && m_ticks == setup + m_view.ticks(60)) {
        m_view.add_raw_experience(m_xp_item, m_xp * 32);
        m_mod.info(
            "test: gave item " + std::to_string(m_xp_item) + " " + std::to_string(m_xp)
            + " experience, now " + std::to_string(m_view.experience(m_xp_item))
        );
    }

    if (m_ticks >= setup) {
        watch_vendor();
    }

    if (m_hold_ticks > 0) {
        --m_hold_ticks;
        const openrac::Memory memory = m_view.memory();

        for (int c = 0; c < 3; ++c) {
            memory.write(m_mod.relocate(m_view.disc().hero + kHeroPosition) + 4u * static_cast<std::uint32_t>(c), m_spot[c]);
        }

        // Facing the vendor, which opens only to a hero who looks at it (g_heroOrientVec's z).
        if (m_face) {
            memory.write(m_mod.relocate(m_view.disc().hero + kHeroFacing), m_yaw);
        }

        // The vendor, once it sees him near (its state 2), opened as his button would (state 4,
        // the vendor's update): no button to time against the spot.
        if (m_face && m_vendor_moby != 0
            && memory.read<std::uint8_t>(m_vendor_moby + kMobyStateByte) == 2) {
            memory.write(m_vendor_moby + kMobyStateByte, std::uint8_t{4});
            m_mod.info("test: vendor opened");
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
            memory.write(m_mod.relocate(m_view.disc().progress) + kProgressArmour, std::int16_t{kBestArmour});
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
        memory.write(m_mod.relocate(m_view.disc().owned) + static_cast<std::uint32_t>(item), std::uint8_t{1});
        Row row = m_view.row(item);

        for (int n = 0; n < 16 && row.parent() > 0; ++n) {
            row = m_view.row(row.parent());
        }

        for (int n = 0; n < 16 && row.next() > 0; ++n) {
            row = m_view.row(row.next());
        }

        memory.write(
            m_mod.relocate(m_view.disc().slot) + static_cast<std::uint32_t>(item),
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
        m_vendor = rest.find("vendor=1") != std::string::npos;
        const std::size_t a = rest.find("at=");

        // at=CLASS: held at another moby class than the vendor (the Ship Shack's button).
        if (a != std::string::npos) {
            m_vendor = true;
            m_at_class = std::atoi(rest.c_str() + a + 3);
        }
        const std::size_t b = rest.find("buy=");
        m_buy = b == std::string::npos ? -1 : std::atoi(rest.c_str() + b + 4);
        const std::size_t x = rest.find("xp=");

        // ITEM:N.
        if (x != std::string::npos) {
            m_xp_item = std::atoi(rest.c_str() + x + 3);
            const std::size_t colon = rest.find(':', x);
            m_xp = colon == std::string::npos ? 0 : std::atoi(rest.c_str() + colon + 1);
        }
        const std::size_t c = rest.find("cycle=");

        // A challenge mode cycle (the save block's counter, +0x12).
        if (c != std::string::npos) {
            const auto cycle = static_cast<std::int16_t>(std::atoi(rest.c_str() + c + 6));
            memory.write(m_mod.relocate(m_view.disc().progress) + kProgressCycle, cycle);
            m_mod.info("test: cycle " + std::to_string(cycle));
        }
    }

    // The weapon in hand.
    if (hold >= 0) {
        using EquipFn = std::int32_t (*)(std::int32_t);
        const auto equip = reinterpret_cast<EquipFn>(m_mod.api().function(m_view.disc().equip));

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

void TestSetup::bring_to_vendor() {
    MobyScan scan(m_view);
    scan.read(0xFFFFFFE0u + static_cast<std::uint32_t>(m_ticks % 15));
    const std::array<float, 3> hero = scan.position(m_view.hero_moby());
    const ScannedMoby* vendor = nullptr;
    float best = 1e30f;

    // The nearest vendor.
    for (const ScannedMoby& moby : scan.mobys()) {
        if (moby.moby_class == m_at_class && distance2(moby.position, hero) < best) {
            best = distance2(moby.position, hero);
            vendor = &moby;
        }
    }

    // None in this level.
    if (vendor == nullptr) {
        m_mod.info("test: no moby of class " + std::to_string(m_at_class) + " in this level");
        m_vendor = false;
        return;
    }

    // Around it, a side a try, as its front is not known (it opens within 4 units and 2 of height,
    // to a hero facing it: the vendor's update), turned toward it.
    const int side = (m_ticks / m_view.ticks(120)) % 4;
    const float dx[4] = {kVendorStand, 0.0f, -kVendorStand, 0.0f};
    const float dy[4] = {0.0f, kVendorStand, 0.0f, -kVendorStand};
    m_spot[0] = vendor->position[0] + dx[side];
    m_spot[1] = vendor->position[1] + dy[side];
    m_spot[2] = vendor->position[2] + 0.2f;
    m_yaw = std::atan2(-dy[side], -dx[side]);
    m_vendor_moby = vendor->address;
    m_face = true;
    m_hold_ticks = m_view.ticks(60);
    m_mod.info(
        "test: hero put at class " + std::to_string(m_at_class) + " (side " + std::to_string(side) + ") at "
        + std::to_string(m_spot[0]) + "," + std::to_string(m_spot[1]) + ","
        + std::to_string(m_spot[2])
    );
}

std::string TestSetup::shown_text(std::int32_t id) const {
    using TextFn = std::int32_t (*)(std::int32_t);
    const auto text = reinterpret_cast<TextFn>(m_mod.api().function(m_view.disc().text));

    // No lookup.
    if (text == nullptr) {
        return "?";
    }

    const auto address = static_cast<std::uint32_t>(text(id));
    const openrac::Memory memory = m_view.memory();
    std::string out;

    // The string, printable bytes only, up to its end.
    for (std::uint32_t n = 0; n < 96 && GameView::is_memory(address + n, 1); ++n) {
        const char c = memory.read<char>(address + n);

        if (c == '\0') {
            break;
        }

        out += c >= 0x20 && c < 0x7F ? c : '.';
    }

    return out;
}

void TestSetup::watch_vendor() {
    const int count = m_view.vendor_count();

    // The list as it is now, once each time it changes.
    if (count != m_vendor_seen && count >= 0 && count <= kVendorMaxEntries) {
        m_vendor_seen = count;

        if (count > 0) {
            using PriceFn = std::int32_t (*)();
            const auto price =
                reinterpret_cast<PriceFn>(m_mod.api().function(price_function(m_mod.version())));
            const int cursor = m_view.vendor_cursor();
            const openrac::Memory memory = m_view.memory();
            m_mod.info("test: vendor list of " + std::to_string(count) + ", cursor " + std::to_string(cursor));

            for (int n = 0; n < count; ++n) {
                const VendorEntry entry = m_view.vendor_entry(n);
                memory.write(
                    m_mod.relocate(m_view.disc().vendor) + kVendorCursor, static_cast<std::int32_t>(n)
                );
                const std::int32_t cost = price != nullptr ? price() : -1;
                const bool item = entry.item >= 0 && entry.item < kItems;
                const std::string name =
                    item ? shown_text(m_view.current_row(entry.item).label_id()) : std::string();
                const std::string label =
                    entry.upgrade ? shown_text(m_view.upgrade_label_text()) : std::string();
                m_mod.info(
                    "test: vendor " + std::to_string(n) + ": item " + std::to_string(entry.item)
                    + (entry.upgrade ? " upgrade" : "") + ", price " + std::to_string(cost)
                    + ", tier " + std::to_string(item ? m_tiers.own_level(entry.item) : -1)
                    + ", name '" + name + "', label '" + label + "'"
                );
            }

            memory.write(
                m_mod.relocate(m_view.disc().vendor) + kVendorCursor, static_cast<std::int32_t>(cursor)
            );
        }
    }

    // The cursor on the tier entry to buy.
    if (m_buy >= 0 && count > 0) {
        for (int n = 0; n < count && n < kVendorMaxEntries; ++n) {
            const VendorEntry entry = m_view.vendor_entry(n);

            if (entry.item == m_buy && entry.upgrade) {
                m_view.memory().write(
                    m_mod.relocate(m_view.disc().vendor) + kVendorCursor, static_cast<std::int32_t>(n)
                );
                break;
            }
        }
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
    m_face = false;
    m_mod.info(
        "test: hero put near class " + std::to_string(nearest->moby_class) + " at "
        + std::to_string(m_spot[0]) + "," + std::to_string(m_spot[1]) + ","
        + std::to_string(m_spot[2])
    );
}

}  // namespace ng_plus_plus

#endif
