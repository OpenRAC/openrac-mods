// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: an enemy's grab (world_hold.cpp): the lock on its
// victim, the victim's game holding its own hero, every way a grab ends, and what an update does to
// a player standing in.
#include "test_support.hpp"

#include "world_util.hpp"

namespace mpt {
namespace {

constexpr int kRope = 1106;
constexpr int kLeap = 1059;

/* A party in level 3 with one enemy of class `oclass` at the origin, the member near it, the host
 * far off. */
struct Party {
    Player host{1}, member{2};
    std::vector<Player*> all;
    SimWorld w;
    int slot = 0;

    explicit Party(int oclass, int enemies = 1) : w(SimWorld{enemies, 0}) {
        all = {&host, &member};
        join_party(all, {{1, "P1"}, {2, "P2"}});
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route(all);
        tick(all, 16);

        for (Player* p : all) {
            p->game.now = {mp::kPhasePlaying, 3, 2};
            p->game.level = w.made(0);

            for (mp::MobyState& m : p->game.level) {
                m.oclass = oclass;
                m.state = 1;
            }
        }

        host.game.hero_pose.position[0] = 100.0f;
        member.game.hero_pose.position[0] = 12.0f;
        run(40);
        CHECK(member.world.synced());
        slot = host.game.moby_slot(0);
    }

    void run(int ticks, int ms = 50) {
        for (int i = 0; i < ticks; ++i) {
            tick(all, ms);
        }
    }

    /* One update of the enemy; true when a stand-in went on for it. */
    bool update(int which = -1) {
        const int s = which < 0 ? slot : which;
        const int before = host.game.stood;
        host.world.gate(s, mp::kMobyBefore);
        const bool stood = host.game.stood != before;
        host.world.gate(s, mp::kMobyAfter);
        return stood;
    }

    /* The update puts the hero in `state`. */
    void update_does(std::int32_t state, std::int32_t health = 8, float x = 3.0f) {
        host.game.stand_did = mp::StandIn{};
        host.game.stand_did.position[0] = x;
        host.game.stand_did.state = state;
        host.game.stand_did.health = health;
        CHECK(update());
    }

    bool has(const Player& p, const std::string& call) const {
        return std::find(p.game.calls.begin(), p.game.calls.end(), call) != p.game.calls.end();
    }

