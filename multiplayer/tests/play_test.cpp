// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket:
//   - a pose's bytes (play.hpp): 65 of them, read back as written, a broken one refused, a
//     non-finite float read as 0, sequences compared across the wrap; the pieces worn after them
//     and a SHOT, read back as written, an older sender's pose wearing nothing;
//   - the interpolation buffer: a steady stream played back a delay behind, smoothly and in order;
//     repeated and late poses dropped; a teleport put there at once; a stall then a burst caught
//     up without a jump and the cursor never past the newest pose by more than the extrapolation;
//     past the newest pose, on along its velocity for 150 ms, then held;
//   - the co-op (coop.hpp) with a host and a member, their sessions on recorders and a router that
//     does what the server does with DATA, and fake games: the host's Start sends its save in
//     pieces and the member starts it from the title; a new game; the member follows the host's
//     level; a member back in the title asks again (WANT) and gets the host's progress; one who
//     joins late gets it unasked; poses pass through the host to the others and become figures in
//     the same level only, which go with the level, the silence or the party.
#include <cmath>
#include <cstdio>
#include <functional>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "coop.hpp"
#include "play.hpp"
#include "protocol.hpp"
#include "session.hpp"
#include "world.hpp"

namespace {

int g_failures = 0;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

using mp::Bytes;

mp::Pose pose_at(float x, float vx = 0.0f, int level = 3) {
    mp::Pose p;
    p.level = level;
    p.position[0] = x;
    p.velocity[0] = vx;
    return p;
}

void pose_bytes() {
    mp::PoseMessage m;
    m.owner = 7;
    m.sequence = 0xABCD;
    m.stamp_ms = 123456789012LL;
    m.teleport = true;
    m.pose.level = 11;
    m.pose.position[0] = 1.5f;
    m.pose.position[2] = -20.25f;
    m.pose.rotation[2] = 3.0f;
    m.pose.velocity[1] = 4.0f;
    m.pose.state = 2;
    m.pose.health = 8;
    m.pose.item = -1;
    m.pose.seq = 41;
    m.pose.frame = 7;
    m.pose.next_frame = 8;
    m.pose.anim_time = 0.5f;
    m.pose.anim_speed = 1.25f;
    m.pose.flags = mp::kPoseBlending;
    const Bytes b = mp::encode_pose(m);
    // The pose, then a count of 0 pieces worn.
    CHECK(b.size() == mp::kPoseBytes + 1 && b[0] == mp::kPoseKind);
    mp::PoseMessage r;
    CHECK(mp::decode_pose(b, r));
    CHECK(r.owner == 7 && r.sequence == 0xABCD && r.stamp_ms == 123456789012LL && r.teleport);
    CHECK(r.pose.level == 11 && r.pose.position[0] == 1.5f && r.pose.position[2] == -20.25f);
    CHECK(r.pose.rotation[2] == 3.0f && r.pose.velocity[1] == 4.0f && r.pose.state == 2);
    CHECK(r.pose.health == 8 && r.pose.item == -1 && r.pose.seq == 41 && r.pose.frame == 7);
    CHECK(r.pose.next_frame == 8 && std::fabs(r.pose.anim_time - 0.5f) < 0.01f);
    CHECK(r.pose.anim_speed == 1.25f && r.pose.flags == mp::kPoseBlending);
    // Cut short, or another kind: refused.
    CHECK(!mp::decode_pose(Bytes(b.begin(), b.end() - 2), r));
    Bytes other = b;
    other[0] = 6;
    CHECK(!mp::decode_pose(other, r));
    // A NaN position from a broken sender reads as 0.
    m.pose.position[1] = std::nanf("");
    CHECK(mp::decode_pose(mp::encode_pose(m), r) && r.pose.position[1] == 0.0f);
    // Sequences across the wrap.
    CHECK(mp::sequence_newer(1, 0xFFFF) && !mp::sequence_newer(0xFFFF, 1));
    CHECK(!mp::sequence_newer(5, 5) && mp::sequence_newer(6, 5));
    // Angles the short way round.
    CHECK(std::fabs(mp::lerp_angle(3.0f, -3.0f, 0.5f) - 3.1416f) < 0.01f);
}

mp::HeldPiece piece(int slot, int part, int item, int oclass, int joint) {
    mp::HeldPiece h;
    h.slot = slot;
    h.part = part;
    h.item = item;
    h.oclass = oclass;
    h.joint = joint;
    h.seq = 1;
    h.frame = 3;
    h.next_frame = 4;
    h.anim_time = 0.25f;
    h.anim_speed = 1.0f;
    return h;
}

void held_and_shot_bytes() {
    mp::PoseMessage m;
    m.owner = 2;
    m.pose.level = 0;
    m.pose.held_count = 3;
    m.pose.held[0] = piece(0, 0, 8, 0x47, 5);  // the wrench in the hand
    m.pose.held[1] = piece(3, 1, 2, 0x3D9, 2);  // on the back
    m.pose.held[1].flags = mp::kHeldHidden | mp::kHeldBlending;
    m.pose.held[2] = piece(0, 0, 8, 0x47, 5);  // a thrown wrench, loose
    m.pose.held[2].flags = mp::kHeldLoose;
    m.pose.held[2].position[0] = 101.5f;
    m.pose.held[2].rotation[2] = -1.5f;
    const Bytes b = mp::encode_pose(m);
    CHECK(b.size() == mp::kPoseBytes + 1 + 13 * 3 + 24);
    mp::PoseMessage r;
    CHECK(mp::decode_pose(b, r) && r.pose.held_count == 3);
    const mp::HeldPiece& w = r.pose.held[0];
    CHECK(w.slot == 0 && w.part == 0 && w.item == 8 && w.oclass == 0x47 && w.joint == 5);
    CHECK(w.seq == 1 && w.frame == 3 && w.next_frame == 4 && w.anim_speed == 1.0f);
    CHECK(std::fabs(w.anim_time - 0.25f) < 0.01f && w.flags == 0);
    CHECK(r.pose.held[1].oclass == 0x3D9 && r.pose.held[1].part == 1);
    CHECK(r.pose.held[1].flags == (mp::kHeldHidden | mp::kHeldBlending));
    CHECK(r.pose.held[2].flags == mp::kHeldLoose && r.pose.held[2].position[0] == 101.5f);
    CHECK(r.pose.held[2].rotation[2] == -1.5f);
    // An older sender's pose (no count): the same pose, wearing nothing.
    CHECK(mp::decode_pose(Bytes(b.begin(), b.begin() + mp::kPoseBytes), r) && r.pose.held_count == 0);
    // Its pieces cut short: the pose stands, wearing nothing.
    CHECK(mp::decode_pose(Bytes(b.begin(), b.end() - 3), r) && r.pose.held_count == 0);
    CHECK(r.pose.level == 0 && r.owner == 2);
    // More pieces than a body has are not written.
    m.pose.held_count = 40;
    CHECK(mp::decode_pose(mp::encode_pose(m), r) && r.pose.held_count == mp::kHeldMax);

    mp::ShotMessage s;
    s.owner = 3;
    s.level = 4;
    s.shot.item = 0x0F;
    s.shot.position[1] = 7.25f;
    s.shot.velocity[2] = -0.5f;
    s.shot.aim[0] = 1.0f;
    s.shot.aim[3] = -0.25f;
    const Bytes sb = mp::encode_shot(s);
    CHECK(sb.size() == mp::kShotBytes && sb[0] == mp::kShotKind);
    mp::ShotMessage sr;
    CHECK(mp::decode_shot(sb, sr) && sr.owner == 3 && sr.level == 4 && sr.shot.item == 0x0F);
    CHECK(sr.shot.position[1] == 7.25f && sr.shot.velocity[2] == -0.5f);
    CHECK(sr.shot.aim[0] == 1.0f && sr.shot.aim[3] == -0.25f);
    CHECK(!mp::decode_shot(Bytes(sb.begin(), sb.end() - 1), sr));
    CHECK(!mp::decode_shot(b, sr));
}

// A pose every 50 ms moving at 10 units a second, sampled every 16 ms.
void a_steady_stream_plays_smoothly() {
    mp::Interpolator in;
    double last_x = -1e9;
    double worst_step = 0.0;
    bool ordered = true;
    std::uint16_t seq = 0;

    for (int t = 0; t <= 3000; t += 1) {
        if (t % 50 == 0) {
            in.add(pose_at(static_cast<float>(t) / 100.0f, 10.0f), ++seq, t, t + 30, false);
        }

        if (t % 16 == 0 && t > 0) {
            mp::Pose p;
            CHECK(in.at(t + 30, p));
            ordered = ordered && p.position[0] >= last_x - 1e-4;
            if (last_x > -1e8) {
                worst_step = std::max(worst_step, std::fabs(p.position[0] - last_x));
            }
            last_x = p.position[0];
        }
    }

    CHECK(ordered);
    // 16 ms at 10 units a second is 0.16; a little more while the cursor runs fast.
    CHECK(worst_step < 0.3);
    // A delay behind: 2.5 sends of 50 ms (at least 100 ms), about 1.25 units back.
    CHECK(std::fabs(in.delay_ms() - 125.0) < 15.0);
    mp::Pose p;
    in.at(3030, p);
    CHECK(p.position[0] > 30.0 - 1.6 && p.position[0] < 30.0 - 0.6);
}

void repeats_late_poses_and_teleports() {
    mp::Interpolator in;
    in.add(pose_at(0), 10, 0, 0, false);
    in.add(pose_at(0.5f), 11, 50, 50, false);
    in.add(pose_at(9.0f), 11, 60, 60, false);   // the same sequence again: dropped
    in.add(pose_at(9.0f), 9, 70, 70, false);    // older: dropped
    CHECK(in.size() == 2);
    // A step longer than a run makes: put there at once.
    in.add(pose_at(100.0f), 12, 100, 100, false);
    mp::Pose p;
    CHECK(in.size() == 1 && in.at(100, p) && p.position[0] == 100.0f);
    // The sender's flag, and another level, do the same.
    in.add(pose_at(101.0f), 13, 150, 150, true);
    CHECK(in.size() == 1);
    in.add(pose_at(101.5f, 0, 4), 14, 200, 200, false);
    CHECK(in.size() == 1 && in.at(200, p) && p.level == 4);
}

// 20 a second, then 300 ms of nothing (TCP held back), then the held poses at once.
void a_stall_then_a_burst() {
    mp::Interpolator in;
    std::uint16_t seq = 0;
    double last_x = 0.0;
    double worst_jump = 0.0;
    std::vector<int> held;

    for (int t = 0; t <= 4000; ++t) {
        if (t % 50 == 0) {
            if (t > 1000 && t <= 1300) {
                held.push_back(t);  // sent, not arrived
            } else {
                // The burst arrives with the next one.
                for (int h : held) {
                    in.add(pose_at(static_cast<float>(h) / 100.0f, 10.0f), ++seq, h, t, false);
                }
                held.clear();
                in.add(pose_at(static_cast<float>(t) / 100.0f, 10.0f), ++seq, t, t, false);
            }
        }

        if (t % 16 == 0 && t > 200) {
            mp::Pose p;
            in.at(t, p);
            worst_jump = std::max(worst_jump, std::fabs(p.position[0] - last_x));
            last_x = p.position[0];
            // Never past the newest pose's place by more than 150 ms of its run (1.5 units).
            CHECK(p.position[0] <= static_cast<float>(t) / 100.0f + 1.5f + 1e-3f);
        }
    }

    // No jump near the snap distance, and caught up within two seconds of the burst.
    CHECK(worst_jump < 1.0);
    mp::Pose p;
    in.at(4000, p);
    CHECK(std::fabs((40.0 - p.position[0]) - in.delay_ms() / 100.0) < 0.3);
}

void past_the_newest_pose() {
    mp::Interpolator in;
    in.add(pose_at(0.0f, 10.0f), 1, 0, 0, false);
    in.add(pose_at(0.5f, 10.0f), 2, 50, 50, false);
    mp::Pose p;
    in.at(150, p);  // the cursor set at its target: the newest (50)
    in.at(600, p);  // far past it: held 150 ms of run on (less the fade), at most 1.5 units
    CHECK(p.position[0] > 0.5f + 1.0f && p.position[0] <= 0.5f + 1.5f);
    const float held = p.position[0];
    in.at(900, p);
    CHECK(p.position[0] == held);
    CHECK(in.stalled(2000) && !in.stalled(500));
}

// ---- The co-op, a host and a member ----

struct Recorder : mp::Transport {
    std::vector<Bytes> frames;

