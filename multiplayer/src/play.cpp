/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The players' poses on the wire and the buffer they are played back from (play.hpp).
 */
#include "play.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mp {
namespace {

constexpr float kPi = 3.14159265358979f;

std::uint32_t float_bits(float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

float bits_float(std::uint32_t u) {
    float f = 0;
    std::memcpy(&f, &u, sizeof f);
    return f;
}

/* A float read back: a non-finite one (from a broken sender) as 0. */
float finite(float f) {
    return std::isfinite(f) ? f : 0.0f;
}

std::int16_t clamp16(double v) {
    return static_cast<std::int16_t>(std::clamp(v, -32768.0, 32767.0));
}

double distance(const Pose& a, const Pose& b) {
    const double dx = a.position[0] - b.position[0];
    const double dy = a.position[1] - b.position[1];
    const double dz = a.position[2] - b.position[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double speed(const Pose& p) {
    return std::sqrt(p.velocity[0] * p.velocity[0] + p.velocity[1] * p.velocity[1]
                     + p.velocity[2] * p.velocity[2]);
}

/* The animation `s` of the way from `a` to `b`, in `out` (a pose's, or a piece's: the same fields).
 * Within one seq, neither blending, the place in it (key and time to the next key) runs on from
 * a's to b's; otherwise (a new seq, a blend, a seq that came round to its start, whose keys are not
 * known here) out keeps the nearer one's. */
template <typename T>
void run_on(const T& a, const T& b, bool blending, double s, T& out) {
    if (a.seq != b.seq || blending) {
        return;
    }

    const double from = a.frame + static_cast<double>(a.anim_time);
    const double to = b.frame + static_cast<double>(b.anim_time);

    if (to < from) {
        return;
    }

    const double place = from + (to - from) * s;
    out.frame = static_cast<std::uint32_t>(place);
    out.anim_time = static_cast<float>(place - out.frame);
    out.next_frame = out.frame == b.frame ? b.next_frame : out.frame == a.frame ? a.next_frame : out.frame + 1;
    out.anim_speed = static_cast<float>(a.anim_speed + (b.anim_speed - a.anim_speed) * s);
}

/* The piece of `p` that is `piece` (the same slot, part and class), or none. */
const HeldPiece* same_piece(const Pose& p, const HeldPiece& piece) {
    for (std::uint32_t i = 0; i < p.held_count && i < kHeldMax; ++i) {
        const HeldPiece& h = p.held[i];

        if (h.slot == piece.slot && h.part == piece.part && h.oclass == piece.oclass) {
            return &h;
        }
    }

    return nullptr;
}

/* The body's animation, and each piece's that both poses have (a swing, a shot, the glove's
 * throw): the way from a to b, as the body's; a piece off the body (a thrown wrench) moves along it
 * too. */
void animate(const Pose& a, const Pose& b, double s, Pose& out) {
    run_on(a, b, ((a.flags | b.flags) & kPoseBlending) != 0, s, out);

    for (std::uint32_t i = 0; i < out.held_count && i < kHeldMax; ++i) {
        HeldPiece& piece = out.held[i];
        const HeldPiece* pa = same_piece(a, piece);
        const HeldPiece* pb = same_piece(b, piece);

        if (pa == nullptr || pb == nullptr) {
            continue;
        }

        run_on(*pa, *pb, ((pa->flags | pb->flags) & kHeldBlending) != 0, s, piece);

        if ((pa->flags & pb->flags & kHeldLoose) != 0) {
            for (int c = 0; c < 3; ++c) {
                piece.position[c] = static_cast<float>(pa->position[c] + (pb->position[c] - pa->position[c]) * s);
                piece.rotation[c] = lerp_angle(pa->rotation[c], pb->rotation[c], static_cast<float>(s));
            }
        }
    }
}

/* A piece off the body has no velocity of its own: past the newest pose it goes on along its last
 * step, for `ahead` ms of a step `span` ms long, as the body does along its velocity. */
void carry_loose_on(const Pose& before, double span, double ahead, Pose& out) {
    if (span <= 0.0) {
        return;
    }

    for (std::uint32_t i = 0; i < out.held_count && i < kHeldMax; ++i) {
        HeldPiece& piece = out.held[i];
        const HeldPiece* was = same_piece(before, piece);

        if ((piece.flags & kHeldLoose) == 0 || was == nullptr || (was->flags & kHeldLoose) == 0) {
            continue;
        }

        for (int c = 0; c < 3; ++c) {
            piece.position[c] += static_cast<float>((piece.position[c] - was->position[c]) * ahead / span);
        }
    }
}

}  // namespace

Bytes encode_pose(const PoseMessage& m) {
    const Pose& p = m.pose;
    Writer w;
    w.u8(kPoseKind).i32(m.owner).u8(static_cast<std::uint8_t>(m.sequence >> 8))
        .u8(static_cast<std::uint8_t>(m.sequence))
        .i64(m.stamp_ms)
        .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(p.level, -1, 127))))
        .u8(static_cast<std::uint8_t>((p.flags & kPoseBlending) | (m.teleport ? kWireTeleport : 0)
                                  | (m.scene ? kWireScene : 0)
                                  | ((p.flags & kPoseAirborne) != 0 ? kWireAirborne : 0)));

