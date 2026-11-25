// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: WORLD's delivery (acks, what goes again), its
// priority, its share of the state budget and each stream's margin. A pair of games whose datagrams
// the test delivers one by one, so a loss is the one the test means.

#include "test_support.hpp"

namespace mpt {
namespace {

// The first byte of a WORLD datagram, to tell it from the other kinds.
constexpr std::uint8_t kWorldByte = static_cast<std::uint8_t>(mp::WorldKind::kWorld);

// A host and `members` members in level 3, with UDP, every datagram at the next step unless `drop`
// says so. What went is kept for the test to read.
struct Pair {
    // The players, owned here; `all` points at them and `host` and `member` name the first two.
    std::vector<std::unique_ptr<Player>> keep;
    std::vector<Player*> all;
    Player& host;
    Player& member;
    // Decides, per datagram, whether the link loses it; empty means nothing is lost.
    std::function<bool(const Player& from, const Bytes&)> drop;
    std::vector<std::pair<std::int32_t, Bytes>> worlds;  // the WORLDs the host sent, by member
    // How many datagrams each player sent, by first byte (plus 256 for a member's).
    std::map<int, long> state_out;

    // Returns the host (id 1) and `members` members (ids 2 and up), not yet joined.
    static std::vector<std::unique_ptr<Player>> make(int members) {
        std::vector<std::unique_ptr<Player>> out;

        for (int i = 0; i <= members; ++i) {
            out.push_back(std::make_unique<Player>(1 + i));
        }

        return out;
    }

    // Starts a game for the host and members, all in level 3 holding `level`'s mobys, over UDP.
    explicit Pair(std::vector<mp::MobyState> level, int members = 1)
        : keep(make(members)),
          host(*keep[0]),
          member(*keep[1]) {
        std::vector<mp::Member> list;

        for (auto& p : keep) {
            all.push_back(p.get());
            list.push_back({p->id, "P" + std::to_string(p->id)});
        }

        join_party(all, list);
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route(all);

        // Every game starts in level 3 with the same mobys and a UDP link.
        for (Player* p : all) {
            p->game.now = {mp::kPhasePlaying, 3, 2};
            p->game.level = level;
            p->link.udp = true;
        }
    }

    // Advances every player's clock by `ms`, runs its coop tick and delivers what that sent.
    void step(std::uint64_t ms = 16) {
        for (Player* p : all) {
            p->clock += ms;
            p->coop.tick(p->clock);
            p->world.tick(p->clock);
            p->session.flush();
        }

        route(all);

        // Datagrams a game sent this step are taken from its link and delivered below.
        for (Player* from : all) {
            std::vector<std::pair<std::int32_t, Bytes>> out;
            out.swap(from->link.datagrams);

            for (const auto& [target, bytes] : out) {
                ++state_out[bytes[0] | (from == &host ? 0 : 256)];

                // Only the host's WORLDs are kept, for told() to count.
                if (from == &host && bytes[0] == kWorldByte) {
                    worlds.emplace_back(target, bytes);
                }

                // The test's drop rule decides which datagrams the link loses.
                if (drop && drop(*from, bytes)) {
                    continue;
                }

                // The host's datagrams go to the member they name, or to all; a member's go to the host.
                for (Player* to : all) {
                    const bool reaches =
                        to != from
                        && (from == &host ? target == 0 || target == to->id : to == &host);

                    if (reaches) {
                        mp::Message m;
                        m.kind = mp::Message::Kind::kData;
                        m.id = from->id;
                        m.bytes = bytes;
                        to->session.handle(m);
                    }
                }
            }
        }
    }

    // Runs `steps` ticks of 16 ms.
    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            step();
        }
    }

    // Everyone in and every moby told.
    void settle() {
        run(200);
        worlds.clear();
    }

