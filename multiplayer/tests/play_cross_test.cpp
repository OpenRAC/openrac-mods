/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Two discs in one party: the check goes by the world the disc belongs to, a player never reads of a
 * disc that plays, and a level that does not agree is caught as it loads.
 */
#include "test_support.hpp"

#include <algorithm>

namespace mpt {

namespace {

mp::ModList list_of(const std::string& disc, const std::string& family) {
    mp::ModList list;
    list.multiplayer = "0.9.0";
    list.openrac = "0.21.0";
    list.disc = disc;
    list.family = family;
    return list;
}

/* Text a player could read, with nothing of a region or of a disc that plays. */
void says_no_region(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) {
        CHECK(line.find("PAL") == std::string::npos && line.find("NTSC") == std::string::npos);
        CHECK(line.find("pal") == std::string::npos && line.find("ntsc") == std::string::npos);
    }
}

void two_discs_of_a_world_fit() {
    const mp::ModList pal = list_of("pal", "rac1-60-1");
    const mp::ModList ntsc = list_of("ntsc", "rac1-60-1");
    CHECK(mp::compare_mods(ntsc, pal).empty() && mp::compare_mods(pal, ntsc).empty());
    CHECK(mp::mods_digest(pal) == mp::mods_digest(ntsc));
    // And it stays over the wire, with the disc still said.
    const Bytes bytes = mp::encode_mod_list(ntsc);
    mp::Reader r(bytes.data(), bytes.size());
    mp::ModList read;
    CHECK(mp::decode_mod_list(r, read) && read.family == "rac1-60-1" && read.disc == "ntsc");
    // Everything else still refuses, with no word of a disc.
    mp::ModList other = ntsc;
    other.openrac = "0.20.0";
    std::vector<mp::Difference> d = mp::compare_mods(other, pal);
    CHECK(d.size() == 1 && d[0].kind == mp::Difference::Kind::kOpenRac);
    CHECK(mp::refusal_lines(d)[0] == "OpenRAC: the host has 0.21.0, you have 0.20.0");
    other = ntsc;
    other.multiplayer = "0.8.0";
    d = mp::compare_mods(other, pal);
    CHECK(d.size() == 1 && d[0].kind == mp::Difference::Kind::kMultiplayer);
    other = ntsc;
    other.mods.push_back({"chaos", "Chaos", "1.0.0", mp::kModGameplay, {}});
    d = mp::compare_mods(other, pal);
    CHECK(d.size() == 1 && d[0].kind == mp::Difference::Kind::kYouOnly);
    CHECK(mp::mods_digest(other) != mp::mods_digest(pal));
    says_no_region(mp::refusal_lines(d));
}

void a_truly_different_disc_is_refused() {
    const mp::ModList host = list_of("pal", "rac1-60-1");
    const mp::ModList other = list_of("jp", "rac1-50-1");
    const std::vector<mp::Difference> d = mp::compare_mods(other, host);
    CHECK(d.size() == 1 && d[0].kind == mp::Difference::Kind::kDisc);
    CHECK(mp::refusal_lines(d)[0] == "You have a different disc from the host");
    CHECK(mp::refusal_short(d) == "a different disc");
    CHECK(mp::mods_digest(other) != mp::mods_digest(host));
    says_no_region(mp::refusal_lines(d));
    // A port that does not say: the disc stands for the family, so only the same disc plays.
    const mp::ModList pal = list_of("pal", "pal");
    const mp::ModList ntsc = list_of("ntsc", "ntsc");
    CHECK(!mp::compare_mods(ntsc, pal).empty());
    // An older game's MODS with no family reads as its disc.
    mp::Writer w;
    w.str("0.8.0").str("0.21.0").str("ntsc").u8(0);
    const Bytes bytes = w.bytes();
    mp::Reader r(bytes.data(), bytes.size());
    mp::ModList old;
    CHECK(mp::decode_mod_list(r, old) && old.family == "ntsc");
}

void the_rules_say_whose_enemies() {
    const mp::ModList pal = list_of("pal", "rac1-60-1");
    const mp::ModList ntsc = list_of("ntsc", "rac1-60-1");
    const auto says = [](const std::vector<std::string>& lines) {
        return std::find(lines.begin(), lines.end(), "Enemies follow the host's game") != lines.end();
    };
    CHECK(says(mp::rules_lines(pal, {}, ntsc, mp::kTitleLine)));
    CHECK(says(mp::rules_lines(ntsc, {}, pal, mp::kPauseLine)));
    CHECK(!says(mp::rules_lines(pal, {}, pal, mp::kTitleLine)));
    says_no_region(mp::rules_lines(pal, {}, ntsc, mp::kTitleLine));
}

/* The party in a level, each game with its digest of it. */
struct InLevel {
    Player host{1}, member{2};

    InLevel(std::uint32_t host_digest, std::uint32_t member_digest) {
        const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
        host.session.handle(info(both));
        member.session.handle(info(both));
        host.game.digests[0] = host_digest;
        member.game.digests[0] = member_digest;
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route({&host, &member});
        tick({&host, &member}, 16);
        host.game.now = {mp::kPhasePlaying, 0, 2};
        member.game.now = {mp::kPhasePlaying, 0, 2};
        host.game.level = {moby(0, 10, 100, mp::kMobyUpdates, 1.0f)};
        member.game.level = {moby(0, 10, 100, mp::kMobyUpdates, 1.0f)};

        for (int i = 0; i < 6; ++i) {
            tick({&host, &member}, 200);
            route({&host, &member});
        }
    }
};

void levels_that_agree_are_shared() {
    InLevel party(0xDD9BEA77u, 0xDD9BEA77u);
    CHECK(party.member.world.synced());
    CHECK(!party.member.world.take_level_differs());
}

void a_level_that_does_not_agree_is_not() {
    InLevel party(0xDD9BEA77u, 0x12345678u);
    CHECK(!party.member.world.synced());
    CHECK(party.member.world.take_level_differs());
    // Said once.
    CHECK(!party.member.world.take_level_differs());
}

void a_game_that_does_not_know_is_let_through() {
    InLevel old_host(0, 0x12345678u);
    CHECK(old_host.member.world.synced() && !old_host.member.world.take_level_differs());
    InLevel old_member(0xDD9BEA77u, 0);
    CHECK(old_member.member.world.synced() && !old_member.member.world.take_level_differs());
}

}  // namespace

void run_cross_tests() {
    two_discs_of_a_world_fit();
    a_truly_different_disc_is_refused();
    the_rules_say_whose_enemies();
    levels_that_agree_are_shared();
    a_level_that_does_not_agree_is_not();
    a_game_that_does_not_know_is_let_through();
}

}  // namespace mpt
