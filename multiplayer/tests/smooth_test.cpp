// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The interpolation buffer (play.hpp) over a real network: streams of poses that arrive late,
// unevenly, out of order and some not at all, shown 60 times a second as the game does, and how
// smoothly what is shown moves:
//   - a figure (its poses carry velocities, 20 a second) and a moby or a bolt of the host's world
//     (WORLD carries no velocity, 15 a second) at 50 to 250 ms one way, with jitter, reordering and
//     loss, and the stamps off the receiver's clock by a constant (the clock estimates' error):
//     never back, every frame's step close to the run's, hardly ever past the newest pose;
//   - the arrivals recorded in a session through game.openrac.dev (tests/data/world_internet.csv);
//   - the delay following the link: up when its jitter grows, slowly down when it calms, a
//     larger one way;
//   - a moby that stops (WORLD stops sending it) held where it stopped, and one that starts again
//     after a silence going there along the way, not jumping;
//   - a Ratchet standing still, whose velocity says he falls (the game's gravity, which the ground
//     takes back: tests/data/pose_stall.csv), with poses now and then late enough that the cursor
//     runs past the newest: he does not bob; and his jump drawn along its arc all the same.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "play.hpp"

namespace {

int g_failures = 0;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

// A small deterministic random source (the streams are the same on every run).
struct Random {
    std::uint64_t s;

    explicit Random(std::uint64_t seed) : s(seed * 2862933555777941757ull + 3037000493ull) {}

    double uniform() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(s >> 11) / 9007199254740992.0;
    }

    // Mostly small, now and then large: the delay a packet picks up on the way.
    double jitter(double scale) { return -scale * std::log(1.0 - uniform() * 0.999); }
};

// A pose sent and when it came.
struct Packet {
    double stamp = 0;
    double arrival = 0;
    std::uint16_t sequence = 0;
    mp::Pose pose;
};

// Where the thing is at `t` ms: along x at `speed` units a second, round a wide curve in y.
struct Path {
    double speed = 8.0;

    void at(double t, mp::Pose& p, bool with_velocity) const {
        const double s = t / 1000.0;
        p.level = 3;
        p.position[0] = static_cast<float>(speed * s);
        p.position[1] = static_cast<float>(4.0 * std::sin(s * 0.7));
        p.position[2] = 0.0f;
        p.rotation[2] = static_cast<float>(0.3 * std::sin(s * 0.7));

        if (with_velocity) {
            // The game's own velocity is not quite the step from one pose to the next: off by
            // up to a tenth either way.
            const double off = 1.0 + 0.1 * std::sin(t * 0.37);
            p.velocity[0] = static_cast<float>(speed * off);
            p.velocity[1] = static_cast<float>(4.0 * 0.7 * std::cos(s * 0.7) * off);
        }
    }
};

struct Link {
    double one_way = 100.0;  // ms
    double jitter = 10.0;    // ms, the scale of the extra delay
    double loss = 0.0;       // 0 to 1
    double clock_error = 0.0;  // the stamps' error in the receiver's clock, ms
};

// Poses sent every `interval` ms for `length` ms over `link`, in the order they arrive.
std::vector<Packet> stream(const Path& path, const Link& link, double interval, double length,
                           bool with_velocity, std::uint64_t seed) {
    Random r(seed);
    std::vector<Packet> out;
    std::uint16_t sequence = 0;

    for (double t = 0; t <= length; t += interval) {
        Packet p;
        p.stamp = t + link.clock_error;
        p.sequence = ++sequence;
        path.at(t, p.pose, with_velocity);
        // The sender's tick: its sends land on 60 Hz frames.
        p.stamp = std::floor(p.stamp / (1000.0 / 60.0)) * (1000.0 / 60.0);
        p.arrival = t + link.one_way + r.jitter(link.jitter);

        if (r.uniform() >= link.loss) {
            out.push_back(p);
        }
    }

    std::stable_sort(out.begin(), out.end(), [](const Packet& a, const Packet& b) {
        return a.arrival < b.arrival;
    });
    return out;
}

