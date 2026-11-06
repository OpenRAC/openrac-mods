/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * A save a party starts from, checked (save_check.hpp): the SDK's check, in the co-op's names.
 */
#include "save_check.hpp"

namespace mp {

std::uint32_t save_checksum(const std::uint8_t* data, std::size_t size) {
    return openrac_rac1_save_checksum(data, size);
}

const SaveBlock* save_game_blocks(std::size_t& count) {
    count = OPENRAC_RAC1_SAVE_GAME_BLOCKS;
    return openrac_rac1_save_game_blocks;
}

const SaveBlock* save_level_blocks(std::size_t& count) {
    count = OPENRAC_RAC1_SAVE_LEVEL_BLOCKS;
    return openrac_rac1_save_level_blocks;
}

SaveFault check_save(const Bytes& save) {
    return static_cast<SaveFault>(openrac_rac1_save_check(save.data(), save.size()));
}

const char* save_fault_name(SaveFault fault) {
    switch (fault) {
        case SaveFault::kNone:
            return "none";
        case SaveFault::kSize:
            return "size";
        case SaveFault::kChecksum:
            return "checksum";
        case SaveFault::kBlocks:
            return "blocks";
        case SaveFault::kRange:
            return "range";
    }

    return "?";
}

}  // namespace mp
