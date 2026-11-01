/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * A save a party starts from, checked before the game loads it. The check is the SDK's
 * (openrac/games/rac1_save.h), the one the port runs before play_start and coop_save_write, so a
 * save has one answer everywhere: the size, the two sizes at its front, each part's checksum, every
 * block's id and size in the game's own order, and the values the game uses as an index or a count.
 * These are the co-op's names for it. Nothing here needs a game: tests/play_save_test.cpp.
 */
#ifndef MULTIPLAYER_SAVE_CHECK_H
#define MULTIPLAYER_SAVE_CHECK_H

#include <cstddef>
#include <cstdint>

#include "openrac/games/rac1_save.h"
#include "protocol.hpp"

namespace mp {

constexpr std::size_t kSaveGameBytes = OPENRAC_RAC1_SAVE_GAME_BYTES;
constexpr std::size_t kSaveLevelBytes = OPENRAC_RAC1_SAVE_LEVEL_BYTES;
constexpr std::size_t kSaveLevels = OPENRAC_RAC1_SAVE_LEVELS;
constexpr std::size_t kSaveBytes = OPENRAC_RAC1_SAVE_BYTES;

enum class SaveFault : std::uint8_t {
    kNone = OPENRAC_RAC1_SAVE_OK,
    kSize = OPENRAC_RAC1_SAVE_SIZE,          // not a save's size, or the sizes at its front are not the game's
    kChecksum = OPENRAC_RAC1_SAVE_CHECKSUM,  // a part's checksum is wrong
    kBlocks = OPENRAC_RAC1_SAVE_BLOCKS,      // a part's blocks are not the game's, in its order
    kRange = OPENRAC_RAC1_SAVE_RANGE         // a value the game indexes with is out of its range
};

/* What is wrong with `save`, for the log: never shown to a player. */
SaveFault check_save(const Bytes& save);
const char* save_fault_name(SaveFault fault);

/* The game's checksum of a part (memcard_Checksum). */
std::uint32_t save_checksum(const std::uint8_t* data, std::size_t size);

/* One block of a save: tests build saves from these. */
using SaveBlock = openrac_rac1_save_block;

/* The blocks of the game part and of a level part, in the game's order. */
const SaveBlock* save_game_blocks(std::size_t& count);
const SaveBlock* save_level_blocks(std::size_t& count);

}  // namespace mp

#endif  // MULTIPLAYER_SAVE_CHECK_H
