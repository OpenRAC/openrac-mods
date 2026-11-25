// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The ship's weapon tiers (ship_upgrades.h).

#include "ship_upgrades.h"

#include <cmath>
#include <optional>
#include <string>

#include "hooks.h"

namespace ng_plus_plus {

namespace {

// The tiers' names, Alpha to Omega, as the weapons' (tiers.cpp).
constexpr const char* kNames[kShipTiers] = {
    "Alpha",
    "Beta",
    "Gamma",
    "Delta",
    "Epsilon",
    "Zeta",
    "Eta",
    "Omega",
};

// Each tier's damage and fire rate, times the game's.
constexpr float kDamage[kShipTiers] = {1.25f, 1.5f, 1.8f, 2.1f, 2.5f, 3.0f, 3.5f, 4.2f};
constexpr float kRate[kShipTiers] = {1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.65f, 1.8f, 2.0f};

// Alpha's price and each tier's more.
constexpr std::int32_t kFirstPrice = 80;
constexpr std::int32_t kPriceStep = 40;

// The port's text slots the entry's name and description go into: the weapon vendor's (text.cpp),
// as no weapon vendor is open while the Ship Shack's list shows.
constexpr int kNameSlot = 0;
constexpr int kTextSlot = 1;

// The raritanium word in the save block.
constexpr std::uint32_t kRaritanium = 0x04u;

// A moby's pvars pointer.
constexpr std::uint32_t kMobyPvars = 0x68u;

// The ship's per-tick timers, how many in all.
constexpr std::size_t kTimers = kShipTimers16.size() + kShipTimers32.size();

}  // namespace

const char* ship_tier_name(int tier) {
    return tier >= 1 && tier <= kShipTiers ? kNames[tier - 1] : "";
}

std::int32_t ship_tier_price(int tier) {
    return tier >= 1 && tier <= kShipTiers ? kFirstPrice + kPriceStep * (tier - 1) : 0;
}

float ship_damage(int tier) {
    return tier >= 1 && tier <= kShipTiers ? kDamage[tier - 1] : 1.0f;
}

float ship_fire_rate(int tier) {
    return tier >= 1 && tier <= kShipTiers ? kRate[tier - 1] : 1.0f;
}

std::string ship_tier_title(int tier) {
    return std::string(ship_tier_name(tier)) + " Ship Weapons";
}

std::string ship_tier_text(int tier) {
    const int damage = static_cast<int>(std::lround((ship_damage(tier) - 1.0f) * 100.0f));
    const int rate = static_cast<int>(std::lround((ship_fire_rate(tier) - 1.0f) * 100.0f));
    std::string text = "NG++: every ship weapon deals " + std::to_string(damage)
                       + "% more damage and fires " + std::to_string(rate) + "% faster.";

    // The perk this tier adds.
    if (tier == kShipOverchargeTier) {
        text += " Every fourth hit is overcharged.";
    } else if (tier == kShipExecuteTier) {
        text += " Hits finish off badly damaged ships.";
    } else if (tier == kShipArcTier) {
        text += " Hits arc to the nearest enemy ship.";
    }

    return text;
}

int extra_timer_ticks(float rate, std::int32_t left, float& carry) {
    // No faster rate, or a timer the game's own tick fires.
    if (rate <= 1.0f || left <= 1) {
        carry = 0.0f;
        return 0;
    }

    carry += rate - 1.0f;
    const auto whole = static_cast<int>(carry);
    const int taken = whole < left - 1 ? whole : left - 1;
    carry -= static_cast<float>(whole);
    return taken;
}

std::uint32_t equipment_word(std::string_view version) {
    // Each disc's g_equipment_bits (its level load's reads and func_002926B8's).
    if (version == "ntsc") {
        return 0x001A7AF0u;
    }

    if (version == "pal") {
        return 0x001A7B78u;
    }

    return version == "ntsc2" ? 0x001A7AF8u : 0u;
}

int ShipUpgrades::tier() const {
    return m_tiers.level(kShipItem);
}

void ShipUpgrades::tick(std::uint32_t tick) {
    // An unknown disc, or no level.
    if (m_equipment == 0 || m_view.loaded_level() < 0) {
        return;
    }

    take_purchase();
    keep_entry();
    speed_weapons(tick);
}

void ShipUpgrades::take_purchase() {
    const openrac::Memory memory = m_view.memory();
    const openrac::gaddr word = m_view.address_of(m_equipment);
    const auto bits = memory.read<std::uint32_t>(word);

    // No purchase of NG++'s entry.
    if ((bits & kShipTierBit) == 0) {
        return;
    }

    // The bit is NG++'s alone: never left in the save.
    memory.write(word, bits & ~kShipTierBit);

    // A party's guest, or every tier had: nothing to take.
    if (m_tiers.party_on() || m_tiers.own_level(kShipItem) >= kShipTiers) {
        return;
    }

    m_tiers.step(kShipItem);
    const int now = m_tiers.own_level(kShipItem);
    m_mod.info(
        "bought " + ship_tier_title(now) + " for " + std::to_string(ship_tier_price(now))
        + " raritanium"
    );
}

std::int32_t ShipUpgrades::raritanium() const {
    return m_view.memory().read<std::int32_t>(m_view.address_of(m_progress + kRaritanium));
}

void ShipUpgrades::keep_entry() {
    const openrac::Memory memory = m_view.memory();
    const openrac::gaddr state = m_view.address_of(m_vendor);
    const int next = m_tiers.own_level(kShipItem) + 1;

    // Not the Ship Shack's list, a purchase under way, a guest's game or every tier had.
    if (memory.read<std::int32_t>(state + kShopMode) != kShopShipMode
        || memory.read<std::int32_t>(state + kShopBusy) != 0 || m_tiers.party_on()
        || next > kShipTiers) {
        return;
    }

    const auto count = memory.read<std::int32_t>(state + kShopCount);

    // A list not built yet, or full.
    if (count < 0 || count >= kShopMaxEntries) {
        return;
    }

    const auto entry_at = [state](std::int32_t place) {
        return state + kShopEntries + static_cast<std::uint32_t>(place) * kShopEntryBytes;
    };

    // The name and description, in the port's slots while the list shows (the game prints the
    // name from its address each frame, and looks the description up by its id).
    const std::optional<std::uint32_t> name = m_view.write_slot(kNameSlot, ship_tier_title(next));
    const std::optional<std::uint32_t> text = m_view.write_slot(kTextSlot, ship_tier_text(next));

    // A port without text slots: no entry, as it could not be named.
    if (!name || !text) {
        return;
    }

    const bool present =
        count > 0 && memory.read<std::uint32_t>(entry_at(count - 1) + kShopMask) == kShipTierBit;
    const openrac::gaddr entry = present ? entry_at(count - 1) : entry_at(count);

    // The entry, as the game's builder writes its parts' (+0x10, a catalogue index, is left as
    // the slot had it, as the game's own paint entries leave it).
    memory.write(entry + kShopIcon, kShipTierIcon);
    memory.write(entry + kShopPrice, ship_tier_price(next));
    memory.write(entry + kShopMask, kShipTierBit);
    memory.write(entry + kShopValue, kShipTierBit);
    memory.write(entry + kShopName, *name);
    memory.write(entry + kShopText, hooks::kTextSlot + kTextSlot);

    // New in this list: counted.
    if (!present) {
        memory.write(state + kShopCount, count + 1);
    }

    // Once a tier: for a player's log.
    if (m_logged_tier != next) {
        m_logged_tier = next;
        m_mod.info(
            "the Ship Shack sells " + ship_tier_title(next) + " for "
            + std::to_string(ship_tier_price(next)) + " raritanium (you have "
            + std::to_string(raritanium()) + ")"
        );
    }
}

void ShipUpgrades::speed_weapons(std::uint32_t tick) {
    const float rate = ship_fire_rate(tier());

    // The game's own rate.
    if (rate <= 1.0f || !m_scan.read(tick)) {
        return;
    }

    const openrac::Memory memory = m_view.memory();

    // The ship the player flies.
    for (const ScannedMoby& moby : m_scan.mobys()) {
        // Another moby.
        if (moby.moby_class != kShipClass) {
            continue;
        }

        const auto pvars = memory.read<std::uint32_t>(moby.address + kMobyPvars);

        // No pvars to run.
        if (!GameView::is_memory(pvars, kShipTimers32.back() + 4u)) {
            return;
        }

        // Another ship (a level loaded): the fractions start again.
        if (moby.address != m_ship) {
            m_ship = moby.address;
            m_carry = {};
        }

        std::size_t index = 0;

        // The twin guns' and two specials' s16 timers.
        for (const std::uint32_t offset : kShipTimers16) {
            const auto left = memory.read<std::int16_t>(pvars + offset);
            const int extra = extra_timer_ticks(rate, left, m_carry[index++]);
            memory.write(pvars + offset, static_cast<std::int16_t>(left - extra));
        }

        // The missiles' s32 timers.
        for (const std::uint32_t offset : kShipTimers32) {
            const auto left = memory.read<std::int32_t>(pvars + offset);
            const int extra = extra_timer_ticks(rate, left, m_carry[index++]);
            memory.write(pvars + offset, left - extra);
        }

        static_assert(kTimers == 5, "the fractions hold five timers");
        return;
    }
}

}  // namespace ng_plus_plus
