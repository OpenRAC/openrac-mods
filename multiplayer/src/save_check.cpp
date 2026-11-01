/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * A save a party starts from, checked (save_check.hpp).
 */
#include "save_check.hpp"

#include <cstring>

namespace mp {

namespace {

/* The two discs' tables of what a save holds (mc_gamedata, mc_leveldata): the same ids and sizes in
 * the same order, entry for entry, read from both executables on 2026-10-31. */
constexpr SaveBlock kGameBlocks[] = {
    {0, 4},      {1, 4},      {2, 4},      {3, 4},     {4, 8},    {5, 0x80},  {7, 0xC},   {8, 0x20},
    {9, 0x94},   {10, 0x25},  {11, 0x25},  {12, 0xC},  {13, 0x20}, {19, 4},   {14, 0x14}, {20, 0x50},
    {15, 0x790}, {16, 0x4A0}, {17, 0x120}, {18, 0x128}, {21, 4},  {22, 4},    {23, 4},    {24, 4},
    {25, 4},     {26, 4},     {27, 4},     {28, 1},    {29, 1},   {30, 0x28}, {31, 4},    {32, 0x1C},
    {33, 1},     {34, 4},     {35, 4},     {36, 4},    {37, 0xC}, {1000, 0x94}, {1001, 0x94}, {1002, 0x94},
    {1003, 4},   {1004, 4},   {1005, 4},   {1008, 4},  {1009, 4}, {1010, 0x96}, {1011, 4},
};

constexpr SaveBlock kLevelBlocks[] = {
    {3001, 1},    {3002, 0x800}, {3003, 4},  {3004, 0x10}, {3005, 0x100}, {3006, 0x100},
    {3007, 8},    {3008, 0x10},  {4000, 4},  {4001, 4},    {4002, 4},
};

/* The ids whose values the game uses as an index or a count. */
constexpr std::int32_t kBlockLevel = 0;
constexpr std::int32_t kBlockBolts = 1;
constexpr std::int32_t kBlockGoldBolts = 2;
constexpr std::int32_t kBlockClock = 3;
constexpr std::int32_t kBlockQuickSelect = 13;
constexpr std::int32_t kBlockPlanetList = 20;
constexpr std::int32_t kBlockQuickSwap = 21;
constexpr std::int32_t kBlockLastWeapons = 32;
constexpr std::int32_t kBlockMessages = 1011;

constexpr std::int32_t kLevels = 19;
constexpr std::int32_t kGoldBolts = 80;
constexpr std::int32_t kItems = 40;
constexpr std::int32_t kPlanetSlots = 20;
constexpr std::int32_t kMessages = 0x96;

std::int32_t word(const std::uint8_t* at) {
    std::uint32_t v = 0;
    std::memcpy(&v, at, sizeof v);
    return static_cast<std::int32_t>(v);
}

std::size_t padded(std::size_t size) { return (size + 3) & ~static_cast<std::size_t>(3); }

/* Every word of a block in [low, high). */
bool words_in(const std::uint8_t* at, std::size_t size, std::int32_t low, std::int32_t high) {
    for (std::size_t i = 0; i + 4 <= size; i += 4) {
        const std::int32_t v = word(at + i);

        if (v < low || v >= high) {
            return false;
        }
    }

    return true;
}

bool in_range(const SaveBlock& block, const std::uint8_t* at) {
    const std::size_t size = static_cast<std::size_t>(block.size);

    switch (block.id) {
        case kBlockLevel:
            return words_in(at, size, 0, kLevels);
        case kBlockBolts:
        case kBlockClock:
            return word(at) >= 0;
        case kBlockGoldBolts:
            return words_in(at, size, 0, kGoldBolts + 1);
        case kBlockQuickSelect:
        case kBlockLastWeapons:
        case kBlockQuickSwap:
            return words_in(at, size, -1, kItems);
        case kBlockPlanetList:
            return words_in(at, size, 0, kPlanetSlots);
        case kBlockMessages:
            return words_in(at, size, 0, kMessages + 1);
        default:
            return true;
    }
}

SaveFault check_part(
    const std::uint8_t* part,
    std::size_t size,
    const SaveBlock* blocks,
    std::size_t count,
    bool values
) {
    const std::int32_t length = word(part);
    const std::int32_t sum = word(part + 4);

    if (length != static_cast<std::int32_t>(size) - 8) {
        return SaveFault::kBlocks;
    }

    const auto wanted = save_checksum(part + 8, static_cast<std::size_t>(length));

    if (sum == 0 || static_cast<std::uint32_t>(sum) != wanted) {
        return SaveFault::kChecksum;
    }

    std::size_t at = 8;

    for (std::size_t i = 0; i < count; ++i) {
        const SaveBlock& block = blocks[i];
        const std::size_t room = 8 + padded(static_cast<std::size_t>(block.size));

        if (at + room + 8 > size || word(part + at) != block.id || word(part + at + 4) != block.size) {
            return SaveFault::kBlocks;
        }

        if (values && !in_range(block, part + at + 8)) {
            return SaveFault::kRange;
        }

        at += room;
    }

    if (at + 8 != size || word(part + at) != -1 || word(part + at + 4) != 0) {
        return SaveFault::kBlocks;
    }

    return SaveFault::kNone;
}

}  // namespace

std::uint32_t save_checksum(const std::uint8_t* data, std::size_t size) {
    if (size > 0x1800) {
        return 0;
    }

    std::uint32_t crc = 0xEDB88320u;

    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]) << 8;

        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) != 0 ? (crc << 1) ^ 0x1F45u : crc << 1;
        }
    }

    return crc & 0xFFFFu;
}

const SaveBlock* save_game_blocks(std::size_t& count) {
    count = sizeof kGameBlocks / sizeof kGameBlocks[0];
    return kGameBlocks;
}

const SaveBlock* save_level_blocks(std::size_t& count) {
    count = sizeof kLevelBlocks / sizeof kLevelBlocks[0];
    return kLevelBlocks;
}

SaveFault check_save(const Bytes& save) {
    if (save.size() != kSaveBytes || word(save.data()) != static_cast<std::int32_t>(kSaveGameBytes)
        || word(save.data() + 4) != static_cast<std::int32_t>(kSaveLevelBytes)) {
        return SaveFault::kSize;
    }

    std::size_t count = 0;
    const SaveBlock* blocks = save_game_blocks(count);
    const SaveFault game = check_part(save.data() + 8, kSaveGameBytes, blocks, count, true);

    if (game != SaveFault::kNone) {
        return game;
    }

    blocks = save_level_blocks(count);

    for (std::size_t i = 0; i < kSaveLevels; ++i) {
        const std::uint8_t* part = save.data() + 8 + kSaveGameBytes + i * kSaveLevelBytes;
        const SaveFault level = check_part(part, kSaveLevelBytes, blocks, count, false);

        if (level != SaveFault::kNone) {
            return level;
        }
    }

    return SaveFault::kNone;
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