    for (const float f : p.position) {
        w.i32(static_cast<std::int32_t>(float_bits(f)));
    }

    for (const float f : p.rotation) {
        w.i32(static_cast<std::int32_t>(float_bits(f)));
    }

    for (const float f : p.velocity) {
        w.i32(static_cast<std::int32_t>(float_bits(f)));
    }

    // The 16-bit fields, high byte first.
    const std::int16_t shorts[3] = {clamp16(p.state), clamp16(p.health), clamp16(p.item)};

    for (const std::int16_t s : shorts) {
        w.u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(s) >> 8))
            .u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(s)));
    }

    const std::int16_t speed = clamp16(std::round(p.anim_speed * 1000.0));
    w.u8(static_cast<std::uint8_t>(std::min<std::uint32_t>(p.seq, 255)))
        .u8(static_cast<std::uint8_t>(std::min<std::uint32_t>(p.frame, 255)))
        .u8(static_cast<std::uint8_t>(std::min<std::uint32_t>(p.next_frame, 255)))
        .u8(static_cast<std::uint8_t>(std::clamp(std::lround(p.anim_time * 255.0f), 0L, 255L)))
        .u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(speed) >> 8))
        .u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(speed)));

    // What it wears and holds.
    const std::size_t count = std::min<std::size_t>(p.held_count, kHeldMax);
    w.u8(static_cast<std::uint8_t>(count));

    for (std::size_t i = 0; i < count; ++i) {
        const HeldPiece& h = p.held[i];
        const std::int16_t oclass = clamp16(h.oclass);
        const std::int16_t piece_speed = clamp16(std::round(h.anim_speed * 1000.0));
        w.u8(static_cast<std::uint8_t>(std::clamp(h.slot, 0, 255)))
            .u8(static_cast<std::uint8_t>(std::clamp(h.part, 0, 255)))
            .u8(static_cast<std::uint8_t>(std::clamp(h.item, 0, 255)))
            .u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(oclass) >> 8))
            .u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(oclass)))
            .u8(static_cast<std::uint8_t>(std::clamp(h.joint, 0, 255)))
            .u8(static_cast<std::uint8_t>(h.flags & (kHeldHidden | kHeldBlending | kHeldLoose)))
            .u8(static_cast<std::uint8_t>(std::min<std::uint32_t>(h.seq, 255)))
            .u8(static_cast<std::uint8_t>(std::min<std::uint32_t>(h.frame, 255)))
            .u8(static_cast<std::uint8_t>(std::min<std::uint32_t>(h.next_frame, 255)))
            .u8(static_cast<std::uint8_t>(std::clamp(std::lround(h.anim_time * 255.0f), 0L, 255L)))
            .u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(piece_speed) >> 8))
            .u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(piece_speed)));

        if ((h.flags & kHeldLoose) != 0) {
            for (const float f : h.position) {
                w.i32(static_cast<std::int32_t>(float_bits(f)));
            }

            for (const float f : h.rotation) {
                w.i32(static_cast<std::int32_t>(float_bits(f)));
            }
        }
    }

    if (m.world_ack) {
        w.u8(static_cast<std::uint8_t>(m.world_newest >> 8)).u8(static_cast<std::uint8_t>(m.world_newest));
        w.i32(static_cast<std::int32_t>(m.world_bits));
    }

    return w.bytes();
}

