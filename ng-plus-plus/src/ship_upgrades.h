// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The ship's weapon tiers (README.md, "The ship"): eight steps past the ship's last upgrade, Alpha
// to Omega, bought from Slim Cognito's Ship Shack for raritanium, as the game sells its own ship
// parts. Each tier hits harder and fires faster; Gamma overcharges every fourth hit, Epsilon
// finishes a damaged ship, Omega's hits arc to the next ship.
//
// The Ship Shack is the vendor's mode 2 (rac2-decomp: shop_build_vendor_list_by_flags,
// func_002F7738, and its confirm, func_L00_00374348): a list of 0x1C-byte entries at the vendor's
// state +0x140 (count +0x740), each an icon, a raritanium price and the equipment word's mask and
// value it gives (g_equipment_bits, 0x1A7AF0 on NTSC-U v1.01). The confirm takes the price from the
// raritanium word (the save block +4) when it is enough and writes the word. NG++ appends one entry
// of its own while the list shows, whose value is the word's unused bit 21: a purchase sets it,
// and NG++ takes it as the next tier and clears it. The tier is kept in NG++'s book (tiers.txt) as
// item byte kShipItem.
//
// The weapons themselves (UpdateShipWeapons, L05_00443D08): the twin guns and the four specials
// each wait on a timer in the ship's pvars, which NG++ runs faster; the damage goes through
// QueueMobyImpact (Moby_PostHit), which space_missions.h scales.
//
// Nothing in the rules reads game memory, so tests/space_test.cpp runs them alone. Only the game's
// thread uses the class.

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "game_view.h"
#include "moby_scan.h"
#include "openrac/mod.hpp"
#include "save_tiers.h"

