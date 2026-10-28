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
//     past the newest pose, on along its velocity for 150 ms, then held, and at once when it was
//     slowing down; a sender back after a silence with its count started again; poses recorded
//     from two games replayed (tests/data): the seqs shown in the sender's order, the place in a
//     seq running on without jumps, no stop overshot, repeated and reordered copies changing
//     nothing;
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
#include "menu.hpp"
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

// A sender slowing to a stop, then nothing more (a stall at the stop): held where the last pose
// is, not carried on past it along the last velocity and pulled back when the next one comes.
void a_stop_is_not_overshot() {
    mp::Interpolator in;
    const float speeds[] = {10.0f, 10.0f, 10.0f, 8.0f, 6.0f, 4.0f, 2.0f};
    float x = 0.0f;
    std::uint16_t seq = 0;

    for (int i = 0; i < 7; ++i) {
        in.add(pose_at(x, speeds[i]), ++seq, 50.0 * i, 50.0 * i, false);
        x += speeds[i] * 0.05f;
    }

    const float stop = x - speeds[6] * 0.05f;
    float worst = -1e9f;

    for (double t = 300; t <= 1000; t += 16) {
        mp::Pose p;
        in.at(t, p);
        worst = std::max(worst, p.position[0]);
    }

    CHECK(worst <= stop + 1e-3f);
}

// A sender gone quiet for longer than a stall and back with its count started again (a new
// connection): taken, not dropped as older until the count comes round. And across the wrap.
void a_count_started_again_and_the_wrap() {
    mp::Interpolator in;
    in.add(pose_at(0.0f), 40000, 0, 0, false);
    in.add(pose_at(0.1f), 40001, 50, 50, false);
    in.add(pose_at(0.2f), 1, 2050, 2050, false);
    mp::Pose p;
    CHECK(in.size() == 1 && in.newest(p) && p.position[0] == 0.2f);
    in.add(pose_at(0.3f), 65535, 2100, 2100, false);  // 1 - 65535 wraps: older, dropped
    CHECK(in.size() == 1);

    mp::Interpolator w;
    const std::uint16_t counts[] = {65534, 65535, 0, 1};

    for (int i = 0; i < 4; ++i) {
        w.add(pose_at(0.1f * i), counts[i], 50.0 * i, 50.0 * i, false);
    }

    w.add(pose_at(5.0f), 65535, 210, 210, false);  // a late copy from before the wrap
    CHECK(w.size() == 4 && w.newest(p) && std::fabs(p.position[0] - 0.3f) < 1e-6f);
}

// A recorded pose: when it came, and what came.
struct Recorded {
    double arrival = 0;
    double stamp = 0;
    std::uint16_t sequence = 0;
    bool teleport = false;
    mp::Pose pose;
};

std::vector<Recorded> recorded(const char* name) {
    std::vector<Recorded> out;
    const std::string path = std::string(MP_TEST_DATA) + "/" + name;
    std::FILE* f = std::fopen(path.c_str(), "r");
    CHECK(f != nullptr);

    if (f == nullptr) {
        return out;
    }

    char line[512];

    while (std::fgets(line, sizeof line, f) != nullptr) {
        if (line[0] == '#') {
            continue;
        }

        Recorded r;
        mp::Pose& p = r.pose;
        unsigned sequence = 0, teleport = 0, flags = 0, seq = 0, frame = 0, next = 0;
        int level = 0, state = 0;
        const int n = std::sscanf(
            line, "%lf,%lf,%u,%u,%d,%u,%f,%f,%f,%f,%f,%f,%f,%d,%u,%u,%u,%f,%f", &r.arrival, &r.stamp,
            &sequence, &teleport, &level, &flags, &p.position[0], &p.position[1], &p.position[2],
            &p.rotation[2], &p.velocity[0], &p.velocity[1], &p.velocity[2], &state, &seq, &frame,
            &next, &p.anim_time, &p.anim_speed
        );

        if (n != 19) {
            continue;
        }

        r.sequence = static_cast<std::uint16_t>(sequence);
        r.teleport = teleport != 0;
        p.level = level;
        p.flags = flags;
        p.state = static_cast<int>(out.size());  // the index, for the checks (replays)
        (void)state;
        p.health = 4;
        p.seq = seq;
        p.frame = frame;
        p.next_frame = next;
        out.push_back(r);
    }

    std::fclose(f);
    return out;
}

