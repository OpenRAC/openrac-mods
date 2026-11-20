// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Prizes and prices that keep up with the richer new games (README.md, "Prizes and prices"): from
// the second challenge mode the hoverbike races' and the arenas' prizes pay the run scale
// (bolt_rules.h: x1.5, x2.5, x4, x6, Generous half as much again), and the characters who sell
// for bolts ask half the run scale's rise (x1.25, x1.75, x2.5, x3.5).
//
// Where each lives in the game (rac2-decomp, NTSC-U v1.01 names; the same records on the three
// discs, the levels' data at each disc's own addresses):
//   - The hoverbike races of Barlow and Joba: the race table (g_race_challenge_table, 5 races of
//     0x40 bytes), whose words +0x08, +0x0C and +0x10 are the prize of a race's first, second and
//     later wins. The race's panel (func_L04_0040AD28) prints it and the finish
//     (race_update_player_progress) pays it, ten times in challenge mode, both straight from the
//     table: the mod writes the scaled prizes into the table while the level is loaded.
//   - The arenas of Maktar and Joba: their prize function (moby_compute_scaled_entry_value), which
//     the menu prints and the win pays: the hook point rac2.arena.prize.
//   - The sellers: each level's price table (pairs of the game's and challenge mode's price, 0x48
//     bytes, in Notak, Tabora, Dobbo, Aranos's prison, Smolg and Grelbin: Notak's toll door words
//     0-1, Dobbo's seller 4-5, the prison's seller 10-11, Smolg's machine 12-13; Tabora's and
//     Grelbin's are crystals and moonstones, left alone), and the gadget sellers' item row (+0x80,
//     the price the weapon vendor never shows for these): Oozla's Tractor Beam, Barlow's
//     Thermanator, Joba's Levitator and Damosel's Hypnomatic. Each seller's dialog prints the
//     price it then takes, both from there: the mod writes the scaled prices while the level is
//     loaded. The weapon vendor's prices are the tiers' (vendor.h).
//
// A written word is the game's own value scaled, never scaled twice: the mod keeps each word's
// game value, takes a word that differs from what it wrote as the game's again (a level loaded
// anew), and writes the game's value back when the run pays as the game does.
//
// Only the game's thread uses it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>

#include "bolt_rules.h"
#include "game_view.h"
#include "openrac/mod.hpp"