    void send(Bytes frame) override { frames.push_back(std::move(frame)); }
};

struct FakeGame : mp::Game {
    mp::GameStatus now{mp::kPhaseFrontEnd, -1, 1};
    std::vector<std::string> calls;
    Bytes started_save;
    Bytes progress;
    int next_figure = 0;
    std::map<int, mp::Pose> figures;
    mp::Pose hero_pose = [] {
        mp::Pose p = pose_at(1.0f);
        p.health = 8;
        return p;
    }();

    mp::GameStatus status() override { return now; }

    bool start(int level, const Bytes& save) override {
        calls.push_back("start " + std::to_string(level) + " " + std::to_string(save.size()));
        started_save = save;
        return true;
    }

    bool travel(int level) override {
        calls.push_back("travel " + std::to_string(level));
        return true;
    }

    Bytes capture() override { return progress; }

    bool hero(mp::Pose& out) override {
        out = hero_pose;
        out.level = now.level;
        return mp::in_level(now.phase);
    }

    int figure_create() override {
        figures[next_figure] = mp::Pose{};
        return next_figure++;
    }

    bool figure_pose(int id, const mp::Pose& pose) override {
        if (figures.count(id) == 0) {
            return false;
        }
        figures[id] = pose;
        return true;
    }

    void figure_remove(int id) override { figures.erase(id); }

