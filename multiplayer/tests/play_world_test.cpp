// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: the shared world: its bytes, the host's mobys and the despawn log.
#include "test_support.hpp"

namespace mpt {
namespace {


void world_bytes() {
    mp::Pose p = pose_at(12.5f);
    p.rotation[2] = -1.5f;
    p.seq = 7;
    p.frame = 3;
    p.anim_time = 0.5f;
    p.anim_speed = 2.0f;
    std::vector<mp::WorldEntry> entries(mp::kWorldEntries + 5, mp::WorldEntry{0x8003, p});
    const Bytes b = mp::encode_world(4, 123456, entries);
    CHECK(b.size() == 1 + 1 + 8 + 2 + 1 + mp::kWorldEntries * mp::kWorldEntryBytes);
    CHECK(b.size() <= mp::kMaxDatagramBytes - mp::kClientStateHeader);
    int level = 0;
    std::int64_t stamp = 0;
    std::vector<mp::WorldEntry> out;
    CHECK(mp::decode_world(b, level, stamp, out) && level == 4 && stamp == 123456);
    CHECK(out.size() == mp::kWorldEntries && out[0].id == 0x8003 && out[0].pose.position[0] == 12.5f);
    CHECK(std::fabs(out[0].pose.rotation[2] + 1.5f) < 1e-3f && out[0].pose.seq == 7 && out[0].pose.frame == 3);
    CHECK(std::fabs(out[0].pose.anim_time - 0.5f) < 0.01f && out[0].pose.anim_speed == 2.0f);
    CHECK(!mp::decode_world(Bytes(b.begin(), b.end() - 3), level, stamp, out));
}


// The host runs the level's world; the member drives its copy from it.
void the_world_is_the_hosts() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);

    // The same level in both: an enemy (key 0), a crate (key 1), a static rock (key 2, no
    // update); the host also has a bolt it spawned (slot 300, class 13).
    const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced;
    for (Player* p : {&host, &member}) {
        p->game.now = {mp::kPhasePlaying, 0, 2};
        p->game.level = {moby(0, 0, 300, runs | mp::kMobyCreature, 10.0f), moby(1, 1, 500, runs, 20.0f),
                         moby(2, 2, 77, mp::kMobyPlaced, 30.0f)};
    }
    host.game.level.push_back(moby(300, -1, 13, mp::kMobyUpdates, 11.0f));
    host.game.hero_pose.position[0] = 100.0f;
    member.game.hero_pose.position[0] = 12.0f;
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }

    // The member drives the enemy only (not the crate: each game's; not the rock: no update).
    CHECK((member.game.drives == std::set<int>{0}));
    CHECK(member.world.driven() == 2 && member.world.spawned() == 1);  // the enemy, the bolt
    CHECK(std::count(member.game.world_calls.begin(), member.game.world_calls.end(), "spawn 13") == 1);
    CHECK(host.world.hosting() && !member.world.hosting());

    // The host's enemy moves: the member's follows, a delay behind.
    host.game.level[0].pose.position[0] = 15.0f;
    host.game.level[0].pose.seq = 4;
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(std::fabs(member.game.level[0].pose.position[0] - 15.0f) < 0.01f && member.game.level[0].pose.seq == 4);

    // The enemy's update on the host: the member is nearer than the host, so it stands in; the
    // bolts its update gave go to the member.
    const int before = host.game.stood;
    CHECK(!host.world.gate(0, mp::kMobyBefore));
    host.game.bolt_count += 25;
    CHECK(!host.world.gate(0, mp::kMobyAfter));
    CHECK(host.game.stood == before + 1 && host.game.world_calls.back().rfind("stand 12", 0) == 0);
    CHECK(host.game.bolt_count == 0);
    // The crate's update keeps the host's hero.
    host.world.gate(1, mp::kMobyBefore);
    host.world.gate(1, mp::kMobyAfter);
    CHECK(host.game.stood == before + 1);
    tick({&host, &member}, 50);
    CHECK(member.game.bolt_count == 25);

    // The member's wrench on the driven enemy: to the host, which posts it from the member's
    // figure; the member's own copy never sees it.
    mp::Hit wrench;
    wrench.victim = 0;
    wrench.attacker = 99;
    wrench.mask = 0x10000;
    wrench.damage = 1.0f;
    CHECK(member.world.hit(wrench));
    route({&host, &member});
    CHECK(host.game.hits.size() == 1 && host.game.hits[0].victim == 0 && host.game.hits[0].mask == 0x10000);
    CHECK(host.game.hits[0].attacker == 100 + host.coop.figure_of(2));
    // A hit on the member's own hero is its game's.
    wrench.victim = 99;
    CHECK(!member.world.hit(wrench));

    // The enemy hits the member's figure on the host: the member's hero is hurt, by the enemy.
    mp::Hit bite;
    bite.victim = 100 + host.coop.figure_of(2);
    bite.attacker = 0;
    bite.mask = 1;
    bite.damage = 2.0f;
    CHECK(host.world.hit(bite));
    route({&host, &member});
    CHECK(member.game.hits.size() == 1 && member.game.hits[0].victim == 99 && member.game.hits[0].attacker == 0);
    CHECK(member.game.hits[0].damage == 2.0f);

    // The member breaks the crate: the host's breaks too (a wrench's hit posted to it, its drop
    // kept quiet), and is deleted if it has not broken by the deadline.
    const std::size_t hits_before = host.game.hits.size();
    member.game.die(1);
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(host.game.hits.size() == hits_before + 1 && host.game.hits.back().victim == 1);
    CHECK(host.game.hits.back().mask == 0x10000 && host.game.hits.back().attacker == 99);
    CHECK(!host.world.quiet_drop(1) && !host.world.quiet_drop(0) && member.world.quiet_drop(1));
    tick({&host, &member}, mp::kReplayMs + 50);
    CHECK(host.game.moby_slot(1) == -1);

    // The host's enemy dies, its bolt is picked up: gone in the member's game too.
    host.game.die(0);
    host.game.die(300);
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(member.game.moby_slot(0) == -1 && member.world.spawned() == 0);
    CHECK(member.game.moby_slot(2) == 2 && member.game.moby_slot(1) == -1);
}