// How smoothly it was shown.
struct Smooth {
    int frames = 0;
    int backs = 0;           // frames that went back along the way
    double worst_back = 0;   // the largest step back, units
    double low = 1e9;        // the smallest step of a frame over the run's
    double high = 0;         // the largest
    double rough = 0;        // the mean of |step - run's step| over the run's step
    int past = 0;            // frames past the newest pose (on along its velocity, or held)
    double kink = 0;         // the largest change of a frame's step (both ways) over the run's step
    double delay = 0;        // the delay at the end
    double worst_delay = 0;  // the largest delay on the way
};

void print(const char* name, const Smooth& s) {
    std::printf("%-34s frames %5d back %3d (worst %.3f)  step %.2f..%.2f of the run's, rough %.3f, "
                "kink %.2f, past %d, delay %.0f ms (most %.0f)\n",
                name, s.frames, s.backs, s.worst_back, s.low, s.high, s.rough, s.kink, s.past, s.delay,
                s.worst_delay);
}

// The packets played into a buffer and shown every 1/60 s from `from` ms of the stream.
Smooth show(const Path& path, const std::vector<Packet>& packets, double from, double until,
           bool derive) {
    mp::Interpolator in(derive);
    Smooth out;
    std::size_t next = 0;
    double last_x = 0;
    double last_y = 0;
    double last_dx = 0;
    double last_dy = 0;
    int have = 0;
    double rough_sum = 0;
    const double frame = 1000.0 / 60.0;
    const double run = path.speed * frame / 1000.0;

    for (double t = 0; t <= until; t += frame) {
        for (; next < packets.size() && packets[next].arrival <= t; ++next) {
            const Packet& p = packets[next];
            in.add(p.pose, p.sequence, p.stamp, t, false);
        }

        mp::Pose p;

        if (!in.at(t, p)) {
            continue;
        }

        out.worst_delay = std::max(out.worst_delay, t >= from ? in.delay_ms() : 0.0);

        const double dx = p.position[0] - last_x;
        const double dy = p.position[1] - last_y;

        if (t >= from && (in.shown() == mp::Shown::kAhead || in.shown() == mp::Shown::kHeld)) {
            ++out.past;
        }

        if (have >= 2 && t >= from) {
            out.kink = std::max(out.kink, std::hypot(dx - last_dx, dy - last_dy) / run);
        }

        if (have >= 1 && t >= from) {
            const double step = dx;
            ++out.frames;

            if (step < -1e-4) {
                ++out.backs;
                out.worst_back = std::max(out.worst_back, -step);
            }

            if (std::getenv("SMOOTH_DEBUG") != nullptr && (step / run < 0.5 || step / run > 1.6)) {
                std::printf("  t %.0f step %.2f shown %s ahead %.1f delay %.0f size %zu\n", t, step / run,
                            mp::shown_name(in.shown()), in.ahead_ms(), in.delay_ms(), in.size());
            }

            out.low = std::min(out.low, step / run);
            out.high = std::max(out.high, step / run);
            rough_sum += std::fabs(step - run) / run;
        }

        last_x = p.position[0];
        last_y = p.position[1];
        last_dx = dx;
        last_dy = dy;
        ++have;
    }

    out.rough = out.frames > 0 ? rough_sum / out.frames : 0;
    out.delay = in.delay_ms();
    return out;
}

// A figure: 20 poses a second with their velocities.
void figures_over_the_internet() {
    const Path path;
    const double one_ways[] = {50, 100, 150, 250};

    for (const double one_way : one_ways) {
        Link link;
        link.one_way = one_way;
        link.jitter = 8.0 + one_way * 0.08;
        link.loss = 0.02;
        link.clock_error = 23.0;
        const Smooth s = show(path, stream(path, link, 50.0, 20000.0, true, 1), 3000.0, 20000.0, false);
        char name[64];
        std::snprintf(name, sizeof name, "figure, %3.0f ms one way", one_way);
        print(name, s);
        CHECK(s.backs == 0);
        CHECK(s.low > 0.5 && s.high < 1.6);
        CHECK(s.rough < 0.12);
    }
}