    // The hero's shots to give, and the figures' shots fired (figure id, shot).
    std::vector<mp::Shot> shots_out;
    std::vector<std::pair<int, mp::Shot>> shots_in;

    std::vector<mp::Shot> hero_shots() override {
        std::vector<mp::Shot> out;
        out.swap(shots_out);
        return out;
    }

    bool figure_shot(int id, const mp::Shot& shot) override {
        if (figures.count(id) == 0) {
            return false;
        }
        shots_in.emplace_back(id, shot);
        return true;
    }

    // The world: a level's mobys by slot, the drives, kills, spawns, hits, stand-ins and bolts. The
    // hero's moby is slot 99, figure id N is slot 100 + N.
    std::vector<mp::MobyState> level;
    std::set<int> drives;
    std::vector<std::string> world_calls;
    std::vector<mp::Hit> hits;
    int stood = 0;
    int bolt_count = 0;

    std::vector<mp::MobyState> mobys() override {
        return listing_blank ? std::vector<mp::MobyState>{} : level;
    }

    int moby_slot(int key) override {
        for (const mp::MobyState& m : level) {
            if (m.key == key && (m.flags & mp::kMobyDead) == 0) {
                return m.slot;
            }
        }
        return -1;
    }

    bool moby_drive(int slot, bool on) override {
        if (on) {
            drives.insert(slot);
        } else {
            drives.erase(slot);
        }
        return true;
    }

    bool moby_pose(int slot, const mp::Pose& pose) override {
        for (mp::MobyState& m : level) {
            if (m.slot == slot) {
                m.pose = pose;
                return true;
            }
        }
        return false;
    }

    // A moby killed: a placed one stays listed, dead (as the port lists it), a spawned one goes.
    bool moby_kill(int slot) override {
        world_calls.push_back("kill " + std::to_string(slot));
        return die(slot);
    }

    bool die(int slot) {
        for (auto it = level.begin(); it != level.end(); ++it) {
            if (it->slot != slot || (it->flags & mp::kMobyDead) != 0) {
                continue;
            }
            if (it->key >= 0) {
                it->flags |= mp::kMobyDead;
            } else {
                level.erase(it);
            }
            return true;
        }
        return false;
    }

    // The game's listing as a menu with its own mobys (or a load) gives it: nothing.
    bool listing_blank = false;
    bool overlay = false;