bool decode_pose(const Bytes& bytes, PoseMessage& out) {
    if (bytes.size() < kPoseBytes || bytes[0] != kPoseKind) {
        return false;
    }

    Reader r(bytes.data(), bytes.size());
    r.u8();
    out = PoseMessage{};
    out.owner = r.i32();
    out.sequence = static_cast<std::uint16_t>((r.u8() << 8) | r.u8());
    out.stamp_ms = r.i64();
    Pose& p = out.pose;
    p.level = static_cast<std::int8_t>(r.u8());
    const std::uint8_t flags = r.u8();
    p.flags = (flags & kPoseBlending) | ((flags & kWireAirborne) != 0 ? kPoseAirborne : 0);
    out.teleport = (flags & kWireTeleport) != 0;
    out.scene = (flags & kWireScene) != 0;

    for (float& f : p.position) {
        f = finite(bits_float(static_cast<std::uint32_t>(r.i32())));
    }

    for (float& f : p.rotation) {
        f = finite(bits_float(static_cast<std::uint32_t>(r.i32())));
    }

    for (float& f : p.velocity) {
        f = finite(bits_float(static_cast<std::uint32_t>(r.i32())));
    }

    std::int32_t* shorts[3] = {&p.state, &p.health, &p.item};

    for (std::int32_t* s : shorts) {
        const auto high = static_cast<std::uint16_t>(r.u8() << 8);
        *s = static_cast<std::int16_t>(high | r.u8());
    }

    p.seq = r.u8();
    p.frame = r.u8();
    p.next_frame = r.u8();
    p.anim_time = static_cast<float>(r.u8()) / 255.0f;
    const auto high = static_cast<std::uint16_t>(r.u8() << 8);
    p.anim_speed = static_cast<float>(static_cast<std::int16_t>(high | r.u8())) / 1000.0f;

    if (!r.ok()) {
        return false;
    }

    // An older sender's pose ends here: it wears nothing.
    if (r.left() == 0) {
        return true;
    }

    const std::size_t count = std::min<std::size_t>(r.u8(), kHeldMax);

    for (std::size_t i = 0; i < count; ++i) {
        HeldPiece& h = p.held[i];
        h.slot = r.u8();
        h.part = r.u8();
        h.item = r.u8();
        const auto oclass_high = static_cast<std::uint16_t>(r.u8() << 8);
        h.oclass = static_cast<std::int16_t>(oclass_high | r.u8());
        h.joint = r.u8();
        h.flags = r.u8() & (kHeldHidden | kHeldBlending | kHeldLoose);
        h.seq = r.u8();
        h.frame = r.u8();
        h.next_frame = r.u8();
        h.anim_time = static_cast<float>(r.u8()) / 255.0f;
        const auto speed_high = static_cast<std::uint16_t>(r.u8() << 8);
        h.anim_speed = static_cast<float>(static_cast<std::int16_t>(speed_high | r.u8())) / 1000.0f;

        if ((h.flags & kHeldLoose) != 0) {
            for (float& f : h.position) {
                f = finite(bits_float(static_cast<std::uint32_t>(r.i32())));
            }

            for (float& f : h.rotation) {
                f = finite(bits_float(static_cast<std::uint32_t>(r.i32())));
            }
        }
    }

    // A tail cut short: the pose stands, wearing nothing.
    p.held_count = r.ok() ? static_cast<std::uint32_t>(count) : 0;

    if (r.ok() && r.left() >= kPoseAckBytes) {
        const auto newest_high = static_cast<std::uint16_t>(r.u8() << 8);
        out.world_newest = static_cast<std::uint16_t>(newest_high | r.u8());
        out.world_bits = static_cast<std::uint32_t>(r.i32());
        out.world_ack = true;
    }

    return true;
}