// The poses shown 60 times a second while `poses` arrive; with `copies`, each pose arrives again
// later (a repeat) and an older one with it (reordered), as UDP and the TCP fallback may give them.
std::vector<mp::Pose> replay(const std::vector<Recorded>& poses, bool copies) {
    struct Arrival {
        double at;
        const Recorded* r;
    };

    std::vector<Arrival> arrivals;

    for (std::size_t i = 0; i < poses.size(); ++i) {
        arrivals.push_back({poses[i].arrival, &poses[i]});

        if (copies) {
            arrivals.push_back({poses[i].arrival + 30.0, &poses[i]});

            if (i > 0) {
                arrivals.push_back({poses[i].arrival + 1.0, &poses[i - 1]});
            }
        }
    }

    std::stable_sort(arrivals.begin(), arrivals.end(), [](const Arrival& a, const Arrival& b) {
        return a.at < b.at;
    });
    mp::Interpolator in;
    std::vector<mp::Pose> shown;
    std::size_t next = 0;
    const double end = poses.back().arrival + 500.0;

    for (double t = poses.front().arrival; t <= end; t += 1000.0 / 60.0) {
        for (; next < arrivals.size() && arrivals[next].at <= t; ++next) {
            const Recorded& r = *arrivals[next].r;
            in.add(r.pose, r.sequence, r.stamp, arrivals[next].at, r.teleport);
        }

        mp::Pose p;

        if (in.at(t, p)) {
            shown.push_back(p);
        }
    }

    return shown;
}

// The seqs one after another (each change once).
std::vector<std::uint32_t> seq_changes(const std::vector<mp::Pose>& poses) {
    std::vector<std::uint32_t> out;

    for (const mp::Pose& p : poses) {
        if (out.empty() || out.back() != p.seq) {
            out.push_back(p.seq);
        }
    }

    return out;
}

void replays(const char* name) {
    const std::vector<Recorded> poses = recorded(name);
    CHECK(poses.size() > 30);

    if (poses.size() <= 30) {
        return;
    }

    std::vector<mp::Pose> sent;

    for (const Recorded& r : poses) {
        sent.push_back(r.pose);
    }

    const std::vector<mp::Pose> shown = replay(poses, false);
    CHECK(!shown.empty());

    // Every seq shown is the sender's next: none goes back to one it left (a short one may be
    // passed over between two poses).
    const std::vector<std::uint32_t> from = seq_changes(sent);
    std::size_t at = 0;

    for (const std::uint32_t seq : seq_changes(shown)) {
        while (at < from.size() && from[at] != seq) {
            ++at;
        }

        CHECK(at < from.size());
        ++at;
    }

    // In a seq, the place runs on: a key or so a frame (the fastest run, 4 keys a tick at most
    // in a burst less), never back, but where the seq comes round to its start.
    for (std::size_t i = 1; i < shown.size(); ++i) {
        const mp::Pose& a = shown[i - 1];
        const mp::Pose& b = shown[i];

        if (a.seq != b.seq || ((a.flags | b.flags) & mp::kPoseBlending) != 0) {
            continue;
        }

        const double step = (b.frame + b.anim_time) - (a.frame + a.anim_time);
        const bool round = step < 0.0 && b.frame <= 1;
        CHECK(round || (step >= -1e-4 && step <= 2.0));

        if (!(round || (step >= -1e-4 && step <= 2.0))) {
            std::fprintf(stderr, "%s: seq %u from %u+%.3f to %u+%.3f\n", name, a.seq, a.frame,
                         a.anim_time, b.frame, b.anim_time);
        }
    }

    // Where the sender came to a stop (still for three poses or more), the figure never went past
    // it on the way there or while it stood. A pose shown carries the index of the nearer pose
    // sent in its state (replay's), so those of the stop are known.
    for (std::size_t i = 1; i < sent.size();) {
        const auto still = [&](std::size_t k) {
            return std::fabs(sent[k].velocity[0]) < 1e-3f && std::fabs(sent[k].velocity[1]) < 1e-3f;
        };

        if (!still(i) || still(i - 1)) {
            ++i;
            continue;
        }

        std::size_t end = i;

        while (end + 1 < sent.size() && still(end + 1)) {
            ++end;
        }

        if (end - i >= 2) {
            // The way it went, from the last poses that moved.
            const std::size_t from = i >= 4 ? i - 4 : 0;
            const float dx = sent[i].position[0] - sent[from].position[0];
            const float dy = sent[i].position[1] - sent[from].position[1];
            const float n = std::sqrt(dx * dx + dy * dy);

            for (const mp::Pose& p : shown) {
                if (n > 1e-3f && p.state >= static_cast<int>(from) && p.state <= static_cast<int>(end)) {
                    const float past = ((p.position[0] - sent[i].position[0]) * dx
                                        + (p.position[1] - sent[i].position[1]) * dy)
                                       / n;
                    CHECK(past < 0.02f);
                }
            }
        }

        i = end + 1;
    }

    // Repeated and reordered copies change nothing.
    const std::vector<mp::Pose> again = replay(poses, true);
    CHECK(again.size() == shown.size());

    for (std::size_t i = 0; i < std::min(again.size(), shown.size()); ++i) {
        CHECK(again[i].seq == shown[i].seq && again[i].frame == shown[i].frame
              && again[i].position[0] == shown[i].position[0]);
    }
}