    /* The rope's hold, on, played and shown. */
    void rope_taken() {
        update_does(0x78);
        run(2);
        CHECK(has(member, "grab 11 2") && member.game.hero_held);
        member.game.hero_pose.state = 0x78;
        run(3);
    }
};

void a_grab_is_played_in_the_victims_game() {
    Party p(kRope);
    // Before it, the update sees the player's own state.
    p.member.game.hero_pose.state = 2;
    p.member.game.hero_pose.flags = mp::kPoseAirborne;
    p.run(3);
    p.update_does(2);
    CHECK(p.host.game.stood_as.state == 2 && p.host.game.stood_as.health == 8);
    CHECK((p.host.game.stood_as.flags & mp::kStandInAirborne) != 0);
    CHECK(!p.member.game.hero_held);

    // The update takes him: HOLD, and his own game holds him; the host's hero stays as he is.
    p.rope_taken();
    CHECK(p.host.game.hero_pose.position[0] == 100.0f && !p.has(p.host, "grab 11 2"));

    // It sees him held, whatever a late pose says, and says where it pulls him.
    p.member.game.hero_pose.state = 0x78;
    p.update_does(0x78, 8, 5.0f);
    p.run(3);
    CHECK(p.host.game.stood_as.state == 0x78);
    CHECK(p.member.game.held_at[0] == 5.0f);

    // It lets go: RELEASE, and he is told where he ends up, from a word that must arrive.
    p.update_does(0, 8, 6.0f);
    p.run(2);
    CHECK(p.has(p.member, "let go") && !p.member.game.hero_held);
    p.member.game.hero_pose.state = 0;
    // The next update chooses again.
    p.host.game.hero_pose.position[0] = 1.0f;
    p.member.game.hero_pose.position[0] = 90.0f;
    p.run(40);
    CHECK(!p.update());
}

void a_grab_holds_past_any_stick() {
    Party p(kRope);
    p.rope_taken();
    // The host far nearer, for many times the stick of an ordinary attack.
    p.host.game.hero_pose.position[0] = 0.5f;
    p.member.game.hero_pose.position[0] = 40.0f;

    for (int i = 0; i < 20; ++i) {
        p.run(10);
        p.host.game.stand_did.state = 0x78;
        CHECK(p.update());
    }

    CHECK(p.host.game.stood_as.state == 0x78);
    CHECK(p.member.game.hero_held);
}

void a_lunge_keeps_its_target() {
    Party p(kLeap);
    p.update_does(1);
    p.run(30);
    // The moby in its lunge, long past the stick: the host comes close, and the leap stays on.
    for (mp::MobyState& m : p.host.game.level) {
        m.state = 5;
    }

    p.run(2);
    CHECK(p.update());
    p.host.game.hero_pose.position[0] = 0.5f;
    p.run(60);
    CHECK(p.update());
    // Out of the lunge, the ordinary rule: the nearer player takes it, once its stick has run out.
    for (mp::MobyState& m : p.host.game.level) {
        m.state = 2;
    }

    p.run(2);
    CHECK(p.update());
    p.run(60);
    CHECK(!p.update());
}

void a_fatal_grab_ends_with_the_victim_back() {
    Party p(kLeap);
    p.update_does(0x3D, 0);
    p.run(2);
    CHECK(p.has(p.member, "grab -1 1"));
    // Carried in its mouth, not left where the update wrote nothing.
    p.host.game.level[0].pose.position[0] = 7.0f;
    p.run(1);
    p.update_does(0x3D, 0);
    p.run(2);
    CHECK(p.member.game.held_at[0] == 7.0f);
    CHECK(p.host.game.stood_as.health == 0);
    // Dead, and not let go for being dead.
    p.member.game.hero_pose.health = 0;
    p.member.game.hero_pose.state = 0x3D;
    p.run(10);
    CHECK(!p.has(p.member, "let go"));
    // Back from the checkpoint: over.
    p.member.game.hero_pose.health = 8;
    p.member.game.hero_pose.state = 0;
    p.run(10);
    CHECK(p.has(p.member, "let go"));
    CHECK(p.host.game.stood_as.state == 0x3D);
}

void every_end_of_a_grab_lets_go() {
    // The moby deleted.
    {
        Party p(kRope, 2);
        p.rope_taken();
        p.host.game.level.erase(p.host.game.level.begin());
        p.run(8);
        CHECK(!p.member.game.hero_held);
    }
    // The player dies.
    {
        Party p(kRope);
        p.rope_taken();
        p.member.game.hero_pose.health = 0;
        p.run(8);
        CHECK(!p.member.game.hero_held);
    }
    // The player leaves the level.
    {
        Party p(kRope);
        p.rope_taken();
        p.member.game.now = {mp::kPhasePlaying, 4, 3};
        p.run(20);
        CHECK(!p.member.game.hero_held);
    }
    // The level is made again.
    {
        Party p(kRope);
        p.rope_taken();
        p.host.game.now.loads += 1;
        p.run(8);
        CHECK(!p.member.game.hero_held);
    }
    // His pose never shows it (a ride, a scene): the host checks what it asked.
    {
        Party p(kRope);
        p.update_does(0x78);
        p.run(2);
        CHECK(p.member.game.hero_held);
        p.member.game.hero_pose.state = 0;
        p.run(mp::kHoldAckMs / 50 + 4);
        CHECK(!p.member.game.hero_held);
    }
    // His pose leaves it (the game's own end).
    {
        Party p(kRope);
        p.rope_taken();
        p.member.game.hero_pose.state = 0;
        p.run(8);
        CHECK(!p.member.game.hero_held);
    }
    // No word from the host: his game lets him go by itself.
    {
        Party p(kRope);
        p.rope_taken();
        std::vector<Player*> alone = {&p.member};

        for (std::uint64_t ms = 0; ms < mp::kHoldLeaseMs + 200; ms += 50) {
            tick(alone, 50);
        }

        CHECK(!p.member.game.hero_held);
    }
}

void a_player_in_a_grip_is_no_prey_for_a_second() {
    Party p(kRope, 2);
    const int second = p.host.game.moby_slot(1);
    p.rope_taken();
    CHECK(!p.update(second) || p.host.game.world_calls.back().rfind("as", 0) != 0
          || p.host.game.stood_as.state != 0x78);
    // The second has nobody else near: the far host, on no stand-in, rather than the held one.
    const int before = p.host.game.stood;
    p.host.game.stand_did = mp::StandIn{};
    p.update(second);
    CHECK(p.host.game.stood == before);
    CHECK(p.member.game.hero_held);
}

void the_hosts_own_hero_locks_too() {
    Party p(kRope);
    p.host.game.hero_pose.position[0] = 1.0f;
    p.member.game.hero_pose.position[0] = 90.0f;
    p.run(40);
    CHECK(!p.update());
    // The game has him; no message, but the moby stays on him with the member far nearer.
    p.host.game.hero_pose.state = 0x78;
    CHECK(!p.update());
    p.host.game.hero_pose.position[0] = 80.0f;
    p.member.game.hero_pose.position[0] = 1.0f;
    p.run(60);
    CHECK(!p.update());
    CHECK(!p.member.game.hero_held);
    // Over, and the nearer player is its target.
    p.host.game.hero_pose.state = 0;
    p.update();
    CHECK(p.update());
}

void what_an_update_ends_is_the_players() {
    // A kill volume under a stand-in: the member goes to his checkpoint, the host stays.
    Party p(300);
    p.host.game.stand_did = mp::StandIn{};
    p.host.game.stand_did.health = 8;
    p.host.game.stand_did.flags = mp::kStandInKilled;
    CHECK(p.update());
    p.run(2);
    CHECK(p.member.game.respawns == 1);
    CHECK(p.host.game.respawns == 0);
    // Once a while, however many ticks the volume sees him.
    CHECK(p.update());
    p.run(2);
    CHECK(p.member.game.respawns == 1);
    // Health written to nothing does the same.
    p.run(int(mp::kKillAgainMs / 50) + 1);
    p.host.game.stand_did = mp::StandIn{};
    p.host.game.stand_did.health = 0;
    CHECK(p.update());
    p.run(2);
    CHECK(p.member.game.respawns == 2);
}

void the_lists_of_the_hero_writers() {
    // Rilgar's: each game's own, so a member's water kills him.
    CHECK(mp::local_class(35) && !mp::local_class(1059) && !mp::local_class(1106));
    CHECK(mp::grab_class(1059) != nullptr && mp::grab_class(1106) != nullptr && mp::grab_class(35) == nullptr);
    CHECK(mp::grab_class(300) == nullptr);

    // A moby that keeps the hero in memory keeps its aim past the stick, unlike another.
    for (const int oclass : {749, 300}) {
        Party p(oclass);
        CHECK(p.update());
        p.run(30);
        p.host.game.hero_pose.position[0] = 1.0f;
        p.run(4);
        CHECK(p.update() == (oclass == 749));
    }
}

void the_wire_keeps_the_feet() {
    mp::PoseMessage m;
    m.pose.flags = mp::kPoseAirborne | mp::kPoseBlending;
    mp::PoseMessage back;
    CHECK(mp::decode_pose(mp::encode_pose(m), back));
    CHECK(back.pose.flags == (mp::kPoseAirborne | mp::kPoseBlending));
    m.pose.flags = 0;
    CHECK(mp::decode_pose(mp::encode_pose(m), back) && back.pose.flags == 0);
    CHECK(mp::owner_of(mp::kind::kHold) == mp::Owner::kTether);
    CHECK(mp::owner_of(mp::kind::kRelease) == mp::Owner::kTether);
}

void a_game_that_cannot_hold_stays_as_it_was() {
    Party p(kRope);
    p.host.game.holds_ok = false;
    p.run(2);
    p.host.game.world_calls.clear();
    CHECK(p.update());
    CHECK(p.host.game.world_calls.back().rfind("stand", 0) == 0);
}

}  // namespace

void run_hold_tests() {
    a_grab_is_played_in_the_victims_game();
    a_grab_holds_past_any_stick();
    a_lunge_keeps_its_target();
    a_fatal_grab_ends_with_the_victim_back();
    every_end_of_a_grab_lets_go();
    a_player_in_a_grip_is_no_prey_for_a_second();
    the_hosts_own_hero_locks_too();
    what_an_update_ends_is_the_players();
    the_lists_of_the_hero_writers();
    the_wire_keeps_the_feet();
    a_game_that_cannot_hold_stays_as_it_was();
}

}  // namespace mpt