Bytes encode_shot(const ShotMessage& m) {
    Writer w;
    w.u8(kShotKind).i32(m.owner)
        .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(m.level, -1, 127))));
    const std::int16_t item = clamp16(m.shot.item);
    w.u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(item) >> 8))
        .u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(item)));

    for (const float f : m.shot.position) {
        w.i32(static_cast<std::int32_t>(float_bits(f)));
    }

    for (const float f : m.shot.velocity) {
        w.i32(static_cast<std::int32_t>(float_bits(f)));
    }

    for (const float f : m.shot.aim) {
        w.i32(static_cast<std::int32_t>(float_bits(f)));
    }

    return w.bytes();
}

bool decode_shot(const Bytes& bytes, ShotMessage& out) {
    if (bytes.size() < kShotBytes || bytes[0] != kShotKind) {
        return false;
    }

    Reader r(bytes.data(), bytes.size());
    r.u8();
    out = ShotMessage{};
    out.owner = r.i32();
    out.level = static_cast<std::int8_t>(r.u8());
    const auto high = static_cast<std::uint16_t>(r.u8() << 8);
    out.shot.item = static_cast<std::int16_t>(high | r.u8());

    for (float& f : out.shot.position) {
        f = finite(bits_float(static_cast<std::uint32_t>(r.i32())));
    }

    for (float& f : out.shot.velocity) {
        f = finite(bits_float(static_cast<std::uint32_t>(r.i32())));
    }

    for (float& f : out.shot.aim) {
        f = finite(bits_float(static_cast<std::uint32_t>(r.i32())));
    }

    return r.ok();
}

namespace {

constexpr std::uint8_t kHasVelocity = 0x1;
constexpr std::uint8_t kHasAim = 0x2;
constexpr std::uint8_t kHasVelocity2 = 0x4;
constexpr std::uint8_t kHasAux = 0x8;
constexpr std::uint8_t kHasTarget = 0x10;

template <std::size_t N>
bool any_set(const float (&values)[N]) {
    return std::any_of(values, values + N, [](float f) { return f != 0.0f; });
}

template <std::size_t N>
void write_floats(Writer& w, const float (&values)[N]) {
    for (const float f : values) {
        w.i32(static_cast<std::int32_t>(float_bits(f)));
    }
}

template <std::size_t N>
void read_floats(Reader& r, float (&values)[N]) {
    for (float& f : values) {
        f = bits_float(static_cast<std::uint32_t>(r.i32()));
    }
}

}  // namespace

Bytes encode_shotx(const std::vector<ShotEntry>& shots) {
    Writer w;
    const std::size_t count = std::min(shots.size(), kShotXMax);
    w.u8(kShotXKind).u8(static_cast<std::uint8_t>(count));

    for (std::size_t i = 0; i < count; ++i) {
        const ShotEntry& e = shots[i];
        const Shot& s = e.shot;
        const std::uint8_t has =
            static_cast<std::uint8_t>((any_set(s.velocity) ? kHasVelocity : 0) | (any_set(s.aim) ? kHasAim : 0)
                                      | (any_set(s.velocity2) ? kHasVelocity2 : 0) | (any_set(s.aux) ? kHasAux : 0)
                                      | (s.target_key >= 0 ? kHasTarget : 0));
        w.i32(e.owner)
            .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(e.level, -1, 127))))
            .u8(static_cast<std::uint8_t>(std::clamp(s.item, 0, 255)))
            .u8(static_cast<std::uint8_t>(s.flags))
            .u8(has)
            .i32(static_cast<std::int32_t>(e.stamp));
        write_floats(w, s.position);

        if ((has & kHasVelocity) != 0) {
            write_floats(w, s.velocity);
        }

        if ((has & kHasAim) != 0) {
            write_floats(w, s.aim);
        }

        if ((has & kHasVelocity2) != 0) {
            write_floats(w, s.velocity2);
        }

        if ((has & kHasAux) != 0) {
            write_floats(w, s.aux);
        }

        if ((has & kHasTarget) != 0) {
            const std::int16_t key = clamp16(s.target_key);
            w.u8(static_cast<std::uint8_t>(static_cast<std::uint16_t>(key) >> 8)).u8(static_cast<std::uint8_t>(key));
        }
    }

    return w.bytes();
}