void recorded_poses_replay_smoothly() {
    replays("pose_stop.csv");
    replays("pose_stall.csv");
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

    bool open_home() override {
        calls.push_back("home");
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

    // A scene asked for (the test plays it by setting `now`), and -1 a request dropped.
    bool play_scene(int scene) override {
        calls.push_back("scene " + std::to_string(scene));
        return mp::in_level(now.phase);
    }

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

    // The member left its game for the title: it asks (WANT) and is given the progress.
    member.game.now = {mp::kPhaseFrontEnd, -1, 2};
    late.game.now = {mp::kPhasePlaying, 0, 2};
    // It still has this game's play: no WANT. A new play id (the host started again) would.
    tick({&host, &member, &late}, mp::kWantMs);
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

// The pause menu's Multiplayer page (menu.hpp): out of a party one line; in one the code, the
// host's level, the members with their links as the party heard them (LINK, LINKS: a member's
// reaches everyone through the host) and Leave party. Leaving from the pause menu in the host's
// game goes back to the title from under the menu (play_travel works there since 1.9).
void the_pause_page_shows_the_party_and_leaves() {
    mp::PartyPageInput none;
    std::vector<mp::PartyLine> lines = mp::party_lines(none);
    CHECK(lines.size() == 1 && lines[0].kind == mp::PartyLine::Kind::kText
          && lines[0].label == "Not in a party");
    CHECK(mp::link_text(38, true) == "38 ms UDP" && mp::link_text(-1, false) == "... TCP");
    CHECK(std::string(mp::pause_leave_question(false, false)) == "Leave the party?");
    CHECK(std::string(mp::pause_leave_question(false, true)) == "End the party?");
    CHECK(std::string(mp::pause_leave_question(true, true)) == "Stop hosting?");

    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &member});
    tick({&host, &member}, 16);
    member.game.now = {mp::kPhasePlaying, 0, 2};
    host.game.now = {mp::kPhasePlaying, 0, 2};
    tick({&host, &member}, 16);

    // Nobody's link is known before it is reported.
    CHECK(host.session.link(2).ping_ms == -1 && member.session.link(1).ping_ms == -1);
    member.session.report_link(38, true);
    route({&host, &member});
    CHECK(host.session.link(2).ping_ms == 38 && host.session.link(2).udp);
    CHECK(member.session.link(2).ping_ms == 38);
    host.session.report_link(5, false);
    route({&host, &member});
    CHECK(member.session.link(1).ping_ms == 5 && !member.session.link(1).udp);
    CHECK(member.session.link(2).ping_ms == 38 && member.session.link(2).udp);

    // The page as the member sees it.
    mp::PartyPageInput in;
    in.in_party = member.session.in_party();
    in.code = member.session.code();
    in.host_level = "Novalis";
    for (const mp::Member& m : member.session.members()) {
        const mp::Link l = member.session.link(m.id);
        in.members.push_back({m.name, m.id == member.session.host(), m.id == member.session.id(),
                              l.ping_ms, l.udp});
    }
    lines = mp::party_lines(in);
    CHECK(lines.size() == 5);
    CHECK(lines[0].label == "Party code" && lines[0].value == "ABCDEF");
    CHECK(lines[1].label == "Host's level" && lines[1].value == "Novalis");
    CHECK(lines[2].label == "P1 (host)" && lines[2].value == "5 ms TCP");
    CHECK(lines[3].label == "P2 (you)" && lines[3].value == "38 ms UDP");
    CHECK(lines[4].kind == mp::PartyLine::Kind::kLeave && lines[4].label == "Leave party");
    in.direct = true;
    in.where = "10.0.0.2:7770";
    in.host_level.clear();
    lines = mp::party_lines(in);
    CHECK(lines[0].label == "Direct" && lines[0].value == "10.0.0.2:7770" && lines[1].value == "None");

    // Leave party from the pause menu: back to the title from under the menu, once.
    member.game.now = {mp::kPhaseMenu, 0, 2};
    tick({&member}, 16);
    member.session.leave();
    CHECK(!member.session.in_party() && member.session.link(1).ping_ms == -1);
    tick({&member}, 16);
    CHECK(member.game.calls.back() == "travel -1" && member.coop.to_title());
    tick({&member}, 16);
    member.game.now = {mp::kPhaseLoading, -1, 2};
    tick({&member}, 16);
    CHECK(std::count(member.game.calls.begin(), member.game.calls.end(), "travel -1") == 1);
    // Out of a party nothing is reported.
    member.link.frames.clear();
    member.session.report_link(40, true);
    CHECK(member.link.frames.empty());
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

// A pause as the port has it in a party (2026-10-28): the paused game's world goes on under its
// menu and lists the level's mobys as before (phase Menu), its mobys sit still for a while (no
// snapshot for them). Host pause, then member pause: nothing is killed or despawned on the
// watching side, the spawned creature and the placed mobys keep their slots,
// and a creature the host spawns while the member is paused comes to the member then.
void a_pause_keeps_the_world_whole() {
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
        p->game.level = {moby(0, 0, 300, runs | mp::kMobyCreature, 10.0f), moby(1, 1, 500, runs, 20.0f),
                         moby(2, 2, 501, runs, 25.0f)};
    }
    // An enemy the host spawned (a creature, no key).
    host.game.level.push_back(moby(300, -1, 301, mp::kMobyUpdates | mp::kMobyCreature, 12.0f));
    member.game.hero_pose.position[0] = 12.0f;
    for (int i = 0; i < 10; ++i) {
        host.game.level[0].pose.position[1] = static_cast<float>(i);
        tick({&host, &member}, 50);
    }
    CHECK(member.world.driven() == 2 && member.world.spawned() == 1);
    const auto listed = [](const Player& p) { return p.world.mobys(); };
    const std::size_t member_mobys = listed(member);
    const std::size_t member_calls = member.game.world_calls.size();
    const std::size_t host_calls = host.game.world_calls.size();

    // The host pauses: its world runs on, listed as before; its enemy stands still a second.
    host.game.now.phase = mp::kPhaseMenu;
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
        CHECK(listed(member) == member_mobys);
    }
    host.game.now.phase = mp::kPhasePlaying;

    // The member pauses; the host spawns another enemy meanwhile.
    member.game.now.phase = mp::kPhaseMenu;
    host.game.level.push_back(moby(301, -1, 302, mp::kMobyUpdates | mp::kMobyCreature, 14.0f));
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    member.game.now.phase = mp::kPhasePlaying;
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }

    // Nothing killed or despawned in either game; the new enemy came to the member.
    const auto removals = [](const std::vector<std::string>& calls, std::size_t from) {
        return std::count_if(calls.begin() + static_cast<std::ptrdiff_t>(from), calls.end(),
                             [](const std::string& c) { return c.rfind("kill", 0) == 0; });
    };
    CHECK(removals(member.game.world_calls, member_calls) == 0);
    CHECK(removals(host.game.world_calls, host_calls) == 0);
    CHECK(member.world.spawned() == 2 && member.world.driven() == 3);
    CHECK(std::count(member.game.world_calls.begin(), member.game.world_calls.end(), "spawn 302") == 1);
    for (Player* p : {&host, &member}) {
        CHECK(p->game.moby_slot(0) == 0 && p->game.moby_slot(1) == 1 && p->game.moby_slot(2) == 2);
    }
    CHECK(host.coop.figures() == 1 && member.coop.figures() == 1);
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