    void overlay_pause(bool on) override { overlay = on; }

    void skip() override { calls.push_back("skip"); }

    int moby_spawn(int oclass, const mp::Pose& pose) override {
        mp::MobyState m;
        m.slot = 500 + static_cast<int>(level.size());
        m.oclass = oclass;
        m.pose = pose;
        level.push_back(m);
        world_calls.push_back("spawn " + std::to_string(oclass));
        return m.slot;
    }

    int figure_slot(int id) override { return id < 0 ? 99 : 100 + id; }

    bool stand_in(const float* position, int figure) override {
        ++stood;
        world_calls.push_back("stand " + std::to_string(static_cast<int>(position[0])) + " " + std::to_string(figure));
        return true;
    }

    bool hit(const mp::Hit& h) override {
        hits.push_back(h);
        return true;
    }

    int bolts(int add) override { return bolt_count += add; }
};

mp::MobyState moby(int slot, int key, int oclass, std::uint32_t flags, float x) {
    mp::MobyState m;
    m.slot = slot;
    m.key = key;
    m.oclass = oclass;
    m.flags = flags;
    m.pose.position[0] = x;
    return m;
}

struct Player {
    std::int32_t id;
    Recorder link;
    std::uint64_t clock = 1000;
    mp::Session session;
    FakeGame game;
    mp::Coop coop;
    mp::World world;

    explicit Player(std::int32_t id_)
        : id(id_),
          session(link, "test/1", "P" + std::to_string(id_), [this] { return clock; }),
          coop(session, game, [this] { return clock; }),
          world(session, game, coop, [this] { return clock; }) {
        session.on_data([this](std::int32_t sender, const Bytes& bytes) {
            if (!bytes.empty() && mp::world_kind(bytes[0])) {
                world.handle(sender, bytes);
            } else {
                coop.handle(sender, bytes);
            }
        });
        mp::Message hello;
        hello.kind = mp::Message::Kind::kHello;
        hello.id = id;
        session.connected();
        session.handle(hello);
    }
};

mp::Message info(const std::vector<mp::Member>& members) {
    mp::Message m;
    m.kind = mp::Message::Kind::kPartyInfo;
    m.code = "ABCDEF";
    m.host = 1;
    m.members = members;
    m.description = "Kerwan";
    return m;
}

// What the server does with the players' DATA: a member's to the host, the host's to everyone
// else (target 0) or one. Frames other than DATA (hello, time, the lobby's own) are dropped.
void route(std::vector<Player*> players) {
    bool moved = true;

    while (moved) {
        moved = false;

        for (Player* from : players) {
            std::vector<Bytes> frames;
            frames.swap(from->link.frames);

            for (const Bytes& frame : frames) {
                mp::FrameReader r;
                r.feed(frame.data(), frame.size());
                Bytes body;

                if (!r.next(body)) {
                    continue;
                }

                const mp::Request q = mp::decode_request(body);

                if (q.kind != mp::Request::Kind::kData) {
                    continue;
                }

                for (Player* to : players) {
                    const bool host_sends = from->id == 1;
                    const bool reaches = to != from
                                         && (host_sends ? q.target == 0 || q.target == to->id
                                                        : to->id == 1);

                    if (reaches && to->session.in_party()) {
                        mp::Message m;
                        m.kind = mp::Message::Kind::kData;
                        m.id = from->id;
                        m.bytes = q.bytes;
                        to->session.handle(m);
                        moved = true;
                    }
                }
            }
        }
    }
}

void tick(std::vector<Player*> players, std::uint64_t ms) {
    for (Player* p : players) {
        p->clock += ms;
        p->coop.tick(p->clock);
        p->world.tick(p->clock);
    }
    route(players);
}

void the_host_starts_a_save_and_the_member_follows() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info({{1, "P1"}}));
    member.session.handle(info(both));
    mp::Message joined;
    joined.kind = mp::Message::Kind::kPartyJoined;
    joined.id = 2;
    joined.text = "P2";
    host.session.handle(joined);
    CHECK(host.session.is_host() && member.session.in_party() && member.session.description() == "Kerwan");

    // A save of 40,000 bytes: three pieces.
    Bytes save(40000);
    for (std::size_t i = 0; i < save.size(); ++i) {
        save[i] = static_cast<std::uint8_t>(i * 7);
    }
    host.coop.choose(save, "Kerwan");
    CHECK(!member.coop.start());  // only the host starts
    CHECK(host.coop.start());
    CHECK(!host.coop.start());  // once
    CHECK((host.game.calls == std::vector<std::string>{"start -1 40000"}));
    route({&host, &member});
    CHECK(member.coop.playing() && member.session.started());
    tick({&host, &member}, 16);
    CHECK((member.game.calls == std::vector<std::string>{"start -1 40000"}));
    CHECK(member.game.started_save == save);

    // Both in levels: the host in 5, the member in 3 travels there (and not again at once).
    host.game.now = {mp::kPhasePlaying, 5, 2};
    member.game.now = {mp::kPhasePlaying, 3, 2};
    tick({&host, &member}, 16);
    tick({&host, &member}, 16);
    CHECK(member.coop.host_level() == 5);
    CHECK((member.game.calls == std::vector<std::string>{"start -1 40000", "travel 5"}));
    tick({&host, &member}, 1000);
    CHECK(member.game.calls.size() == 2);
    tick({&host, &member}, 4000);
    CHECK(member.game.calls.size() == 3 && member.game.calls.back() == "travel 5");

    // The member arrives in 5: the host's figure in its level, and the member's in the host's.
    member.game.now = {mp::kPhasePlaying, 5, 3};
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.coop.figures() == 1 && host.coop.figures() == 1);
    CHECK(member.game.figures.size() == 1 && member.game.figures.begin()->second.level == 5);
    CHECK(std::fabs(member.game.figures.begin()->second.position[0] - 1.0f) < 1e-4f);

    // The host goes to the title: the member's figure goes, and it travels nowhere.
    host.game.now = {mp::kPhaseFrontEnd, -1, 2};
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.coop.host_level() == -1);
    tick({&host, &member}, mp::kGoneMs + 100);
    CHECK(member.coop.figures() == 0 && member.game.figures.empty());

    // The member back in the title asks for the host's game, and gets its progress.
    host.game.now = {mp::kPhasePlaying, 8, 3};
    host.game.progress = Bytes(100, 9);
    member.game.now = {mp::kPhaseFrontEnd, -1, 3};
    member.game.calls.clear();
    // Not asked while it plays the game it has; asked once out of it (it started this one).
    tick({&host, &member}, mp::kWantMs);
    CHECK(member.game.calls.empty());
}

