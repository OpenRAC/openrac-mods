// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: a pose's bytes, the pieces held and the interpolation buffer.
#include "test_support.hpp"

namespace mpt {
namespace {


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


// A held item's animation (the wrench's swing, the Blaster's recoil, a glove's throw) plays on the
// figure as the body's does: between two poses its key and time run on from the one's to the
// other's, every frame, not only when a pose comes; a new seq or a blend keeps the nearer pose's;
// a thrown wrench moves along between its two places.
// A SHOTX: only what a shot has goes, a whole datagram or none, and a track's MOVE gives way to its
// next.
void shotx_bytes() {
    std::vector<mp::ShotEntry> in(3);
    in[0].owner = 2;
    in[0].level = 5;
    in[0].stamp = 0xF0000001u;
    in[0].shot.item = 0x0F;
    in[0].shot.position[1] = 7.25f;
    in[0].shot.aim[3] = -0.25f;
    in[0].shot.target_key = 300;
    in[1].owner = 3;
    in[1].level = -1;
    in[1].shot.item = 0x10;
    in[1].shot.flags = mp::kShotMove;
    in[1].shot.velocity[0] = 2.0f;
    in[1].shot.aux[0] = 1.0f;
    in[2].owner = 4;
    in[2].level = 0;
    in[2].shot.item = 0x0B;
    in[2].shot.flags = mp::kShotStop | mp::kShotBurst;
    in[2].shot.velocity2[2] = 3.0f;
    const Bytes b = mp::encode_shotx(in);
    CHECK(b[0] == mp::kShotXKind && b[1] == 3);
    // Header 2; each shot 12 and its position 12; 16 of aim and 2 of target; 12 and 16; 12.
    CHECK(b.size() == 2 + 3 * 24 + 18 + 28 + 12);

    std::vector<mp::ShotEntry> r;
    CHECK(mp::decode_shotx(b, r) && r.size() == 3);
    CHECK(r[0].owner == 2 && r[0].level == 5 && r[0].stamp == 0xF0000001u && r[0].shot.item == 0x0F);
    CHECK(r[0].shot.position[1] == 7.25f && r[0].shot.aim[3] == -0.25f && r[0].shot.target_key == 300);
    CHECK(r[0].shot.flags == 0);
    CHECK(r[1].level == -1 && r[1].shot.flags == mp::kShotMove && r[1].shot.velocity[0] == 2.0f);
    CHECK(r[1].shot.aux[0] == 1.0f && r[1].shot.target_key == -1);
    CHECK(r[2].shot.flags == (mp::kShotStop | mp::kShotBurst) && r[2].shot.velocity2[2] == 3.0f);

    // Any shorter is no SHOTX.
    for (std::size_t n = 0; n < b.size(); ++n) {
        CHECK(!mp::decode_shotx(Bytes(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(n)), r));
    }
    CHECK(!mp::decode_shotx(Bytes{mp::kShotKind, 0}, r));
    // More than a SHOTX holds, whole as they may be, is no SHOTX either.
    Bytes over = mp::encode_shotx(std::vector<mp::ShotEntry>(mp::kShotXMax));
    const std::size_t each = (over.size() - 2) / mp::kShotXMax;
    over.insert(over.end(), over.begin() + 2, over.begin() + 2 + static_cast<std::ptrdiff_t>(each));
    over[1] = static_cast<std::uint8_t>(mp::kShotXMax + 1);
    CHECK(!mp::decode_shotx(over, r));

    // The most a SHOTX holds, each with everything, fits a datagram; more are left for the next.
    std::vector<mp::ShotEntry> many(mp::kShotXMax + 3);
    for (mp::ShotEntry& e : many) {
        std::fill(e.shot.velocity, e.shot.velocity + 3, 1.0f);
        std::fill(e.shot.aim, e.shot.aim + 4, 1.0f);
        std::fill(e.shot.velocity2, e.shot.velocity2 + 3, 1.0f);
        std::fill(e.shot.aux, e.shot.aux + 4, 1.0f);
        e.shot.target_key = 1;
    }
    const Bytes full = mp::encode_shotx(many);
    CHECK(full.size() <= 1200 && full[1] == mp::kShotXMax);
    CHECK(mp::decode_shotx(full, r) && r.size() == mp::kShotXMax);

    // A MOVE is replaced by its track's next, and by nothing else.
    mp::ShotEntry a, c;
    a.owner = c.owner = 2;
    a.shot.flags = c.shot.flags = mp::kShotMove;
    a.shot.aux[0] = c.shot.aux[0] = 1.0f;
    CHECK(mp::shot_replaces(c, a));
    c.shot.aux[0] = 2.0f;
    CHECK(!mp::shot_replaces(c, a));
    c.shot.aux[0] = 1.0f;
    c.owner = 3;
    CHECK(!mp::shot_replaces(c, a));
    c.owner = 2;
    c.shot.flags = mp::kShotStop;
    CHECK(!mp::shot_replaces(c, a));
    c.shot.flags = mp::kShotMove;
    a.shot.flags = mp::kShotStart;
    CHECK(!mp::shot_replaces(c, a));
}


void held_item_animation_runs_on() {
    // A swing: the wrench (seq 3) a key every 2 ticks (speed 0.5 a key), a pose every 50 ms.
    auto swing = [](int ms) {
        mp::Pose p = pose_at(0.0f);
        p.seq = 7;
        p.frame = 2;
        p.next_frame = 3;
        p.held_count = 2;
        p.held[0] = piece(0, 0, 8, 0x47, 5);
        const double place = ms * 60.0 / 1000.0 * 0.5;  // keys since the swing began
        p.held[0].seq = 3;
        p.held[0].frame = static_cast<std::uint32_t>(place);
        p.held[0].next_frame = p.held[0].frame + 1;
        p.held[0].anim_time = static_cast<float>(place - p.held[0].frame);
        p.held[0].anim_speed = 1.0f;
        p.held[1] = piece(3, 0, 2, 0x25F, 2);  // the pack on the back: still
        p.held[1].seq = 0;
        p.held[1].frame = p.held[1].next_frame = 0;
        p.held[1].anim_time = 0.0f;
        return p;
    };

    mp::Interpolator in;
    std::uint16_t seq = 0;
    double last = -1.0;
    bool runs_on = true;
    int between_keys = 0;

    for (int t = 0; t <= 500; ++t) {
        if (t % 50 == 0) {
            in.add(swing(t), ++seq, t, t + 30, false);
        }

        if (t % 16 == 0 && t >= 200) {
            mp::Pose p;
            CHECK(in.at(t + 30, p) && p.held_count == 2);
            const double place = p.held[0].frame + static_cast<double>(p.held[0].anim_time);
            runs_on = runs_on && place >= last - 1e-6;
            // Not a pose's own key: one the frames between two poses make.
            const double ms_place = std::fmod(place / (60.0 / 1000.0 * 0.5), 50.0);
            between_keys += ms_place > 1.0 && ms_place < 49.0 ? 1 : 0;
            last = place;
            CHECK(p.held[1].frame == 0 && p.held[1].anim_time == 0.0f && p.held[1].oclass == 0x25F);
        }
    }

    CHECK(runs_on && last > 3.0 && between_keys > 5);

    // By hand: half way between keys 2.0 and 3.5 of the swing is 2.75.
    mp::Interpolator two;
    mp::Pose a = swing(0), b = swing(0);
    a.held[0].frame = 2;
    a.held[0].next_frame = 3;
    a.held[0].anim_time = 0.0f;
    b.held[0].frame = 3;
    b.held[0].next_frame = 4;
    b.held[0].anim_time = 0.5f;
    b.position[0] = 1.0f;
    two.add(a, 1, 0, 0, false);
    two.add(b, 2, 100, 100, false);
    mp::Pose mid;
    CHECK(two.at(50.0 + two.delay_ms(), mid));
    const double mid_place = mid.held[0].frame + static_cast<double>(mid.held[0].anim_time);
    CHECK(std::fabs(mid_place - 2.75) < 0.05 && mid.held[0].next_frame == 3);

    // Another seq in the later pose (the swing ends, the wrench is put away): no running on, the
    // nearer pose's.
    mp::Interpolator other;
    b.held[0].seq = 1;
    b.held[0].frame = 0;
    b.held[0].anim_time = 0.0f;
    other.add(a, 1, 0, 0, false);
    other.add(b, 2, 100, 100, false);
    CHECK(other.at(30.0 + other.delay_ms(), mid) && mid.held[0].seq == 3 && mid.held[0].frame == 2
          && mid.held[0].anim_time == 0.0f);
    CHECK(other.at(70.0 + other.delay_ms(), mid) && mid.held[0].seq == 1 && mid.held[0].frame == 0);

    // A thrown wrench (loose in both): along from one place to the other.
    mp::Interpolator thrown;
    a = swing(0);
    b = swing(0);
    a.held[0].flags = b.held[0].flags = mp::kHeldLoose;
    a.held[0].position[0] = 10.0f;
    b.held[0].position[0] = 20.0f;
    thrown.add(a, 1, 0, 0, false);
    thrown.add(b, 2, 100, 100, false);
    CHECK(thrown.at(50.0 + thrown.delay_ms(), mid) && std::fabs(mid.held[0].position[0] - 15.0f) < 0.5f);
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
    // A delay behind: the 30 ms in transit and a margin of at least 100 ms past it, about 1.3
    // units back.
    CHECK(std::fabs(in.delay_ms() - 130.0) < 15.0);
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
            // From the second frame shown (the first is where it starts).
            if (t > 216) {
                worst_jump = std::max(worst_jump, std::fabs(p.position[0] - last_x));
            }
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


// A thrown wrench past the newest pose: on along its last step, as the body along its velocity, and
// no further than it; a piece on the body stays where its pose has it.
void a_thrown_wrench_runs_on() {
    mp::Interpolator in;
    mp::Pose a = pose_at(0.0f, 0.0f), b = pose_at(0.0f, 0.0f);
    a.held_count = b.held_count = 2;
    a.held[0] = piece(0, 0, 8, 0x47, 5);
    b.held[0] = a.held[0];
    a.held[0].flags = b.held[0].flags = mp::kHeldLoose;
    a.held[0].position[0] = 10.0f;
    b.held[0].position[0] = 11.0f;  // 20 units a second
    a.held[1] = piece(1, 0, 8, 0x48, 5);
    b.held[1] = a.held[1];
    a.held[1].position[0] = b.held[1].position[0] = 0.5f;
    in.add(a, 1, 0, 0, false);
    in.add(b, 2, 50, 50, false);
    mp::Pose p;
    in.at(150, p);
    in.at(600, p);  // far past: 150 ms on along it
    CHECK(std::fabs(p.held[0].position[0] - 14.0f) < 0.01f);
    CHECK(p.held[1].position[0] == 0.5f);
    in.at(900, p);
    CHECK(std::fabs(p.held[0].position[0] - 14.0f) < 0.01f);

    // Just thrown, with no step yet: where its pose has it.
    mp::Interpolator first;
    a.held[0].flags = 0;
    first.add(a, 1, 0, 0, false);
    first.add(b, 2, 50, 50, false);
    first.at(150, p);
    first.at(600, p);
    CHECK(p.held[0].position[0] == 11.0f);
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
    // On from where it stood (the last pose before the silence, just before this one).
    CHECK(in.size() == 2 && in.newest(p) && p.position[0] == 0.2f);
    in.add(pose_at(0.3f), 65535, 2100, 2100, false);  // 1 - 65535 wraps: older, dropped
    CHECK(in.size() == 2);

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
}  // namespace

void run_poses_tests() {
    pose_bytes();
    held_and_shot_bytes();
    shotx_bytes();
    held_item_animation_runs_on();
    a_steady_stream_plays_smoothly();
    repeats_late_poses_and_teleports();
    a_stall_then_a_burst();
    past_the_newest_pose();
    a_thrown_wrench_runs_on();
    a_stop_is_not_overshot();
    a_count_started_again_and_the_wrap();
    recorded_poses_replay_smoothly();
}

}  // namespace mpt