// An enemy the host's game hides (its update: one waiting to appear) is hidden in the member's,
// whose game does not run that update, and shown with the host's; one the host has not sent is held
// as it is (a missing snapshot changes nothing).
void the_hosts_hidden_mobys_are_hidden_here() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);

    // Two enemies: key 0 near the member, key 1 far from it and still (sent once, then held).
    const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced | mp::kMobyCreature;
    for (Player* p : {&host, &member}) {
        p->game.now = {mp::kPhasePlaying, 0, 2};
        p->game.level = {moby(0, 0, 300, runs, 10.0f), moby(1, 1, 301, runs, 900.0f)};
    }
    member.game.hero_pose.position[0] = 12.0f;
    host.game.level[0].flags |= mp::kMobyHidden;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK((member.game.drives == std::set<int>{0, 1}));
    CHECK((member.game.level[0].flags & mp::kMobyHidden) != 0);
    CHECK((member.game.level[1].flags & mp::kMobyHidden) == 0);

    // It appears in the host's game: shown in the member's.
    host.game.level[0].flags &= ~mp::kMobyHidden;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK((member.game.level[0].flags & mp::kMobyHidden) == 0);
    CHECK(std::count(member.game.world_calls.begin(), member.game.world_calls.end(), "hide 0") == 1);
    CHECK(std::count(member.game.world_calls.begin(), member.game.world_calls.end(), "show 0") == 1);

    // The host stops sending both (no snapshot for a long while): nothing hidden, nothing killed.
    host.game.listing_blank = true;
    const std::size_t calls = member.game.world_calls.size();
    for (int i = 0; i < 40; ++i) {
        tick({&host, &member}, 100);
    }
    CHECK(member.game.world_calls.size() == calls && member.game.moby_slot(0) == 0
          && member.game.moby_slot(1) == 1);
}