void a_new_game_and_a_late_member() {
    Player host(1), member(2), late(3);
    host.session.handle(info({{1, "P1"}, {2, "P2"}}));
    member.session.handle(info({{1, "P1"}, {2, "P2"}}));
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &member});
    tick({&host, &member}, 16);
    CHECK((member.game.calls == std::vector<std::string>{"start 0 0"}));

    // The host in level 0 with progress; a third joins: told the game unasked, the progress.
    host.game.now = {mp::kPhasePlaying, 0, 2};
    host.game.progress = Bytes(500, 3);
    late.session.handle(info({{1, "P1"}, {2, "P2"}, {3, "P3"}}));
    mp::Message joined;
    joined.kind = mp::Message::Kind::kPartyJoined;
    joined.id = 3;
    joined.text = "P3";
    host.session.handle(joined);
    member.session.handle(joined);
    tick({&host, &member, &late}, 16);
    tick({&host, &member, &late}, 16);
    CHECK((late.game.calls == std::vector<std::string>{"start 0 500"}));
    CHECK(late.game.started_save == host.game.progress);
    CHECK(member.game.calls.size() == 1);  // not told again

    // The member left its game for the title: it asks (WANT) and is given the progress.
    member.game.now = {mp::kPhaseFrontEnd, -1, 2};
    late.game.now = {mp::kPhasePlaying, 0, 2};
    // It still has this game's play: no WANT. A new play id (the host started again) would.
    tick({&host, &member, &late}, mp::kWantMs);
    CHECK(member.game.calls.size() == 1);
    // Its lobby's Join the game: it asks, and is given the host's progress.
    CHECK(member.coop.rejoin() && !host.coop.rejoin());
    tick({&host, &member, &late}, 16);
    tick({&host, &member, &late}, 16);
    CHECK(member.game.calls.size() == 2 && member.game.calls.back() == "start 0 500");

    // The late one's poses reach the member through the host, as figures in level 0.
    member.game.now = {mp::kPhasePlaying, 0, 4};
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member, &late}, 50);
    }
    CHECK(member.coop.figures() == 2 && member.game.figures.size() == 2);
    CHECK(late.coop.figures() == 2 && host.coop.figures() == 2);

    // A new level load takes the figures; they are made again.
    member.game.now.loads = 5;
    member.game.figures.clear();
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member, &late}, 50);
    }
    CHECK(member.game.figures.size() == 2);

    // The late one leaves: its figure goes.
    mp::Message left;
    left.kind = mp::Message::Kind::kPartyLeft;
    left.id = 3;
    host.session.handle(left);
    member.session.handle(left);
    tick({&host, &member}, 50);
    CHECK(member.game.figures.size() == 1 && member.coop.remotes().count(3) == 0);

    // The party ends: everything goes.
    mp::Message end;
    end.kind = mp::Message::Kind::kPartyEnd;
    member.session.handle(end);
    tick({&member}, 50);
    CHECK(member.game.figures.empty() && !member.coop.playing());
}

void a_member_out_of_the_game_asks_for_it() {
    Player host(1), member(2);
    host.session.handle(info({{1, "P1"}, {2, "P2"}}));
    // The member joins after the start, while the host is still loading: not told yet.
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    host.game.now = {mp::kPhaseLoading, 0, 1};
    tick({&host}, 16);
    member.session.handle(info({{1, "P1"}, {2, "P2"}}));
    // The host's LEVEL names the game; the member, in the title without it, asks.
    host.game.now = {mp::kPhaseLoading, -1, 1};
    tick({&host, &member}, mp::kLevelMs);
    CHECK(member.coop.playing() && member.game.calls.empty());
    host.game.now = {mp::kPhaseLoading, -1, 1};
    tick({&host, &member}, mp::kWantMs);
    // The host has no progress yet (loading): the chosen game, a new one, started the tick after.
    tick({&host, &member}, 16);
    CHECK((member.game.calls == std::vector<std::string>{"start 0 0"}));
}