    // How many WORLDs since the last settle() named a moby.
    long told(std::uint16_t id) const {
        long n = 0;

        for (const auto& [target, bytes] : worlds) {
            int level = -1;
            std::int64_t stamp = 0;
            std::vector<mp::WorldEntry> entries;
            CHECK(mp::decode_world(bytes, level, stamp, entries));

            for (const mp::WorldEntry& e : entries) {
                n += e.id == id ? 1 : 0;
            }
        }

        return n;
    }
};

// Returns whether `bytes` is a WORLD datagram with an entry for moby `id`.
bool names(const Bytes& bytes, std::uint16_t id) {
    if (bytes[0] != kWorldByte) {
        return false;
    }

    int level = -1;
    std::int64_t stamp = 0;
    std::vector<mp::WorldEntry> entries;
    return mp::decode_world(bytes, level, stamp, entries)
           && std::any_of(entries.begin(), entries.end(), [id](const mp::WorldEntry& e) {
                  return e.id == id;
              });
}

// Returns whether `bytes` is a pose datagram sent by a player other than the host.
bool pose_from_member(const Player& from, const Bytes& bytes) {
    return from.id != 1 && bytes[0] == mp::kPoseKind;
}

// Returns `count` creature mobys that run on their own, `spread` units apart along x.
std::vector<mp::MobyState> enemies(int count, float spread = 2.0f) {
    std::vector<mp::MobyState> out;

    for (int i = 0; i < count; ++i) {
        mp::MobyState m =
            moby(i, i, 300, SimWorld::kRuns | mp::kMobyCreature, static_cast<float>(i) * spread);
        out.push_back(m);
    }

    return out;
}

// Checks that a WORLD datagram's bytes carry its sequence and the acknowledgement.
void the_bytes_say_the_sequence_and_the_ack() {
    mp::WorldEntry e;
    e.id = 7;
    e.pose.level = 3;
    const Bytes b = mp::encode_world(3, 1234, {e}, 65000);
    CHECK(b.size() == 1 + 1 + 8 + 2 + 1 + mp::kWorldEntryBytes);
    int level = -1;
    std::int64_t stamp = 0;
    std::uint16_t sequence = 0;
    std::vector<mp::WorldEntry> out;
    CHECK(mp::decode_world(b, level, stamp, out, &sequence));
    CHECK(sequence == 65000 && out.size() == 1 && out[0].id == 7);
    // Within a datagram at its fullest, so it never falls back on the stream.
    CHECK(
        1 + 1 + 8 + 2 + 1 + mp::kWorldEntries * mp::kWorldEntryBytes + mp::kClientStateHeader
        <= mp::kMaxDatagramBytes
    );

    mp::PoseMessage m;
    m.owner = 2;
    m.pose = pose_at(3.0f);
    const Bytes plain = mp::encode_pose(m);
    m.world_ack = true;
    m.world_newest = 0xABCD;
    m.world_bits = 0x80000001u;
    m.pose.held_count = 1;
    m.pose.held[0] = piece(1, 0, 2, 5, 4);
    const Bytes with = mp::encode_pose(m);
    mp::PoseMessage heard;
    CHECK(mp::decode_pose(with, heard));
    CHECK(heard.world_ack && heard.world_newest == 0xABCD && heard.world_bits == 0x80000001u);
    CHECK(heard.pose.held_count == 1 && heard.pose.held[0].oclass == 5);
    CHECK(mp::decode_pose(plain, heard) && !heard.world_ack);
}

