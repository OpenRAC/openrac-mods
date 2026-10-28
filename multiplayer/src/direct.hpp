/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Direct connect's page logic without the game (direct.cpp), so the tests check it: the address
 * typed with choice rows, and the lobby's lines when hosting.
 *
 * The game's front end has no text entry, and a choice row steps through at most
 * OPENRAC_UI_CHOICES_MAX (64) values. An octet (0 to 255) and a port (1 to 65535) have more, so a
 * row is a ring: the page gives it a window of kRingWindow values with the current one in the
 * middle (kRingCentre), and after each step reads the new value from the choice and gives a new
 * window centred on it. Left and Right step it by one choice, around the ends (255 to 0); held,
 * the port repeats the step, and Acceleration makes a long hold step ten at a time.
 */
#ifndef MULTIPLAYER_DIRECT_H
#define MULTIPLAYER_DIRECT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mp {

/* A ring row's choices, and where the current value sits among them. */
constexpr int kRingWindow = 64;
constexpr int kRingCentre = 32;

/* The values a ring row steps through: lo to hi, `step` a choice. */
struct Ring {
    int lo;
    int hi;
    int step;
};

/* An address's octet; the port by one; the port by a hundred (the coarse row). */
constexpr Ring kOctetRing{0, 255, 1};
constexpr Ring kPortRing{1, 65535, 1};
constexpr Ring kPortCoarseRing{1, 65535, 100};

/* `value` brought into the ring (lo to hi), around its ends. */
int ring_wrap(long long value, const Ring& ring);

/* The row's kRingWindow choices: the values `step` apart around `value`, which is at
 * kRingCentre. */
std::vector<std::string> ring_choices(int value, const Ring& ring);

/* The value after the row's choice changed to `choice` (an index into ring_choices(value)), the
 * step taken `times` times over (Acceleration's). */
int ring_step(int value, int choice, const Ring& ring, int times = 1);

/* How many steps a change takes: one, and ten once a direction has been held a while (the port
 * repeats a held step every few ticks: kHeldRepeats repeats on one row, each within kHeldGapMs of
 * the last). */
class Acceleration {
public:
    static constexpr int kHeldRepeats = 8;
    static constexpr std::uint64_t kHeldGapMs = 200;
    static constexpr int kFast = 10;

    /* A change of row `item` at `now_ms`: the steps it takes. */
    int times(int item, std::uint64_t now_ms);

    /* Forgets the hold (the page changed). */
    void reset() { run_ = 0, item_ = -1; }

private:
    int item_ = -1;
    int run_ = 0;
    std::uint64_t last_ms_ = 0;
};

/* The address Join by address connects to: four octets and the port. It starts at a common LAN
 * address with the default port, and keeps the last one joined for the rest of the game. */
struct DirectAddress {
    std::array<int, 4> octets{192, 168, 1, 1};
    int port = 7770;

    /* "a.b.c.d:port", as connect_direct takes it. */
    std::string text() const;
};

/* The hosting lobby's lines: "Hosting on <address>:<port>" for each of this machine's addresses,
 * at most `most` of them; "Hosting on port <port>" when it knows none. */
std::vector<std::string> hosting_lines(
    const std::vector<std::string>& addresses, int port, std::size_t most
);

}  // namespace mp

#endif /* MULTIPLAYER_DIRECT_H */
