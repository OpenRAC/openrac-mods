// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: a connection lost and found again, the host's
// among them (migrate.hpp).
#include "test_support.hpp"

namespace mpt {
namespace {

constexpr std::uint32_t kRuns = mp::kMobyUpdates | mp::kMobyPlaced;

/* A party of three in level 3, all given the world; offers RESUME as a server that keeps slots. */
struct Trio {
    Player host{1}, a{2}, b{3};
    std::vector<Player*> all{&host, &a, &b};
    const std::vector<mp::Member> list = {{1, "P1"}, {2, "P2"}, {3, "P3"}};
    SimWorld w{4, 4, 2.0f};

    Trio() {
        join_party(all, list);
        offer_resume(all);
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route(all);
        tick(all, 16);

        for (Player* p : all) {
            p->game.now = {mp::kPhasePlaying, 3, 2};
            p->game.level = w.made(0);
            p->game.hero_pose.position[0] = 5.0f * static_cast<float>(p->id);
            p->game.hero_pose.flags = mp::kPoseSteady;
        }

        run(all, 40);
        CHECK(a.world.synced() && b.world.synced());
    }

    void run(const std::vector<Player*>& who, int ticks, int ms = 50) {
        for (int i = 0; i < ticks; ++i) {
            tick(who, ms);
        }
    }

    std::vector<Player*> without(const Player& gone) {
        std::vector<Player*> out;

        for (Player* p : all) {
            if (p != &gone) {
                out.push_back(p);
            }
        }

        return out;
    }

    mp::Message party(std::int32_t host_id = 1, std::int32_t epoch = 0) {
        mp::Message m = info(list);
        m.host = host_id;
        m.epoch = epoch;
        return m;
    }
};

void a_member_dropped_keeps_its_place_and_goes_nowhere() {
    Trio t;
    const auto rest = t.without(t.a);
    lose(t.a, rest);

    // Everything of the party stands, its id with it; nothing it sends goes.
    CHECK(t.a.session.in_party() && t.a.session.dropped() && t.a.session.id() == 2);
    t.a.session.send_data(1, Bytes{mp::kind::kPose, 1});
    t.a.session.flush();
    CHECK(t.a.link.frames.empty());
    tick({&t.a}, 16);
    CHECK(t.a.migrate.banner(t.a.clock) == mp::kDroppedLine);
    CHECK(t.host.session.away(2) && t.b.session.away(2));

    // It stands where it was, past the time silence takes a figure away; and in nobody's way.
    t.run(rest, 140);
    CHECK(t.host.coop.figure_of(2) >= 0);
    mp::Pose pose;
    CHECK(!t.host.coop.latest(2, pose));
    CHECK(t.host.session.members().size() == 3);
}

void a_member_comes_back_as_a_late_joiner_does() {
    Trio t;
    const auto rest = t.without(t.a);
    t.a.game.placeable = true;
    lose(t.a, rest);
    t.run(rest, 40);

    // RESUME names its old session, with its token, and only once.
    t.a.session.connected();
    mp::Message hello;
    hello.kind = mp::Message::Kind::kHello;
    hello.id = 77;
    t.a.link.frames.clear();
    t.a.session.handle(hello);
    CHECK(!t.a.link.frames.empty() && t.a.link.frames[0] == mp::party_resume("ABCDEF", 2, 1002));
    CHECK(t.a.session.id() == 2 && t.a.session.dropped());
    t.a.link.frames.clear();
    hello.id = 2;
    t.a.session.handle(hello);
    t.a.session.handle(udp_offer_of(1002));
    CHECK(t.a.session.dropped());
    t.a.session.handle(t.party());
    server_away(rest, 2, true);
    t.a.link.frames.clear();

    CHECK(!t.a.session.dropped() && t.a.session.id() == 2 && t.a.session.in_party());
    CHECK(!t.host.session.away(2));

    // In its game still, it asks for the world and is held while it comes.
    tick(t.all, 16);
    CHECK(t.a.coop.joining() && t.a.coop.quiet_gifts());
    CHECK(!t.a.world.synced());
    t.run(t.all, 40);
    CHECK(t.a.world.synced());
    CHECK(!t.a.game.placed.empty());
}

void a_slot_refused_or_out_of_time_ends_the_party_here() {
    {
        Trio t;
        lose(t.a, t.without(t.a));
        t.a.session.connected();
        mp::Message error;
        error.kind = mp::Message::Kind::kError;
        error.reason = mp::ErrorReason::kResumeRefused;
        t.a.session.handle(error);
        CHECK(!t.a.session.in_party() && !t.a.session.dropped());
        CHECK(t.a.session.take_notice() == mp::kLostSlotNotice);
    }

    Trio t;
    lose(t.a, t.without(t.a));
    t.a.clock += mp::kResumeMs - 1;
    t.a.session.flush();
    CHECK(t.a.session.in_party());
    t.a.clock += 1;
    t.a.session.flush();
    CHECK(!t.a.session.in_party());
    CHECK(t.a.session.take_notice() == mp::kLostSlotNotice);
}

void a_server_that_keeps_no_slots_ends_the_party_as_ever() {
    Trio t;
    t.a.session.handle([] {
        mp::Message m;
        m.kind = mp::Message::Kind::kUdp;
        m.features = mp::kFeatureKick;
        return m;
    }());
    t.a.session.disconnected();
    CHECK(!t.a.session.in_party() && !t.a.session.dropped());
}

void the_party_waits_for_a_host_that_drops() {
    Trio t;
    const auto rest = t.without(t.host);
    lose(t.host, rest);
    tick(rest, 16);

    for (Player* p : rest) {
        CHECK(p->coop.hold_input());
        CHECK(p->migrate.host_gone());
        CHECK(p->migrate.banner(p->clock).find("waiting for it") != std::string::npos);
    }

    // Its enemies stand in the members' games; the figure stands too.
    t.run(rest, 100);
    CHECK(t.a.coop.hold_input());

    // Back in time: it is the host still, and every member asks for the world.
    t.run({&t.host}, 20);
    come_back(t.host, rest, t.party());
    CHECK(t.host.session.is_host());
    tick(t.all, 16);
    CHECK(!t.a.coop.hold_input() && !t.b.coop.hold_input());
    CHECK(t.a.migrate.banner(t.a.clock).empty());
    t.run(t.all, 40);
    CHECK(t.a.world.synced() && t.b.world.synced());
    CHECK(t.a.world.driven() > 0);
}

void what_went_to_a_cut_stream_comes_with_the_world_again() {
    Trio t;
    const auto rest = t.without(t.host);
    lose(t.host, rest);

    // The host's game goes on: a moby dies there, and nobody can be told.
    const int slot = t.host.game.moby_slot(1);
    t.host.game.die(slot);
    t.run({&t.host}, 20);
    CHECK(t.a.game.moby_slot(1) >= 0);
    come_back(t.host, rest, t.party());
    t.run(t.all, 40);
    CHECK(t.a.game.moby_slot(1) < 0);
}

}  // namespace

void run_migrate_tests() {
    a_member_dropped_keeps_its_place_and_goes_nowhere();
    a_member_comes_back_as_a_late_joiner_does();
    a_slot_refused_or_out_of_time_ends_the_party_here();
    a_server_that_keeps_no_slots_ends_the_party_as_ever();
    the_party_waits_for_a_host_that_drops();
    what_went_to_a_cut_stream_comes_with_the_world_again();
}

}  // namespace mpt