// Checks that a member's acknowledgement goes to the host and a third player never sees it.
void the_ack_stays_between_the_member_and_the_host() {
    // A third player hears the member's pose by way of the host, without its word on WORLD.
    Pair p(enemies(1), 2);
    p.settle();
    Player& third = *p.all[2];
    bool passed_on = false;
    p.drop = [&](const Player& from, const Bytes& bytes) {
        // Only the host's pose datagrams are read.
        if (&from == &p.host && bytes[0] == static_cast<std::uint8_t>(mp::GameKind::kPoses)) {
            passed_on = true;
            mp::Reader r(bytes.data() + 1, bytes.size() - 1);
            const std::uint8_t count = r.u8();

            // A pose datagram holds a count, then each pose with its 16-bit size.
            for (std::uint8_t i = 0; i < count; ++i) {
                const std::uint8_t high = r.u8();
                const std::size_t size = static_cast<std::size_t>(high) << 8 | r.u8();
                Bytes one(size);

                for (std::uint8_t& b : one) {
                    b = r.u8();
                }

                mp::PoseMessage pose;
                CHECK(mp::decode_pose(one, pose) && !pose.world_ack);
            }
        }

        return false;
    };
    p.run(20);
    CHECK(passed_on);
    mp::Pose seen;
    CHECK(third.coop.latest(2, seen));
}

// Checks that a change that arrived is not sent again.
void a_delivered_change_goes_once() {
    Pair p(enemies(2));
    p.settle();
    p.host.game.level[0].pose.position[1] = 5.0f;
    p.run(100);
    CHECK(std::fabs(by_key(p.member.game, 0)->pose.position[1] - 5.0f) < 0.01f);
    // Told, heard, done: not the three sends of old, nor once a second after that.
    CHECK(p.told(0) == 1);
    CHECK(p.told(1) == 0);
}

// Checks that a change undone while its word is in flight is sent as well.
void a_change_undone_on_the_way_goes_too() {
    // It hides and the word goes; before that word is known to have come it shows again, as the
    // member was last known to have it. The word on its way will hide it there all the same.
    std::vector<mp::MobyState> level = enemies(1);
    level[0].pose.position[0] = 300.0f;
    Pair p(level);
    p.settle();
    p.drop = pose_from_member;
    p.host.game.level[0].flags |= mp::kMobyHidden;

    // Run until the host's WORLD first names the hidden moby.
    while (p.told(0) == 0) {
        p.step();
    }

    p.run(4);
    CHECK((by_key(p.member.game, 0)->flags & mp::kMobyHidden) != 0);
    p.host.game.level[0].flags &= ~mp::kMobyHidden;
    p.drop = nullptr;
    p.run(40);
    CHECK((by_key(p.member.game, 0)->flags & mp::kMobyHidden) == 0);
}

// Checks that a word given up for lost still counts when it arrives late.
void a_word_given_up_for_lost_can_still_come() {
    // The word is late, not lost: given up, the moby changed back, and then the word comes.
    std::vector<mp::MobyState> level = enemies(1);
    level[0].pose.position[0] = 300.0f;
    Pair p(level);
    p.settle();
    Bytes late;
    p.drop = [&late](const Player&, const Bytes& b) {
        late = names(b, 0) ? b : late;
        return !late.empty() && b[0] == kWorldByte;
    };
    p.host.game.level[0].flags |= mp::kMobyHidden;

    // Run until the first WORLD naming the moby has been held back.
    while (late.empty()) {
        p.step();
    }

    p.host.game.level[0].flags &= ~mp::kMobyHidden;
    p.drop = nullptr;
    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = 1;
    p.run(static_cast<int>(2 * mp::kWorldStartRttMs / 16) + 4);
    m.bytes = late;
    p.member.session.handle(m);
    p.run(100);
    CHECK((by_key(p.member.game, 0)->flags & mp::kMobyHidden) == 0);
}