// A moby of the host's: 15 a second, no velocity.
void world_mobys_over_the_internet() {
    Path path;
    path.speed = 5.0;
    const double one_ways[] = {2, 50, 100, 150, 250};

    for (const double one_way : one_ways) {
        Link link;
        link.one_way = one_way;
        link.jitter = 8.0 + one_way * 0.08;
        link.loss = 0.02;
        link.clock_error = -17.0;
        const Smooth s = show(path, stream(path, link, 1000.0 / 15.0, 20000.0, false, 2), 3000.0, 20000.0,
                               true);
        char name[64];
        std::snprintf(name, sizeof name, "moby, %3.0f ms one way", one_way);
        print(name, s);
        CHECK(s.backs == 0);
        CHECK(s.low > 0.5 && s.high < 1.6);
        CHECK(s.rough < 0.12);
    }
}

// The arrivals of a session through the public server.
void the_recorded_session() {
    const std::string path_name = std::string(MP_TEST_DATA) + "/world_internet.csv";
    std::FILE* f = std::fopen(path_name.c_str(), "r");
    CHECK(f != nullptr);

    if (f == nullptr) {
        return;
    }

    std::vector<std::pair<double, double>> times;
    char line[128];

    while (std::fgets(line, sizeof line, f) != nullptr) {
        double sent = 0, arrival = 0;

        if (line[0] != '#' && std::sscanf(line, "%lf,%lf", &sent, &arrival) == 2) {
            times.emplace_back(sent, arrival);
        }
    }

    std::fclose(f);
    CHECK(times.size() > 300);

    for (const bool with_velocity : {false, true}) {
        Path path;
        path.speed = with_velocity ? 8.0 : 5.0;
        std::vector<Packet> packets;
        std::uint16_t sequence = 0;

        for (const auto& [sent, arrival] : times) {
            Packet p;
            p.stamp = sent;
            p.arrival = arrival;
            p.sequence = ++sequence;
            path.at(sent, p.pose, with_velocity);
            packets.push_back(p);
        }

        const Smooth s = show(path, packets, 3000.0, times.back().second, !with_velocity);
        print(with_velocity ? "recorded arrivals, a figure" : "recorded arrivals, a moby", s);
        CHECK(s.backs == 0);
        CHECK(s.low > 0.5 && s.high < 1.6);
        CHECK(s.rough < 0.12);
        // The link's one way is about 160 ms: the delay is past it and a send or two more.
        CHECK(s.delay > 200.0 && s.delay < 400.0);
    }
}


// Every tenth pose overtaken by the next one (UDP), and each pose twice (the copy 40 ms later):
// the late ones dropped, what is shown as smooth as without.
void reordered_and_repeated() {
    const Path path;
    Link link;
    link.one_way = 120.0;
    link.jitter = 15.0;
    std::vector<Packet> packets = stream(path, link, 50.0, 12000.0, true, 3);

    for (std::size_t i = 10; i < packets.size(); i += 10) {
        packets[i - 1].arrival = packets[i].arrival + 5.0;
    }

    std::vector<Packet> mixed;

    for (const Packet& p : packets) {
        mixed.push_back(p);
        Packet copy = p;
        copy.arrival += 40.0;
        mixed.push_back(copy);
    }

    std::stable_sort(mixed.begin(), mixed.end(), [](const Packet& a, const Packet& b) {
        return a.arrival < b.arrival;
    });
    int out_of_order = 0;

    for (std::size_t i = 1; i < mixed.size(); ++i) {
        out_of_order += mixed[i].stamp < mixed[i - 1].stamp ? 1 : 0;
    }

    std::printf("reordered: %d poses came after a newer one\n", out_of_order);
    CHECK(out_of_order > 30);
    const Smooth s = show(path, mixed, 3000.0, 12000.0, false);
    print("figure, reordered and repeated", s);
    CHECK(s.backs == 0 && s.past < 5);
    // An overtaken pose is dropped: the 100 ms between its neighbours shows the velocities' error
    // twice as much.
    CHECK(s.low > 0.5 && s.high < 1.8);
}