void world_bytes() {
    mp::Pose p = pose_at(12.5f);
    p.rotation[2] = -1.5f;
    p.seq = 7;
    p.frame = 3;
    p.anim_time = 0.5f;
    p.anim_speed = 2.0f;
    std::vector<mp::WorldEntry> entries(mp::kWorldEntries + 5, mp::WorldEntry{0x8003, p});
    const Bytes b = mp::encode_world(4, 123456, entries);
    CHECK(b.size() == 1 + 1 + 8 + 1 + mp::kWorldEntries * mp::kWorldEntryBytes);
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
    CHECK(host.coop.start());
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
    CHECK(host.world.quiet_death(1) && !host.world.quiet_death(0));
    tick({&host, &member}, mp::kReplayMs + 50);
    CHECK(host.game.moby_slot(1) == -1 && !host.world.quiet_death(1));

    // The host's enemy dies, its bolt is picked up: gone in the member's game too.
    host.game.die(0);
    host.game.die(300);
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(member.game.moby_slot(0) == -1 && member.world.spawned() == 0);
    CHECK(member.game.moby_slot(2) == 2 && member.game.moby_slot(1) == -1);
}

// A member of another build refuses the host's game; a member in the host's game goes back to the
// title when the party ends.
void builds_and_the_end() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.set_build("mp/0.5.0 1.0 pal");
    member.coop.set_build("mp/0.5.0 1.0 ntsc");
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &member});
    tick({&host, &member}, 16);
    CHECK(member.game.calls.empty() && member.coop.take_notice().find("Incompatible version") == 0);
    // Not asked for again.
    tick({&host, &member}, mp::kWantMs + 16);
    CHECK(member.game.calls.empty());

    // The same build: it plays, and when the party ends in a level it goes back to the title.
    Player host2(1), member2(2);
    host2.session.handle(info(both));
    member2.session.handle(info(both));
    host2.coop.choose({}, "New game");
    CHECK(host2.coop.start());
    route({&host2, &member2});
    tick({&host2, &member2}, 16);
    member2.game.now = {mp::kPhasePlaying, 0, 2};
    tick({&host2, &member2}, 16);
    mp::Message end;
    end.kind = mp::Message::Kind::kPartyEnd;
    member2.session.handle(end);
    tick({&member2}, 16);
    CHECK(member2.game.calls.back() == "travel -1" && !member2.coop.playing());
}

// A game in a menu (the pause menu's own mobys up, or a load) lists nothing: nothing is taken for
// dead, on either side; the world is asked to run on under menus while the party plays.
void a_menu_kills_nothing() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    CHECK(!host.game.overlay);
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &member});
    tick({&host, &member}, 16);
    CHECK(host.game.overlay && member.game.overlay);
    const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced;
    for (Player* p : {&host, &member}) {
        p->game.now = {mp::kPhasePlaying, 0, 2};
        p->game.level = {moby(0, 0, 300, runs | mp::kMobyCreature, 10.0f), moby(1, 1, 500, runs, 20.0f),
                         moby(2, 2, 501, runs, 25.0f)};
    }
    member.game.hero_pose.position[0] = 12.0f;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    const std::size_t member_kills = member.game.world_calls.size();
    const std::size_t host_kills = host.game.world_calls.size();

    // The host pauses (its listing goes blank for a while), then the member does.
    host.game.now.phase = mp::kPhaseMenu;
    host.game.listing_blank = true;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    host.game.now.phase = mp::kPhasePlaying;
    host.game.listing_blank = false;
    member.game.now.phase = mp::kPhaseMenu;
    member.game.listing_blank = true;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    member.game.now.phase = mp::kPhasePlaying;
    member.game.listing_blank = false;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }

    // Nothing killed in either game, every moby still there, and the member still shown.
    CHECK(member.game.world_calls.size() == member_kills);
    CHECK(std::none_of(host.game.world_calls.begin() + static_cast<std::ptrdiff_t>(host_kills),
                       host.game.world_calls.end(), [](const std::string& c) { return c.rfind("kill", 0) == 0; }));
    for (Player* p : {&host, &member}) {
        CHECK(p->game.moby_slot(0) == 0 && p->game.moby_slot(1) == 1 && p->game.moby_slot(2) == 2);
    }
    CHECK(host.coop.figures() == 1 && member.coop.figures() == 1);

    // The party over: the game is the game again.
    mp::Message end;
    end.kind = mp::Message::Kind::kPartyEnd;
    member.session.handle(end);
    tick({&member}, 16);
    CHECK(!member.game.overlay);
}

// Each class as the inventory has it: a vendor is each game's own (not driven), a gold bolt a member
// collects is gone in the host's game too (removed, no hit), ammo an enemy of the host drops is the
// member's own to collect (made undriven, kept when the host's is taken).
void classes_are_shared_as_they_should_be() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &member});
    tick({&host, &member}, 16);
    const std::uint32_t runs = mp::kMobyUpdates | mp::kMobyPlaced;
    for (Player* p : {&host, &member}) {
        p->game.now = {mp::kPhasePlaying, 0, 2};
        p->game.level = {moby(0, 0, 11, runs, 5.0f), moby(1, 1, 1134, runs, 6.0f),
                         moby(2, 2, 300, runs | mp::kMobyCreature, 7.0f)};
    }
    member.game.hero_pose.position[0] = 6.0f;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK((member.game.drives == std::set<int>{2}));  // the enemy only

    // The member takes the gold bolt: the host's is removed, without a hit.
    const std::size_t hits = host.game.hits.size();
    member.game.die(1);
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(host.game.moby_slot(1) == -1 && host.game.hits.size() == hits);

    // An enemy of the host drops ammo (class 204): the member gets its own, undriven.
    host.game.level.push_back(moby(301, -1, 204, mp::kMobyUpdates, 8.0f));
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    const auto ammo = std::find_if(member.game.level.begin(), member.game.level.end(),
                                   [](const mp::MobyState& m) { return m.oclass == 204; });
    CHECK(ammo != member.game.level.end() && member.game.drives.count(ammo->slot) == 0);
    // The host collects its own: the member keeps its copy.
    host.game.level.pop_back();
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(std::any_of(member.game.level.begin(), member.game.level.end(),
                      [](const mp::MobyState& m) { return m.oclass == 204; }));
}