// Checks that a change whose datagram is known lost goes again as soon as newer ones are heard.
void a_lost_change_goes_again_at_once() {
    // Far away, where the old world came back to it every 3 seconds: a moby 300 units off.
    std::vector<mp::MobyState> level = enemies(2);
    level[1].pose.position[0] = 300.0f;
    Pair p(level);
    p.settle();
    int lost = 0;
    p.drop = [&lost](const Player&, const Bytes& b) {
        const bool drop = names(b, 1) && lost < 3;
        lost += drop ? 1 : 0;
        return drop;
    };
    p.host.game.level[1].pose.position[1] = 5.0f;

    // Moby 0 keeps WORLD going: each lost datagram is known by the newer ones heard, and the far
    // moby's word is back in a few snapshots, each time. Its first at the floor's quarter second.
    for (int i = 0; i < 80; ++i) {
        p.host.game.level[0].pose.position[1] += 0.1f;
        p.step();
    }

    CHECK(lost == 3);
    CHECK(p.told(1) == 4);
    p.run(50);
    CHECK(std::fabs(by_key(p.member.game, 1)->pose.position[1] - 5.0f) < 0.01f);
}

// Checks that a lost change with no newer datagram goes again after the time-out.
void a_lost_change_with_nothing_after_it_goes_again_in_time() {
    std::vector<mp::MobyState> level = enemies(1);
    level[0].pose.position[0] = 300.0f;
    Pair p(level);
    p.settle();
    bool lost = false;
    p.drop = [&lost](const Player&, const Bytes& b) {
        const bool drop = names(b, 0) && !lost;
        lost = lost || drop;
        return drop;
    };
    p.host.game.level[0].pose.position[1] = 5.0f;
    // Nothing newer to tell it by: the time-out, twice the round trip (not yet learned here).
    p.run(static_cast<int>((2 * mp::kWorldStartRttMs + 2 * mp::kWorldFloorMs) / 16));
    CHECK(lost && p.told(0) == 2);
    p.run(100);
    CHECK(std::fabs(by_key(p.member.game, 0)->pose.position[1] - 5.0f) < 0.01f);
}

// Checks that missing acknowledgements make the host resend once per time-out and no faster.
void acks_that_never_come_slow_nothing_down() {
    // No word back at all: each moby goes again each time-out, and no faster.
    Pair p(enemies(1));
    p.settle();
    p.drop = pose_from_member;
    p.host.game.level[0].pose.position[1] = 5.0f;
    p.run(125);  // two seconds
    const long told = p.told(0);
    CHECK(told >= 2);
    CHECK(told <= 2000 / static_cast<long>(mp::kWorldMinWaitMs) + 1);
    CHECK(std::fabs(by_key(p.member.game, 0)->pose.position[1] - 5.0f) < 0.01f);
}

// Checks that a moby that does not move is still sent once each keep-alive period.
void a_moby_that_stands_goes_now_and_then() {
    // For whatever the host cannot see: one more word each kWorldKeepMs, no more.
    Pair p(enemies(1));
    p.settle();
    p.run(static_cast<int>(2 * mp::kWorldKeepMs / 16) + 10);
    CHECK(p.told(0) == 2);
}

// Checks that an acknowledgement from before the member's level was made again is ignored.
void a_word_from_before_a_hello_says_nothing() {
    // The moby is told where it stands, and that datagram's fate is still open when the member's
    // level is made again: everything it heard is gone with its load.
    Pair p(enemies(1));
    p.settle();
    p.drop = pose_from_member;
    p.host.game.level[0].pose.position[1] = 7.0f;
    p.run(8);
    CHECK(!p.worlds.empty());
    int level = -1;
    std::int64_t stamp = 0;
    std::uint16_t before = 0;
    std::vector<mp::WorldEntry> entries;
    CHECK(mp::decode_world(p.worlds.back().second, level, stamp, entries, &before));
    SimWorld w{1, 0, 2.0f};
    remake(p.member, w);
    // The first word to the new load is lost, and just then the old word comes in: it says the
    // old datagram was heard, which is true of mobys that no longer are.
    bool lost = false;
    p.drop = [&lost](const Player& from, const Bytes& b) {
        const bool drop = from.id == 1 && b[0] == kWorldByte && !lost;
        const bool pose = pose_from_member(from, b) && !lost;
        lost = lost || drop;
        return drop || pose;
    };

    // Run until the first WORLD to the new load has been lost.
    while (!lost) {
        p.step();
    }

    mp::PoseMessage old;
    old.owner = 2;
    old.pose = pose_at(1.0f);
    old.stamp_ms = static_cast<std::int64_t>(p.host.clock);
    old.sequence = 9000;
    old.world_ack = true;
    old.world_newest = before;
    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = 2;
    m.bytes = mp::encode_pose(old);
    p.host.session.handle(m);
    p.run(100);
    CHECK(std::fabs(by_key(p.member.game, 0)->pose.position[1] - 7.0f) < 0.01f);
}