bool decode_shotx(const Bytes& bytes, std::vector<ShotEntry>& out) {
    out.clear();

    if (bytes.size() < 2 || bytes[0] != kShotXKind) {
        return false;
    }

    Reader r(bytes.data() + 1, bytes.size() - 1);
    const std::uint8_t count = r.u8();

    if (count > kShotXMax) {
        return false;
    }

    for (std::uint8_t i = 0; i < count; ++i) {
        ShotEntry e;
        Shot& s = e.shot;
        e.owner = r.i32();
        e.level = static_cast<std::int8_t>(r.u8());
        s.item = r.u8();
        s.flags = r.u8();
        const std::uint8_t has = r.u8();
        e.stamp = static_cast<std::uint32_t>(r.i32());
        read_floats(r, s.position);

        if ((has & kHasVelocity) != 0) {
            read_floats(r, s.velocity);
        }

        if ((has & kHasAim) != 0) {
            read_floats(r, s.aim);
        }

        if ((has & kHasVelocity2) != 0) {
            read_floats(r, s.velocity2);
        }

        if ((has & kHasAux) != 0) {
            read_floats(r, s.aux);
        }

        if ((has & kHasTarget) != 0) {
            const std::uint8_t high = r.u8();
            s.target_key = static_cast<std::int16_t>(high << 8 | r.u8());
        }

        if (!r.ok()) {
            out.clear();
            return false;
        }

        out.push_back(e);
    }

    return true;
}

bool shot_replaces(const ShotEntry& newer, const ShotEntry& older) {
    return newer.owner == older.owner && (newer.shot.flags & kShotMove) != 0 && (older.shot.flags & kShotMove) != 0
           && newer.shot.aux[0] == older.shot.aux[0];
}

bool sequence_newer(std::uint16_t a, std::uint16_t b) {
    const auto d = static_cast<std::uint16_t>(a - b);
    return d != 0 && d < 0x8000;
}

float lerp_angle(float a, float b, float s) {
    float d = std::fmod(b - a, 2.0f * kPi);

    // The short way round.
    if (d > kPi) {
        d -= 2.0f * kPi;
    } else if (d < -kPi) {
        d += 2.0f * kPi;
    }

    return a + d * s;
}

const char* shown_name(Shown s) {
    switch (s) {
        case Shown::kBefore:
            return "before";
        case Shown::kBetween:
            return "between";
        case Shown::kAhead:
            return "ahead";
        case Shown::kHeld:
            return "held";
        case Shown::kJump:
            return "jump";
        case Shown::kNone:
            break;
    }

    return "none";
}

std::string counts_text(const int by[6]) {
    char text[128];
    std::snprintf(text, sizeof text, "between %d ahead %d held %d jump %d before %d",
                  by[static_cast<int>(Shown::kBetween)], by[static_cast<int>(Shown::kAhead)],
                  by[static_cast<int>(Shown::kHeld)], by[static_cast<int>(Shown::kJump)],
                  by[static_cast<int>(Shown::kBefore)]);
    return text;
}

std::string Interpolator::describe() const {
    char text[192];
    std::snprintf(text, sizeof text, "delay %.0f transit %.0f jitter %.1f interval %.0f loss %.3f ahead %.0f depth %zu",
                  delay_ms(), transit_, jitter_, interval_, loss_, ahead_ms(), samples_.size());
    return text;
}

void Interpolator::reset() {
    samples_.clear();
    cursor_set_ = false;
    jumped_ = false;
    shown_ = Shown::kNone;
    shown_set_ = false;
    std::fill(offset_, offset_ + 3, 0.0f);
}

