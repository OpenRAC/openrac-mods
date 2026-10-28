/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The players' poses on the wire and the buffer they are played back from (play.hpp).
 */
#include "play.hpp"

#include <algorithm>
#include <cmath>
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

}  // namespace

Bytes encode_pose(const PoseMessage& m) {
    const Pose& p = m.pose;
    Writer w;
    w.u8(kPoseKind).i32(m.owner).u8(static_cast<std::uint8_t>(m.sequence >> 8))
        .u8(static_cast<std::uint8_t>(m.sequence))
        .i64(m.stamp_ms)
        .u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(p.level, -1, 127))))
        .u8(static_cast<std::uint8_t>((p.flags & kPoseBlending) | (m.teleport ? kWireTeleport : 0)
                                  | (m.scene ? kWireScene : 0)));

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
    p.flags = flags & kPoseBlending;
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

void Interpolator::reset() {
    samples_.clear();
    cursor_set_ = false;
    jumped_ = false;
}

void Interpolator::add(
    const Pose& pose, std::uint16_t sequence, double time_ms, double arrival_ms, bool teleport
) {
    // Late, repeated or reordered: the newest stands.
    if (!samples_.empty() && !sequence_newer(sequence, sequence_)) {
        return;
    }

    const bool restart = teleport || samples_.empty() || pose.level != samples_.back().pose.level
                         || distance(pose, samples_.back().pose) > kSnapDistance;

    if (restart) {
        samples_.clear();
        cursor_set_ = false;
    } else {
        // A stamp not after the newest (a sender's clock step) is moved just past it.
        const double gap = time_ms - samples_.back().time;
        if (gap <= 0.0) {
            time_ms = samples_.back().time + 1.0;
        } else if (gap < 200.0) {
            // The send interval, learned from the gaps of a steady stream (not a pause).
            interval_ += 0.1 * (gap - interval_);
        }

        // Jitter: how much the time in transit changes from one pose to the next.
        const double transit = arrival_ms - time_ms;
        jitter_ += 0.1 * (std::fabs(transit - last_transit_) - jitter_);
    }

    last_transit_ = arrival_ms - time_ms;
    last_arrival_ = arrival_ms;
    sequence_ = sequence;
    samples_.push_back({time_ms, pose});

    if (samples_.size() > kBufferSamples) {
        samples_.pop_front();
    }
}

double Interpolator::delay_ms() const {
    return std::clamp(2.5 * interval_ + 2.0 * jitter_, kMinDelayMs, kMaxDelayMs);
}

bool Interpolator::at(double now_ms, Pose& out) {
    jumped_ = false;

    if (samples_.empty()) {
        return false;
    }

    const double delay = delay_ms();
    const double newest = samples_.back().time;
    const double target = std::min(now_ms - delay, newest);

    // The cursor: put on its target at first and after a jump; otherwise run to it, a little
    // faster or slower.
    if (!cursor_set_ || std::fabs(target - cursor_) > 2.5 * delay) {
        jumped_ = cursor_set_;
        cursor_set_ = true;
        cursor_ = target;
    } else {
        const double deficit = (target - cursor_) / delay;
        const double rate = std::clamp(1.0 + 2.0 * deficit, kRateMin, kRateMax);
        cursor_ += std::max(0.0, now_ms - last_now_) * rate;
    }

    last_now_ = now_ms;
    cursor_ = std::min(cursor_, newest + kExtrapolateMs);

    // Before the first pose: it stands there.
    if (cursor_ <= samples_.front().time) {
        out = samples_.front().pose;
        return true;
    }

    // Past the newest: on along its velocity, slowing in the last kFadeMs, then held.
    if (cursor_ >= newest) {
        out = samples_.back().pose;
        const double ahead = std::min(cursor_ - newest, kExtrapolateMs);
        const double fade = kExtrapolateMs - kFadeMs;
        // The time moved: all of it until the fade, then less and less.
        double moved = ahead;
        if (ahead > fade) {
            const double f = ahead - fade;
            moved = fade + f - f * f / (2.0 * kFadeMs);
        }

        for (int i = 0; i < 3; ++i) {
            out.position[i] += static_cast<float>(out.velocity[i] * moved / 1000.0);
        }

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

    for (int i = 0; i < 3; ++i) {
        out.position[i] = static_cast<float>(
            h00 * sa.pose.position[i] + h10 * h * sa.pose.velocity[i] + h01 * sb.pose.position[i]
            + h11 * h * sb.pose.velocity[i]
        );
        out.velocity[i] =
            static_cast<float>(sa.pose.velocity[i] + (sb.pose.velocity[i] - sa.pose.velocity[i]) * s);
    }

    out.rotation[0] = lerp_angle(sa.pose.rotation[0], sb.pose.rotation[0], static_cast<float>(s));
    out.rotation[1] = lerp_angle(sa.pose.rotation[1], sb.pose.rotation[1], static_cast<float>(s));
    out.rotation[2] = lerp_angle(sa.pose.rotation[2], sb.pose.rotation[2], static_cast<float>(s));

    // What is behind the cursor's pair is no longer needed (one older is kept).
    while (b > 2) {
        samples_.pop_front();
        --b;
    }

    return true;
}

}  // namespace mp