// The delay follows the link: a calm 80 ms one way, then 6 s of a jittery one, then calm again.
void the_delay_follows_the_link() {
    const Path path;
    Random r(4);
    mp::Interpolator in(true);
    std::uint16_t sequence = 0;
    double calm = 0, rough = 0, after = 0, later = 0;
    std::vector<Packet> packets;

    for (double t = 0; t <= 24000; t += 1000.0 / 15.0) {
        Packet p;
        p.stamp = t;
        p.sequence = ++sequence;
        path.at(t, p.pose, false);
        const bool jittery = t >= 8000 && t < 14000;
        p.arrival = t + 80.0 + r.jitter(jittery ? 60.0 : 4.0);
        packets.push_back(p);
    }

    std::stable_sort(packets.begin(), packets.end(), [](const Packet& a, const Packet& b) {
        return a.arrival < b.arrival;
    });
    std::size_t next = 0;

    for (double t = 0; t <= 24000; t += 1000.0 / 60.0) {
        for (; next < packets.size() && packets[next].arrival <= t; ++next) {
            in.add(packets[next].pose, packets[next].sequence, packets[next].stamp, t, false);
        }

        mp::Pose p;
        in.at(t, p);

        if (std::fabs(t - 7900) < 9) {
            calm = in.delay_ms();
        } else if (std::fabs(t - 13900) < 9) {
            rough = in.delay_ms();
        } else if (std::fabs(t - 15000) < 9) {
            after = in.delay_ms();
        } else if (std::fabs(t - 23900) < 9) {
            later = in.delay_ms();
        }
    }

    std::printf("delay: calm %.0f ms, jittery %.0f, a second after %.0f, ten after %.0f\n", calm, rough,
                after, later);
    // Calm: past the 80 ms in transit by two sends and a little.
    CHECK(calm > 80.0 + 2 * 66.0 && calm < 80.0 + 2 * 66.0 + 60.0);
    // Jittery: well up; then down, slowly (not at once), back near the calm one.
    CHECK(rough > calm + 80.0);
    CHECK(after > calm + 40.0 && after < rough + 1.0);
    CHECK(later < calm + 40.0);
}

// A moby WORLD stops sending once it stands (only a refresh a second), then sends again when it
// moves on: held where it stopped, never past it, and off again along the way without a jump.
void a_moby_stops_and_goes_on() {
    mp::Interpolator in(true);
    std::uint16_t sequence = 0;
    const double frame = 1000.0 / 60.0;
    const double speed = 5.0;
    // Moving until 2 s, standing at 10 until 5 s (WORLD refreshes it at 3 and 4 s), then moving.
    const auto x_at = [&](double t) {
        return t < 2000 ? speed * t / 1000.0 : t < 5000 ? 10.0 : 10.0 + speed * (t - 5000) / 1000.0;
    };
    std::vector<double> sends;

    for (double t = 0; t <= 8000; t += 1000.0 / 15.0) {
        const bool standing = t > 2000 && t < 5000;

        if (!standing) {
            sends.push_back(t);
        }
    }

    sends.push_back(3000);
    sends.push_back(4000);
    std::sort(sends.begin(), sends.end());
    std::size_t next = 0;
    double worst_past = 0, last = 0, worst_step = 0;

    for (double t = 0; t <= 8500; t += frame) {
        for (; next < sends.size() && sends[next] + 100.0 <= t; ++next) {
            mp::Pose p;
            p.level = 3;
            p.position[0] = static_cast<float>(x_at(sends[next]));
            in.add(p, ++sequence, sends[next], t, false);
        }

        mp::Pose p;

        if (!in.at(t, p)) {
            continue;
        }

        if (t > 500) {
            worst_step = std::max(worst_step, std::fabs(p.position[0] - last));
        }

        if (t < 5000 + in.delay_ms()) {
            worst_past = std::max(worst_past, p.position[0] - 10.0);
        }

        last = p.position[0];
    }

    std::printf("a moby stops and goes on: past the stop by %.3f, worst step %.3f (a frame's run %.3f)\n",
                worst_past, worst_step, speed * frame / 1000.0);
    CHECK(worst_past < 0.01);
    CHECK(worst_step < 2.0 * speed * frame / 1000.0);
}

