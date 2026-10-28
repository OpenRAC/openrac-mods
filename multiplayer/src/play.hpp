/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The players' poses (play.cpp): a pose as the game gives it (the SDK's openrac_pose, without the
 * SDK: the tests need no game), its bytes on the wire, and the buffer a remote player's poses go
 * through to be shown smoothly (INTERPOLATION.md).
 *
 * The wire, the POSE kind of the game's DATA (coop.hpp), sent as state (UDP where offered), each
 * game about 20 a second:
 *   kind u8 (7), owner i32 (whose Ratchet), sequence u16, stamp i64 (the server's clock, ms),
 *   level i8, flags u8 (blending 1, teleport 2, in a scene 4), position 3 x f32, rotation 3 x f32,
 *   velocity 3 x f32, state i16, health i16, item i16, seq u8, frame u8, next frame u8,
 *   anim time u8 (0 to 255 for 0 to 1), anim speed i16 (thousandths)
 * 65 bytes. Integers big-endian as the server's; floats their IEEE bits as a u32. Then what the
 * Ratchet wears and holds (since 0.7.0; a pose without it is read as wearing nothing):
 *   count u8, then a piece each: slot u8, part u8, item u8, class i16, joint u8, flags u8 (hidden
 *   1, blending 2, loose 4), seq u8, frame u8, next frame u8, anim time u8, anim speed i16, and
 *   for a loose piece its position 3 x f32 and rotation 3 x f32
 * (13 bytes a piece, 37 a loose one).
 *
 * A SHOT (coop.hpp's kinds; reliable): kind u8 (19), owner i32, level i8, item i16, position
 * 3 x f32, velocity 3 x f32, aim 4 x f32: 48 bytes. The figure fires it as far behind as its poses
 * are played.
 *
 * The buffer. Each pose is stamped with the sender's estimate of the server's clock; the receiver
 * turns it into its own clock and plays the poses back a delay behind (about 100 ms at 20 a second,
 * more with jitter), between two of them: the position along a cubic Hermite curve through their
 * velocities (a jump stays an arc), the yaw the short way round, the animation and state from the
 * nearer one. The playback cursor runs up to 15 % fast or slow to keep its delay, so a change of
 * delay never jumps; far behind (a stall, then a burst), it jumps. Past the newest pose it goes on
 * along its velocity for at most 150 ms, then holds. A teleport (the sender's flag, a level change,
 * or a step of more than kSnapDistance) starts the buffer again: the figure is put there.
 */
#ifndef MULTIPLAYER_PLAY_H
#define MULTIPLAYER_PLAY_H

#include <cstddef>
#include <cstdint>
#include <deque>

#include "protocol.hpp"

namespace mp {

/* openrac_pose.flags: blending into `seq`. And the wire's teleport flag. */
constexpr std::uint32_t kPoseBlending = 0x1;
constexpr std::uint8_t kWireTeleport = 0x2;
constexpr std::uint8_t kWireScene = 0x4;

/* A piece a Ratchet wears or holds (the SDK's openrac_held_piece): the game's slot and which of
 * its mobys, the item, the moby's class, the body's joint it follows, kHeld* flags, where a loose
 * one is, and its own animation. */
constexpr std::uint32_t kHeldHidden = 0x1;
constexpr std::uint32_t kHeldBlending = 0x2;
constexpr std::uint32_t kHeldLoose = 0x4;
constexpr std::size_t kHeldMax = 8;

struct HeldPiece {
    std::int32_t slot = 0;
    std::int32_t part = 0;
    std::int32_t item = 0;
    std::int32_t oclass = 0;
    std::int32_t joint = 0;
    std::uint32_t flags = 0;
    float position[3] = {0, 0, 0};
    float rotation[3] = {0, 0, 0};
    std::uint32_t seq = 0;
    std::uint32_t frame = 0;
    std::uint32_t next_frame = 0;
    float anim_time = 0;
    float anim_speed = 0;
};

/* A pose of a Ratchet (the SDK's openrac_pose after its size; the game's units), and what it wears
 * and holds (the SDK's openrac_held: `held_count` of `held`). */
struct Pose {
    std::int32_t level = -1;
    float position[3] = {0, 0, 0};
    float rotation[3] = {0, 0, 0};
    float scale = 0;
    float velocity[3] = {0, 0, 0};
    std::int32_t state = 0;
    std::int32_t health = 0;
    std::int32_t item = -1;
    std::uint32_t seq = 0;
    std::uint32_t frame = 0;
    std::uint32_t next_frame = 0;
    float anim_time = 0;
    float anim_speed = 0;
    std::uint32_t flags = 0;
    std::uint32_t held_count = 0;
    HeldPiece held[kHeldMax];
};

/* A POSE message: whose, its sequence and stamp, the teleport flag, the pose. */
struct PoseMessage {
    std::int32_t owner = 0;
    std::uint16_t sequence = 0;
    std::int64_t stamp_ms = 0;
    bool teleport = false;
    /* The sender's game plays a scene (a cutscene, a landing): its player waits on it. */
    bool scene = false;
    Pose pose;
};

/* The POSE kind's first byte (coop.hpp's kinds), and its size on the wire. */
constexpr std::uint8_t kPoseKind = 7;
constexpr std::size_t kPoseBytes = 65;

Bytes encode_pose(const PoseMessage& message);
/* False for bytes that are not a whole POSE. */
bool decode_pose(const Bytes& bytes, PoseMessage& out);

/* A shot a Ratchet fired (the SDK's openrac_shot), and the SHOT message: whose, in which level. */
struct Shot {
    std::int32_t item = 0;
    float position[3] = {0, 0, 0};
    float velocity[3] = {0, 0, 0};
    float aim[4] = {0, 0, 0, 0};
};

struct ShotMessage {
    std::int32_t owner = 0;
    std::int32_t level = -1;
    Shot shot;
};

constexpr std::uint8_t kShotKind = 19;
constexpr std::size_t kShotBytes = 48;

Bytes encode_shot(const ShotMessage& message);
/* False for bytes that are not a whole SHOT. */
bool decode_shot(const Bytes& bytes, ShotMessage& out);

/* Whether sequence `a` is newer than `b`, 16 bits wrapping (serial arithmetic). */
bool sequence_newer(std::uint16_t a, std::uint16_t b);

/* The buffer's constants (INTERPOLATION.md, section 2), in milliseconds and game units. */
constexpr std::size_t kBufferSamples = 32;
constexpr double kMinDelayMs = 100.0;
constexpr double kMaxDelayMs = 400.0;
constexpr double kExtrapolateMs = 150.0;
constexpr double kFadeMs = 50.0;
constexpr double kSnapDistance = 8.0;
constexpr double kStallMs = 1000.0;
constexpr double kRateMin = 0.85;
constexpr double kRateMax = 1.15;

/* One remote player's poses, played back a delay behind. Times are this game's clock, ms. */
class Interpolator {
public:
    /* A pose sent at `time_ms` (its stamp in this clock) that arrived at `arrival_ms`. One not
     * newer than the newest (by sequence) is dropped; a teleport starts the buffer again. */
    void add(const Pose& pose, std::uint16_t sequence, double time_ms, double arrival_ms, bool teleport);

    /* The pose to show at `now_ms`; false while there is none. */
    bool at(double now_ms, Pose& out);

    /* The delay played behind now. */
    double delay_ms() const;

    /* Whether nothing came for kStallMs (the figure holds), and how long since anything came. */
    bool stalled(double now_ms) const { return empty() || now_ms - last_arrival_ > kStallMs; }

    double silence_ms(double now_ms) const { return empty() ? 1e9 : now_ms - last_arrival_; }

    bool empty() const { return samples_.empty(); }

    /* The newest pose that came; false while none. */
    bool newest(Pose& out) const {
        if (samples_.empty()) {
            return false;
        }
        out = samples_.back().pose;
        return true;
    }

    std::size_t size() const { return samples_.size(); }

    /* Whether the last at() jumped (a teleport or the cursor far behind). */
    bool jumped() const { return jumped_; }

    void reset();

private:
    struct Sample {
        double time = 0;
        Pose pose;
    };

    std::deque<Sample> samples_;
    std::uint16_t sequence_ = 0;
    double interval_ = 50.0;
    double jitter_ = 0.0;
    double last_arrival_ = 0.0;
    double last_transit_ = 0.0;
    bool cursor_set_ = false;
    double cursor_ = 0.0;
    double last_now_ = 0.0;
    bool jumped_ = false;
};

/* The yaw from `a` to `b` by `s` (0 to 1), the short way round. */
float lerp_angle(float a, float b, float s);

}  // namespace mp

#endif /* MULTIPLAYER_PLAY_H */