// Checks that the member acknowledges only the WORLDs of its own level.
void only_what_was_taken_is_said_heard() {
    // A WORLD of another level changes nothing here, and the host must not take it as heard.
    Pair p(enemies(1));
    p.settle();
    Bytes pose;
    p.drop = [&pose](const Player& from, const Bytes& b) {
        pose = pose_from_member(from, b) ? b : pose;
        return false;
    };
    p.run(8);
    mp::PoseMessage said;
    CHECK(mp::decode_pose(pose, said) && said.world_ack);
    const auto next = static_cast<std::uint16_t>(said.world_newest + 1);
    mp::WorldEntry e;
    e.id = 0;
    e.pose.level = 9;
    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = 1;
    m.bytes = mp::encode_world(9, 500, {e}, next);
    p.member.session.handle(m);
    p.run(8);
    CHECK(mp::decode_pose(pose, said));
    CHECK(said.world_ack && said.world_newest != next);
    // The same sequence in this level is.
    e.pose.level = 3;
    m.bytes = mp::encode_world(3, 500, {e}, next);
    p.member.session.handle(m);
    p.run(8);
    CHECK(mp::decode_pose(pose, said));
    CHECK(said.world_ack && said.world_newest == next);
}

// Checks that a datagram arriving after newer ones is still acknowledged.
void a_late_datagram_still_counts_as_heard() {
    // Sequences 2 and 3 first, then 1: all three said heard, and the bit field right.
    Pair p(enemies(1));
    p.run(3);
    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = 1;
    Bytes pose;
    p.drop = [&pose](const Player& from, const Bytes& b) {
        pose = pose_from_member(from, b) ? b : pose;
        return from.id == 1 && b[0] == kWorldByte;
    };
    p.run(40);
    mp::WorldEntry e;
    e.id = 0;
    e.pose.level = 3;

    // Sequences 2 and 3 arrive first and 1 later, out of order.
    for (const std::uint16_t sequence :
         {std::uint16_t{5002}, std::uint16_t{5003}, std::uint16_t{5001}}) {
        m.bytes = mp::encode_world(3, 100 + sequence, {e}, sequence);
        p.member.session.handle(m);
    }

    p.run(8);
    mp::PoseMessage said;
    CHECK(mp::decode_pose(pose, said));
    CHECK(said.world_ack && said.world_newest == 5003 && (said.world_bits & 3u) == 3u);
}

// Checks that a bolt's landing word that arrives before its spawn still takes effect.
void a_bolt_that_lands_ahead_of_its_spawn_still_lands() {
    // UDP's WORLD overtakes TCP's SPAWN: the one word that it landed must not go for nothing,
    // since it is heard, and the host will not say it again.
    Pair p(enemies(1));
    p.settle();
    const std::size_t driven = p.member.world.driven();
    mp::WorldEntry e;
    e.id = mp::kSpawnedId | 5;
    e.pose.level = 3;
    e.loose = true;
    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = 1;
    m.bytes = mp::encode_world(3, static_cast<std::int64_t>(p.member.clock), {e}, 900);
    p.member.session.handle(m);
    mp::Writer w;
    w.u8(static_cast<std::uint8_t>(mp::WorldKind::kSpawn)).u8(3).u8(0).u8(5).u8(0).u8(13);

    // The rest of the spawn word is zero: the test uses none of those fields.
    for (int i = 0; i < 14; ++i) {
        w.u8(0);
    }

    m.bytes = w.bytes();
    p.member.session.handle(m);
    const int bolt = slot_of_class(p.member.game, 13);
    CHECK(bolt >= 0);
    CHECK(p.member.world.spawned() == 1);
    // Made, and running as its own: not driven from the host.
    CHECK(p.member.world.driven() == driven);
}

