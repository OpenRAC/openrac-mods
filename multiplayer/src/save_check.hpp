/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * A save a party starts from, checked before the game loads it (save_check.cpp). The save comes
 * from the host, maybe from another disc, and the game trusts its card: memcard_RestoreData walks
 * the blocks by the sizes in them, and the game indexes tables with what it reads. Both discs write
 * one layout, so one check holds for both: the size, the two sizes at the front, each part's
 * checksum, every block's id and size in the game's own order, and the values the game uses as an
 * index or a count.
 *
 * The file: the sizes of the game part and of a level part (8 bytes), the game part, 20 level parts.
 * A part: its length after this header, its checksum, then the blocks, each {id, size, bytes}
 * padded to 4, and {-1, 0} at the end. Nothing here needs a game: tests/play_save_test.cpp.
 */
#ifndef MULTIPLAYER_SAVE_CHECK_H
#define MULTIPLAYER_SAVE_CHECK_H

#include <cstddef>
#include <cstdint>

#include "protocol.hpp"

namespace mp {

constexpr std::size_t kSaveGameBytes = 5424;
constexpr std::size_t kSaveLevelBytes = 2724;
constexpr std::size_t kSaveLevels = 20;
constexpr std::size_t kSaveBytes = 8 + kSaveGameBytes + kSaveLevels * kSaveLevelBytes;

enum class SaveFault : std::uint8_t {
    kNone,
    kSize,      // not a save's size, or the sizes at its front are not the game's
    kChecksum,  // a part's checksum is wrong
    kBlocks,    // a part's blocks are not the game's, in its order
    kRange      // a value the game indexes with is out of its range
};

/* What is wrong with `save`, for the log: never shown to a player. */
SaveFault check_save(const Bytes& save);
const char* save_fault_name(SaveFault fault);

/* The game's checksum of a part (memcard_Checksum). */
std::uint32_t save_checksum(const std::uint8_t* data, std::size_t size);

/* One block of a save: tests build saves from these. */
struct SaveBlock {
    std::int32_t id;
    std::int32_t size;
};

/* The blocks of the game part and of a level part, in the game's order. */
const SaveBlock* save_game_blocks(std::size_t& count);
const SaveBlock* save_level_blocks(std::size_t& count);

}  // namespace mp

#endif  // MULTIPLAYER_SAVE_CHECK_H