// A long session: the server's clock is asked again every kClockMs (TIME frames), the buffers stay
// bounded however long poses come.
void a_long_session() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = member.game.now = {mp::kPhasePlaying, 0, 2};
    int times = 0;
    for (int i = 0; i < 2000; ++i) {  // 100 seconds
        host.game.hero_pose.position[0] = static_cast<float>(i) * 0.05f;
        for (Player* p : {&host, &member}) {
            p->clock += 50;
            p->coop.tick(p->clock);
            for (const Bytes& f : p->link.frames) {
                times += f.size() > 4 && f[4] == static_cast<std::uint8_t>(mp::PacketId::kTime) ? 1 : 0;
            }
        }
        route({&host, &member});
    }
    CHECK(times >= 6);  // each game about 3 times in 100 s
    CHECK(member.coop.remotes().at(1).poses.size() <= mp::kBufferSamples);
    CHECK(member.coop.figures() == 1);
}

// Scenes end together: the host's skip ends the member's scene (the game's own skip), the member's
// Start does nothing while the host's scene plays, and whoever is out first waits for the others.
void scenes_end_together() {
    Player host(1), member(2);
    std::vector<std::string> host_log, member_log;
    host.coop.on_log([&](const std::string& l) { host_log.push_back(l); });
    member.coop.on_log([&](const std::string& l) { member_log.push_back(l); });
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = member.game.now = {mp::kPhasePlaying, 3, 2};
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    member.game.calls.clear();

    // Both in the level's scene (a landing): the member's Start is held back, nobody is held.
    host.game.now.phase = member.game.now.phase = mp::kPhaseScene;
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.coop.host_scene());
    CHECK(!member.coop.hold_input() && !host.coop.hold_input());
    CHECK(member.game.calls.empty());

    // The host skips: out of its scene; the member's is skipped a moment later (again until it is
    // out), and the host waits for it.
    host.game.now.phase = mp::kPhasePlaying;
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.calls.empty());  // not at once
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!member.coop.host_scene());
    CHECK(!member.game.calls.empty() && member.game.calls.back() == "skip");
    CHECK(host.coop.hold_input());
    member.game.now.phase = mp::kPhasePlaying;
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!host.coop.hold_input() && !member.coop.hold_input());
    // What happened is in the logs: the scene's start, the skip asked, its end after it.
    const auto has = [](const std::vector<std::string>& log, const std::string& line) {
        return std::find(log.begin(), log.end(), line) != log.end();
    };
    CHECK(std::any_of(member_log.begin(), member_log.end(), [](const std::string& l) {
        return l.rfind("scene started (member", 0) == 0;
    }));
    CHECK(has(host_log, "scene started (host)"));
    CHECK(has(member_log, "skip requested: the host is out of the scene"));
    CHECK(has(member_log, "scene ended after the party's skip") && has(host_log, "scene ended"));
    CHECK(has(host_log, "waiting for the members to leave their scene"));
    const std::size_t skips = member.game.calls.size();
    tick({&host, &member}, 1000);
    CHECK(member.game.calls.size() == skips);  // no skip out of a scene

    // The host in a scene the member is out of: the member waits (its control held).
    host.game.now.phase = mp::kPhaseScene;
    for (int i = 0; i < 3; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.coop.hold_input() && !host.coop.hold_input());
    host.game.now.phase = mp::kPhasePlaying;
    for (int i = 0; i < 3; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!member.coop.hold_input());

    // A member that never leaves its scene does not hold the host for ever.
    member.game.now.phase = mp::kPhaseScene;
    for (int i = 0; i < 3; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(host.coop.hold_input());
    for (std::uint64_t t = 0; t <= mp::kBarrierMs; t += 50) {
        tick({&host, &member}, 50);
    }
    CHECK(!host.coop.hold_input());
}