// Checks that a moby's send weight falls with distance and rises with what makes it matter.
void the_weight_reads_distance_and_what_matters() {
    mp::WorldSight near;
    near.distance = 5.0f;
    mp::WorldSight far = near;
    far.distance = 200.0f;
    // Close: each snapshot; far: the floor, whatever else it is not.
    CHECK(mp::world_weight(near) == 1.0f);
    const float floor = mp::world_weight(far);
    CHECK(std::fabs(floor - mp::kWorldBaseMs / mp::kWorldFloorMs) < 1e-5f);
    far.distance = 10000.0f;
    CHECK(mp::world_weight(far) == floor);
    far.hidden = true;
    CHECK(mp::world_weight(far) == floor);
    far.hidden = false;
    // Far off, a shot of its own still counts, as does a bolt on its way.
    far.hit = true;
    CHECK(mp::world_weight(far) > floor);
    far.hit = false;
    far.flying = true;
    CHECK(mp::world_weight(far) > floor);
    // Between, it falls with distance.
    mp::WorldSight mid = near;
    mid.distance = 40.0f;
    mp::WorldSight midfar = near;
    midfar.distance = 50.0f;
    CHECK(mp::world_weight(mid) < 1.0f && mp::world_weight(midfar) < mp::world_weight(mid));

    // What can hurt, what it faces, what it hit, what flies: each up. Unseen: down.
    for (const auto term :
         {&mp::WorldSight::creature,
          &mp::WorldSight::ahead,
          &mp::WorldSight::hit,
          &mp::WorldSight::flying}) {
        mp::WorldSight up = mid;
        up.*term = true;
        CHECK(mp::world_weight(up) > mp::world_weight(mid));
    }

    mp::WorldSight unseen = mid;
    unseen.hidden = true;
    CHECK(mp::world_weight(unseen) < mp::world_weight(mid));
}

// Checks that a far moby still gets its share of WORLD when many others move.
void a_far_moby_cannot_starve() {
    // Far more moving by the member than WORLD can hold, and one moby far away: it gets its share.
    std::vector<mp::MobyState> level = enemies(300, 0.1f);
    level.push_back(moby(300, 300, 300, SimWorld::kRuns, 500.0f));
    Pair p(level);
    p.settle();
    std::uint64_t last = p.host.clock, worst = 0;
    p.drop = [&](const Player&, const Bytes& b) {
        // Record the longest time between two WORLDs that name the far moby.
        if (names(b, 300)) {
            worst = std::max(worst, p.host.clock - last);
            last = p.host.clock;
        }

        return false;
    };

    // Everything moves each tick, so WORLD always has more to send than it can hold.
    for (int t = 0; t < 1250; ++t) {
        for (mp::MobyState& m : p.host.game.level) {
            m.pose.position[1] += 0.05f;
        }

        p.step();
    }

    // Everything moves: 575 entries a second for 301 mobys, the close ones each worth 8 of the far
    // one (the old rank made that 9 seconds).
    std::printf(
        "the far moby among 300 moving: unheard for %llu ms at the most\n",
        static_cast<unsigned long long>(worst)
    );
    CHECK(worst > 0 && worst < 5000);
    CHECK(p.host.clock - last < 5000);
}