void Interpolator::add(
    const Pose& pose, std::uint16_t sequence, double time_ms, double arrival_ms, bool teleport
) {
    // Late, repeated or reordered: the newest stands. After a silence (the sender gone and back,
    // its count started again) whatever comes is taken.
    const bool silent = !samples_.empty() && arrival_ms - last_arrival_ > kStallMs;

    if (!samples_.empty() && !silent && !sequence_newer(sequence, sequence_)) {
        return;
    }

    // The link: how long a pose is in transit (in this clock, so the clock estimates' error is in
    // it too) and how much that varies. A far outlier (a burst after a stall) moves the average
    // by no more than a few jitters.
    const double transit = arrival_ms - time_ms;

    if (!link_set_) {
        link_set_ = true;
        transit_ = transit;
        jitter_ = 0.0;
    } else {
        const double dev = transit - transit_;
        jitter_ += 0.1 * (std::fabs(dev) - jitter_);
        const double bound = 4.0 * jitter_ + 20.0;
        transit_ += 0.05 * std::clamp(dev, -bound, bound);
    }

    const bool snap = teleport || samples_.empty() || pose.level != samples_.back().pose.level
                      || distance(pose, samples_.back().pose) > kSnapDistance;

    if (snap) {
        samples_.clear();
        cursor_set_ = false;
    } else if (silent) {
        // Back after a silence (a moby WORLD did not send while it stood, or a sender gone a
        // while): on from where it stood, as if it stood until just before this pose.
        Sample base = samples_.back();
        samples_.clear();
        base.time = time_ms - interval_;
        std::fill(base.pose.velocity, base.pose.velocity + 3, 0.0f);
        samples_.push_back(base);
        cursor_set_ = true;
        cursor_ = base.time;
    } else {
        // A stamp not after the newest (a sender's clock step) is moved just past it.
        const double gap = time_ms - samples_.back().time;
        if (gap <= 0.0) {
            time_ms = samples_.back().time + 1.0;
        } else if (gap < (derive_ ? kMobyPauseGapMs : kPauseGapMs)) {
            // The send interval, learned from the gaps of a steady stream (not a pause).
            interval_ += 0.1 * (gap - interval_);
        }
    }

    // The poses lost on the way: the gaps in the sequence (a pose's own; WORLD's is its stamp's,
    // and a moby it leaves out stood still).
    if (!derive_ && !samples_.empty() && !silent && !snap) {
        const double missing = static_cast<std::uint16_t>(sequence - sequence_) - 1.0;
        loss_ += 0.05 * (std::min(missing, 4.0) / (missing + 1.0) - loss_);
    }

    last_arrival_ = arrival_ms;
    sequence_ = sequence;
    samples_.push_back({time_ms, pose});

    if (samples_.size() > kBufferSamples) {
        samples_.pop_front();
    }

    if (derive_) {
        derive_velocities();
    }
}

void Interpolator::derive_velocities() {
    const std::size_t n = samples_.size();

    if (n < 2) {
        if (n == 1) {
            std::fill(samples_[0].pose.velocity, samples_[0].pose.velocity + 3, 0.0f);
        }
        return;
    }

    Sample& a = samples_[n - 2];
    Sample& b = samples_[n - 1];
    const double h1 = (b.time - a.time) / 1000.0;

    for (int i = 0; i < 3; ++i) {
        const double d1 = h1 > 0.0 ? (b.pose.position[i] - a.pose.position[i]) / h1 : 0.0;
        // The newest: along its last step (it is the end of the curve until the next one comes).
        b.pose.velocity[i] = static_cast<float>(d1);

        if (n == 2) {
            a.pose.velocity[i] = static_cast<float>(d1);
            continue;
        }

        // The one before: the weighted harmonic mean of the steps either side, none where they go
        // opposite ways (Fritsch and Carlson's monotone curve: never past a pose and back).
        const Sample& z = samples_[n - 3];
        const double h0 = (a.time - z.time) / 1000.0;
        const double d0 = h0 > 0.0 ? (a.pose.position[i] - z.pose.position[i]) / h0 : 0.0;
        double m = 0.0;

        if (d0 * d1 > 0.0) {
            m = 3.0 * (h0 + h1) / ((2.0 * h1 + h0) / d0 + (h1 + 2.0 * h0) / d1);
        }

        a.pose.velocity[i] = static_cast<float>(m);
    }
}