// A skip on any player skips for all: a member's Start in a scene skips the host's and the other
// member's (through the host); the host's Start in a movie presses Start for the members' movie
// player; a game where nothing plays does nothing.
void a_skip_anywhere_skips_everywhere() {
    Player host(1), a(2), b(3);
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};
    for (Player* p : {&host, &a, &b}) {
        p->session.handle(info(all));
    }
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &a, &b});
    tick({&host, &a, &b}, 16);
    for (Player* p : {&host, &a, &b}) {
        p->game.now = {mp::kPhaseScene, 0, 2};
        p->game.calls.clear();
    }
    tick({&host, &a, &b}, 16);

    // Member a presses Start in the scene: the host's and b's are skipped, a's own is its game's.
    a.coop.own_skip(mp::kPhaseScene, a.clock);
    route({&host, &a, &b});
    CHECK(!host.game.calls.empty() && host.game.calls.back() == "skip");
    CHECK(!b.game.calls.empty() && b.game.calls.back() == "skip");
    CHECK(a.game.calls.empty());
    // Pressed again at once: no second skip (a while between).
    const std::size_t host_calls = host.game.calls.size();
    a.coop.own_skip(mp::kPhaseScene, a.clock + 10);
    route({&host, &a, &b});
    CHECK(host.game.calls.size() == host_calls);

    // In a movie (a load): the host's Start presses Start for the members' movie player, pressed and
    // let go in turns; not in the host's own game, and not in a member playing.
    for (Player* p : {&host, &a, &b}) {
        p->game.now = {mp::kPhaseLoading, -1, 2};
    }
    b.game.now.phase = mp::kPhasePlaying;
    host.coop.own_skip(mp::kPhaseLoading, host.clock + 1000);
    route({&host, &a, &b});
    int presses = 0, releases = 0;
    for (int i = 0; i < 6; ++i) {
        (a.coop.movie_press(a.clock + 1100) ? presses : releases) += 1;
    }
    CHECK(presses == 3 && releases == 3);
    CHECK(!host.coop.movie_press(host.clock + 1100) && !b.coop.movie_press(b.clock + 1100));
    // Long after, nothing more is pressed.
    CHECK(!a.coop.movie_press(a.clock + 5000));

    // Start in the level (no scene, no movie) skips nothing anywhere.
    host.game.now.phase = mp::kPhasePlaying;
    a.game.now.phase = mp::kPhasePlaying;
    const std::size_t b_calls = b.game.calls.size();
    host.coop.own_skip(mp::kPhasePlaying, host.clock + 3000);
    route({&host, &a, &b});
    CHECK(b.game.calls.size() == b_calls);
}

}  // namespace

// What a player holds reaches its figure with its pose; its shots are fired by its figure, a delay
// behind, in its level only.
void held_items_and_shots_reach_the_figure() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = {mp::kPhasePlaying, 0, 2};
    member.game.now = {mp::kPhasePlaying, 0, 2};
    host.game.hero_pose.held_count = 2;
    host.game.hero_pose.held[0] = piece(0, 0, 8, 0x47, 5);
    host.game.hero_pose.held[1] = piece(3, 1, 2, 0x3D9, 2);
    member.game.hero_pose.held_count = 1;
    member.game.hero_pose.held[0] = piece(0, 0, 0x0F, 0xA8, 5);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }

    // Each figure holds what its player holds.
    CHECK(member.game.figures.size() == 1 && host.game.figures.size() == 1);
    const mp::Pose& on_member = member.game.figures.begin()->second;
    CHECK(on_member.held_count == 2 && on_member.held[0].oclass == 0x47);
    CHECK(on_member.held[1].slot == 3 && on_member.held[1].oclass == 0x3D9);
    const mp::Pose& on_host = host.game.figures.begin()->second;
    CHECK(on_host.held_count == 1 && on_host.held[0].item == 0x0F);

    // The host takes out the Blaster: the member's figure of it holds it after its delay.
    host.game.hero_pose.held_count = 1;
    host.game.hero_pose.held[0] = piece(0, 0, 0x0F, 0xA8, 5);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    const mp::Pose& now_on_member = member.game.figures.begin()->second;
    CHECK(now_on_member.held_count == 1 && now_on_member.held[0].oclass == 0xA8);

    // The member fires: the host's figure of it fires the same shot, not at once (its poses'
    // delay), once.
    mp::Shot shot;
    shot.item = 0x0F;
    shot.position[0] = 12.0f;
    shot.aim[0] = 0.5f;
    member.game.shots_out.push_back(shot);
    tick({&host, &member}, 16);
    CHECK(member.coop.shots_sent() == 1 && host.game.shots_in.empty());
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(host.game.shots_in.size() == 1 && host.coop.shots_fired() == 1);
    CHECK(host.game.shots_in[0].first == host.game.figures.begin()->first);
    CHECK(host.game.shots_in[0].second.position[0] == 12.0f && host.game.shots_in[0].second.aim[0] == 0.5f);
    // The member's own game never fires its own shot again.
    CHECK(member.game.shots_in.empty());

    // The host's shot reaches the member the same way.
    host.game.shots_out.push_back(shot);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.shots_in.size() == 1);

    // A shot fired in another level than this game's is dropped; and the figure goes with a
    // level change, the pieces with it.
    host.game.now = {mp::kPhasePlaying, 1, 3};
    host.game.shots_out.push_back(shot);
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.shots_in.size() == 1 && member.game.figures.empty());

    // Shots outside a level are taken and never sent.
    const std::uint64_t sent = member.coop.shots_sent();
    member.game.now = {mp::kPhaseFrontEnd, -1, 3};
    member.game.shots_out.push_back(shot);
    tick({&host, &member}, 50);
    CHECK(member.game.shots_out.empty() && member.coop.shots_sent() == sent);
}

int main() {
    pose_bytes();
    held_and_shot_bytes();
    a_steady_stream_plays_smoothly();
    repeats_late_poses_and_teleports();
    a_stall_then_a_burst();
    past_the_newest_pose();
    the_host_starts_a_save_and_the_member_follows();
    a_new_game_and_a_late_member();
    a_member_out_of_the_game_asks_for_it();
    world_bytes();
    the_world_is_the_hosts();
    builds_and_the_end();
    a_menu_kills_nothing();
    classes_are_shared_as_they_should_be();
    a_long_session();
    scenes_end_together();
    a_skip_anywhere_skips_everywhere();
    held_items_and_shots_reach_the_figure();

    if (g_failures == 0) {
        std::puts("play: all passed");
    }

    return g_failures == 0 ? 0 : 1;
}