namespace ng_plus_plus {

// ---- The rules ----

// The item byte of NG++'s book the ship's tier is kept in: the Charge Boots', which have no tiers.
inline constexpr int kShipItem = 54;

// The ship's tiers past its last upgrade, and the ones with a perk.
inline constexpr int kShipTiers = 8;
inline constexpr int kShipOverchargeTier = 3;  // Gamma
inline constexpr int kShipExecuteTier = 5;     // Epsilon
inline constexpr int kShipArcTier = 8;         // Omega

// Every kShipOverchargeEvery-th hit of the ship from Gamma deals kShipOvercharge times.
inline constexpr int kShipOverchargeEvery = 4;
inline constexpr float kShipOvercharge = 2.0f;

// From Epsilon a hit that leaves a ship (not a boss) under this share of its health destroys it.
inline constexpr float kShipExecuteShare = 0.2f;

// At Omega this share of each hit arcs to the nearest other enemy ship within kShipArcRange.
inline constexpr float kShipArcShare = 0.35f;
inline constexpr float kShipArcRange = 40.0f;

// Returns the name of ship tier `tier` (1 Alpha to kShipTiers Omega), "" for any other.
const char* ship_tier_name(int tier);

// Returns the raritanium tier `tier` costs (the game's own ship parts cost 10 to 60): 80 for
// Alpha, 40 more each tier, 360 for Omega; 0 for no tier.
std::int32_t ship_tier_price(int tier);

// Returns the ship's damage at `tier`, times the game's (1.0 at 0).
float ship_damage(int tier);

// Returns how much faster the ship's weapons fire at `tier` (1.0 at 0).
float ship_fire_rate(int tier);

// Returns the shop entry's name and description for tier `tier`.
std::string ship_tier_title(int tier);
std::string ship_tier_text(int tier);

// The Ship Shack's list (the vendor's state in mode 2) and an entry's fields.
inline constexpr std::uint32_t kShopMode = 0x50u;  // s32: 2 the Ship Shack.
inline constexpr std::int32_t kShopShipMode = 2;
inline constexpr std::uint32_t kShopBusy = 0x84u;  // s32: a purchase under way.
inline constexpr std::uint32_t kShopEntries = 0x140u;
inline constexpr std::uint32_t kShopEntryBytes = 0x1Cu;
inline constexpr std::uint32_t kShopCount = 0x740u;  // s32: the entries.
inline constexpr int kShopMaxEntries = 54;
inline constexpr std::uint32_t kShopIcon = 0x00u;   // s32: the icon's frame.
inline constexpr std::uint32_t kShopPrice = 0x04u;  // s32: raritanium.
inline constexpr std::uint32_t kShopMask = 0x08u;   // u32: the equipment bits it changes.
inline constexpr std::uint32_t kShopValue = 0x0Cu;  // u32: what it sets them to.
inline constexpr std::uint32_t kShopName = 0x14u;   // u32: the name string's address.
inline constexpr std::uint32_t kShopText = 0x18u;   // s32: the description's text id.

// The equipment word's bit NG++'s entry gives (bits 21 to 31 are unused: the hull is 0-1, the
// parts 2-15, the paint 16-20), and the icon it shows (a weapon part's frame).
inline constexpr std::uint32_t kShipTierBit = 1u << 21;
inline constexpr std::int32_t kShipTierIcon = 0x0E;

// The ship's weapon timers in its pvars (UpdateShipWeapons): three s16, two s32.
inline constexpr std::array<std::uint32_t, 3> kShipTimers16 = {0x08u, 0x0Au, 0xA8u};
inline constexpr std::array<std::uint32_t, 2> kShipTimers32 = {0x20Cu, 0x210u};

// The ship the player flies in the space levels.
inline constexpr int kShipClass = 4031;

// Returns how many ticks a timer of `left` ticks loses this tick beyond the game's own at a fire
// rate of `rate`, with `carry` the fraction carried from earlier ticks (updated): never past 1
// left, so the game's own tick still fires it.
int extra_timer_ticks(float rate, std::int32_t left, float& carry);

// Returns the equipment word's address on the disc `version` (NTSC-U v1.01 0x1A7AF0, PAL 0x1A7B78,
// NTSC-U v2.00 0x1A7AF8), 0 for another.
std::uint32_t equipment_word(std::string_view version);

// ---- In the game ----

// The ship's tiers in the game: the Ship Shack's entry, its purchase, the faster weapons.
class ShipUpgrades {
public:
    // Sells the tiers in `view`'s game, kept in `tiers`; `progress` is the save block's address
    // (the disc's: its raritanium is at +4) and `vendor` the vendor's state.
    ShipUpgrades(
        openrac::Mod& mod,
        GameView& view,
        SaveTiers& tiers,
        std::uint32_t progress,
        std::uint32_t vendor
    )
        : m_mod(mod),
          m_view(view),
          m_tiers(tiers),
          m_scan(view),
          m_progress(progress),
          m_vendor(vendor),
          m_equipment(equipment_word(mod.version())) {}

    // Returns the ship's tier in play (the party host's in a party), 0 for none.
    int tier() const;

    // Returns the raritanium the player has.
    std::int32_t raritanium() const;

    // Once a game tick: a purchase taken, the shop's entry kept in its list, the weapons' timers.
    void tick(std::uint32_t tick);

private:
    // Takes a purchase of the entry: the next tier.
    void take_purchase();

    // Keeps NG++'s entry at the end of the Ship Shack's list while it shows.
    void keep_entry();

    // Runs the ship's weapon timers faster at its tier.
    void speed_weapons(std::uint32_t tick);

    openrac::Mod& m_mod;
    GameView& m_view;
    SaveTiers& m_tiers;
    MobyScan m_scan;
    std::uint32_t m_progress;        // The save block (its raritanium at +4).
    std::uint32_t m_vendor;          // The vendor's state.
    std::uint32_t m_equipment;       // The equipment word, 0 on an unknown disc.
    std::array<float, 5> m_carry{};  // The timers' fractions.
    std::uint32_t m_ship = 0;        // The ship the fractions are for.
    int m_logged_tier = -1;          // The tier the shop's entry was logged for.
};

}  // namespace ng_plus_plus