bool Interpolator::at(double now_ms, Pose& out) {
    const double dt = shown_set_ ? std::max(0.0, now_ms - last_now_) : 0.0;
    const Shown before = shown_;

    if (!place(now_ms, out)) {
        shown_set_ = false;
        return false;
    }

    // Past the newest pose (on along a velocity, or held), then back between two: on from where
    // it was shown by the step it was taking, eased onto the curve, not put there at once. A jump
    // or a new start is put there.
    const bool back = (before == Shown::kAhead || before == Shown::kHeld) && shown_ == Shown::kBetween;
    const float ease = static_cast<float>(std::exp(-dt / kCatchUpMs));

    if (!shown_set_ || shown_ == Shown::kJump || !cursor_set_) {
        std::fill(offset_, offset_ + 3, 0.0f);
    } else if (back) {
        for (int i = 0; i < 3; ++i) {
            offset_[i] = shown_at_[i] + shown_step_[i] - out.position[i];
        }
    }

    if (std::sqrt(offset_[0] * offset_[0] + offset_[1] * offset_[1] + offset_[2] * offset_[2]) > kSnapDistance) {
        std::fill(offset_, offset_ + 3, 0.0f);
    }

    for (int i = 0; i < 3; ++i) {
        if (!back) {
            offset_[i] *= ease;
        }
        out.position[i] += offset_[i];
        shown_step_[i] = shown_set_ ? out.position[i] - shown_at_[i] : 0.0f;
        shown_at_[i] = out.position[i];
    }

    shown_set_ = true;
    ++counts_.by[static_cast<int>(shown_)];
    return true;
}

double Interpolator::wanted_ms() const {
    // Past the transit: the next pose's interval (one more without velocities: the one after a
    // pose makes its velocity) and the jitter, and a frame. Whatever the stream's rate (a moby's
    // goes with its priority, and all of WORLD slows while the party fires), at least two intervals:
    // a lost pose leaves a gap of two. A figure's third comes by 5 % lost; a moby's, of the three
    // the longer, by 25 %: at 5 % a third costs 100 ms of delay and shows nothing.
    const double sends = derive_ ? 2.0 + std::min(1.0, 4.0 * loss_) : 1.0 + std::min(1.0, 20.0 * loss_);
    const double margin = std::clamp(std::max(sends * interval_ + 2.5 * jitter_ + 17.0, 2.0 * interval_), kMinBufferMs,
                                     derive_ ? kMaxMobyBufferMs : kMaxBufferMs);
    return std::clamp(transit_ + margin, 0.0, kMaxDelayMs);
}

double Interpolator::delay_ms() const {
    return delay_set_ ? delay_ : wanted_ms();
}