namespace ng_plus_plus {

namespace hooks {

// The bolts arena challenge `challenge` pays (moby_compute_scaled_entry_value, Maktar's and Joba's
// arena): the menu prints it and a win adds it. An OpenRAC without it keeps the game's prizes.
struct Arena_Prize {
    static constexpr const char* id = "rac2.arena.prize";
    static constexpr const char* name = "Arena_Prize";
    static constexpr const char* signature = "i32(i32)";
    using fn = std::int32_t (*)(std::int32_t);
};

}  // namespace hooks

// The levels with prizes or sellers.
inline constexpr int kOozla = 1;
inline constexpr int kBarlow = 4;
inline constexpr int kNotak = 6;
inline constexpr int kDobbo = 9;
inline constexpr int kJoba = 11;
inline constexpr int kAranosPrison = 14;
inline constexpr int kSmolg = 17;
inline constexpr int kDamosel = 18;

// The race table: its races, their size, and the three prizes' place in a race.
inline constexpr int kRaces = 5;
inline constexpr std::uint32_t kRaceBytes = 0x40u;
inline constexpr std::uint32_t kRacePrize = 0x08u;
inline constexpr int kRacePrizes = 3;

// The words of a level's price table that are bolts: Notak's toll door, Dobbo's seller, the
// prison's seller and Smolg's machine, each the game's price and challenge mode's.
inline constexpr int kPriceWords[] = {0, 1, 4, 5, 10, 11, 12, 13};

// A level's table of one disc: the level and the table's address in that level's program.
struct LevelTable {
    int level;
    std::uint32_t address;
};

// The tables of one disc.
struct RewardDisc {
    const char* version;     // openrac_api.version.
    LevelTable races[2];     // Barlow's and Joba's race table (g_race_challenge_table).
    LevelTable prices[4];    // Notak's, Dobbo's, the prison's and Smolg's price table.
};

// clang-format off: one disc per row. NTSC-U v1.01's names: D_L04_002C63C0 (D_L11_002B6D40 in
// Joba), D_L06_002958D0, D_L09_0028E350, D_L14_00291850, D_L17_0028C550; the other discs' from
// OpenRAC's port/game/rac2-<disc>/release/release_names.json, each checked against the disc's data.
inline constexpr RewardDisc kRewardDiscs[] = {
    {"ntsc",  {{kBarlow, 0x002C63C0u}, {kJoba, 0x002B6D40u}},
              {{kNotak, 0x002958D0u}, {kDobbo, 0x0028E350u}, {kAranosPrison, 0x00291850u},
               {kSmolg, 0x0028C550u}}},
    {"pal",   {{kBarlow, 0x002C6240u}, {kJoba, 0x002B6BC0u}},
              {{kNotak, 0x002956D0u}, {kDobbo, 0x0028E150u}, {kAranosPrison, 0x002915D0u},
               {kSmolg, 0x0028C350u}}},
    {"ntsc2", {{kBarlow, 0x002C64C0u}, {kJoba, 0x002B6E40u}},
              {{kNotak, 0x00295950u}, {kDobbo, 0x0028E350u}, {kAranosPrison, 0x00291850u},
               {kSmolg, 0x0028C5D0u}}},
};
// clang-format on

// A gadget a character sells for its item row's price, and the level he is in.
struct GadgetSeller {
    int level;
    int item;
};

// Oozla's Megacorp scientist (the Tractor Beam), Barlow's inventor (the Thermanator), Joba's shady
// salesman (the Levitator) and Damosel's Hypnomatic maker.
inline constexpr GadgetSeller kGadgetSellers[] = {
    {kOozla, 46},
    {kBarlow, 39},
    {kJoba, 8},
    {kDamosel, 55},
};

// Returns the tables of openrac_api.version `version`, or null for a disc NG++ does not know.
inline const RewardDisc* reward_disc_of(std::string_view version) {
    // The port names its discs as the table does.
    for (const RewardDisc& disc : kRewardDiscs) {
        // The port names its discs as the table does.
        if (version == disc.version) {
            return &disc;
        }
    }

    return nullptr;
}

// Returns how much more a seller asks at run scale `run`: half its rise (x1.25 at x1.5, x3.5 at x6).
float price_scale(float run);

// Returns `value` times `scale`, rounded, within an s32 (0 and less as they are).
std::int32_t scaled(std::int32_t value, float scale);

// The prizes and prices of the run.
//
// Holds the mod and the view by reference; the mod's state owns them.
class Rewards {
public:
    // Scales the prizes and prices of `view`'s game as `scaling` says, through `mod`.
    Rewards(openrac::Mod& mod, const GameView& view, BoltScaling scaling);

    // Takes the arena's hook point, when bolt scaling is on and the port has it.
    void start();

    // Once a game tick: the loaded level's race prizes and prices at the run's scale.
    void tick();

private:
    // The game's value of one word the mod writes, and what it wrote.
    struct Word {
        std::int32_t game;
        std::int32_t written;
    };

    // Returns the run scale now (1.0: as the game pays).
    float run() const;

    // The arena's prize (rac2.arena.prize): the game's, at the run scale.
    std::int32_t arena_prize(openrac::Original<std::int32_t(std::int32_t)> original, std::int32_t id);

    // Writes the game's value of the word at game address `address` times `scale`.
    void scale_word(openrac::gaddr address, float scale);

    openrac::Mod& m_mod;                       // The mod; the SDK keeps it for the program.
    const GameView& m_view;                    // The game; outlives this.
    BoltScaling m_scaling;                     // The "bolt_scaling" setting.
    const RewardDisc* m_disc;                  // This disc's tables (null: none known).
    const Disc* m_items;                       // This disc's item table (null: none known).
    int m_level = -1;                          // The level the words below are of.
    std::map<openrac::gaddr, Word> m_words;    // The words written in that level.
};

}  // namespace ng_plus_plus