// The level's scenes play in every game: the host's scene 3 starts in the member's game too (its
// own start, once its player is in control: not under a menu), late; the member's scene plays out
// after the host's has ended as long as it was late, the host waiting for it; a request the game
// did not act on is dropped when the host's scene ends; a member in another level starts nothing;
// the host's scene actors are not shared (each game has its own).
void the_hosts_scenes_play_everywhere() {
    Player host(1), member(2);
    std::vector<std::string> host_log, member_log;
    host.coop.on_log([&](const std::string& l) { host_log.push_back(l); });
    member.coop.on_log([&](const std::string& l) { member_log.push_back(l); });
    const auto has = [](const std::vector<std::string>& log, const std::string& start) {
        return std::any_of(log.begin(), log.end(), [&](const std::string& l) { return l.rfind(start, 0) == 0; });
    };
    const auto asked = [&](const std::string& call) {
        return std::count(member.game.calls.begin(), member.game.calls.end(), call);
    };
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = member.game.now = {mp::kPhasePlaying, 0, 2};
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    member.game.calls.clear();

    // Veldin's opening in the host's game (its actors: a creature it spawned for the scene), the
    // member in its pause menu: nothing asked yet, the member's control held for the host's scene.
    host.game.now = {mp::kPhaseScene, 0, 2, 3};
    host.game.level.push_back(moby(310, -1, 1250, mp::kMobyUpdates | mp::kMobyCreature | mp::kMobyScene, 5.0f));
    member.game.now.phase = mp::kPhaseMenu;
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(has(host_log, "scene 3 of level 0 started here: the party plays it too"));
    CHECK(has(member_log, "the host plays scene 3 of level 0"));
    CHECK(asked("scene 3") == 0);
    CHECK(member.world.spawned() == 0);
    CHECK(std::count(member.game.world_calls.begin(), member.game.world_calls.end(), "spawn 1250") == 0);

    // The member's menu closed: the same scene asked of its game, once (again only kSceneAskMs on).
    member.game.now.phase = mp::kPhasePlaying;
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    CHECK(asked("scene 3") == 1);
    CHECK(has(member_log, "starting the host's scene 3 here"));
    CHECK(member.coop.hold_input());
    for (std::uint64_t t = 0; t < mp::kSceneAskMs; t += 50) {
        tick({&host, &member}, 50);
    }
    CHECK(asked("scene 3") == 2);

    // Its game plays it (4 s after the host's started): no more asking, its own actors not counted.
    member.game.now = {mp::kPhaseScene, 0, 2, 3};
    member.game.level.push_back(moby(311, -1, 1250, mp::kMobyUpdates | mp::kMobyCreature | mp::kMobyScene, 5.0f));
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(has(member_log, "playing the host's scene 3 here, "));
    CHECK(asked("scene 3") == 2 && asked("scene -1") == 0);
    CHECK(!member.coop.hold_input() && !host.coop.hold_input());
    CHECK(host.world.spawned() == 0 && member.world.spawned() == 0);

    // The host's ends (its actors gone): the member's plays on, past kHostOutMs, the host waiting.
    host.game.now = {mp::kPhasePlaying, 0, 2};
    host.game.level.pop_back();
    for (int i = 0; i < 50; ++i) {  // 2.5 s
        tick({&host, &member}, 50);
    }
    CHECK(std::count(member.game.calls.begin(), member.game.calls.end(), "skip") == 0);
    CHECK(host.coop.hold_input());
    CHECK(has(host_log, "waiting for the members to leave their scene"));
    // Its end: everyone goes on, nothing skipped.
    member.game.now = {mp::kPhasePlaying, 0, 2};
    member.game.level.pop_back();
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!host.coop.hold_input() && !member.coop.hold_input());
    CHECK(std::count(member.game.calls.begin(), member.game.calls.end(), "skip") == 0);
    // Played once: not asked again while the host plays on.
    tick({&host, &member}, 4000);
    CHECK(asked("scene 3") == 2);

    // Scene 5: asked, the member's game never starts it (as after a load), the host's ends: the
    // request is dropped.
    host.game.now = {mp::kPhaseScene, 0, 2, 5};
    for (int i = 0; i < 6; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(asked("scene 5") == 1);
    host.game.now = {mp::kPhasePlaying, 0, 2};
    for (int i = 0; i < 6; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(asked("scene -1") == 1);
    CHECK(has(member_log, "the host's scene is over: not starting it here"));

    // Scene 3 again (a new start): played again. The member in another level: nothing asked.
    member.game.calls.clear();
    host.game.now = {mp::kPhaseScene, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(asked("scene 3") == 1);
    member.game.now = {mp::kPhaseScene, 0, 2, 3};
    tick({&host, &member}, 50);
    member.game.now = host.game.now = {mp::kPhasePlaying, 0, 2};
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    member.game.calls.clear();
    member.game.now = {mp::kPhasePlaying, 4, 3};
    host.game.now = {mp::kPhaseScene, 0, 2, 7};
    for (int i = 0; i < 10; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(asked("scene 7") == 0);

    // A member that started late is let play only so long: the host's skip still skips it at once.
    member.game.now = {mp::kPhasePlaying, 0, 2};
    host.game.now = {mp::kPhasePlaying, 0, 2};
    for (int i = 0; i < 30; ++i) {
        tick({&host, &member}, 50);
    }
    host.game.now = {mp::kPhaseScene, 0, 2, 9};
    tick({&host, &member}, 50);
    tick({&host, &member}, 50);
    member.game.now = {mp::kPhaseScene, 0, 2, 9};
    tick({&host, &member}, 50);
    member.game.calls.clear();
    host.coop.own_skip(mp::kPhaseScene, host.clock);
    host.game.now = {mp::kPhasePlaying, 0, 2};
    route({&host, &member});
    tick({&host, &member}, 50);
    CHECK(std::count(member.game.calls.begin(), member.game.calls.end(), "skip") >= 1);
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

// The host's ship travel: a member's own is refused; the host's takes every member along at once,
// from a menu too, and the party arrives together: nobody goes on, no figure is shown and none of
// the host's world arrives until all are in the new level; a slow member is waited for, a member
// that never comes is not waited for ever.
void a_ship_travel_takes_the_party_along() {
    Player host(1), member(2);
    std::vector<std::string> host_log, member_log;
    host.coop.on_log([&](const std::string& l) { host_log.push_back(l); });
    member.coop.on_log([&](const std::string& l) { member_log.push_back(l); });
    const auto has = [](const std::vector<std::string>& log, const std::string& start) {
        return std::any_of(log.begin(), log.end(), [&](const std::string& l) { return l.rfind(start, 0) == 0; });
    };
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    host.session.handle(info(both));
    member.session.handle(info(both));
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &member});
    tick({&host, &member}, 16);
    host.game.now = member.game.now = {mp::kPhasePlaying, 11, 2};
    for (int i = 0; i < 20; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.coop.figures() == 1 && host.coop.figures() == 1);
    member.game.calls.clear();

    // A member's planet select: only the host flies; the same planet (a landing again) is the game's.
    CHECK(!member.coop.ship_travel(5, 11));
    CHECK(member.coop.take_notice() == "Only the host flies the ship to another planet");
    CHECK(member.coop.banner(member.clock) == "Only the host flies the ship to another planet");
    CHECK(member.coop.banner(member.clock + mp::kRefusedMs).empty());
    CHECK(member.coop.ship_travel(11, 11) && host.coop.ship_travel(11, 11));
    CHECK(!host.coop.arriving());
    tick({&host, &member}, 50);
    CHECK(member.game.calls.empty());

    // The member is in the pause menu when the host's ship leaves for 5: it leaves at once (asked
    // again a while later if its game has not gone), and the host's flight out pulls nobody back.
    member.game.now.phase = mp::kPhaseMenu;
    CHECK(host.coop.ship_travel(5, 11));
    CHECK(host.coop.arriving() && host.coop.waiting_for() == 1);
    host.game.now.phase = mp::kPhaseScene;
    route({&host, &member});
    tick({&host, &member}, 16);
    CHECK((member.game.calls == std::vector<std::string>{"travel 5"}));
    CHECK(member.coop.arriving() && has(member_log, "leaving level 11 for 5 (phase 5)"));
    tick({&host, &member}, 500);
    CHECK(member.game.calls.size() == 1);
    tick({&host, &member}, mp::kLeaveMs);
    CHECK(member.game.calls.size() == 2 && member.game.calls.back() == "travel 5");
    member.game.now = {mp::kPhaseLoading, 5, 3};
    for (int i = 0; i < 4; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(has(member_log, "left for level 5"));

    // The member lands first, and is out of its landing while the host still flies out: it waits,
    // with no figure and no travel back to the host's old level.
    member.game.now = {mp::kPhasePlaying, 5, 3};
    member.game.calls.clear();
    for (int i = 0; i < 100; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(member.game.calls.empty() && member.coop.hold_input() && member.coop.figures() == 0);
    CHECK(member.game.figures.empty());

    // The host on its way, then in 5 with a bolt it spawned there: everyone is in, so all go on,
    // the figures come back, and the host's world arrives only now.
    host.game.now = {mp::kPhaseLoading, 5, 2};
    tick({&host, &member}, 50);
    host.game.level = {moby(300, -1, 13, mp::kMobyUpdates, 11.0f)};
    host.game.now = {mp::kPhasePlaying, 5, 3};
    for (int i = 0; i < 40; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!host.coop.arriving() && !member.coop.arriving());
    CHECK(!host.coop.hold_input() && !member.coop.hold_input());
    CHECK(has(host_log, "everyone is in level 5: going on"));
    CHECK(has(member_log, "everyone is in level 5: going on"));
    CHECK(member.coop.figures() == 1 && host.coop.figures() == 1);
    CHECK(member.world.spawned() == 1);
    CHECK(member.game.calls.empty());

    // The next travel, to 7: this time the host lands first; its control is held until the member
    // is in.
    member.game.now.phase = mp::kPhasePlaying;
    CHECK(host.coop.ship_travel(7, 5));
    route({&host, &member});
    tick({&host, &member}, 16);
    CHECK(member.game.calls.back() == "travel 7");
    member.game.now = {mp::kPhaseLoading, 7, 4};
    host.game.now = {mp::kPhasePlaying, 7, 4};
    for (int i = 0; i < 200; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(host.coop.arriving() && host.coop.hold_input() && host.coop.waiting_for() == 1);
    CHECK(has(host_log, "waiting for 1 players to arrive in level 7"));
    CHECK(host.coop.banner(host.clock) == "Waiting for the other players to arrive");
    member.game.now = {mp::kPhasePlaying, 7, 5};
    for (int i = 0; i < 40; ++i) {
        tick({&host, &member}, 50);
    }
    CHECK(!host.coop.hold_input() && !member.coop.hold_input() && host.coop.figures() == 1);
    CHECK(host.coop.banner(host.clock).empty());

    // A member that never arrives (its game stuck in a load) does not hold the host for ever.
    CHECK(host.coop.ship_travel(9, 7));
    route({&host, &member});
    tick({&host, &member}, 16);
    member.game.now = {mp::kPhaseLoading, 9, 6};
    host.game.now = {mp::kPhasePlaying, 9, 5};
    tick({&host, &member}, 1000);
    CHECK(host.coop.hold_input());
    for (std::uint64_t t = 0; t <= mp::kArriveMs; t += 1000) {
        tick({&host, &member}, 1000);
    }
    CHECK(!host.coop.arriving() && !host.coop.hold_input());
}

// Three players: one member in the new level waits, given nothing of the host's world and shown
// nobody, until the slower one is in too.
void the_party_arrives_together() {
    Player host(1), fast(2), slow(3);
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};
    for (Player* p : {&host, &fast, &slow}) {
        p->session.handle(info(all));
    }
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    route({&host, &fast, &slow});
    tick({&host, &fast, &slow}, 16);
    for (Player* p : {&host, &fast, &slow}) {
        p->game.now = {mp::kPhasePlaying, 11, 2};
    }
    for (int i = 0; i < 20; ++i) {
        tick({&host, &fast, &slow}, 50);
    }
    CHECK(fast.coop.figures() == 2);

    CHECK(host.coop.ship_travel(5, 11));
    CHECK(host.coop.waiting_for() == 2);
    route({&host, &fast, &slow});
    tick({&host, &fast, &slow}, 16);
    CHECK(fast.game.calls.back() == "travel 5" && slow.game.calls.back() == "travel 5");
    for (Player* p : {&host, &fast, &slow}) {
        p->game.now = {mp::kPhaseLoading, 5, 3};
    }
    tick({&host, &fast, &slow}, 50);

    // The host and the fast member in 5 (the host's bolt there), the slow one still loading.
    host.game.level = {moby(300, -1, 13, mp::kMobyUpdates, 11.0f)};
    host.game.now = fast.game.now = {mp::kPhasePlaying, 5, 4};
    for (int i = 0; i < 100; ++i) {
        tick({&host, &fast, &slow}, 50);
    }
    CHECK(host.coop.arriving() && host.coop.waiting_for() == 1);
    CHECK(host.coop.hold_input() && fast.coop.hold_input());
    CHECK(fast.world.spawned() == 0 && fast.coop.figures() == 0 && host.coop.figures() == 0);

    // The slow one in: everyone goes on, the world and the figures come.
    slow.game.now = {mp::kPhasePlaying, 5, 4};
    for (int i = 0; i < 40; ++i) {
        tick({&host, &fast, &slow}, 50);
    }
    CHECK(!host.coop.arriving() && !fast.coop.arriving() && !slow.coop.arriving());
    CHECK(!host.coop.hold_input() && !fast.coop.hold_input() && !slow.coop.hold_input());
    CHECK(fast.world.spawned() == 1 && slow.world.spawned() == 1);
    CHECK(fast.coop.figures() == 2 && host.coop.figures() == 2);
}

// Leaving (coop.hpp). Three players in the host's level 4: each plays the party's game.
struct Party {
    Player host{1}, a{2}, b{3};
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};

    Party() {
        host.session.handle(info(all));
        a.session.handle(info(all));
        b.session.handle(info(all));
        host.coop.choose({}, "New game");
        CHECK(host.coop.start());
        route(players());
        tick(players(), 16);

        for (Player* p : players()) {
            p->game.now = {mp::kPhasePlaying, 4, 2};
            p->game.calls.clear();
        }

        tick(players(), 16);
    }

    std::vector<Player*> players() { return {&host, &a, &b}; }

    // The server's word that `id` left, to everyone still in the party.
    void left(std::int32_t id) {
        mp::Message m;
        m.kind = mp::Message::Kind::kPartyLeft;
        m.id = id;

        for (Player* p : players()) {
            if (p->id != id && p->session.in_party()) {
                p->session.handle(m);
            }
        }
    }
};

std::size_t count_of(const FakeGame& game, const std::string& call) {
    return static_cast<std::size_t>(std::count(game.calls.begin(), game.calls.end(), call));
}

// A player out of its level and into the title: Loading, then the title's main menu.
void reach_title(Player& p) {
    p.game.now = {mp::kPhaseLoading, -1, 3};
    tick({&p}, 16);
    p.game.now = {mp::kPhaseFrontEnd, -1, 3};
    tick({&p}, 16);
}

// The host leaves (from its pause menu): the party ends, and the host and every member go back
// to the title and its Multiplayer page; nobody stays in a level alone.
void the_host_leaving_takes_everyone_home() {
    Party party;
    party.host.game.now = {mp::kPhaseMenu, 4, 2};
    party.a.game.now = {mp::kPhaseScene, 4, 2};
    tick(party.players(), 16);
    party.host.session.leave();
    mp::Message end;
    end.kind = mp::Message::Kind::kPartyEnd;
    party.a.session.handle(end);
    party.b.session.handle(end);
    tick(party.players(), 16);

    // Each leaves its level at once: from the pause menu, a scene, playing.
    for (Player* p : party.players()) {
        CHECK(!p->session.in_party() && p->coop.to_title());
        CHECK(p->game.calls.size() == 1 && p->game.calls[0] == "travel -1");
    }

    // Not asked again while the travel is under way; the page asked for once the title is up.
    tick(party.players(), mp::kLeaveMs);
    for (Player* p : party.players()) {
        CHECK(count_of(p->game, "travel -1") == 1 && count_of(p->game, "home") == 0);
        reach_title(*p);
        CHECK(!p->coop.to_title() && p->coop.wants_home() && count_of(p->game, "home") == 1);
    }

    // The main menu not up yet: asked again until the page shows, then no more.
    tick({&party.host}, mp::kHomeRetryMs);
    CHECK(count_of(party.host.game, "home") == 2);
    party.host.coop.home_shown();
    tick({&party.host}, mp::kHomeRetryMs);
    CHECK(count_of(party.host.game, "home") == 2 && !party.host.coop.wants_home());

    // Never shown: given up after a while.
    tick({&party.a}, mp::kHomeMs);
    CHECK(!party.a.coop.wants_home());
    const std::size_t asked = count_of(party.a.game, "home");
    tick({&party.a}, mp::kHomeRetryMs);
    CHECK(count_of(party.a.game, "home") == asked);

    // The player who starts a game from the title meanwhile keeps it.
    party.b.game.now = {mp::kPhaseLoading, -1, 4};
    tick({&party.b}, mp::kHomeRetryMs);
    CHECK(!party.b.coop.wants_home());
}

// A member leaves (from a scene): only it goes back to the title and its Multiplayer page; the
// host and the other member play on, together.
void a_member_leaving_goes_home_alone() {
    Party party;
    party.a.game.now = {mp::kPhaseScene, 4, 2};
    tick(party.players(), 16);
    party.a.session.leave();
    party.left(2);
    tick(party.players(), 16);
    CHECK(party.a.game.calls.size() == 1 && party.a.game.calls[0] == "travel -1");
    reach_title(party.a);
    CHECK(count_of(party.a.game, "home") == 1);

    for (int i = 0; i < 20; ++i) {
        tick({&party.host, &party.b}, 50);
    }

    CHECK(party.host.session.in_party() && party.host.session.members().size() == 2);
    CHECK(party.b.session.in_party() && party.b.coop.host_level() == 4);
    CHECK(party.host.coop.playing() && party.b.coop.playing());
    CHECK(party.host.coop.figures() == 1 && party.b.coop.figures() == 1);

    for (Player* p : {&party.host, &party.b}) {
        CHECK(count_of(p->game, "travel -1") == 0 && count_of(p->game, "home") == 0);
        CHECK(!p->coop.to_title() && !p->coop.wants_home());
    }
}

// The connection lost under a member (the host gone, or the server): it goes home as one that
// left; the server tells the others it left, and they play on.
void a_member_disconnected_goes_home_alone() {
    Party party;
    party.b.session.disconnected();
    party.left(3);
    tick(party.players(), 16);
    CHECK(party.b.game.calls.size() == 1 && party.b.game.calls[0] == "travel -1");
    reach_title(party.b);
    CHECK(count_of(party.b.game, "home") == 1);
    tick({&party.host, &party.a}, 1000);
    CHECK(party.host.session.members().size() == 2 && party.a.coop.host_level() == 4);

    for (Player* p : {&party.host, &party.a}) {
        CHECK(count_of(p->game, "travel -1") == 0 && count_of(p->game, "home") == 0);
    }

    // The host's own link lost: it goes home too, alone in no level.
    party.host.session.disconnected();
    tick({&party.host}, 16);
    CHECK(party.host.game.calls.back() == "travel -1");
}

// A player in the title's pages (the lobby: it never played, or left the host's game) when the
// party ends stays there: no travel, and the lobby itself goes back to Home. One whose start was
// just asked follows its game into the level and comes back.
void leaving_from_the_title() {
    Party party;
    party.a.game.now = {mp::kPhaseFrontEnd, -1, 3};
    tick(party.players(), mp::kStartingMs);
    party.a.session.disconnected();
    tick({&party.a}, 16);
    tick({&party.a}, mp::kStartingMs);
    CHECK(party.a.game.calls.empty() && !party.a.coop.to_title() && !party.a.coop.wants_home());

    // The host's Start, then its party ended before the title left: back once in the level.
    Player host(1);
    host.session.handle(info({{1, "P1"}}));
    host.coop.choose({}, "New game");
    CHECK(host.coop.start());
    tick({&host}, 16);
    host.session.leave();
    tick({&host}, 16);
    CHECK(host.coop.to_title() && host.game.calls.back() == "start 0 0");
    host.game.now = {mp::kPhasePlaying, 0, 2};
    tick({&host}, 16);
    CHECK(host.game.calls.back() == "travel -1");

    // Why the party went, for Home: nothing when the player left it.
    CHECK(mp::leave_reason(true, true, false, "").empty());
    CHECK(mp::leave_reason(false, false, false, "The host left: the party is over")
          == "The host left: the party is over");
    CHECK(mp::leave_reason(false, true, true, "") == "The host went away");
    CHECK(mp::leave_reason(false, true, false, "") == "The server went away");
    CHECK(mp::leave_reason(false, false, false, "") == "The party is over");

    for (const char* reason : {"The host left: the party is over", "The host went away",
                               "The server went away"}) {
        CHECK(std::string(reason).size() <= mp::kFooterMax);
    }
}

int main() {
    pose_bytes();
    held_and_shot_bytes();
    a_steady_stream_plays_smoothly();
    repeats_late_poses_and_teleports();
    a_stall_then_a_burst();
    past_the_newest_pose();
    a_stop_is_not_overshot();
    a_count_started_again_and_the_wrap();
    recorded_poses_replay_smoothly();
    the_host_starts_a_save_and_the_member_follows();
    a_new_game_and_a_late_member();
    a_member_out_of_the_game_asks_for_it();
    world_bytes();
    the_world_is_the_hosts();
    builds_and_the_end();
    the_pause_page_shows_the_party_and_leaves();
    a_menu_kills_nothing();
    a_pause_keeps_the_world_whole();
    classes_are_shared_as_they_should_be();
    a_long_session();
    scenes_end_together();
    the_hosts_scenes_play_everywhere();
    a_skip_anywhere_skips_everywhere();
    held_items_and_shots_reach_the_figure();
    a_ship_travel_takes_the_party_along();
    the_party_arrives_together();
    the_host_leaving_takes_everyone_home();
    a_member_leaving_goes_home_alone();
    a_member_disconnected_goes_home_alone();
    leaving_from_the_title();

    if (g_failures == 0) {
        std::puts("play: all passed");
    }

    return g_failures == 0 ? 0 : 1;
}