// The despawn log's who and why, from each tick's listing against the last.
void the_despawn_log_says_who_and_why() {
    std::vector<std::string> lines;
    std::map<int, mp::MobyDeletion> deleted;
    mp::DespawnLog log;
    log.out = [&](const std::string& l) { lines.push_back(l); };
    log.deletion = [&](int slot, mp::MobyDeletion& d) {
        const auto it = deleted.find(slot);
        if (it == deleted.end()) {
            return false;
        }
        d = it->second;
        return true;
    };
    log.role = [](int slot, const mp::MobyState&) { return slot < 5 ? std::string("driven") : std::string("local"); };
    mp::DespawnLog::View view;
    view.hero = true;
    view.hero_at[0] = 10.0f;
    view.camera = true;
    view.camera_at[0] = 14.0f;

    const std::uint32_t placed = mp::kMobyPlaced | mp::kMobyUpdates;
    std::vector<mp::MobyState> level;
    for (int slot = 0; slot < 8; ++slot) {
        level.push_back(moby(slot, slot < 6 ? slot : -1, 300 + slot, slot < 6 ? placed : mp::kMobyUpdates, 20.0f));
    }
    log.tick(1000, level, view);
    CHECK(lines.empty());

    // 0 killed by the sync, 1 by its own update, 2 by slot 7's update, 3 by another mod, 4 listed
    // dead with no deletion seen, 6 (spawned) gone from the listing; 5 hidden, its death recorded
    // for none.
    log.note_sync(0, "KILLED", 1);
    deleted[0] = {-1, -1, true, 300};
    deleted[1] = {1, 301, false, 301};
    deleted[2] = {7, 307, false, 302};
    deleted[3] = {-1, -1, true, 303};
    log.note_death(1);
    std::vector<mp::MobyState> next = level;
    for (int slot : {0, 1, 2, 3, 4}) {
        next[static_cast<std::size_t>(slot)].flags |= mp::kMobyDead;
    }
    next[5].flags |= mp::kMobyHidden;
    next.erase(next.begin() + 6);
    log.tick(1050, next, view);

    const auto has = [&](const std::string& text) {
        return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(text) != std::string::npos; });
    };
    CHECK(lines.size() == 7);
    CHECK(has("gone class 300 key 0 slot 0 driven by sync (KILLED from 1) at 20.0 0.0 0.0, hero 10.0, camera 6.0"));
    CHECK(has("gone class 301 key 1 slot 1 driven by game (its own update, its death recorded)"));
    CHECK(has("gone class 302 key 2 slot 2 driven by game (the update of slot 7 class 307)"));
    CHECK(has("gone class 303 key 3 slot 3 driven by mod (moby_kill, not the sync's)"));
    CHECK(has("gone class 304 key 4 slot 4 driven vanished (listed dead, no deletion seen)"));
    CHECK(has("gone class 306 key -1 slot 6 local vanished (left the listing, no deletion seen)"));
    CHECK(has("hidden class 305 key 5 slot 5 local by game"));

    // A dead one stays dead (no line again); an empty listing (a load) changes nothing.
    lines.clear();
    log.tick(1100, next, view);
    log.tick(1150, {}, view);
    log.tick(1200, next, view);
    CHECK(lines.empty());

    // A slot made again for another class: the old one gone.
    next[5].flags &= ~mp::kMobyHidden;  // slot 5 shown
    std::vector<mp::MobyState> again = next;
    again.back().oclass = 999;  // slot 7 now another moby
    log.tick(1250, again, view);
    CHECK(lines.size() == 2 && has("shown class 305") && has("gone class 307 key -1 slot 7 local vanished"));

    // The verbose log's cap: kTraceLinesPerSecond lines a second, the rest said in the next.
    mp::Trace trace;
    std::vector<std::string> traced;
    trace.tick(5000, "host");
    trace.line("run", "off: nothing");
    CHECK(!trace.on() && trace.written() == 0);
    trace.open([&](const std::string& l) { traced.push_back(l); });
    trace.tick(5000, "host");
    for (int i = 0; i < mp::kTraceLinesPerSecond + 10; ++i) {
        trace.line("run", "far");
    }
    CHECK(traced.size() == static_cast<std::size_t>(mp::kTraceLinesPerSecond) && trace.dropped() == 10);
    CHECK(traced.front() == "[mp-trace] f=2 t=5000 host run far");
    trace.tick(6100, "host");
    CHECK(traced.size() == static_cast<std::size_t>(mp::kTraceLinesPerSecond) + 1);
    CHECK(traced.back() == "[mp-trace] f=3 t=6100 host trace dropped 10 lines in the last second (at most 2000 a second)");
}


// With the verbose log on, the member's despawn line for a moby the host killed names the sync and the
// sender, and the moby as one it drove.
void the_world_logs_its_despawns() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route({&host, &member});
    tick({&host, &member}, 16);
    std::vector<std::string> member_log;
    mp::Trace trace;
    trace.open([&](const std::string& l) { member_log.push_back(l); });
    member.world.trace(&trace);
    const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced | mp::kMobyCreature;
    for (Player* p : {&host, &member}) {
        p->game.now = {mp::kPhasePlaying, 0, 2};
        // (and a rock: a level always lists some moby alive)
        p->game.level = {moby(0, 0, 300, runs, 10.0f), moby(1, 1, 77, mp::kMobyPlaced, 30.0f)};
    }
    host.game.hero_pose.position[0] = 100.0f;
    member.game.hero_pose.position[0] = 12.0f;
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    host.game.die(0);
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(std::any_of(member_log.begin(), member_log.end(), [](const std::string& l) {
        return l.rfind("[mp-trace] ", 0) == 0
               && l.find(" despawn gone class 300 key 0 slot 0 driven by sync (KILLED from 1)") != std::string::npos
               && l.find("host sent it") != std::string::npos;
    }));
}