// Checks that the moby a member last hit goes ahead of its neighbours when WORLD is full.
void a_moby_it_just_hit_comes_first() {
    // More to send than a datagram holds: the moby this member hit goes ahead of its neighbours.
    std::vector<mp::MobyState> level = enemies(100, 0.5f);

    for (mp::MobyState& m : level) {
        m.pose.position[0] += 300.0f;  // all far off, at the floor
    }

    Pair p(level);
    p.settle();
    mp::Writer hit;
    hit.u8(static_cast<std::uint8_t>(mp::WorldKind::kHit)).u8(0).u8(50);

    // The rest of the hit word is zero: the test uses none of those fields.
    for (int i = 0; i < 32; ++i) {
        hit.u8(0);
    }

    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = 2;
    m.bytes = hit.bytes();
    p.host.session.handle(m);

    // Run with everything moving, so WORLD has more to send than a datagram holds.
    for (int t = 0; t < 60; ++t) {
        for (mp::MobyState& e : p.host.game.level) {
            e.pose.position[1] += 0.05f;
        }

        p.step();
    }

    CHECK(p.told(50) > p.told(51) + 1);
}

// Checks that a near moby is sent each snapshot and a far one at the floor rate.
void the_near_goes_each_snapshot_and_the_far_less_often() {
    std::vector<mp::MobyState> level = enemies(2, 1.0f);
    level[1].pose.position[0] = 400.0f;
    Pair p(level);
    p.settle();

    // Both mobys move every tick, so each has a new state to send.
    for (int t = 0; t < 125; ++t) {
        p.host.game.level[0].pose.position[1] += 0.05f;
        p.host.game.level[1].pose.position[1] += 0.05f;
        p.step();
    }

    // Two seconds: a close one at WORLD's 12 a second (15, on a tick of 16 ms), a far one at the
    // floor's 4.
    CHECK(p.told(0) >= 20);
    CHECK(p.told(1) >= 7 && p.told(1) <= 9);
}

// Checks that a firing party lowers WORLD's rate while its poses and shots keep theirs.
void firing_takes_its_share_of_world() {
    // Three members and everything moving: WORLD at its 30 a second until the party fires.
    Pair p(enemies(60, 1.0f), 3);
    p.settle();
    const auto play = [&p](bool fire) {
        p.state_out.clear();
        std::uint64_t held = p.host.session.counts().state_held;

        // Ten seconds with everything moving, enough to measure WORLD's rate.
        for (int t = 0; t < 625; ++t) {
            for (mp::MobyState& e : p.host.game.level) {
                e.pose.position[1] += 0.02f;
            }

            // When the party fires, a shot goes out every sixth tick, about ten a second.
            if (fire && t % 6 == 0) {
                mp::Shot s;
                s.item = 2;
                p.host.game.shots_out.push_back(s);
            }

            p.step();
        }

        return p.host.session.counts().state_held - held;
    };

    CHECK(play(false) == 0);
    const long calm = p.state_out[kWorldByte];
    CHECK(calm >= 10 * 24 && calm <= 10 * 30);
    // Ten seconds of POSES at 20 and SHOTX at 10: the bucket's 52 refuses nothing.
    CHECK(play(true) == 0);
    const long firing = p.state_out[kWorldByte];
    const auto poses = p.state_out[static_cast<std::uint8_t>(mp::GameKind::kPoses)];
    const auto shots = p.state_out[static_cast<std::uint8_t>(mp::kShotXKind)];
    std::printf("WORLD %ld a 10 s, firing %ld, POSES %ld, SHOTX %ld\n", calm, firing, poses, shots);
    CHECK(firing >= 10 * 16 && firing <= 10 * 22);
    CHECK(poses >= 10 * 15);  // 20 a second, on a game's 60 Hz; each 64 ms on this tick of 16
    CHECK(shots >= 10 * 8);
}

// A stream every `interval` ms, each lost by `lost`, a constant link; how far the delay stands past
// the transit, and the frames shown past the newest pose.
struct Margin {
    double behind = 0;
    int past = 0, frames = 0;
};

