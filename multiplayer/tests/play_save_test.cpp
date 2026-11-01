/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * A save a party starts from (save_check.hpp): what passes and what does not, and what the game
 * does with one that does not. MP_REAL_SAVES=<folder> also runs every save<n>.bin under it that is
 * a played slot (an empty slot's level is -1).
 */
#include "save_check.hpp"
#include "test_support.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace mpt {

namespace {

void put(Bytes& out, std::size_t at, std::int32_t value) { std::memcpy(out.data() + at, &value, 4); }

std::int32_t got(const Bytes& out, std::size_t at) {
    std::int32_t v = 0;
    std::memcpy(&v, out.data() + at, 4);
    return v;
}

/* A part's blocks written as the game does (memcard_PrepData), zeros in each. */
void write_part(Bytes& out, std::size_t at, std::size_t size, const mp::SaveBlock* blocks, std::size_t count) {
    std::size_t p = at + 8;

    for (std::size_t i = 0; i < count; ++i) {
        put(out, p, blocks[i].id);
        put(out, p + 4, blocks[i].size);
        p += 8 + ((static_cast<std::size_t>(blocks[i].size) + 3) & ~static_cast<std::size_t>(3));
    }

    put(out, p, -1);
    put(out, p + 4, 0);
    put(out, at, static_cast<std::int32_t>(size - 8));
}

void seal(Bytes& out, std::size_t at, std::size_t size) {
    put(out, at + 4, static_cast<std::int32_t>(mp::save_checksum(out.data() + at + 8, size - 8)));
}

constexpr std::size_t kGamePart = 8;

std::size_t level_part(std::size_t i) { return 8 + mp::kSaveGameBytes + i * mp::kSaveLevelBytes; }

Bytes good_save() {
    Bytes out(mp::kSaveBytes, 0);
    put(out, 0, static_cast<std::int32_t>(mp::kSaveGameBytes));
    put(out, 4, static_cast<std::int32_t>(mp::kSaveLevelBytes));
    std::size_t count = 0;
    const mp::SaveBlock* blocks = mp::save_game_blocks(count);
    write_part(out, kGamePart, mp::kSaveGameBytes, blocks, count);
    seal(out, kGamePart, mp::kSaveGameBytes);
    blocks = mp::save_level_blocks(count);

    for (std::size_t i = 0; i < mp::kSaveLevels; ++i) {
        write_part(out, level_part(i), mp::kSaveLevelBytes, blocks, count);
        seal(out, level_part(i), mp::kSaveLevelBytes);
    }

    return out;
}

/* Where a game block's value is in the file. */
std::size_t game_value(std::int32_t id) {
    std::size_t count = 0;
    const mp::SaveBlock* blocks = mp::save_game_blocks(count);
    std::size_t p = kGamePart + 8;

    for (std::size_t i = 0; i < count; ++i) {
        if (blocks[i].id == id) {
            return p + 8;
        }

        p += 8 + ((static_cast<std::size_t>(blocks[i].size) + 3) & ~static_cast<std::size_t>(3));
    }

    CHECK(false);
    return 0;
}

/* The good save with a game block's first word set, the checksum made right again: what a host
 * that means harm sends. */
mp::SaveFault with_value(std::int32_t id, std::int32_t value) {
    Bytes save = good_save();
    put(save, game_value(id), value);
    seal(save, kGamePart, mp::kSaveGameBytes);
    return mp::check_save(save);
}

void a_save_read_as_the_game_writes_it() {
    std::size_t game = 0, level = 0;
    mp::save_game_blocks(game);
    mp::save_level_blocks(level);
    CHECK(game == 47 && level == 11);
    CHECK(mp::kSaveBytes == 59912);
    // The checksum as the game has it: 0 past 0x1800 bytes, and of nothing its seed's low half.
    const Bytes big(0x1801, 1);
    CHECK(mp::save_checksum(big.data(), big.size()) == 0);
    CHECK(mp::save_checksum(nullptr, 0) == 0x8320u);
    CHECK(mp::check_save(good_save()) == mp::SaveFault::kNone);
}

void what_does_not_pass() {
    using F = mp::SaveFault;
    // The size, either way, and the sizes at its front.
    CHECK(mp::check_save({}) == F::kSize);
    Bytes save = good_save();
    save.pop_back();
    CHECK(mp::check_save(save) == F::kSize);
    save = good_save();
    save.push_back(0);
    CHECK(mp::check_save(save) == F::kSize);
    save = good_save();
    put(save, 0, 5420);
    CHECK(mp::check_save(save) == F::kSize);

    // A byte changed, in each kind of part: all twenty level parts are read.
    save = good_save();
    save[game_value(1)] ^= 1;
    CHECK(mp::check_save(save) == F::kChecksum);
    save = good_save();
    save[level_part(19) + 20] ^= 1;
    CHECK(mp::check_save(save) == F::kChecksum);
    // A checksum of 0 the game takes as none.
    save = good_save();
    put(save, kGamePart + 4, 0);
    CHECK(mp::check_save(save) == F::kChecksum);

    // Blocks that walk the game off the file, or into another table's place, checksummed right: a
    // size of its own, a negative one, another id, the wrong order, no end.
    save = good_save();
    put(save, kGamePart + 8 + 4, 0x7FFFFFF0);
    seal(save, kGamePart, mp::kSaveGameBytes);
    CHECK(mp::check_save(save) == F::kBlocks);
    save = good_save();
    put(save, kGamePart + 8 + 4, -8);
    seal(save, kGamePart, mp::kSaveGameBytes);
    CHECK(mp::check_save(save) == F::kBlocks);
    save = good_save();
    put(save, kGamePart + 8, 999);
    seal(save, kGamePart, mp::kSaveGameBytes);
    CHECK(mp::check_save(save) == F::kBlocks);
    save = good_save();
    put(save, kGamePart + 8, 1);
    put(save, kGamePart + 8 + 12, 0);
    seal(save, kGamePart, mp::kSaveGameBytes);
    CHECK(mp::check_save(save) == F::kBlocks);
    save = good_save();
    put(save, kGamePart + mp::kSaveGameBytes - 8, 0);
    seal(save, kGamePart, mp::kSaveGameBytes);
    CHECK(mp::check_save(save) == F::kBlocks);
    save = good_save();
    put(save, level_part(3), 100);
    CHECK(mp::check_save(save) == F::kBlocks);

    // What the game indexes with, and the empty slot's level.
    CHECK(with_value(0, -1) == F::kRange);
    CHECK(with_value(0, 19) == F::kRange);
    CHECK(with_value(0, 18) == F::kNone);
    CHECK(with_value(1, -1) == F::kRange);
    CHECK(with_value(2, 81) == F::kRange);
    CHECK(with_value(3, -1) == F::kRange);
    CHECK(with_value(13, 40) == F::kRange);
    CHECK(with_value(13, 39) == F::kNone);
    CHECK(with_value(20, 20) == F::kRange);
    CHECK(with_value(21, 1000) == F::kRange);
    CHECK(with_value(32, -2) == F::kRange);
    CHECK(with_value(32, -1) == F::kNone);
    CHECK(with_value(1011, 0x97) == F::kRange);
    CHECK(with_value(1011, 0x96) == F::kNone);
}

void real_saves() {
    const char* folder = std::getenv("MP_REAL_SAVES");

    if (folder == nullptr) {
        return;
    }

    int played = 0;

    for (const auto& e : std::filesystem::recursive_directory_iterator(folder)) {
        const std::string name = e.path().filename().string();

        if (name.rfind("save", 0) != 0 || e.path().extension() != ".bin") {
            continue;
        }

        std::ifstream in(e.path(), std::ios::binary);
        Bytes save((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const mp::SaveFault fault = mp::check_save(save);

        if (got(save, 8 + 8 + 8) == -1) {
            CHECK(fault == mp::SaveFault::kRange);  // an empty slot: its level is -1
        } else {
            CHECK(fault == mp::SaveFault::kNone);
            ++played;
        }
    }

    std::printf("real saves: %d played slots pass\n", played);
}

/* A member given a save that does not pass does not load it, is told, and does not ask again; the
 * host's own that does not pass tells nobody of a game. */
void a_refused_save() {
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    {
        Player host(1), member(2);
        host.session.handle(info(both));
        member.session.handle(info(both));
        member.game.saves_usable = false;
        host.coop.choose(good_save(), "Kerwan");
        CHECK(start_ready(host));
        route({&host, &member});
        tick({&host, &member}, 16);
        CHECK(member.game.calls.empty());
        CHECK(member.coop.take_notice() == mp::kBadSaveMember);
        tick({&host, &member}, mp::kWantMs + 16);
        route({&host, &member});
        tick({&host, &member}, 16);
        CHECK(member.game.calls.empty());
    }
    {
        Player host(1), member(2);
        host.session.handle(info(both));
        member.session.handle(info(both));
        host.game.saves_usable = false;
        host.coop.choose(good_save(), "Kerwan");
        CHECK(!start_ready(host));
        CHECK(host.coop.take_notice() == mp::kBadSaveHost);
        CHECK(host.game.calls.empty());
        route({&host, &member});
        tick({&host, &member}, 16);
        CHECK(member.game.calls.empty());
    }
    {
        // The good one plays on the other side as it is.
        Player host(1), member(2);
        host.session.handle(info(both));
        member.session.handle(info(both));
        const Bytes save = good_save();
        host.coop.choose(save, "Kerwan");
        CHECK(start_ready(host));
        route({&host, &member});
        tick({&host, &member}, 16);
        CHECK(member.game.started_save == save);
    }
}

}  // namespace

void run_save_tests() {
    a_save_read_as_the_game_writes_it();
    what_does_not_pass();
    real_saves();
    a_refused_save();
}

}  // namespace mpt