bool Interpolator::place(double now_ms, Pose& out) {
    jumped_ = false;

    if (samples_.empty()) {
        shown_ = Shown::kNone;
        return false;
    }

    // The delay eases to what the link asks: up in about kDelayRiseMs, down in about kDelayFallMs.
    const double dt = delay_set_ ? std::max(0.0, now_ms - last_now_) : 0.0;
    const double wanted = wanted_ms();

    if (!delay_set_) {
        delay_set_ = true;
        delay_ = wanted;
    } else {
        const double tau = wanted > delay_ ? kDelayRiseMs : kDelayFallMs;
        delay_ += (wanted - delay_) * std::min(1.0, dt / tau);
    }

    const double delay = delay_;
    const double newest = samples_.back().time;
    const double target = std::min(now_ms - delay, newest);

    // The cursor: put on its target at first and after a jump; otherwise run to it, a little
    // faster or slower (a deficit of 50 ms is made up at the most).
    if (!cursor_set_ || std::fabs(target - cursor_) > std::max(2.5 * delay, 300.0)) {
        jumped_ = cursor_set_;
        cursor_set_ = true;
        cursor_ = target;
    } else {
        const double rate = std::clamp(1.0 + (target - cursor_) / 500.0, kRateMin, kRateMax);
        cursor_ += std::max(0.0, now_ms - last_now_) * rate;
    }

    last_now_ = now_ms;
    cursor_ = std::min(cursor_, newest + kExtrapolateMs);

    // Before the first pose: it stands there.
    if (cursor_ <= samples_.front().time) {
        out = samples_.front().pose;
        shown_ = jumped_ ? Shown::kJump : Shown::kBefore;
        return true;
    }

    // Past the newest: on along its velocity, slowing in the last kFadeMs, then held; held at
    // once when it was slowing down (a stop: on along its last velocity it would go past where it
    // stops, then come back), and always for poses without velocities of their own.
    if (cursor_ >= newest) {
        out = samples_.back().pose;
        shown_ = jumped_ ? Shown::kJump : Shown::kHeld;

        if (samples_.size() >= 2) {
            const Sample& last = samples_[samples_.size() - 2];
            carry_loose_on(last.pose, newest - last.time, std::min(cursor_ - newest, kExtrapolateMs), out);
        }

        if (derive_) {
            std::fill(out.velocity, out.velocity + 3, 0.0f);
            return true;
        }

        if (samples_.size() >= 2 && speed(out) < 0.8 * speed(samples_[samples_.size() - 2].pose)) {
            return true;
        }

        const double ahead = std::min(cursor_ - newest, kExtrapolateMs);
        const double fade = kExtrapolateMs - kFadeMs;
        // The time moved: all of it until the fade, then less and less.
        double moved = ahead;
        if (ahead > fade) {
            const double f = ahead - fade;
            moved = fade + f - f * f / (2.0 * kFadeMs);
        }

        // Along an axis the last two poses did not move on, nothing: a standing Ratchet's
        // velocity says he falls (the game's gravity, which the ground takes back each tick).
        const Pose* before = samples_.size() >= 2 ? &samples_[samples_.size() - 2].pose : nullptr;

        for (int i = 0; i < 3; ++i) {
            if (before == nullptr || std::fabs(out.position[i] - before->position[i]) >= kStillStep) {
                out.position[i] += static_cast<float>(out.velocity[i] * moved / 1000.0);
            }
        }

        shown_ = jumped_ ? Shown::kJump : Shown::kAhead;
        return true;
    }

    // Between two: a, the last at or before the cursor, and b after it.
    std::size_t b = 1;
    while (b < samples_.size() && samples_[b].time <= cursor_) {
        ++b;
    }

    const Sample& sa = samples_[b - 1];
    const Sample& sb = samples_[b];
    const double span = sb.time - sa.time;
    const double s = span > 0.0 ? (cursor_ - sa.time) / span : 1.0;
    const double h = span / 1000.0;
    const double s2 = s * s;
    const double s3 = s2 * s;
    const double h00 = 2 * s3 - 3 * s2 + 1;
    const double h10 = s3 - 2 * s2 + s;
    const double h01 = -2 * s3 + 3 * s2;
    const double h11 = s3 - s2;

    out = s < 0.5 ? sa.pose : sb.pose;
    animate(sa.pose, sb.pose, s, out);

    for (int i = 0; i < 3; ++i) {
        // Along an axis the two poses did not move on, a straight line (it stands): not a bump
        // drawn through velocities that say otherwise (a standing Ratchet's says he falls).
        const bool still = std::fabs(sb.pose.position[i] - sa.pose.position[i]) < kStillStep;
        const double va = still ? 0.0 : sa.pose.velocity[i];
        const double vb = still ? 0.0 : sb.pose.velocity[i];
        out.position[i] = static_cast<float>(
            h00 * sa.pose.position[i] + h10 * h * va + h01 * sb.pose.position[i] + h11 * h * vb
        );
        out.velocity[i] =
            static_cast<float>(sa.pose.velocity[i] + (sb.pose.velocity[i] - sa.pose.velocity[i]) * s);
    }

    out.rotation[0] = lerp_angle(sa.pose.rotation[0], sb.pose.rotation[0], static_cast<float>(s));
    out.rotation[1] = lerp_angle(sa.pose.rotation[1], sb.pose.rotation[1], static_cast<float>(s));
    out.rotation[2] = lerp_angle(sa.pose.rotation[2], sb.pose.rotation[2], static_cast<float>(s));
    shown_ = jumped_ ? Shown::kJump : Shown::kBetween;

    // What is behind the cursor's pair is no longer needed (one older is kept).
    while (b > 2) {
        samples_.pop_front();
        --b;
    }

    return true;
}

}  // namespace mp