// Feeds an interpolator a stream and returns how far its delay stands past the transit.
//
// `derive` sets whether the delay is derived from the stream's own timing. `lost_each` drops
// every that-many-th pose; `loss` is the loss rate the interpolator is told of.
Margin margin_of(bool derive, double interval, int lost_each = 0, double loss = 0) {
    mp::Interpolator in(derive);
    Margin out;
    std::uint16_t sequence = 0;
    int n = 0;
    double next = 0;

    // The stream is sampled 60 times a second for 12 seconds.
    for (double t = 0; t < 12000; t += 1000.0 / 60.0) {
        // Each pose that is due by now arrives, one every `interval` ms.
        for (; next <= t; next += interval) {
            ++n;

            // Every `lost_each`-th pose is lost: its sequence number is used but it never arrives.
            if (lost_each != 0 && n % lost_each == 0) {
                ++sequence;
                continue;
            }

            in.set_loss(derive ? loss : in.loss());
            in.add(
                pose_at(static_cast<float>(next) / 100.0f, derive ? 0.0f : 10.0f),
                ++sequence,
                next,
                t,
                false
            );
        }

        mp::Pose p;

        // Only frames after 4 seconds count, once the delay has settled.
        if (in.at(t + 30, p) && t > 4000) {
            ++out.frames;
            out.past += in.shown() == mp::Shown::kHeld || in.shown() == mp::Shown::kAhead ? 1 : 0;
        }
    }

    out.behind = in.delay_ms() - in.transit_ms();
    return out;
}

// Checks that each stream's delay stands about two intervals behind its newest data.
void every_stream_keeps_two_intervals() {
    // WORLD's snapshots at each rate it can come to: 15 a second, its 10, the 7 of a firing party,
    // a far moby's 4. Poses, slowed too, though they have no reason to be.
    for (const double interval : {66.0, 100.0, 136.0, 250.0}) {
        const Margin m = margin_of(true, interval);
        CHECK(m.behind >= 2.0 * interval);
        CHECK(m.past == 0);
        // One in six lost: the next pose is always there.
        const Margin lossy = margin_of(true, interval, 6, 0.17);
        CHECK(lossy.behind >= 2.0 * interval);
        CHECK(lossy.past == 0);
    }

    // Without the derived delay, the delay still stands two intervals behind, less 3 ms.
    for (const double interval : {50.0, 66.0, 100.0, 150.0}) {
        CHECK(margin_of(false, interval).behind >= 2.0 * interval - 3.0);
    }

    // The steady figure's stays what it was, and a third interval comes with loss.
    CHECK(std::fabs(margin_of(false, 50.0).behind - 100.0) < 5.0);
    CHECK(margin_of(true, 100.0, 0, 0.3).behind >= 3.0 * 100.0);
    CHECK(margin_of(true, 100.0, 0, 0.0).behind < 3.0 * 100.0);
    // And the bound: however slow a stream, a moby's delay does not run away.
    CHECK(margin_of(true, 500.0).behind <= mp::kMaxMobyBufferMs);
}
}  // namespace

void run_sync_tests() {
    the_bytes_say_the_sequence_and_the_ack();
    the_ack_stays_between_the_member_and_the_host();
    a_delivered_change_goes_once();
    a_change_undone_on_the_way_goes_too();
    a_word_given_up_for_lost_can_still_come();
    a_lost_change_goes_again_at_once();
    a_lost_change_with_nothing_after_it_goes_again_in_time();
    acks_that_never_come_slow_nothing_down();
    a_moby_that_stands_goes_now_and_then();
    a_word_from_before_a_hello_says_nothing();
    only_what_was_taken_is_said_heard();
    a_late_datagram_still_counts_as_heard();
    a_bolt_that_lands_ahead_of_its_spawn_still_lands();
    the_weight_reads_distance_and_what_matters();
    a_far_moby_cannot_starve();
    a_moby_it_just_hit_comes_first();
    the_near_goes_each_snapshot_and_the_far_less_often();
    firing_takes_its_share_of_world();
    every_stream_keeps_two_intervals();
}

}  // namespace mpt
