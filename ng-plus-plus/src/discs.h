// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Going Commando: what NG++ reads and writes of the game, per disc and in the game's records.
//
// Every address goes through relocate_data (below 0x1A7C80 every program shares the word; above
// it the boot program's address stands for the loaded level's copy). NTSC-U v1.01's are
// rac2-decomp's globals named in the comments; PAL's and USA v2.00's are the same globals at those
// discs' addresses (rac2-decomp's symbol_addrs/eu and usa-v2, OpenRAC's
// port/game/rac2-<disc>/host/rac2_addresses.h, and the discs' own code: the vendor's state is what
// func_002F6510 loads on each). The records' layouts are the same on the three discs.
//
// Sources: rac2-decomp's C (the functions named in the comments), seen in game code throughout.

#pragma once

#include <cstdint>
#include <string_view>

namespace ng_plus_plus {

// The game's addresses and functions on one disc.
struct Disc {
    const char* version;         // openrac_api.version.
    std::uint32_t weapon_table;  // The items' rows, kRowBytes each (g_weaponTable, D_00239BA0).
    std::uint32_t slot;          // u8 per item: its row (g_itemEquippedSlot, D_00139568).
    std::uint32_t exp;           // s32 per item: its experience, << 5 (g_weaponXp, D_00139868).
    std::uint32_t ammo;          // s32 per item: its ammo (g_weaponAmmo, D_00139688).
    std::uint32_t owned;         // u8 per item: non-zero when owned (g_inventoryOwned, D_001A7AF8).
    std::uint32_t hero;          // The hero (D_00189E20); his fields are kHero*.
    std::uint32_t vendor;        // The vendor's state (g_vendorUi, D_0021F240); fields kVendor*.
    std::uint32_t clock;         // u8[8]: the save's chunk 4, stamped when it saves (D_001A7360).
    const char* text;            // The game's text by id (func_00288FB8): its string's address.
    std::uint32_t progress;      // The save block (g_boltCount): +kProgressCycle the cycle.
    const char* banner;          // A HUD help popup by key (func_00288E18): the level-up's.
    const char* message;         // A message line by text id (func_002B0A18).
    const char* upgrade_fx;      // The in-hand level-up (func_L00_0037E6A8): (item, row).
};

// clang-format off: one disc per row, the columns in the order of Disc's members.
// The three discs NG++ knows.
inline constexpr Disc kDiscs[] = {
    {"ntsc",  0x00239BA0u, 0x00139568u, 0x00139868u, 0x00139688u, 0x001A7AF8u, 0x00189E20u,
     0x0021F240u, 0x001A7360u, "func_00288FB8", 0x001A79F8u, "func_00288E18", "func_002B0A18",
     "func_L00_0037E6A8"},
    {"pal",   0x00239BA0u, 0x001395E8u, 0x001398E8u, 0x00139708u, 0x001A7B80u, 0x00189EA0u,
     0x0021F1C0u, 0x001A73E0u, "func_002898E8", 0x001A7A80u, "FUN_00289730", "FUN_002B1588",
     "func_L00_003813E0"},
    {"ntsc2", 0x00239B20u, 0x00139568u, 0x00139868u, 0x00139688u, 0x001A7B00u, 0x00189E20u,
     0x0021F1C0u, 0x001A7360u, "func_002899F8", 0x001A7A00u, "FUN_00289840", "FUN_002B1880",
     "func_L00_0037FB10"},
};
// clang-format on

// Returns the disc of openrac_api.version `version`, or null for one NG++ does not know.
inline const Disc* disc_of(std::string_view version) {
    // The port names its discs as the table does.
    for (const Disc& disc : kDiscs) {
        // The port names its discs as the table does.
        if (version == disc.version) {
            return &disc;
        }
    }

    return nullptr;
}

// What the game's level-up of a weapon not in hand shows (func_00288508): its popup's key and
// selection.
inline constexpr std::int32_t kLevelUpKey = 0x9AA;
inline constexpr std::int32_t kLevelUpSelection = 1;

// An item's row in the item table: its size and fields (func_00288870, func_002F6608).
inline constexpr std::uint32_t kRowBytes = 0xE0u;
inline constexpr std::uint32_t kRowVersion = 0x04u;  // u8: its version, from 0.
inline constexpr std::uint32_t kRowClass = 0x14u;    // s32: the class the vendor shows.
inline constexpr std::uint32_t kRowNext = 0x4Au;     // s16: the next version's row, 0 at the last.
inline constexpr std::uint32_t kRowParent =
    0x4Cu;  // s16: the version before's row, 0 at the first.
inline constexpr std::uint32_t kRowExpGoal =
    0x6Cu;                                         // s32: experience (>> 5) to the next; <= 0 full.
inline constexpr std::uint32_t kRowPrice = 0x80u;  // s32: its price (the Mega's: the upgrade's).
inline constexpr std::uint32_t kRowAmmoMax = 0x8Eu;  // u16: the most ammo.

// The two fields of a row that hold its name's text id.
inline constexpr std::uint32_t kRowLabel = 0x08u;  // s32: the name the vendor and the menus print.
inline constexpr std::uint32_t kRowName = 0x48u;   // s16: the name the weapon wheel prints.

// The width of a text id field of a row.
enum class TextWidth : std::uint8_t {
    S16,
    S32,
};

// A field of a row that holds a text id, and what the game shows it on.
struct RowTextField {
    std::uint32_t offset;  // From the row's start.
    TextWidth width;       // s16 or s32.
    const char* shown;     // The screens that print it.
};

// Every field of an item's row the game looks up as a text, by offset. Each screen reads its own
// text id from the row and prints the game's finished, translated string (seen in game code).
inline constexpr RowTextField kRowTextFields[] = {
    // DrawVendorPurchasePrompt (vendor dialog title and buy prompt), item_menu_draw,
    // UpdateItemGridMenu, the weapons screen's grid label (func_00337210), the upgrade shop's price
    // line (shop_draw_upgrade_price_lines).
    {kRowLabel, TextWidth::S32, "the name in the vendor's dialog, the menus and the weapons grid"},
    // The ammo pickup (func_0031F210, ammo_pickup_update): a format filled with the count.
    {0x38u, TextWidth::S16, "the ammo pickup's message for several rounds"},
    {0x3Au, TextWidth::S16, "the ammo pickup's message for one round"},
    // grid_menu_get_selected_text_id: the pause menu's weapon panel (its blurb, by its use).
    {0x42u, TextWidth::S16, "the pause menu's weapon panel text"},
    // The vendor's preview (func_002F9BC0): shop_set_message_text.
    {0x46u, TextWidth::S16, "the vendor's description of the weapon"},
    // weapon_wheel_draw.
    {kRowName, TextWidth::S16, "the name on the weapon wheel"},
};

// The hero's fields (func_L00_002D5EB8, Moby_PostHit's callers).
inline constexpr std::uint32_t kHeroReady = 0x0340u;  // s32: non-zero when he can play a level-up.
inline constexpr std::uint32_t kHeroHeld = 0x1248u;   // s32: the item in hand.
inline constexpr std::uint32_t kHeroMoby = 0x2290u;   // u32: his moby.

// The vendor's state (func_002F6608, func_002F6510, func_002F8918).
inline constexpr std::uint32_t kVendorCursor = 0x80u;      // s32: the entry under the cursor.
inline constexpr std::uint32_t kVendorEntries = 0x140u;    // The list.
inline constexpr std::uint32_t kVendorEntryBytes = 0x18u;  // One entry's size.
inline constexpr std::uint32_t kVendorCount = 0x740u;      // s32: the entries in the list.
inline constexpr int kVendorMaxEntries = 64;               // The list's room.

// A vendor entry's fields, each an s32.
inline constexpr std::uint32_t kEntryItem = 0x00u;
inline constexpr std::uint32_t kEntryKind = 0x04u;   // 1: the item's ammo.
inline constexpr std::uint32_t kEntryClass = 0x08u;  // The preview model's class.
inline constexpr std::uint32_t kEntryPrice = 0x0Cu;  // The price's text id.
inline constexpr std::uint32_t kEntryMarked = 0x10u;
inline constexpr std::uint32_t kEntryUpgrade = 0x14u;  // 1: an upgrade.

// The price text an upgrade entry carries, as the game's Mega entry (func_002F6608).
inline constexpr std::int32_t kPriceText = 0xCDB;

// The label under an upgrade entry's icon in the vendor's list, "Mega" (func_002F9C80).
inline constexpr std::int32_t kUpgradeLabelText = 0x325F;

// The rows the vendor's list draws around its cursor, from kVendorDrawFirst to before
// kVendorDrawEnd (func_002F9C80).
inline constexpr int kVendorDrawFirst = -5;
inline constexpr int kVendorDrawEnd = 7;

// The alpha byte of a colour the game draws with, which a tint keeps from the game's colour.
inline constexpr std::uint32_t kColorAlpha = 0xFF000000u;

// The challenge mode counter in the save block, s16 (+1 at each new game in challenge mode,
// func_002DE990).
inline constexpr std::uint32_t kProgressCycle = 0x12u;

// The bolts, s32 (+0x00, D_001A79F8 on NTSC-U v1.01), Tabora's crystals, s16 (+0x22, D_001A7A1A),
// and Grelbin's moonstones, s32 (+0x24, D_001A7A1C), of the save block.
inline constexpr std::uint32_t kProgressBolts = 0x00u;
inline constexpr std::uint32_t kProgressCrystals = 0x22u;
inline constexpr std::uint32_t kProgressMoonstones = 0x24u;

// The kill streak's multiplier in the save block, u8 (g_bolt_multiplier, D_001A7A32 on NTSC-U:
// 1 to 20, shown as "x2" to "x20"; moby_spawn_bolt_pickups and SetDeathBits).
inline constexpr std::uint32_t kProgressStreak = 0x3Au;

// The bolts a burst could not make into coins, s32 (D_001A7A14 on NTSC-U: the word
// moby_spawn_bolt_pickups adds a coin limit's rest to, divided by the streak).
inline constexpr std::uint32_t kProgressBoltCredit = 0x1Cu;

// A moby's class record, u32 (+0x24), and the record's kind, s16 (+0x46), 5 for an enemy: what
// SetDeathBits checks before it counts a kill toward the streak (rac2-decomp src/game/002A0430.c).
inline constexpr std::uint32_t kMobyClassInfo = 0x24u;
inline constexpr std::uint32_t kClassKind = 0x46u;
inline constexpr std::int16_t kKindEnemy = 5;

// A moby's own bolts, s16 (+0xB4: its drop before the repeat-kill falloff and the streak, set per
// moby in the level's data; SetDeathBits).
inline constexpr std::uint32_t kMobyBolts = 0xB4u;

// A hit request's fields (Moby_PostHit, QueueMobyImpact in rac2-decomp: src/game/002A7750.c,
// L_A8E20Req; filled by make_hit_descriptor and part_init_effect_record).
inline constexpr std::uint32_t kHitSource = 0x10u;  // u32: the moby that dealt it (the shot, blast...).
inline constexpr std::uint32_t kHitDamage = 0x1Cu;  // f32: the damage.

// A moby's class, u16 (moby +0xAA: what the game's update dispatch and the ammo pickup key on).
inline constexpr std::uint32_t kMobyClass = 0xAAu;

// A moby class whose hits are one weapon's, and that weapon's item.
struct HitSource {
    std::uint16_t moby_class;  // The source moby's class.
    std::uint8_t item;         // The item whose row its update reads its damage from.
};

// The weapons' own mobies that post their hits with themselves as the source, by the item row
// their update code reads the damage from (g_weapon_table[g_item_slot_to_record[item]], a literal
// item in each): a shot or blast still in flight, a lava pool or a barrier still hurting after the
// player switched weapons is the weapon that fired it, not the one in hand. Every other source
// (the hero himself, a class not listed) takes the weapon in hand. Sources: rac2-decomp's level
// code (src/levels/shared, the function named) and docs/game/moby-dispatch.tsv's classes.
// clang-format off: one class per row.
inline constexpr HitSource kHitSources[] = {
    {1490u, 30u},  // Lancer: lancer_update (L00_003A6C30), item 0x1E
    {3077u, 30u},  // Heavy Lancer, the Lancer's upgrade model
    {1489u, 26u},  // Blitz Gun: blitz_gun_update (L00_003A53E8), item 0x1A
    {2801u, 26u},  // Blitz Cannon: UpdateSpreadWeapon (L00_003D1D28), item 0x1A
    {1481u, 29u},  // Lava Gun: lava_gun_update (L00_00419C98), item 0x1D
    {4406u, 29u},  // Lava Gun's lingering blobs: lava_gun_update_blobs (L00_00412928), item 0x1D
    {1486u, 24u},  // Seeker Gun
    {1497u, 24u},  // Seeker Projectile: seeker_apply_projectile_hit (L00_003AB388), item 0x18
    {3329u, 24u},  // Seeker Projectile (the Mega's), the same hit code
    {1498u, 27u},  // Minirocket Tube's homing shot: homing_l00_shot_update (L00_003ABEC0), item 0x1B
    {3070u, 42u},  // Gravity Bomb's blast: mini_nuke_blast_update (L00_003E3258), item 0x2A
    {3301u, 42u},  // Gravity Bomb's blast: blast_l00_update (L00_003EEE90), item 0x2A
    {3790u, 37u},  // Bouncer's bouncing blast: UpdateBouncingBlastProjectile (L00_004096B0), 0x25
    {3755u, 32u},  // Spiderbot's exploding trail: UpdateExplodingTrailProjectile (L00_004045E8), 0x20
    {2652u, 45u},  // Shield Charger: tesla_barrier_update (L00_003C3D90), item 0x2D
    {4498u, 45u},  // Tesla Barrier, the Shield Charger's upgrade, the same update
    {177u, 18u},   // Tesla Claw: hero_tether_beam_update (the levels' *-tether files)
};
// clang-format on

}  // namespace ng_plus_plus