// Standing at z 31.2 with the game's -0.749 units a second of z velocity (as recorded), 20 poses a
// second at 170 ms, every 37th pose 140 ms late (the cursor goes past the newest); from 6 s a jump
// (10 units a second up, 30 down), then standing again.
void standing_still_and_a_jump() {
    const double ground = 31.2, up = 10.0, gravity = 30.0, start = 6000.0;
    const double air = 2.0 * up / gravity * 1000.0;
    const auto z_at = [&](double t) {
        const double tau = (t - start) / 1000.0;
        return t < start || t > start + air ? ground : ground + up * tau - gravity * tau * tau / 2.0;
    };
    const auto vz_at = [&](double t) {
        return t < start || t > start + air ? -0.749 : up - gravity * (t - start) / 1000.0;
    };
    Random r(5);
    std::vector<Packet> packets;
    std::uint16_t sequence = 0;
    int n = 0;

    for (double t = 0; t <= 12000; t += 50.0) {
        Packet p;
        p.stamp = t;
        p.sequence = ++sequence;
        p.pose.level = 3;
        p.pose.position[0] = 139.1f;
        p.pose.position[1] = 118.8f;
        p.pose.position[2] = static_cast<float>(z_at(t));
        p.pose.velocity[2] = static_cast<float>(vz_at(t));
        p.arrival = t + 170.0 + r.jitter(6.0) + (++n % 37 == 0 ? 140.0 : 0.0);
        packets.push_back(p);
    }

    std::stable_sort(packets.begin(), packets.end(), [](const Packet& a, const Packet& b) {
        return a.arrival < b.arrival;
    });
    mp::Interpolator in;
    std::size_t next = 0;
    double bob = 0, arc = 0;
    int past = 0;

    for (double t = 0; t <= 12500; t += 1000.0 / 60.0) {
        for (; next < packets.size() && packets[next].arrival <= t; ++next) {
            in.add(packets[next].pose, packets[next].sequence, packets[next].stamp, t, false);
        }

        mp::Pose p;

        if (!in.at(t, p) || t < 1000) {
            continue;
        }

        past += in.shown() == mp::Shown::kAhead || in.shown() == mp::Shown::kHeld ? 1 : 0;
        const double c = in.cursor_ms();

        // Standing (well clear of the jump): exactly the ground.
        if (c < start - 100 || c > start + air + 300) {
            bob = std::max(bob, std::fabs(p.position[2] - ground));
        } else if (in.shown() == mp::Shown::kBetween) {
            // In the air: on the arc at the cursor's time.
            arc = std::max(arc, std::fabs(p.position[2] - z_at(c)));
        }
    }

    std::printf("standing: bob %.4f units over %d frames past the newest; the jump off its arc by %.4f\n",
                bob, past, arc);
    CHECK(past > 5);
    CHECK(bob < 0.002);
    CHECK(arc < 0.05);
}

}  // namespace

int main() {
    figures_over_the_internet();
    world_mobys_over_the_internet();
    the_recorded_session();
    reordered_and_repeated();
    the_delay_follows_the_link();
    a_moby_stops_and_goes_on();
    standing_still_and_a_jump();

    if (g_failures == 0) {
        std::puts("smooth: all passed");
    }

    return g_failures == 0 ? 0 : 1;
}