// The verbose log: none of its lines while off; on, a pause with a moby going out of range, a
// snapshot with a moby missing, a moby driven, KILLED, and a census's comings and goings.
void the_verbose_log_traces_a_pause() {
    for (const bool on : {false, true}) {
        Player host(1), member(2);
        const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
        host.session.handle(info(both));
        member.session.handle(info(both));
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route({&host, &member});
        tick({&host, &member}, 16);
        std::vector<std::string> host_lines, member_lines;
        mp::Trace host_trace, member_trace;
        if (on) {
            host_trace.open([&](const std::string& l) { host_lines.push_back(l); });
            member_trace.open([&](const std::string& l) { member_lines.push_back(l); });
        }
        host.world.on_log([&](const std::string& l) { host_lines.push_back(l); });
        member.world.on_log([&](const std::string& l) { member_lines.push_back(l); });
        host.world.trace(&host_trace);
        member.world.trace(&member_trace);
        const auto step = [&](int n) {
            for (int i = 0; i < n; ++i) {
                host.clock += 50;
                member.clock += 50;
                host_trace.tick(host.clock, "host");
                member_trace.tick(member.clock, "member");
                for (Player* p : {&host, &member}) {
                    p->coop.tick(p->clock);
                    p->world.tick(p->clock);
                }
                route({&host, &member});
            }
        };
        const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced | mp::kMobyCreature;
        for (Player* p : {&host, &member}) {
            p->game.now = {mp::kPhasePlaying, 0, 2};
            p->game.level = {moby(0, 0, 300, runs, 10.0f), moby(1, 1, 77, mp::kMobyPlaced, 30.0f),
                             moby(2, 2, 301, runs, 1000.0f), moby(3, 3, 78, mp::kMobyPlaced, 40.0f)};
            p->game.run_on = true;
            p->game.run.ran = 3;  // near
            p->game.run.update_dist = 50;
            p->game.run.distance = 10.0f;
        }
        host.game.hero_pose.position[0] = 100.0f;
        member.game.hero_pose.position[0] = 12.0f;
        step(30);

        // Only moby 0 moves: the next snapshot misses 2. A census later, 3 has gone.
        host.game.level[0].pose.position[0] = 11.0f;
        host.game.level.pop_back();
        step(25);

        // The host pauses: the pause menu's level camera put up; moby 0 left out by distance.
        host.game.now.phase = mp::kPhaseMenu;
        host.game.run.menu = 3;
        host.game.run.menu_ticks = 1;
        host.game.run.level_camera[0] = 99.0f;
        host.game.camera_x = 256.0f;
        step(1);
        host.game.slot_runs[0] = host.game.run;
        host.game.slot_runs[0].ran = 0;
        host.game.slot_runs[0].distance = 60.0f;
        host.game.run.menu_ticks = 2;
        step(1);
        host.game.now.phase = mp::kPhasePlaying;
        host.game.slot_runs.clear();
        step(1);
        host.game.die(2);
        step(4);

        const auto has = [](const std::vector<std::string>& lines, const std::string& text) {
            return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) {
                return l.rfind("[mp-trace] ", 0) == 0 && l.find(text) != std::string::npos;
            });
        };
        const auto any_trace = [](const std::vector<std::string>& lines) {
            return std::any_of(lines.begin(), lines.end(), [](const std::string& l) {
                return l.find("[mp-trace]") != std::string::npos || l.find("despawn") != std::string::npos;
            });
        };
        if (!on) {
            CHECK(!any_trace(host_lines) && !any_trace(member_lines));
            continue;
        }
        CHECK(has(host_lines, " host pause enter phase 5 level 0 mode 0 scene -1 camera_position 256.0 0.0 0.0"
                              " run_camera 0.0 0.0 0.0 level_camera 99.0 0.0 0.0 menu level-camera menu_ticks 1"));
        CHECK(has(host_lines, " host pause frame phase 5"));
        CHECK(has(host_lines, " host run out slot 0 class 300 key 0 at 11.0 0.0 0.0 distance 60.0 update_dist 50"));
        CHECK(has(host_lines, " host pause exit phase 3"));
        CHECK(has(host_lines, " host run in slot 0 class 300"));
        CHECK(has(host_lines, " host snap sent to 2 n=1 added [] missing [2]"));
        CHECK(has(member_lines, " member snap received from 1 n=1 added [] missing [2]"));
        CHECK(has(host_lines, " host census came [] went [k3]"));
        CHECK(has(host_lines, " host eyes set n=1 2 at 12.0 0.0 0.0"));
        CHECK(has(host_lines, " host msg sent KILLED to all level 0 n=1 keys [2]"));
        CHECK(has(member_lines, " member msg received KILLED key 2 slot 2"));
        CHECK(has(member_lines, " member despawn moby_kill slot 2 class 301 key 2"));
        CHECK(has(member_lines, " member drive on slot 0 class 300 key 0"));
        CHECK(has(member_lines, " member drive off slot 2 class 301 key 2"));
    }
}
}  // namespace

void run_world_tests() {
    world_bytes();
    the_world_is_the_hosts();
    the_hosts_hidden_mobys_are_hidden_here();
    the_despawn_log_says_who_and_why();
    the_world_logs_its_despawns();
    the_verbose_log_traces_a_pause();
}

}  // namespace mpt
