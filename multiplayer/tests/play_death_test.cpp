// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: what a death leaves (missions, bolts, a full
// wipe, the revive rule), crates that come back, buried bolts, and who an enemy goes for.
#include "test_support.hpp"

namespace mpt {
namespace {

constexpr std::uint32_t kRuns = mp::kMobyUpdates | mp::kMobyPlaced;

/* A party in level 3, its games' mobys `w`'s; the host far from the member. */
struct Party {
    Player host{1}, member{2}, third{3};
    std::vector<Player*> all;
    SimWorld w;

    explicit Party(SimWorld world, bool three = false) : w(world) {
        all = {&host, &member};

        if (three) {
            all.push_back(&third);
        }

        std::vector<mp::Member> members = {{1, "P1"}, {2, "P2"}};

        if (three) {
            members.push_back({3, "P3"});
        }

        join_party(all, members);
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route(all);
        tick(all, 16);

        for (Player* p : all) {
            p->game.now = {mp::kPhasePlaying, 3, 2};
            p->game.level = w.made(0);
        }

        host.game.hero_pose.position[0] = 100.0f;
        member.game.hero_pose.position[0] = 12.0f;
        run(40);
        CHECK(member.world.synced());
    }

    void run(int ticks, int ms = 50) {
        for (int i = 0; i < ticks; ++i) {
            tick(all, ms);
        }
    }
};

mp::MobyState* find_key(FakeGame& game, int key) {
    for (mp::MobyState& m : game.level) {
        if (m.key == key) {
            return &m;
        }
    }

    return nullptr;
}

/* The fake's made() again, with `mission` on `key`. */
void remake_with_mission(Player& p, const SimWorld& w, int key, int mission) {
    remake(p, w);
    find_key(p.game, key)->mission = mission;
}

int driven_spawns(const FakeGame& game, int oclass) {
    int n = 0;

    for (const mp::MobyState& m : game.level) {
        n += m.key < 0 && m.oclass == oclass ? 1 : 0;
    }

    return n;
}

bool dead(FakeGame& game, int key) {
    const mp::MobyState* m = find_key(game, key);
    return m == nullptr || (m->flags & mp::kMobyDead) != 0;
}

void a_missions_moby_comes_back_with_the_mission() {
    Party p(SimWorld{3, 0});
    find_key(p.host.game, 1)->mission = 2;
    find_key(p.member.game, 1)->mission = 2;
    p.host.game.die(p.host.game.moby_slot(1));
    p.host.game.die(p.host.game.moby_slot(0));
    p.run(10);
    CHECK(dead(p.member.game, 1) && dead(p.member.game, 0));

    // The host dies with the mission open: key 1 stands in its game, key 0 (no mission) does not.
    remake_with_mission(p.host, p.w, 1, 2);
    p.run(10);
    CHECK(!dead(p.host.game, 1));
    CHECK(dead(p.host.game, 0));
    // The member's own copy is gone for good: it shows one made for it, posed from the host's.
    CHECK(driven_spawns(p.member.game, 300) == 1);
    find_key(p.host.game, 1)->pose.position[1] = 33.0f;
    p.run(20);
    bool posed = false;

    for (const mp::MobyState& m : p.member.game.level) {
        posed = posed || (m.key < 0 && std::fabs(m.pose.position[1] - 33.0f) < 1.0f);
    }

    CHECK(posed);
    // A member who dies now gets it from its own level, and no second copy.
    remake(p.member, p.w);
    p.run(30);
    CHECK(driven_spawns(p.member.game, 300) == 0 && !dead(p.member.game, 1));
    // Killed again, it goes for everyone.
    p.host.game.die(p.host.game.moby_slot(1));
    p.run(10);
    CHECK(dead(p.member.game, 1));

    // The same with the mission done: it stays dead, as any.
    Party q(SimWorld{3, 0});
    find_key(q.host.game, 1)->mission = 2;
    q.host.game.die(q.host.game.moby_slot(1));
    q.run(10);
    q.host.game.missions_done.insert(2);
    remake_with_mission(q.host, q.w, 1, 2);
    q.run(10);
    CHECK(dead(q.host.game, 1) && driven_spawns(q.member.game, 300) == 0);
}

void a_hosts_death_keeps_the_bolts_lying_about() {
    Party p(SimWorld{1, 2});
    p.host.game.level.push_back(moby(300, -1, 13, mp::kMobyUpdates, 11.0f));
    p.host.game.level.push_back(moby(301, -1, 16, mp::kMobyUpdates, 21.0f));
    p.run(10);
    CHECK(p.member.world.spawned() == 2);
    // One of them taken: it is not made again.
    CHECK(p.member.world.collect(p.member.world.spawned() != 0 ? p.member.game.level.back().slot : -1) == false);
    p.run(4);
    p.host.game.die(301);
    remake(p.host, p.w);
    p.run(20);
    int bolts = 0;

    for (const std::string& call : p.host.game.world_calls) {
        bolts += call == "spawn 13" || call == "spawn 16" ? 1 : 0;
    }

    CHECK(bolts >= 1 && bolts <= 2);
    CHECK(p.member.world.spawned() == static_cast<std::size_t>(bolts));
}

void a_full_wipe_goes_back_to_the_hosts_checkpoint() {
    Party p(SimWorld{2, 3});
    p.host.game.placeable = p.member.game.placeable = true;
    p.host.game.die(p.host.game.moby_slot(1));
    p.run(10);
    CHECK(dead(p.member.game, 1));
    // Both dead: the host's level made again has the enemy back, and it stays back.
    p.member.game.hero_pose.health = 0;
    p.run(4);
    remake(p.host, p.w);
    p.run(3);
    CHECK(!dead(p.host.game, 1));
    CHECK(p.member.game.respawns == 1);
    // The member's respawn, then: nothing is deleted again, and it goes to the host.
    p.member.game.hero_pose.health = 8;
    remake(p.member, p.w);
    p.host.game.hero_pose.flags |= 2u;
    p.run(40);
    CHECK(p.member.world.synced() && !dead(p.member.game, 1));
    CHECK(!p.member.game.placed.empty());

    // A member alive when the host dies: no wipe, what the party broke stays broken.
    Party q(SimWorld{2, 3});
    q.host.game.die(q.host.game.moby_slot(1));
    q.run(10);
    remake(q.host, q.w);
    q.run(10);
    CHECK(dead(q.host.game, 1) && q.member.game.respawns == 0);
}

void the_revive_rule() {
    // Off by default: nobody is held.
    {
        Party off(SimWorld{1, 0});
        off.member.game.hero_pose.health = 0;
        off.run(20);
        CHECK(!off.host.game.holding && !off.member.game.holding && off.member.game.revives == 0);
        CHECK(off.host.game.figures.empty());
    }

    Party p(SimWorld{1, 0});
    p.host.world.rule_revive(true);
    // A member hears it with the world.
    remake(p.member, p.w);
    p.run(30);
    CHECK(p.host.game.holding && p.member.game.holding && p.member.world.revive_on());

    // Down and alone for a while: nothing yet. A living player standing over: up.
    p.member.game.hero_pose.health = 0;
    p.run(40);
    CHECK(p.member.game.revives == 0);
    // Shown to be found: the host still has the member's figure.
    CHECK(p.host.game.figures.size() == 1);
    p.host.game.hero_pose.position[0] = 13.0f;
    p.run(20);
    CHECK(p.member.game.revives == 0);
    p.run(20);
    CHECK(p.member.game.revives == 1);
    p.member.game.hero_pose.health = 8;
    p.host.game.hero_pose.position[0] = 100.0f;
    p.run(4);

    // Left down too long: the game's own way.
    p.member.game.hero_pose.health = 0;
    p.run(static_cast<int>(mp::kDownedMs / 50) + 8);
    CHECK(p.member.game.respawns == 1 && p.member.game.revives == 1);
    remake(p.member, p.w);
    p.member.game.hero_pose.health = 8;
    p.run(30);

    // Everyone down: the host goes back, and that is a full wipe.
    p.member.game.hero_pose.health = 0;
    p.host.game.hero_pose.health = 0;
    p.run(6);
    CHECK(p.host.game.respawns == 1);
    p.host.game.hero_pose.health = 8;
    remake(p.host, p.w);
    p.run(6);
    CHECK(p.member.game.respawns == 2);
}

void a_crate_that_comes_back_is_shared() {
    Party p(SimWorld{1, 3}, true);
    // Key 1: broken in the member's game; the others break theirs, and no one deletes it.
    mp::MobyState* mine = find_key(p.member.game, 1);
    mine->state = 1;
    find_key(p.host.game, 1)->state = 1;
    find_key(p.third.game, 1)->state = 1;
    p.run(2);
    p.host.game.hits.clear();
    mine->state = mp::kCrateBroken;
    p.run(1);
    mine->state = mp::kCrateWaiting;
    p.run(3);
    CHECK(p.host.game.hits.size() == 1 && p.host.game.hits[0].victim == p.host.game.moby_slot(1));
    // The hit done in the host's game: its drop is the party's; the third's breaks too.
    find_key(p.host.game, 1)->state = mp::kCrateWaiting;
    p.run(3);
    CHECK(p.third.game.hits.size() == 1);
    CHECK(p.member.game.hits.empty());
    // Its own word does not come back from the third, even to a copy that has come back already.
    const std::size_t hits = p.host.game.hits.size();
    find_key(p.host.game, 1)->state = 1;
    p.run(1);
    find_key(p.third.game, 1)->state = mp::kCrateWaiting;
    p.run(4);
    CHECK(p.host.game.hits.size() == hits);
    find_key(p.host.game, 1)->state = mp::kCrateWaiting;
    p.run(100);
    CHECK(p.host.game.hits.size() == hits);
    CHECK(!dead(p.host.game, 1) && !dead(p.member.game, 1) && !dead(p.third.game, 1));

    // Back in each game by itself, and it can break again.
    find_key(p.host.game, 1)->state = 1;
    mine->state = 1;
    p.run(2);
    p.host.game.hits.clear();
    mine->state = mp::kCrateWaiting;
    p.run(3);
    CHECK(p.host.game.hits.size() == 1);

    // A crate that starts hidden (the last level's ammo crates) is no break.
    p.member.game.hits.clear();
    p.host.game.hits.clear();
    find_key(p.member.game, 2)->state = 0;
    p.run(1);
    find_key(p.member.game, 2)->state = mp::kCrateWaiting;
    p.run(3);
    CHECK(p.host.game.hits.empty());

    // A replay no update ran in time leaves the crate as it was.
    find_key(p.member.game, 3)->state = 1;
    p.run(1);
    find_key(p.host.game, 3)->state = 1;
    p.run(1);
    find_key(p.host.game, 3)->state = mp::kCrateWaiting;
    p.run(static_cast<int>(mp::kReplayMs / 50) + 4);
    CHECK(!dead(p.member.game, 3));
}

void one_pile_of_buried_bolts_one_digger() {
    constexpr int kPile = 605;
    Party p(SimWorld{1, 0}, true);

    for (Player* player : p.all) {
        player->game.level.push_back(moby(1, 7, kPile, kRuns, 5.0f));
        player->game.level.push_back(moby(2, 8, kPile, kRuns, 9.0f));
    }

    p.run(4);
    // Out of a party's level, or the host: no asking.
    CHECK(p.host.world.metal_detect(8));
    // A member's word for that pile, ahead of its going from the host's game: its copy goes.
    CHECK(!p.member.world.metal_detect(8));
    p.run(2);
    CHECK(dead(p.member.game, 8));
    // Both ask: the game waits; the first asked gets it.
    CHECK(!p.member.world.metal_detect(7));
    route(p.all);
    CHECK(!p.third.world.metal_detect(7));
    p.run(2);
    CHECK(p.member.world.metal_detect(7));
    CHECK(!p.third.world.metal_detect(7));
    p.run(4);
    // Gone from the host's and the third's; the digger's own dig takes its copy.
    CHECK(dead(p.host.game, 7) && dead(p.third.game, 7));
    CHECK(!dead(p.member.game, 7));
    // Dug by nobody in time: it goes all the same.
    p.run(static_cast<int>(mp::kDigMs / 50) + 2);
    CHECK(dead(p.member.game, 7));
    // The host's own dig goes from every game.
    p.host.game.die(p.host.game.moby_slot(8));
    p.run(4);
    CHECK(dead(p.member.game, 8) && dead(p.third.game, 8));
    // A dug pile does not come back with the host's death.
    remake(p.host, p.w);
    p.host.game.level.push_back(moby(1, 7, kPile, kRuns, 5.0f));
    p.run(4);
    CHECK(dead(p.host.game, 7));
    // A pile of the host's level that is gone: the asker's copy goes.
    p.third.game.level.push_back(moby(3, 9, kPile, kRuns, 3.0f));
    p.run(2);
    CHECK(!p.third.world.metal_detect(9));
    p.run(4);
    CHECK(dead(p.third.game, 9));
}

void an_enemy_keeps_its_target() {
    Party p(SimWorld{1, 0});
    const int slot = p.host.game.moby_slot(0);
    // Enemy at 0: the member at 12, the host at 100: the member's.
    const auto aimed_at = [&]() {
        const int before = p.host.game.stood;
        p.host.world.gate(slot, mp::kMobyBefore);
        const bool stood = p.host.game.stood != before;
        p.host.world.gate(slot, mp::kMobyAfter);
        return stood;
    };
    CHECK(aimed_at());
    p.run(30);  // past the state's first second
    // The host a little nearer: inside the band, the target stays.
    p.host.game.hero_pose.position[0] = 11.0f;
    p.run(2);
    CHECK(aimed_at());
    // Clearly nearer: it changes.
    p.host.game.hero_pose.position[0] = 6.0f;
    p.run(2);
    CHECK(!aimed_at());
    // And back only past the band.
    p.host.game.hero_pose.position[0] = 6.0f;
    p.member.game.hero_pose.position[0] = 5.5f;
    p.run(4);
    CHECK(!aimed_at());

    // An attack runs to its end: a new state keeps the target, however near another comes.
    p.member.game.hero_pose.position[0] = 1.0f;
    find_key(p.host.game, 0)->state = 7;
    p.run(1);
    CHECK(!aimed_at());
    p.run(4);
    CHECK(!aimed_at());
    p.run(20);
    CHECK(aimed_at());

    // A target who dies is left at once; a dead host is nobody's target, and with no one left
    // there is no stand-in.
    p.member.game.hero_pose.health = 0;
    p.run(2);
    CHECK(!aimed_at());
    p.host.game.hero_pose.health = 0;
    p.member.game.hero_pose.health = 8;
    p.run(2);
    CHECK(aimed_at());
    p.member.game.hero_pose.health = 0;
    p.run(2);
    CHECK(!aimed_at());
}

void class_lists_after_reading_the_updates() {
    for (const int oclass : {280, 615, 1005, 1016}) {
        CHECK(mp::interactable_class(oclass) && mp::local_class(oclass));
    }

    // Stay the host's (boarding Giant Clank), and the held items' mobys are never placed.
    for (const int oclass : {1451, 1899, 208, 454, 1251, 152}) {
        CHECK(!mp::local_class(oclass));
    }
}

void class_152_reaches_a_member() {
    Party p(SimWorld{1, 0});
    p.host.game.level.push_back(moby(300, -1, 152, mp::kMobyUpdates, 11.0f));
    p.run(6);
    CHECK(p.member.world.spawned() == 1 && p.member.world.driven() == 2);
    // Under the member's stand-in the bolts its update adds are the member's.
    p.host.world.gate(300, mp::kMobyBefore);
    p.host.game.bolts(50);
    p.host.world.gate(300, mp::kMobyAfter);
    p.run(4);
    CHECK(p.host.game.bolt_count == 0 && p.member.game.bolt_count == 50);
    p.host.game.die(300);
    p.run(4);
    CHECK(p.member.world.spawned() == 0);
}

}  // namespace

void run_death_tests() {
    a_missions_moby_comes_back_with_the_mission();
    a_hosts_death_keeps_the_bolts_lying_about();
    a_full_wipe_goes_back_to_the_hosts_checkpoint();
    the_revive_rule();
    a_crate_that_comes_back_is_shared();
    one_pile_of_buried_bolts_one_digger();
    an_enemy_keeps_its_target();
    class_lists_after_reading_the_updates();
    class_152_reaches_a_member();
}

}  // namespace mpt
